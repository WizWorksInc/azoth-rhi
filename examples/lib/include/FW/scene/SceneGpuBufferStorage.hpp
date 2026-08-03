// Copyright 2026 Ian Pike
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "BuiltinPrimitives.hpp"
#include "Material.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "Transform.hpp"

#include <azoth/rhi/core/flags.hpp>
#include <azoth/rhi/core/resource_handles.hpp>
#include <azoth/rhi/device/device.hpp>
#include <azoth/rhi/resources/pipeline.hpp>
#include <azoth/rhi/resources/resources.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace fw::scene
{
	/**
	 * \brief Where one mesh ended up inside the geometry buffer.
	 *
	 * One offset per attribute, in the order the scene config lists them, so binding slot n takes vertexOffsets[n]. The indices follow the attributes in that same
	 * buffer.
	 */
	struct MeshGpu final
	{
		std::vector<std::uint64_t> vertexOffsets;
		std::uint64_t indexOffset = 0;
		std::uint32_t indexCount  = 0;

		/**
		 * \brief True when the indices were stored 32 bits wide, which is what SetIndexBuffer has to be told.
		 */
		bool index32 = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return indexCount != 0;
		}
	};

	/**
	 * \brief The three buffers a scene keeps on the device and the suballocation of meshes inside the first.
	 *
	 * Geometry goes in one buffer bound as both vertex and index source. A draw changes offsets, not buffers. Transforms and materials go in one storage buffer
	 * each, indexed by object id.
	 *
	 * All three are host visible and stay mapped for as long as this object lives, which keeps the scene off the copy queue.
	 */
	class SceneGpuBufferStorage final
	{
	public:
		explicit SceneGpuBufferStorage(SceneConfig sceneConfig);

		SceneGpuBufferStorage(const SceneGpuBufferStorage &)			 = delete;
		SceneGpuBufferStorage & operator=(const SceneGpuBufferStorage &) = delete;
		SceneGpuBufferStorage(SceneGpuBufferStorage &&)					 = delete;
		SceneGpuBufferStorage & operator=(SceneGpuBufferStorage &&)		 = delete;

		~SceneGpuBufferStorage();

		/**
		 * \brief False when a buffer could not be created or mapped, which leaves every allocation and update below a no-op.
		 */
		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_valid;
		}

		/**
		 * \brief Copies a mesh into the geometry buffer, or hands back where an earlier call put a mesh of the same name.
		 *
		 * An attribute the scene config asks for and the mesh does not carry is filled with zeroes, except tangents, which are generated from the positions, normals
		 * and texture coordinates when all three are present.
		 *
		 * \return Where the mesh landed, or an invalid MeshGpu when it did not fit.
		 */
		[[nodiscard]] MeshGpu AllocateMesh(const MeshPrimitive & meshPrimitive);

		/**
		 * \brief Generates a builtin mesh at the config's subdivision counts and allocates it, or hands back the allocation it already has.
		 */
		[[nodiscard]] MeshGpu AllocateBuiltinMesh(BuiltinMeshType builtinMeshType);

		/**
		 * \brief Writes one object's transform at its slot in the transform storage buffer.
		 */
		bool UpdateTransform(std::uint32_t objectId, const TransformGpu & transformGpuData) const;

		/**
		 * \brief Writes one object's serialized material at its slot in the material storage buffer.
		 *
		 * \attention materialData has to be exactly GetMaterialStride bytes, which is what SerializeMaterial produces for this config's component list.
		 */
		bool UpdateMaterial(std::uint32_t objectId, std::span<const std::uint8_t> materialData) const;

		[[nodiscard]] azo::rhi::BufferHandle GetGeometryBuffer() const noexcept
		{
			return m_geometry.handle;
		}

		[[nodiscard]] azo::rhi::BufferHandle GetTransformStorageBuffer() const noexcept
		{
			return m_transforms.handle;
		}

		[[nodiscard]] azo::rhi::BufferHandle GetMaterialStorageBuffer() const noexcept
		{
			return m_materials.handle;
		}

		/**
		 * \brief Bytes between consecutive materials in the material storage buffer, which is what a shader indexes it by.
		 */
		[[nodiscard]] std::uint64_t GetMaterialStride() const noexcept
		{
			return m_materialStride;
		}

		/**
		 * \brief One past the last object id the two storage buffers have a slot for.
		 */
		[[nodiscard]] std::uint32_t GetObjectCapacity() const noexcept
		{
			return m_config.objectCapacity;
		}

		/**
		 * \brief How many vertex attributes the scene config declares, which is also how many vertex bindings there are.
		 */
		[[nodiscard]] std::uint32_t GetAttributeCount() const noexcept;

		/**
		 * \brief Vertex bindings for the config's attribute layout, one per attribute, each over a tightly packed array of it.
		 */
		[[nodiscard]] std::vector<azo::rhi::VertexBindingDesc> GetBindingDescriptions() const;

		/**
		 * \brief Vertex attributes for the config's attribute layout, each at the location matching its binding.
		 */
		[[nodiscard]] std::vector<azo::rhi::VertexAttributeDesc> GetAttributeDescriptions() const;

		[[nodiscard]] const std::vector<MaterialComponent> & GetEnabledMaterialComponents() const noexcept
		{
			return m_config.enabledMaterialComponents;
		}

	private:
		// A buffer together with the mapping held open on it for this object's lifetime.
		struct MappedBuffer final
		{
			azo::rhi::BufferHandle handle;
			std::uint8_t * data		= nullptr;
			std::uint64_t sizeBytes = 0;
			bool coherent			= false;
		};

		[[nodiscard]] bool CreateMappedBuffer(
			MappedBuffer & buffer, std::uint64_t sizeBytes, azo::rhi::Flags<azo::rhi::BufferUsage> usage, std::uint64_t stride, const char * debugName);

		[[nodiscard]] bool Write(const MappedBuffer & buffer, std::uint64_t offset, const void * data, std::uint64_t sizeBytes) const;

		void DestroyBuffer(MappedBuffer & buffer) noexcept;

		[[nodiscard]] MeshGpu AllocateMeshInternal(const MeshPrimitive & meshPrimitive);

		/**
		 * \brief Places one attribute of a mesh, deriving it or zeroing it when the mesh does not carry it.
		 *
		 * \param vertexCount How long a zeroed attribute has to be, which is the mesh's position count.
		 */
		[[nodiscard]] bool AllocateAttribute(
			const MeshPrimitive & meshPrimitive, AttributeType attributeType, std::uint32_t elementSize, std::size_t vertexCount, std::uint64_t & outOffset);

		/**
		 * \brief Places a mesh's indices after its attributes, narrowing or widening them to one of the two widths a draw can use.
		 */
		[[nodiscard]] bool AllocateIndices(const Accessor & indices, MeshGpu & meshGpu);

		/**
		 * \brief Claims the next aligned run of geometry bytes, or returns false when the buffer has no room left.
		 */
		[[nodiscard]] bool Reserve(std::uint64_t sizeBytes, std::uint64_t & outOffset);

		/**
		 * \brief Copies one attribute array in, element by element when the source and the layout disagree on width or on stride.
		 */
		[[nodiscard]] bool WriteAttribute(std::uint64_t offset, const Accessor & accessor, std::uint32_t elementSize) const;

		static constexpr auto kGeometryBufferName	= "fw.scene.geometry";
		static constexpr auto kTransformStorageName = "fw.scene.transforms";
		static constexpr auto kMaterialStorageName	= "fw.scene.materials";
		// Every attribute is a vector of floats and the widest index is four bytes, so aligning each run to four satisfies both the vertex buffer offset rules and
		// the index buffer's.
		static constexpr std::uint64_t kGeometryAlignment = 4;

		SceneConfig m_config;
		MappedBuffer m_geometry;
		MappedBuffer m_transforms;
		MappedBuffer m_materials;
		std::uint64_t m_materialStride = 0;
		std::uint64_t m_geometryCursor = 0;
		std::unordered_map<std::string, MeshGpu> m_meshCache;
		bool m_valid = false;
	};
} // namespace fw::scene

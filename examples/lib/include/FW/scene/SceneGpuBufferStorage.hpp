// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
	struct MeshGpu final
	{
		std::vector<std::uint64_t> vertexOffsets;
		std::uint64_t indexOffset = 0;
		std::uint32_t indexCount  = 0;

		bool index32 = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return indexCount != 0;
		}
	};

	class SceneGpuBufferStorage final
	{
	public:
		explicit SceneGpuBufferStorage(SceneConfig sceneConfig);

		SceneGpuBufferStorage(const SceneGpuBufferStorage &)			 = delete;
		SceneGpuBufferStorage & operator=(const SceneGpuBufferStorage &) = delete;
		SceneGpuBufferStorage(SceneGpuBufferStorage &&)					 = delete;
		SceneGpuBufferStorage & operator=(SceneGpuBufferStorage &&)		 = delete;

		~SceneGpuBufferStorage();

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_valid;
		}

		[[nodiscard]] MeshGpu AllocateMesh(const MeshPrimitive & meshPrimitive);

		[[nodiscard]] MeshGpu AllocateBuiltinMesh(BuiltinMeshType builtinMeshType);

		bool UpdateTransform(std::uint32_t objectId, const TransformGpu & transformGpuData) const;

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

		[[nodiscard]] std::uint64_t GetMaterialStride() const noexcept
		{
			return m_materialStride;
		}

		[[nodiscard]] std::uint32_t GetObjectCapacity() const noexcept
		{
			return m_config.objectCapacity;
		}

		[[nodiscard]] std::uint32_t GetAttributeCount() const noexcept;

		[[nodiscard]] std::vector<azo::rhi::VertexBindingDesc> GetBindingDescriptions() const;

		[[nodiscard]] std::vector<azo::rhi::VertexAttributeDesc> GetAttributeDescriptions() const;

		[[nodiscard]] const std::vector<MaterialComponent> & GetEnabledMaterialComponents() const noexcept
		{
			return m_config.enabledMaterialComponents;
		}

	private:
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

		[[nodiscard]] bool AllocateAttribute(
			const MeshPrimitive & meshPrimitive, AttributeType attributeType, std::uint32_t elementSize, std::size_t vertexCount, std::uint64_t & outOffset);

		[[nodiscard]] bool AllocateIndices(const Accessor & indices, MeshGpu & meshGpu);

		[[nodiscard]] bool Reserve(std::uint64_t sizeBytes, std::uint64_t & outOffset);

		[[nodiscard]] bool WriteAttribute(std::uint64_t offset, const Accessor & accessor, std::uint32_t elementSize) const;

		static constexpr auto kGeometryBufferName		  = "fw.scene.geometry";
		static constexpr auto kTransformStorageName		  = "fw.scene.transforms";
		static constexpr auto kMaterialStorageName		  = "fw.scene.materials";
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
}

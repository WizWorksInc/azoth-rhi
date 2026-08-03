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

#include "FW/scene/SceneGpuBufferStorage.hpp"

#include "FW/utility/Log.hpp"

#include <azoth/rhi/builders/resource_builders.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace fw::scene
{
	namespace
	{
		void ReportError(const std::string_view what, const azo::rhi::Error & error)
		{
			LOG_ERROR(fw::Log(), "scene storage: {}: {}", what, error.message != nullptr ? error.message : "no diagnostic");
		}

		[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		[[nodiscard]] const Accessor * FindAttribute(const MeshPrimitive & mesh, const AttributeType attributeType) noexcept
		{
			const auto found = mesh.attributes.find(attributeType);
			return found != mesh.attributes.end() ? &found->second : nullptr;
		}

		/*
		 * Per vertex tangents for a mesh that arrived without any, accumulated over the triangles sharing each vertex. The w component is the handedness a shader
		 * multiplies cross(normal, tangent) by to recover the bitangent, which tells the two apart on a mesh whose texture coordinates are mirrored across a seam.
		 * Empty when the mesh lacks positions, normals, texture coordinates or indices.
		 */
		[[nodiscard]] std::vector<glm::vec4> GenerateTangents(const MeshPrimitive & mesh)
		{
			const Accessor * positions = FindAttribute(mesh, AttributeType::ePosition);
			const Accessor * normals   = FindAttribute(mesh, AttributeType::eNormal);
			const Accessor * uvs	   = FindAttribute(mesh, AttributeType::eTexCoord);
			const Accessor & indices   = mesh.indices;

			if (positions == nullptr || normals == nullptr || uvs == nullptr || indices.count < 3)
			{
				return {};
			}

			// Reading through the accessors without casting the buffer view keeps this honest about stride, and lets a mesh whose normals or texture coordinates run
			// shorter than its positions fall through to the bounds check inside GetElement.
			const std::size_t vertexCount = std::min({ positions->count, normals->count, uvs->count });
			if (vertexCount == 0)
			{
				return {};
			}

			const auto Read = []<typename T>(const Accessor & accessor, const std::size_t index, T & out)
			{
				const std::uint8_t * element = GetElement(accessor, index);
				if (element == nullptr)
				{
					return false;
				}

				std::memcpy(&out, element, sizeof(T));
				return true;
			};

			std::vector<glm::vec3> accumulatedTangents(vertexCount, glm::vec3{ 0.0f });
			std::vector<glm::vec3> accumulatedBitangents(vertexCount, glm::vec3{ 0.0f });

			for (std::size_t triangle = 0; triangle + 2 < indices.count; triangle += 3)
			{
				const std::uint32_t i0 = ReadIndex(indices, triangle);
				const std::uint32_t i1 = ReadIndex(indices, triangle + 1);
				const std::uint32_t i2 = ReadIndex(indices, triangle + 2);
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
				{
					continue;
				}

				glm::vec3 p0{};
				glm::vec3 p1{};
				glm::vec3 p2{};
				glm::vec2 uv0{};
				glm::vec2 uv1{};
				glm::vec2 uv2{};
				if (!Read(*positions, i0, p0) || !Read(*positions, i1, p1) || !Read(*positions, i2, p2) || !Read(*uvs, i0, uv0) || !Read(*uvs, i1, uv1) ||
					!Read(*uvs, i2, uv2))
				{
					continue;
				}

				const glm::vec3 edge1  = p1 - p0;
				const glm::vec3 edge2  = p2 - p0;
				const glm::vec2 delta1 = uv1 - uv0;
				const glm::vec2 delta2 = uv2 - uv0;

				// A degenerate texture triangle has no direction to hand back, so it contributes nothing and not a division by nearly zero.
				const float determinant = (delta1.x * delta2.y) - (delta2.x * delta1.y);
				if (std::abs(determinant) < 1e-8f)
				{
					continue;
				}

				const float inverse		  = 1.0f / determinant;
				const glm::vec3 tangent	  = ((edge1 * delta2.y) - (edge2 * delta1.y)) * inverse;
				const glm::vec3 bitangent = ((edge2 * delta1.x) - (edge1 * delta2.x)) * inverse;

				for (const std::uint32_t index : { i0, i1, i2 })
				{
					accumulatedTangents[index] += tangent;
					accumulatedBitangents[index] += bitangent;
				}
			}

			std::vector<glm::vec4> result(vertexCount, glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
			for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
			{
				glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
				if (!Read(*normals, vertex, normal))
				{
					continue;
				}

				// Gram-Schmidt against the normal, so the tangent ends up in the surface plane, not wherever the averaging left it.
				const glm::vec3 accumulated = accumulatedTangents[vertex] - (normal * glm::dot(normal, accumulatedTangents[vertex]));
				if (glm::dot(accumulated, accumulated) < 1e-16f)
				{
					continue;
				}

				const glm::vec3 tangent = glm::normalize(accumulated);
				const float handedness	= glm::dot(glm::cross(normal, tangent), accumulatedBitangents[vertex]) < 0.0f ? -1.0f : 1.0f;
				result[vertex]			= glm::vec4{ tangent, handedness };
			}

			return result;
		}
	} // namespace

	SceneGpuBufferStorage::SceneGpuBufferStorage(SceneConfig sceneConfig) : m_config(std::move(sceneConfig))
	{
		if (!m_config.device.IsValid())
		{
			LOG_INFO(fw::Log(), "scene storage: the scene config carries no device");
			return;
		}

		m_materialStride = MaterialStride(m_config.enabledMaterialComponents);

		const azo::rhi::Flags<azo::rhi::BufferUsage> geometryUsage =
			azo::rhi::Flags<azo::rhi::BufferUsage>(azo::rhi::BufferUsage::eVertex) | azo::rhi::BufferUsage::eIndex;

		if (!CreateMappedBuffer(m_geometry, m_config.geometryCapacityBytes, geometryUsage, 0, kGeometryBufferName) ||
			!CreateMappedBuffer(
				m_transforms, m_config.objectCapacity * sizeof(TransformGpu), azo::rhi::BufferUsage::eStorage, sizeof(TransformGpu), kTransformStorageName))
		{
			return;
		}

		// A config enabling no material components serializes to nothing, and a zero sized buffer is not a thing any backend will create. The buffer is still made,
		// one stride wide, so the handle a pipeline binds stays valid and every write lands out of range and is refused.
		if (!CreateMappedBuffer(m_materials,
				std::max<std::uint64_t>(m_config.objectCapacity * m_materialStride, 1),
				azo::rhi::BufferUsage::eStorage,
				m_materialStride,
				kMaterialStorageName))
		{
			return;
		}

		m_valid = true;
	}

	SceneGpuBufferStorage::~SceneGpuBufferStorage()
	{
		DestroyBuffer(m_materials);
		DestroyBuffer(m_transforms);
		DestroyBuffer(m_geometry);
	}

	MeshGpu SceneGpuBufferStorage::AllocateMesh(const MeshPrimitive & meshPrimitive)
	{
		return AllocateMeshInternal(meshPrimitive);
	}

	MeshGpu SceneGpuBufferStorage::AllocateBuiltinMesh(const BuiltinMeshType builtinMeshType)
	{
		// Checked before the mesh is generated, so a second object asking for the same shape does not pay to build it again.
		if (const auto cached = m_meshCache.find(std::string{ GetBuiltinMeshName(builtinMeshType) }); cached != m_meshCache.end())
		{
			return cached->second;
		}

		const MeshData mesh = CreateBuiltinMesh(builtinMeshType, m_config.primitiveStackCount, m_config.primitiveSectorCount);
		return AllocateMeshInternal(mesh.View());
	}

	bool SceneGpuBufferStorage::UpdateTransform(const std::uint32_t objectId, const TransformGpu & transformGpuData) const
	{
		return Write(m_transforms, objectId * sizeof(TransformGpu), &transformGpuData, sizeof(TransformGpu));
	}

	bool SceneGpuBufferStorage::UpdateMaterial(const std::uint32_t objectId, const std::span<const std::uint8_t> materialData) const
	{
		if (materialData.size() != m_materialStride)
		{
			LOG_INFO(fw::Log(), "scene storage: material for object {} is {} bytes where the layout is {}", objectId, materialData.size(), m_materialStride);
			return false;
		}

		return Write(m_materials, objectId * m_materialStride, materialData.data(), materialData.size_bytes());
	}

	std::uint32_t SceneGpuBufferStorage::GetAttributeCount() const noexcept
	{
		return static_cast<std::uint32_t>(m_config.attributeLayout.size());
	}

	std::vector<azo::rhi::VertexBindingDesc> SceneGpuBufferStorage::GetBindingDescriptions() const
	{
		std::vector<azo::rhi::VertexBindingDesc> result;
		result.reserve(m_config.attributeLayout.size());

		for (std::uint32_t binding = 0; const auto & [attributeType, accessorType] : m_config.attributeLayout)
		{
			result.push_back(azo::rhi::VertexBindingDesc{
				.binding = binding++,
				.stride	 = GetAccessorSize(accessorType),
			});
		}

		return result;
	}

	std::vector<azo::rhi::VertexAttributeDesc> SceneGpuBufferStorage::GetAttributeDescriptions() const
	{
		std::vector<azo::rhi::VertexAttributeDesc> result;
		result.reserve(m_config.attributeLayout.size());

		// Each attribute has a binding to itself, so the location and the binding are the same number and the offset inside that binding is always zero.
		for (std::uint32_t binding = 0; const auto & [attributeType, accessorType] : m_config.attributeLayout)
		{
			result.push_back(azo::rhi::VertexAttributeDesc{
				.location = binding,
				.binding  = binding,
				.format	  = ConvertAccessorTypeToFormat(accessorType),
				.offset	  = 0,
			});
			++binding;
		}

		return result;
	}

	bool SceneGpuBufferStorage::CreateMappedBuffer(MappedBuffer & buffer, const std::uint64_t sizeBytes, const azo::rhi::Flags<azo::rhi::BufferUsage> usage,
		const std::uint64_t stride, const char * debugName)
	{
		azo::rhi::Error error{};

		buffer.handle = m_config.device.CreateBuffer(
			azo::rhi::BufferDesc{
				.size	= sizeBytes,
				.stride = stride,
				.usage	= usage,
				// eCpuToGpu, not eCpuUpload because these are written again and again over the scene's life, not staged once and copied out.
				.memory		   = azo::rhi::MemoryUsage::eCpuToGpu,
				.persistentMap = true,
				.debugName	   = debugName,
			},
			error);

		if (!buffer.handle.IsValid())
		{
			ReportError(debugName, error);
			return false;
		}

		const azo::rhi::MappedMemory mapped = m_config.device.Map(buffer.handle, azo::rhi::MapDesc{ .mode = azo::rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			ReportError("failed to map a scene buffer", error);
			return false;
		}

		buffer.data		 = static_cast<std::uint8_t *>(mapped.data);
		buffer.sizeBytes = sizeBytes;
		buffer.coherent	 = mapped.coherent;

		return true;
	}

	bool SceneGpuBufferStorage::Write(const MappedBuffer & buffer, const std::uint64_t offset, const void * data, const std::uint64_t sizeBytes) const
	{
		if (buffer.data == nullptr)
		{
			return false;
		}

		// Written before the addition so a caller passing an object id past the capacity cannot wrap the sum back inside the buffer.
		if (offset > buffer.sizeBytes || sizeBytes > buffer.sizeBytes - offset)
		{
			LOG_INFO(fw::Log(), "scene storage: a write of {} bytes at {} is outside a buffer of {}", sizeBytes, offset, buffer.sizeBytes);
			return false;
		}

		if (sizeBytes == 0)
		{
			return true;
		}

		std::memcpy(buffer.data + offset, data, sizeBytes); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

		if (buffer.coherent)
		{
			return true;
		}

		// The device view is a handle and not something this object owns, so a copy of it is what lets a const update reach a non const RHI call.
		azo::rhi::Device device = m_config.device;

		azo::rhi::Error error{};
		if (!device.FlushMappedRange(buffer.handle, offset, sizeBytes, error))
		{
			ReportError("failed to flush a scene buffer", error);
			return false;
		}

		return true;
	}

	void SceneGpuBufferStorage::DestroyBuffer(MappedBuffer & buffer) noexcept
	{
		if (!buffer.handle.IsValid())
		{
			return;
		}

		if (buffer.data != nullptr)
		{
			static_cast<void>(m_config.device.Unmap(buffer.handle));
			buffer.data = nullptr;
		}

		static_cast<void>(m_config.device.Destroy(buffer.handle));
		buffer.handle = {};
	}

	MeshGpu SceneGpuBufferStorage::AllocateMeshInternal(const MeshPrimitive & meshPrimitive)
	{
		if (const auto cached = m_meshCache.find(meshPrimitive.name); cached != m_meshCache.end())
		{
			return cached->second;
		}

		if (!m_valid || meshPrimitive.indices.count == 0)
		{
			return {};
		}

		// Every attribute and the indices land in one run, so a mesh that fails part way through gives its bytes back without leaving a hole.
		const std::uint64_t cursorBeforeMesh = m_geometryCursor;

		const Accessor * positions	  = FindAttribute(meshPrimitive, AttributeType::ePosition);
		const std::size_t vertexCount = positions != nullptr ? positions->count : 0;

		MeshGpu meshGpu;
		meshGpu.vertexOffsets.reserve(m_config.attributeLayout.size());

		for (const auto & [attributeType, accessorType] : m_config.attributeLayout)
		{
			std::uint64_t offset = 0;
			if (!AllocateAttribute(meshPrimitive, attributeType, GetAccessorSize(accessorType), vertexCount, offset))
			{
				m_geometryCursor = cursorBeforeMesh;
				return {};
			}

			meshGpu.vertexOffsets.push_back(offset);
		}

		if (!AllocateIndices(meshPrimitive.indices, meshGpu))
		{
			m_geometryCursor = cursorBeforeMesh;
			return {};
		}

		m_meshCache.emplace(meshPrimitive.name, meshGpu);

		return meshGpu;
	}

	bool SceneGpuBufferStorage::AllocateAttribute(const MeshPrimitive & meshPrimitive, const AttributeType attributeType, const std::uint32_t elementSize,
		const std::size_t vertexCount, std::uint64_t & outOffset)
	{
		if (const Accessor * accessor = FindAttribute(meshPrimitive, attributeType); accessor != nullptr)
		{
			return Reserve(accessor->count * elementSize, outOffset) && WriteAttribute(outOffset, *accessor, elementSize);
		}

		// Tangents are the one attribute worth deriving, since a mesh without them still carries everything a tangent is made of.
		if (attributeType == AttributeType::eTangent)
		{
			if (const std::vector<glm::vec4> tangents = GenerateTangents(meshPrimitive); !tangents.empty())
			{
				const std::uint64_t copyBytes = std::min<std::uint64_t>(elementSize, sizeof(glm::vec4));
				if (!Reserve(tangents.size() * elementSize, outOffset))
				{
					return false;
				}

				for (std::size_t vertex = 0; vertex < tangents.size(); ++vertex)
				{
					if (!Write(m_geometry, outOffset + (vertex * elementSize), &tangents[vertex], copyBytes))
					{
						return false;
					}
				}

				return true;
			}
		}

		// Anything still missing is filled with zeroes, which keeps the binding a shader declares pointing at something of the right length, not at whatever the next
		// mesh put there.
		const std::vector<std::uint8_t> zeroes(vertexCount * elementSize, 0);

		return Reserve(zeroes.size(), outOffset) && Write(m_geometry, outOffset, zeroes.data(), zeroes.size());
	}

	bool SceneGpuBufferStorage::AllocateIndices(const Accessor & indices, MeshGpu & meshGpu)
	{
		// Decided by the largest index and not by the width the mesh was authored at, so a small mesh carrying 32 bit indices, which is what a glTF exporter tends to
		// write whatever the vertex count, is stored at half the size.
		std::uint32_t largest = 0;
		for (std::size_t index = 0; index < indices.count; ++index)
		{
			largest = std::max(largest, ReadIndex(indices, index));
		}

		const bool index32				= largest > std::numeric_limits<std::uint16_t>::max();
		const std::uint32_t indexStride = index32 ? sizeof(std::uint32_t) : sizeof(std::uint16_t);

		if (!Reserve(indices.count * indexStride, meshGpu.indexOffset))
		{
			return false;
		}

		// One index at a time, because a mesh is authored at whichever of four widths its vertex count needed and only two of those can be drawn from.
		for (std::size_t index = 0; index < indices.count; ++index)
		{
			const std::uint32_t value = ReadIndex(indices, index);
			const auto narrow		  = static_cast<std::uint16_t>(value);
			const std::uint64_t at	  = meshGpu.indexOffset + (index * indexStride);

			if (!(index32 ? Write(m_geometry, at, &value, sizeof(value)) : Write(m_geometry, at, &narrow, sizeof(narrow))))
			{
				return false;
			}
		}

		meshGpu.indexCount = static_cast<std::uint32_t>(indices.count);
		meshGpu.index32	   = index32;

		return true;
	}

	bool SceneGpuBufferStorage::Reserve(const std::uint64_t sizeBytes, std::uint64_t & outOffset)
	{
		const std::uint64_t offset = AlignUp(m_geometryCursor, kGeometryAlignment);
		if (offset > m_config.geometryCapacityBytes || sizeBytes > m_config.geometryCapacityBytes - offset)
		{
			LOG_INFO(fw::Log(),
				"scene storage: the geometry buffer has no room for {} more bytes, {} of {} used",
				sizeBytes,
				m_geometryCursor,
				m_config.geometryCapacityBytes);
			return false;
		}

		outOffset		 = offset;
		m_geometryCursor = offset + sizeBytes;

		return true;
	}

	bool SceneGpuBufferStorage::WriteAttribute(const std::uint64_t offset, const Accessor & accessor, const std::uint32_t elementSize) const
	{
		const std::uint32_t sourceSize	 = GetElementSize(accessor);
		const std::uint64_t sourceStride = accessor.bufferView.byteStride != 0 ? accessor.bufferView.byteStride : sourceSize;

		if (accessor.count == 0)
		{
			return true;
		}

		// The common case: the mesh stores this attribute at exactly the width the layout asks for and packed tightly, so it goes in as one copy. Both ends of the
		// run are resolved, since tight packing makes those two enough to put the whole of it inside the source.
		if (sourceSize == elementSize && sourceStride == elementSize)
		{
			const std::uint8_t * first = GetElement(accessor, 0);
			const std::uint8_t * last  = GetElement(accessor, accessor.count - 1);

			return first != nullptr && last != nullptr && Write(m_geometry, offset, first, accessor.count * elementSize);
		}

		// Otherwise the run is zeroed and then filled element by element with whichever of the two widths is narrower. A wider source is truncated, and a narrower
		// one leaves the rest of each destination element at the zero just written, which is what a vec3 read as a vec4 wants.
		const std::vector<std::uint8_t> zeroes(accessor.count * elementSize, 0);
		if (!Write(m_geometry, offset, zeroes.data(), zeroes.size()))
		{
			return false;
		}

		const std::uint64_t copyBytes = std::min<std::uint64_t>(sourceSize, elementSize);
		for (std::size_t element = 0; element < accessor.count; ++element)
		{
			const std::uint8_t * source = GetElement(accessor, element);
			if (source == nullptr || !Write(m_geometry, offset + (element * elementSize), source, copyBytes))
			{
				return false;
			}
		}

		return true;
	}
} // namespace fw::scene

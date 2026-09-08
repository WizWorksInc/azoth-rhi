// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "Material.hpp"

#include <azoth/rhi/commands/command.hpp>
#include <azoth/rhi/commands/sync.hpp>
#include <azoth/rhi/core/enums.hpp>
#include <azoth/rhi/core/resource_handles.hpp>
#include <azoth/rhi/device/device.hpp>
#include <azoth/rhi/resources/pipeline.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fw::scene
{
	class SceneGpuBufferStorage;
	class SceneGpuImageStorage;
	class SceneObject;

	enum class AttributeType : std::uint8_t
	{
		ePosition,
		eNormal,
		eTangent,
		eTexCoord,
		eColor,
		eJoints,
		eWeights,
	};

	enum class AccessorType : std::uint8_t
	{
		eVec2,
		eVec3,
		eVec4,
	};

	struct SceneConfig
	{
		azo::rhi::Device device;
		std::vector<std::pair<AttributeType, AccessorType>> attributeLayout;
		std::vector<MaterialComponent> enabledMaterialComponents;
		std::uint32_t primitiveStackCount  = UINT32_C(24);
		std::uint32_t primitiveSectorCount = UINT32_C(24);

		std::uint64_t geometryCapacityBytes = 2uz * 1024 * 1024;

		std::uint32_t objectCapacity = 4096;

		azo::rhi::CommandPool imageTransferCmdPool;
		azo::rhi::Queue imageTransferQueue;
	};

	[[nodiscard]] inline std::uint32_t GetAccessorSize(const AccessorType accessorType) noexcept
	{
		switch (accessorType)
		{
		case AccessorType::eVec2: return sizeof(glm::vec2);
		case AccessorType::eVec3: return sizeof(glm::vec3);
		case AccessorType::eVec4: return sizeof(glm::vec4);
		}

		return 0;
	}

	[[nodiscard]] inline azo::rhi::Format ConvertAccessorTypeToFormat(const AccessorType accessorType) noexcept
	{
		switch (accessorType)
		{
		case AccessorType::eVec2: return azo::rhi::Format::eRG32Float;
		case AccessorType::eVec3: return azo::rhi::Format::eRGB32Float;
		case AccessorType::eVec4: return azo::rhi::Format::eRGBA32Float;
		}

		return azo::rhi::Format::eUndefined;
	}

	using TraverseFunc = std::function<void(const SceneObject &)>;

	class Scene final
	{
	public:
		explicit Scene(const SceneConfig & config);

		Scene(const Scene &)			 = delete;
		Scene & operator=(const Scene &) = delete;
		Scene(Scene &&)					 = delete;
		Scene & operator=(Scene &&)		 = delete;

		~Scene();

		[[nodiscard]] bool IsValid() const noexcept;

		[[nodiscard]] std::shared_ptr<SceneObject> CreateObject(std::string objectName);

		void AddRootObject(std::shared_ptr<SceneObject> rootObject);

		void Traverse(const TraverseFunc & func) const;

		void TraverseOrdered(const glm::vec3 & cameraPosition, const TraverseFunc & func, bool backToFront = true) const;

		[[nodiscard]] std::shared_ptr<SceneObject> FindObjectByName(std::string_view name) const;

		[[nodiscard]] std::uint32_t GetAttributeCount() const noexcept;

		[[nodiscard]] std::vector<azo::rhi::VertexBindingDesc> GetBindingDescriptions() const;

		[[nodiscard]] std::vector<azo::rhi::VertexAttributeDesc> GetAttributeDescriptions() const;

		[[nodiscard]] const std::vector<MaterialComponent> & GetEnabledMaterialComponents() const noexcept;

		[[nodiscard]] azo::rhi::BufferHandle GetGeometryBuffer() const noexcept;

		[[nodiscard]] azo::rhi::BufferHandle GetTransformStorageBuffer() const noexcept;

		[[nodiscard]] azo::rhi::BufferHandle GetMaterialStorageBuffer() const noexcept;

		[[nodiscard]] SceneGpuBufferStorage & GetGpuBufferStorage() const noexcept;

		[[nodiscard]] SceneGpuImageStorage & GetGpuImageStorage() const noexcept;

	private:
		static void TraverseRecursive(const SceneObject & object, const TraverseFunc & func);

		static std::shared_ptr<SceneObject> FindRecursive(const std::shared_ptr<SceneObject> & object, std::string_view name);

		static void CollectObjects(const std::shared_ptr<SceneObject> & object, std::vector<std::shared_ptr<SceneObject>> & orderedObjects);

		std::uint32_t m_currentObjectId = 0;
		std::vector<std::shared_ptr<SceneObject>> m_rootObjects;
		std::unique_ptr<SceneGpuBufferStorage> m_gpuBufferStorage;
		std::unique_ptr<SceneGpuImageStorage> m_gpuImageStorage;
	};
}

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

#include "FW/scene/Scene.hpp"

#include "FW/scene/SceneGpuBufferStorage.hpp"
#include "FW/scene/SceneGpuImageStorage.hpp"
#include "FW/scene/SceneObject.hpp"
#include "FW/utility/Log.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <utility>

namespace fw::scene
{
	Scene::Scene(const SceneConfig & config)
		: m_gpuBufferStorage(std::make_unique<SceneGpuBufferStorage>(config)),
		  m_gpuImageStorage(std::make_unique<SceneGpuImageStorage>(config))
	{
	}

	// Out of line so the header can forward declare the storage, not include it, which is what breaks the cycle between the two.
	Scene::~Scene() = default;

	bool Scene::IsValid() const noexcept
	{
		return m_gpuBufferStorage->IsValid();
	}

	std::shared_ptr<SceneObject> Scene::CreateObject(std::string objectName)
	{
		if (m_currentObjectId >= m_gpuBufferStorage->GetObjectCapacity())
		{
			LOG_INFO(fw::Log(), "scene: no room for \"{}\", the storage buffers hold {} objects", objectName, m_gpuBufferStorage->GetObjectCapacity());
			return nullptr;
		}

		return std::make_shared<SceneObject>(*this, std::move(objectName), m_currentObjectId++);
	}

	void Scene::AddRootObject(std::shared_ptr<SceneObject> rootObject)
	{
		if (rootObject != nullptr)
		{
			m_rootObjects.push_back(std::move(rootObject));
		}
	}

	void Scene::Traverse(const TraverseFunc & func) const
	{
		for (const auto & object : m_rootObjects)
		{
			TraverseRecursive(*object, func);
		}
	}

	void Scene::TraverseOrdered(const glm::vec3 & cameraPosition, const TraverseFunc & func, const bool backToFront) const
	{
		std::vector<std::shared_ptr<SceneObject>> objects;
		for (const auto & root : m_rootObjects)
		{
			CollectObjects(root, objects);
		}

		// Squared distance, since it orders things the same way the distance itself would and it saves a root per comparison.
		std::ranges::sort(objects,
			[&cameraPosition, backToFront](const std::shared_ptr<SceneObject> & lhs, const std::shared_ptr<SceneObject> & rhs)
			{
				const glm::vec3 toLhs = lhs->GetWorldPosition() - cameraPosition;
				const glm::vec3 toRhs = rhs->GetWorldPosition() - cameraPosition;

				const float lhsDistance = glm::dot(toLhs, toLhs);
				const float rhsDistance = glm::dot(toRhs, toRhs);

				return backToFront ? lhsDistance > rhsDistance : lhsDistance < rhsDistance;
			});

		for (const auto & object : objects)
		{
			func(*object);
		}
	}

	std::shared_ptr<SceneObject> Scene::FindObjectByName(const std::string_view name) const
	{
		for (const auto & root : m_rootObjects)
		{
			if (std::shared_ptr<SceneObject> found = FindRecursive(root, name))
			{
				return found;
			}
		}

		return nullptr;
	}

	std::uint32_t Scene::GetAttributeCount() const noexcept
	{
		return m_gpuBufferStorage->GetAttributeCount();
	}

	std::vector<azo::rhi::VertexBindingDesc> Scene::GetBindingDescriptions() const
	{
		return m_gpuBufferStorage->GetBindingDescriptions();
	}

	std::vector<azo::rhi::VertexAttributeDesc> Scene::GetAttributeDescriptions() const
	{
		return m_gpuBufferStorage->GetAttributeDescriptions();
	}

	const std::vector<MaterialComponent> & Scene::GetEnabledMaterialComponents() const noexcept
	{
		return m_gpuBufferStorage->GetEnabledMaterialComponents();
	}

	azo::rhi::BufferHandle Scene::GetGeometryBuffer() const noexcept
	{
		return m_gpuBufferStorage->GetGeometryBuffer();
	}

	azo::rhi::BufferHandle Scene::GetTransformStorageBuffer() const noexcept
	{
		return m_gpuBufferStorage->GetTransformStorageBuffer();
	}

	azo::rhi::BufferHandle Scene::GetMaterialStorageBuffer() const noexcept
	{
		return m_gpuBufferStorage->GetMaterialStorageBuffer();
	}

	SceneGpuBufferStorage & Scene::GetGpuBufferStorage() const noexcept
	{
		return *m_gpuBufferStorage;
	}

	SceneGpuImageStorage & Scene::GetGpuImageStorage() const noexcept
	{
		return *m_gpuImageStorage;
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	void Scene::TraverseRecursive(const SceneObject & object, const TraverseFunc & func)
	{
		func(object);
		for (const auto & child : object.GetChildren())
		{
			TraverseRecursive(*child, func);
		}
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	std::shared_ptr<SceneObject> Scene::FindRecursive(const std::shared_ptr<SceneObject> & object, const std::string_view name)
	{
		if (object->GetName() == name)
		{
			return object;
		}

		for (const auto & child : object->GetChildren())
		{
			if (std::shared_ptr<SceneObject> found = FindRecursive(child, name))
			{
				return found;
			}
		}

		return nullptr;
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	void Scene::CollectObjects(const std::shared_ptr<SceneObject> & object, std::vector<std::shared_ptr<SceneObject>> & orderedObjects)
	{
		orderedObjects.push_back(object);
		for (const auto & child : object->GetChildren())
		{
			CollectObjects(child, orderedObjects);
		}
	}
} // namespace fw::scene

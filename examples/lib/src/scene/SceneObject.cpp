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

#include "FW/scene/SceneObject.hpp"

#include "FW/scene/Scene.hpp"

#include <utility>

namespace fw::scene
{
	SceneObject::SceneObject(Scene & scene, std::string name, const std::uint32_t objectId) : m_scene(scene), m_name(std::move(name)), m_objectId(objectId)
	{
		UpdateTransformGpu();
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	void SceneObject::SetTag(std::string tag)
	{
		for (const auto & child : m_children)
		{
			child->SetTag(tag);
		}

		m_tag = std::move(tag);
	}

	void SceneObject::SetPosition(const glm::vec3 & position)
	{
		m_transform.SetPosition(position);
		MarkWorldDirty();
		UpdateTransformGpu();
	}

	void SceneObject::SetEulerAngles(const glm::vec3 & eulerAngles)
	{
		m_transform.SetEulerAngles(eulerAngles);
		MarkWorldDirty();
		UpdateTransformGpu();
	}

	void SceneObject::SetQuaternion(const glm::vec4 & quaternion)
	{
		m_transform.SetQuaternion(quaternion);
		MarkWorldDirty();
		UpdateTransformGpu();
	}

	void SceneObject::SetScale(const glm::vec3 & scale)
	{
		m_transform.SetScale(scale);
		MarkWorldDirty();
		UpdateTransformGpu();
	}

	void SceneObject::SetMesh(const MeshPrimitive & meshPrimitive)
	{
		if (const MeshGpu allocated = m_scene.GetGpuBufferStorage().AllocateMesh(meshPrimitive); allocated.IsValid())
		{
			m_mesh = allocated;
		}
	}

	void SceneObject::SetBuiltinMesh(const BuiltinMeshType builtinMeshType)
	{
		if (const MeshGpu allocated = m_scene.GetGpuBufferStorage().AllocateBuiltinMesh(builtinMeshType); allocated.IsValid())
		{
			m_mesh = allocated;
		}
	}

	void SceneObject::SetMaterial(const Material & material)
	{
		m_material = material;

		const std::vector<std::uint8_t> serialized = SerializeMaterial(material, m_scene.GetEnabledMaterialComponents());
		static_cast<void>(m_scene.GetGpuBufferStorage().UpdateMaterial(m_objectId, serialized));
	}

	void SceneObject::AddChild(const std::shared_ptr<SceneObject> & childObject)
	{
		if (childObject == nullptr || childObject.get() == this)
		{
			return;
		}

		childObject->m_parent = shared_from_this();
		m_children.push_back(childObject);

		// The child's world matrix is now this object's times its own, and its transform has to reach the GPU again saying so.
		childObject->MarkWorldDirty();
		childObject->UpdateTransformGpu();
	}

	glm::vec3 SceneObject::GetWorldPosition() const noexcept
	{
		return glm::vec3{ GetWorldMatrix()[3] };
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	const glm::mat4 & SceneObject::GetWorldMatrix() const noexcept
	{
		if (m_worldDirty)
		{
			const std::shared_ptr<SceneObject> parent = m_parent.lock();
			m_worldMatrix = parent != nullptr ? parent->GetWorldMatrix() * m_transform.GetLocalMatrix() : m_transform.GetLocalMatrix();
			m_worldDirty  = false;
		}

		return m_worldMatrix;
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	void SceneObject::MarkWorldDirty() noexcept
	{
		m_worldDirty = true;
		for (const auto & child : m_children)
		{
			child->MarkWorldDirty();
		}
	}

	// NOLINTNEXTLINE(misc-no-recursion): a scene graph is a tree, so its walks recurse and are bounded by the graph's depth.
	void SceneObject::UpdateTransformGpu() const
	{
		static_cast<void>(m_scene.GetGpuBufferStorage().UpdateTransform(m_objectId, MakeTransformGpu(GetWorldMatrix())));

		for (const auto & child : m_children)
		{
			child->UpdateTransformGpu();
		}
	}
} // namespace fw::scene

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
#include "SceneGpuBufferStorage.hpp"
#include "SceneMesh.hpp"
#include "Transform.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fw::scene
{
	class Scene;

	/**
	 * \brief One node of the scene tree: a transform, optionally a mesh and a material, and its children.
	 *
	 * Setting anything that reaches the GPU writes it through to the scene's storage at the slot this object's id names, so nothing has to be flushed before a
	 * draw. A transform change carries down to the children.
	 *
	 * \attention Objects come from Scene::CreateObject, which hands out the id, and they hold a reference to the scene that made them.
	 */
	class SceneObject final : public std::enable_shared_from_this<SceneObject>
	{
	public:
		SceneObject(Scene & scene, std::string name, std::uint32_t objectId);

		void SetTag(std::string tag);

		void SetPosition(const glm::vec3 & position);

		void SetEulerAngles(const glm::vec3 & eulerAngles);

		void SetQuaternion(const glm::vec4 & quaternion);

		void SetScale(const glm::vec3 & scale);

		/**
		 * \brief Allocates the mesh in the scene's geometry buffer and points this object at it.
		 */
		void SetMesh(const MeshPrimitive & meshPrimitive);

		void SetBuiltinMesh(BuiltinMeshType builtinMeshType);

		/**
		 * \brief Serializes the material against the scene's enabled components and writes it at this object's slot.
		 */
		void SetMaterial(const Material & material);

		void AddChild(const std::shared_ptr<SceneObject> & childObject);

		[[nodiscard]] const std::string & GetName() const noexcept
		{
			return m_name;
		}

		[[nodiscard]] std::uint32_t GetObjectId() const noexcept
		{
			return m_objectId;
		}

		[[nodiscard]] const std::string & GetTag() const noexcept
		{
			return m_tag;
		}

		[[nodiscard]] const Transform & GetTransform() const noexcept
		{
			return m_transform;
		}

		[[nodiscard]] glm::vec3 GetPosition() const noexcept
		{
			return m_transform.GetPosition();
		}

		[[nodiscard]] glm::vec3 GetWorldPosition() const noexcept;

		[[nodiscard]] const glm::mat4 & GetWorldMatrix() const noexcept;

		[[nodiscard]] const std::optional<MeshGpu> & GetMeshGpu() const noexcept
		{
			return m_mesh;
		}

		[[nodiscard]] const std::optional<Material> & GetMaterial() const noexcept
		{
			return m_material;
		}

		[[nodiscard]] const std::vector<std::shared_ptr<SceneObject>> & GetChildren() const noexcept
		{
			return m_children;
		}

		[[nodiscard]] std::shared_ptr<SceneObject> GetParent() const noexcept
		{
			return m_parent.lock();
		}

		/**
		 * \brief True when this object has both of the things a draw needs, a mesh and a material.
		 */
		[[nodiscard]] bool HasRenderable() const noexcept
		{
			return m_mesh.has_value() && m_material.has_value();
		}

	private:
		void MarkWorldDirty() noexcept;

		void UpdateTransformGpu() const;

		Scene & m_scene;
		std::string m_name;
		std::uint32_t m_objectId = 0;
		std::string m_tag;
		Transform m_transform;
		std::optional<MeshGpu> m_mesh;
		std::optional<Material> m_material;

		// Weak, because a child holds its parent and the parent holds the child. Owning both ways would keep the pair alive forever.
		std::weak_ptr<SceneObject> m_parent;
		std::vector<std::shared_ptr<SceneObject>> m_children;

		mutable glm::mat4 m_worldMatrix{ 1.0f };
		mutable bool m_worldDirty = true;
	};
} // namespace fw::scene

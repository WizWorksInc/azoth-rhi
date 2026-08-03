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

#include "SceneMesh.hpp"

#include <glm/glm.hpp>

#include <span>

namespace fw::scene
{
	/**
	 * \brief An axis aligned box, which is what a sample asks when it wants to know where to put the camera.
	 *
	 * Starts empty, meaning a minimum at positive infinity and a maximum at negative infinity, so the first point added becomes both corners without the box
	 * having to be seeded. IsEmpty is what tells the two apart afterwards.
	 */
	class Aabb final
	{
	public:
		Aabb() = default;

		Aabb(const glm::vec3 & min, const glm::vec3 & max) noexcept : m_min(min), m_max(max) {}

		/**
		 * \brief True until something has been added, which is also what a box built from a mesh carrying no positions stays.
		 */
		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_min.x > m_max.x || m_min.y > m_max.y || m_min.z > m_max.z;
		}

		void Add(const glm::vec3 & point) noexcept;

		/**
		 * \brief Grows to cover another box, ignoring an empty one.
		 */
		void Add(const Aabb & other) noexcept;

		/**
		 * \brief Grows to cover every position a mesh carries.
		 */
		void Add(const MeshData & mesh) noexcept;

		/**
		 * \brief Grows to cover every position an accessor carries, reading it the way the storage does.
		 */
		void Add(const Accessor & positions) noexcept;

		void Reset() noexcept;

		/**
		 * \brief The box that contains this one once transformed.
		 *
		 * All eight corners are put through the matrix and re-bounded and not just the two. Transforming only the corners of the range gives a box that no longer
		 * contains the shape as soon as there is a rotation in the matrix.
		 */
		[[nodiscard]] Aabb Transformed(const glm::mat4 & transform) const noexcept;

		[[nodiscard]] glm::vec3 GetMin() const noexcept
		{
			return m_min;
		}

		[[nodiscard]] glm::vec3 GetMax() const noexcept
		{
			return m_max;
		}

		[[nodiscard]] glm::vec3 GetCenter() const noexcept
		{
			return IsEmpty() ? glm::vec3{ 0.0f } : (m_min + m_max) * 0.5f;
		}

		/**
		 * \brief The box's size along each axis.
		 */
		[[nodiscard]] glm::vec3 GetExtent() const noexcept
		{
			return IsEmpty() ? glm::vec3{ 0.0f } : m_max - m_min;
		}

		/**
		 * \brief Radius of the sphere around the centre that contains the box, which is what a camera distance is worked out from.
		 */
		[[nodiscard]] float GetRadius() const noexcept;

	private:
		glm::vec3 m_min{ std::numeric_limits<float>::max() };
		glm::vec3 m_max{ std::numeric_limits<float>::lowest() };
	};
} // namespace fw::scene

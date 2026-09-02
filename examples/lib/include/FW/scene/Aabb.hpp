// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "SceneMesh.hpp"

#include <glm/glm.hpp>

#include <span>

namespace fw::scene
{
	class Aabb final
	{
	public:
		Aabb() = default;

		Aabb(const glm::vec3 & min, const glm::vec3 & max) noexcept : m_min(min), m_max(max) {}

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_min.x > m_max.x || m_min.y > m_max.y || m_min.z > m_max.z;
		}

		void Add(const glm::vec3 & point) noexcept;

		void Add(const Aabb & other) noexcept;

		void Add(const MeshData & mesh) noexcept;

		void Add(const Accessor & positions) noexcept;

		void Reset() noexcept;

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

		[[nodiscard]] glm::vec3 GetExtent() const noexcept
		{
			return IsEmpty() ? glm::vec3{ 0.0f } : m_max - m_min;
		}

		[[nodiscard]] float GetRadius() const noexcept;

	private:
		glm::vec3 m_min{ std::numeric_limits<float>::max() };
		glm::vec3 m_max{ std::numeric_limits<float>::lowest() };
	};
}

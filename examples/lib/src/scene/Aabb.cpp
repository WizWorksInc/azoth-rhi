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

#include "FW/scene/Aabb.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace fw::scene
{
	void Aabb::Add(const glm::vec3 & point) noexcept
	{
		m_min = glm::min(m_min, point);
		m_max = glm::max(m_max, point);
	}

	void Aabb::Add(const Aabb & other) noexcept
	{
		if (other.IsEmpty())
		{
			return;
		}

		m_min = glm::min(m_min, other.m_min);
		m_max = glm::max(m_max, other.m_max);
	}

	void Aabb::Add(const MeshData & mesh) noexcept
	{
		for (const glm::vec3 & position : mesh.positions)
		{
			Add(position);
		}
	}

	void Aabb::Add(const Accessor & positions) noexcept
	{
		for (std::size_t index = 0; index < positions.count; ++index)
		{
			const std::uint8_t * element = GetElement(positions, index);
			if (element == nullptr)
			{
				return;
			}

			// Copied out, not cast in place, because an accessor points into bytes that carry no alignment guarantee.
			glm::vec3 position{};
			std::memcpy(&position, element, sizeof(position));
			Add(position);
		}
	}

	void Aabb::Reset() noexcept
	{
		m_min = glm::vec3{ std::numeric_limits<float>::max() };
		m_max = glm::vec3{ std::numeric_limits<float>::lowest() };
	}

	Aabb Aabb::Transformed(const glm::mat4 & transform) const noexcept
	{
		if (IsEmpty())
		{
			return {};
		}

		const std::array corners{
			glm::vec3{ m_min.x, m_min.y, m_min.z },
			glm::vec3{ m_max.x, m_min.y, m_min.z },
			glm::vec3{ m_min.x, m_max.y, m_min.z },
			glm::vec3{ m_max.x, m_max.y, m_min.z },
			glm::vec3{ m_min.x, m_min.y, m_max.z },
			glm::vec3{ m_max.x, m_min.y, m_max.z },
			glm::vec3{ m_min.x, m_max.y, m_max.z },
			glm::vec3{ m_max.x, m_max.y, m_max.z },
		};

		Aabb result;
		for (const glm::vec3 & corner : corners)
		{
			result.Add(glm::vec3{ transform * glm::vec4{ corner, 1.0f } });
		}

		return result;
	}

	float Aabb::GetRadius() const noexcept
	{
		return IsEmpty() ? 0.0f : glm::length(GetExtent()) * 0.5f;
	}
} // namespace fw::scene

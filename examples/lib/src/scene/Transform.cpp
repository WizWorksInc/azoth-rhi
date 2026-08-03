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

#include "FW/scene/Transform.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace fw::scene
{
	void Transform::SetPosition(const glm::vec3 & position) noexcept
	{
		m_position = position;
		m_dirty	   = true;
	}

	void Transform::SetEulerAngles(const glm::vec3 & eulerDegrees) noexcept
	{
		m_eulerAngles = eulerDegrees;
		m_rotation	  = glm::quat{ glm::radians(eulerDegrees) };
		m_dirty		  = true;
	}

	void Transform::SetQuaternion(const glm::vec4 & quaternion) noexcept
	{
		SetRotation(glm::quat{ quaternion.w, quaternion.x, quaternion.y, quaternion.z });
	}

	void Transform::SetRotation(const glm::quat & rotation) noexcept
	{
		m_rotation = glm::normalize(rotation);

		// Kept in step with the quaternion so the two getters never disagree about which rotation this is.
		m_eulerAngles = glm::degrees(glm::eulerAngles(m_rotation));
		m_dirty		  = true;
	}

	void Transform::SetScale(const glm::vec3 & scale) noexcept
	{
		m_scale = scale;
		m_dirty = true;
	}

	const glm::mat4 & Transform::GetLocalMatrix() const noexcept
	{
		if (m_dirty)
		{
			m_localMatrix = glm::translate(glm::mat4{ 1.0f }, m_position) * glm::mat4_cast(m_rotation) * glm::scale(glm::mat4{ 1.0f }, m_scale);
			m_dirty		  = false;
		}

		return m_localMatrix;
	}

	TransformGpu MakeTransformGpu(const glm::mat4 & worldMatrix) noexcept
	{
		return TransformGpu{
			.modelMatrix  = worldMatrix,
			.normalMatrix = glm::mat4{ glm::inverseTranspose(glm::mat3{ worldMatrix }) },
		};
	}
} // namespace fw::scene

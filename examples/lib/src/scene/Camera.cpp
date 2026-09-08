// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "FW/scene/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace fw::scene
{
	namespace
	{
		constexpr glm::vec3 kForward{ 0.0f, 0.0f, -1.0f };
		constexpr glm::vec3 kRight{ 1.0f, 0.0f, 0.0f };
		constexpr glm::vec3 kUp{ 0.0f, 1.0f, 0.0f };
	}

	glm::mat4 Camera::GetView() const noexcept
	{
		return glm::inverse(m_transform.GetLocalMatrix());
	}

	void Camera::LookAt(const glm::vec3 & eye, const glm::vec3 & target, const glm::vec3 & up) noexcept
	{
		m_transform.SetPosition(eye);

		const glm::vec3 toTarget = target - eye;
		if (glm::dot(toTarget, toTarget) < 1e-12f)
		{
			return;
		}

		const glm::vec3 forward = glm::normalize(toTarget);

		if (std::abs(glm::dot(forward, glm::normalize(up))) > 0.9999f)
		{
			return;
		}

		m_transform.SetRotation(glm::quatLookAtRH(forward, up));
	}

	glm::vec3 Camera::GetForward() const noexcept
	{
		return m_transform.GetQuaternion() * kForward;
	}

	glm::vec3 Camera::GetRight() const noexcept
	{
		return m_transform.GetQuaternion() * kRight;
	}

	glm::vec3 Camera::GetUp() const noexcept
	{
		return m_transform.GetQuaternion() * kUp;
	}

	glm::mat4 PerspectiveCamera::GetProjection() const noexcept
	{
		return glm::perspectiveRH_ZO(glm::radians(m_fieldOfView), m_aspectRatio, m_nearPlane, m_farPlane);
	}

	void PerspectiveCamera::Frame(const Aabb & bounds, const float margin) noexcept
	{
		if (bounds.IsEmpty())
		{
			return;
		}

		const glm::vec3 center = bounds.GetCenter();
		const float radius	   = std::max(bounds.GetRadius(), 1e-4f) * std::max(margin, 1.0f);

		const float vertical   = glm::radians(m_fieldOfView) * 0.5f;
		const float horizontal = std::atan(std::tan(vertical) * m_aspectRatio);
		const float distance   = radius / std::sin(std::min(vertical, horizontal));

		const glm::vec3 back = -GetForward();

		LookAt(center + (back * distance), center);

		m_nearPlane = std::max(distance - radius, radius * 0.001f);
		m_farPlane	= distance + radius;
	}

	glm::mat4 OrthographicCamera::GetProjection() const noexcept
	{
		return glm::orthoRH_ZO(m_left, m_right, m_bottom, m_top, m_nearPlane, m_farPlane);
	}

	void OrthographicCamera::SetViewSize(const float height, const float aspectRatio) noexcept
	{
		const float halfHeight = height * 0.5f;
		const float halfWidth  = halfHeight * aspectRatio;

		SetBounds(-halfWidth, halfWidth, -halfHeight, halfHeight);
	}
}

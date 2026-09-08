// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "Aabb.hpp"
#include "Transform.hpp"

#include <glm/glm.hpp>

namespace fw::scene
{
	class Camera
	{
	public:
		Camera()							   = default;
		Camera(const Camera &)				   = default;
		Camera & operator=(const Camera &)	   = default;
		Camera(Camera &&) noexcept			   = default;
		Camera & operator=(Camera &&) noexcept = default;
		virtual ~Camera()					   = default;

		[[nodiscard]] virtual glm::mat4 GetProjection() const noexcept = 0;

		[[nodiscard]] glm::mat4 GetView() const noexcept;

		[[nodiscard]] glm::mat4 GetViewProjection() const noexcept
		{
			return GetProjection() * GetView();
		}

		void LookAt(const glm::vec3 & eye, const glm::vec3 & target, const glm::vec3 & up = { 0.0f, 1.0f, 0.0f }) noexcept;

		[[nodiscard]] Transform & GetTransform() noexcept
		{
			return m_transform;
		}

		[[nodiscard]] const Transform & GetTransform() const noexcept
		{
			return m_transform;
		}

		[[nodiscard]] glm::vec3 GetPosition() const noexcept
		{
			return m_transform.GetPosition();
		}

		[[nodiscard]] glm::vec3 GetForward() const noexcept;

		[[nodiscard]] glm::vec3 GetRight() const noexcept;

		[[nodiscard]] glm::vec3 GetUp() const noexcept;

	private:
		Transform m_transform;
	};

	class PerspectiveCamera final : public Camera
	{
	public:
		[[nodiscard]] glm::mat4 GetProjection() const noexcept override;

		void SetFieldOfView(const float degrees) noexcept
		{
			m_fieldOfView = degrees;
		}

		void SetAspectRatio(const float aspectRatio) noexcept
		{
			m_aspectRatio = aspectRatio;
		}

		void SetNearPlane(const float nearPlane) noexcept
		{
			m_nearPlane = nearPlane;
		}

		void SetFarPlane(const float farPlane) noexcept
		{
			m_farPlane = farPlane;
		}

		[[nodiscard]] float GetFieldOfView() const noexcept
		{
			return m_fieldOfView;
		}

		[[nodiscard]] float GetAspectRatio() const noexcept
		{
			return m_aspectRatio;
		}

		[[nodiscard]] float GetNearPlane() const noexcept
		{
			return m_nearPlane;
		}

		[[nodiscard]] float GetFarPlane() const noexcept
		{
			return m_farPlane;
		}

		void Frame(const Aabb & bounds, float margin = 1.25f) noexcept;

	private:
		float m_fieldOfView = 60.0f;
		float m_aspectRatio = 1.0f;
		float m_nearPlane	= 0.1f;
		float m_farPlane	= 1000.0f;
	};

	class OrthographicCamera final : public Camera
	{
	public:
		[[nodiscard]] glm::mat4 GetProjection() const noexcept override;

		void SetBounds(const float left, const float right, const float bottom, const float top) noexcept
		{
			m_left	 = left;
			m_right	 = right;
			m_bottom = bottom;
			m_top	 = top;
		}

		void SetViewSize(float height, float aspectRatio) noexcept;

		void SetNearPlane(const float nearPlane) noexcept
		{
			m_nearPlane = nearPlane;
		}

		void SetFarPlane(const float farPlane) noexcept
		{
			m_farPlane = farPlane;
		}

		[[nodiscard]] float GetNearPlane() const noexcept
		{
			return m_nearPlane;
		}

		[[nodiscard]] float GetFarPlane() const noexcept
		{
			return m_farPlane;
		}

	private:
		float m_left	  = -1.0f;
		float m_right	  = 1.0f;
		float m_bottom	  = -1.0f;
		float m_top		  = 1.0f;
		float m_nearPlane = 0.1f;
		float m_farPlane  = 1000.0f;
	};
}

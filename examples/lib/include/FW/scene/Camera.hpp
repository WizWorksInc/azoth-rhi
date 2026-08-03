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

#include "Aabb.hpp"
#include "Transform.hpp"

#include <glm/glm.hpp>

namespace fw::scene
{
	/**
	 * \brief Where a camera is and what it can see, with the projection left to whichever kind it is.
	 *
	 * The matrices this hands back are for the clip space the RHI presents by default: Y up, depth zero at the near plane to one at the far. A sample calling
	 * azo::rhi::SetClipSpace has to change what it does with these too.
	 *
	 * Position and orientation live in a Transform, so a camera is placed the way anything else in the scene is.
	 */
	class Camera
	{
	public:
		Camera()							   = default;
		Camera(const Camera &)				   = default;
		Camera & operator=(const Camera &)	   = default;
		Camera(Camera &&) noexcept			   = default;
		Camera & operator=(Camera &&) noexcept = default;
		virtual ~Camera()					   = default;

		/**
		 * \brief The projection this kind of camera makes, from its own parameters.
		 */
		[[nodiscard]] virtual glm::mat4 GetProjection() const noexcept = 0;

		/**
		 * \brief The inverse of where the camera is, which is what takes a world position into view space.
		 */
		[[nodiscard]] glm::mat4 GetView() const noexcept;

		/**
		 * \brief Projection times view, which is the one matrix a vertex shader usually wants.
		 */
		[[nodiscard]] glm::mat4 GetViewProjection() const noexcept
		{
			return GetProjection() * GetView();
		}

		/**
		 * \brief Points the camera at a target from where it is.
		 *
		 * \param up Which way is up for the roll this leaves the camera at. Ignored when it is parallel to the direction of view, which would name no
		 * orientation at all, and the camera keeps the one it had.
		 */
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

		/**
		 * \brief The direction the camera looks, which is its own negative Z in world space.
		 */
		[[nodiscard]] glm::vec3 GetForward() const noexcept;

		[[nodiscard]] glm::vec3 GetRight() const noexcept;

		[[nodiscard]] glm::vec3 GetUp() const noexcept;

	private:
		Transform m_transform;
	};

	/**
	 * \brief A camera with a vanishing point, which is what almost every sample wants.
	 */
	class PerspectiveCamera final : public Camera
	{
	public:
		[[nodiscard]] glm::mat4 GetProjection() const noexcept override;

		/**
		 * \brief Sets the vertical field of view in degrees.
		 */
		void SetFieldOfView(const float degrees) noexcept
		{
			m_fieldOfView = degrees;
		}

		/**
		 * \brief Sets width over height. A sample calls this whenever the swapchain resizes, or the picture stretches.
		 */
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

		/**
		 * \brief Places the camera so a box fills the view, and puts the planes either side of it.
		 *
		 * The direction it looks from is kept, so a sample sets an angle it likes once and then frames whatever it loaded from there. Does nothing for an
		 * empty box, since there would be nothing to frame.
		 *
		 * \param margin How much bigger than the box to make the framing. One is a tight fit.
		 */
		void Frame(const Aabb & bounds, float margin = 1.25f) noexcept;

	private:
		float m_fieldOfView = 60.0f;
		float m_aspectRatio = 1.0f;
		float m_nearPlane	= 0.1f;
		float m_farPlane	= 1000.0f;
	};

	/**
	 * \brief A camera with no vanishing point, for a plan view or a shadow map's light.
	 */
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

		/**
		 * \brief Sets the bounds from a height and an aspect ratio, centred on the view.
		 */
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
} // namespace fw::scene

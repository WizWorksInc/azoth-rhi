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
#include "Camera.hpp"

#include <glm/glm.hpp>

namespace fw::scene
{
	struct CameraInput final
	{
		glm::vec2 pointerDelta{ 0.0f };

		float wheelDelta = 0.0f;

		bool rotating = false;

		bool panning = false;

		glm::vec3 movement{ 0.0f };

		bool fast = false;
	};

	class OrbitCameraController final
	{
	public:
		void Frame(PerspectiveCamera & camera, const Aabb & bounds, float margin = 1.25f) noexcept;

		void Update(Camera & camera, const CameraInput & input, float deltaSeconds) noexcept;

		void SetTarget(const glm::vec3 & target) noexcept
		{
			m_target = target;
		}

		[[nodiscard]] glm::vec3 GetTarget() const noexcept
		{
			return m_target;
		}

		void SetDistance(float distance) noexcept;

		[[nodiscard]] float GetDistance() const noexcept
		{
			return m_distance;
		}

		void SetRotateSpeed(const float degreesPerPixel) noexcept
		{
			m_rotateSpeed = degreesPerPixel;
		}

		void SetZoomSpeed(const float factorPerNotch) noexcept
		{
			m_zoomSpeed = factorPerNotch;
		}

	private:
		void Apply(Camera & camera) noexcept;

		glm::vec3 m_target{ 0.0f };
		float m_distance = 5.0f;

		float m_yaw	  = 0.0f;
		float m_pitch = 0.0f;

		float m_rotateSpeed = 0.25f;
		float m_zoomSpeed	= 1.1f;
		float m_minDistance = 0.01f;
	};

	class FlyCameraController final
	{
	public:
		void SyncFrom(const Camera & camera) noexcept;

		void Update(Camera & camera, const CameraInput & input, float deltaSeconds) noexcept;

		void SetSpeed(const float unitsPerSecond, const float fastMultiplier = 4.0f) noexcept
		{
			m_speed			 = unitsPerSecond;
			m_fastMultiplier = fastMultiplier;
		}

		void SetLookSpeed(const float degreesPerPixel) noexcept
		{
			m_lookSpeed = degreesPerPixel;
		}

	private:
		float m_yaw	  = 0.0f;
		float m_pitch = 0.0f;

		float m_speed		   = 3.0f;
		float m_fastMultiplier = 4.0f;
		float m_lookSpeed	   = 0.15f;
	};
}

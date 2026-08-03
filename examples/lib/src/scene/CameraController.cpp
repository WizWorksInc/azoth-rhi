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

#include "FW/scene/CameraController.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace fw::scene
{
	namespace
	{
		// Just short of straight up or straight down. At the pole the direction of view and world up line up, and no rotation about that pair is defined.
		constexpr float kPitchLimit = 89.9f;

		// How far a pixel of pan moves the target, per unit of distance from it, which is what makes a pan cover the same part of the screen at any zoom.
		constexpr float kPanPerPixel = 0.0022f;

		// What the fast modifier multiplies a dolly by. The orbit controller has no setter for this, unlike the fly controller's m_fastMultiplier.
		constexpr float kDollyFastMultiplier = 4.0f;

		/*
		 * A direction from a yaw about world up and a pitch above the horizon, both in degrees.
		 *
		 * At zero and zero this is the camera's resting direction of view, which is negative Z.
		 */
		[[nodiscard]] glm::vec3 DirectionFrom(const float yawDegrees, const float pitchDegrees) noexcept
		{
			const float yaw	  = glm::radians(yawDegrees);
			const float pitch = glm::radians(pitchDegrees);
			const float flat  = std::cos(pitch);

			return { flat * std::sin(yaw), std::sin(pitch), -flat * std::cos(yaw) };
		}

		// The yaw and pitch that DirectionFrom would turn back into this direction.
		void AnglesFrom(const glm::vec3 & direction, float & yawDegrees, float & pitchDegrees) noexcept
		{
			if (glm::dot(direction, direction) < 1e-12f)
			{
				return;
			}

			const glm::vec3 unit = glm::normalize(direction);

			pitchDegrees = glm::degrees(std::asin(std::clamp(unit.y, -1.0f, 1.0f)));
			yawDegrees	 = glm::degrees(std::atan2(unit.x, -unit.z));
		}
	} // namespace

	void OrbitCameraController::Frame(PerspectiveCamera & camera, const Aabb & bounds, const float margin) noexcept
	{
		if (bounds.IsEmpty())
		{
			return;
		}

		// The camera works out where to stand, and the orbit is then read back off it, so the two agree without this repeating the framing arithmetic.
		camera.Frame(bounds, margin);

		m_target		   = bounds.GetCenter();
		const glm::vec3 to = camera.GetPosition() - m_target;
		m_distance		   = std::max(glm::length(to), m_minDistance);

		// The angles describe where the camera stands relative to the target, which is the direction it looks reversed.
		AnglesFrom(-to, m_yaw, m_pitch);
	}

	void OrbitCameraController::Update(Camera & camera, const CameraInput & input, const float deltaSeconds) noexcept
	{
		// Pointer deltas are already how far it moved this frame, so scaling them by the frame time would make a fast frame turn less than a slow one.
		if (input.rotating)
		{
			m_yaw += input.pointerDelta.x * m_rotateSpeed;
			m_pitch -= input.pointerDelta.y * m_rotateSpeed;
			m_pitch = std::clamp(m_pitch, -kPitchLimit, kPitchLimit);
		}

		if (input.panning)
		{
			// The target moves against the drag, so the scene follows the pointer without running away from it.
			const float scale = m_distance * kPanPerPixel;
			m_target += ((-camera.GetRight() * input.pointerDelta.x) + (camera.GetUp() * input.pointerDelta.y)) * scale;
		}

		// Multiplied, not added, so one notch covers the same proportion of the way in whether the camera is near or far.
		if (input.wheelDelta != 0.0f)
		{
			m_distance /= std::pow(m_zoomSpeed, input.wheelDelta);
		}

		if (input.movement.z != 0.0f)
		{
			const float speed = input.fast ? kDollyFastMultiplier : 1.0f;
			m_distance -= input.movement.z * m_distance * speed * deltaSeconds;
		}

		m_distance = std::max(m_distance, m_minDistance);

		Apply(camera);
	}

	void OrbitCameraController::SetDistance(const float distance) noexcept
	{
		m_distance = std::max(distance, m_minDistance);
	}

	void OrbitCameraController::Apply(Camera & camera) noexcept
	{
		// The angles say where the camera looks, so where it stands is that far back along the same line.
		const glm::vec3 offset = -DirectionFrom(m_yaw, m_pitch) * m_distance;

		camera.LookAt(m_target + offset, m_target);
	}

	void FlyCameraController::SyncFrom(const Camera & camera) noexcept
	{
		AnglesFrom(camera.GetForward(), m_yaw, m_pitch);
	}

	void FlyCameraController::Update(Camera & camera, const CameraInput & input, const float deltaSeconds) noexcept
	{
		if (input.rotating)
		{
			m_yaw += input.pointerDelta.x * m_lookSpeed;
			m_pitch -= input.pointerDelta.y * m_lookSpeed;
			m_pitch = std::clamp(m_pitch, -kPitchLimit, kPitchLimit);
		}

		const glm::vec3 forward = DirectionFrom(m_yaw, m_pitch);

		// Built against world up and not the camera's own, which is what keeps the horizon level however far the view has been pitched.
		const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{ 0.0f, 1.0f, 0.0f }));
		const glm::vec3 up	  = glm::cross(right, forward);

		const float speed	   = m_speed * (input.fast ? m_fastMultiplier : 1.0f) * deltaSeconds;
		const glm::vec3 motion = ((right * input.movement.x) + (up * input.movement.y) + (forward * input.movement.z)) * speed;

		const glm::vec3 position = camera.GetPosition() + motion;

		camera.LookAt(position, position + forward);
	}
} // namespace fw::scene

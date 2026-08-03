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
#include "Camera.hpp"

#include <glm/glm.hpp>

namespace fw::scene
{
	/**
	 * \brief What a controller needs to know about the last frame's input.
	 *
	 * A plain struct the sample fills from whatever it uses for a window. The samples each bring their own windowing library, so a framework event system would
	 * have to be translated into on every one of them, and a sample that wants no camera would still pay for it.
	 *
	 * Deltas are what happened since the last frame. Held states are what is true right now.
	 */
	struct CameraInput final
	{
		/**
		 * \brief How far the pointer moved, in pixels.
		 */
		glm::vec2 pointerDelta{ 0.0f };

		/**
		 * \brief How far the wheel turned. Positive is away from the user, which zooms in.
		 */
		float wheelDelta = 0.0f;

		/**
		 * \brief True while the button that orbits or looks is held.
		 */
		bool rotating = false;

		/**
		 * \brief True while the button that slides the camera sideways is held.
		 */
		bool panning = false;

		/**
		 * \brief Movement in the camera's own axes, each component in the range minus one to one.
		 *
		 * X is right, Y is up, Z is forward. A sample fills this from whichever keys it reads as movement.
		 */
		glm::vec3 movement{ 0.0f };

		/**
		 * \brief True while the modifier that moves faster is held.
		 */
		bool fast = false;
	};

	/**
	 * \brief Turns a camera around a point it is looking at, what a sample showing one object wants.
	 *
	 * Rotating swings the camera over the sphere around the target, panning slides the target across the view, and the wheel changes the radius. Pitch stops
	 * short of the poles, where the up vector and the view line up.
	 *
	 * Dragging right turns the object right, which puts the camera to its left. The turntable convention, the opposite of the fly controller below.
	 */
	class OrbitCameraController final
	{
	public:
		/**
		 * \brief Points the camera at a box and sets the orbit up to match, which is the usual thing to do after loading a document.
		 */
		void Frame(PerspectiveCamera & camera, const Aabb & bounds, float margin = 1.25f) noexcept;

		/**
		 * \brief Applies a frame of input.
		 *
		 * \param deltaSeconds How long the last frame took, so panning and zooming move the same distance whatever the frame rate.
		 */
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

		/**
		 * \brief Degrees turned per pixel the pointer moves.
		 */
		void SetRotateSpeed(const float degreesPerPixel) noexcept
		{
			m_rotateSpeed = degreesPerPixel;
		}

		/**
		 * \brief What one notch of the wheel multiplies or divides the distance by.
		 */
		void SetZoomSpeed(const float factorPerNotch) noexcept
		{
			m_zoomSpeed = factorPerNotch;
		}

	private:
		void Apply(Camera & camera) noexcept;

		glm::vec3 m_target{ 0.0f };
		float m_distance = 5.0f;

		// Around the target, in degrees. Yaw turns about world up and pitch rises above the horizon.
		float m_yaw	  = 0.0f;
		float m_pitch = 0.0f;

		float m_rotateSpeed = 0.25f;
		float m_zoomSpeed	= 1.1f;
		float m_minDistance = 0.01f;
	};

	/**
	 * \brief Moves a camera the way a flight or a first person view does, for looking around a scene and not at one thing.
	 */
	class FlyCameraController final
	{
	public:
		/**
		 * \brief Reads the camera's current orientation, so a controller picks up from wherever the camera was pointed.
		 */
		void SyncFrom(const Camera & camera) noexcept;

		void Update(Camera & camera, const CameraInput & input, float deltaSeconds) noexcept;

		/**
		 * \brief Units moved per second, and what holding the fast modifier multiplies that by.
		 */
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
} // namespace fw::scene

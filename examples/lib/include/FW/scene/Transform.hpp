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

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fw::scene
{
	/**
	 * \brief Position, rotation and scale, with the matrix they compose to recomputed only once one of them has moved.
	 *
	 * Rotation is held as a quaternion whichever way it was set, and the Euler angles are kept alongside it so the getter answers for the rotation the
	 * object actually has and not for the last triple somebody happened to pass in.
	 */
	class Transform final
	{
	public:
		void SetPosition(const glm::vec3 & position) noexcept;

		/**
		 * \brief Sets rotation from pitch, yaw and roll in degrees, about X, Y and Z.
		 */
		void SetEulerAngles(const glm::vec3 & eulerDegrees) noexcept;

		/**
		 * \brief Sets rotation directly, taking the quaternion as x, y, z, w.
		 */
		void SetQuaternion(const glm::vec4 & quaternion) noexcept;

		/**
		 * \brief Sets rotation from a quaternion already in glm's own order.
		 */
		void SetRotation(const glm::quat & rotation) noexcept;

		void SetScale(const glm::vec3 & scale) noexcept;

		[[nodiscard]] const glm::mat4 & GetLocalMatrix() const noexcept;

		[[nodiscard]] glm::vec3 GetPosition() const noexcept
		{
			return m_position;
		}

		/**
		 * \brief Pitch, yaw and roll in degrees.
		 *
		 * \attention More than one triple names the same rotation, so this is not guaranteed to return what SetEulerAngles was given, only a triple that
		 * composes to the same rotation.
		 */
		[[nodiscard]] glm::vec3 GetEulerAngles() const noexcept
		{
			return m_eulerAngles;
		}

		[[nodiscard]] glm::quat GetQuaternion() const noexcept
		{
			return m_rotation;
		}

		[[nodiscard]] glm::vec3 GetScale() const noexcept
		{
			return m_scale;
		}

	private:
		glm::vec3 m_position{ 0.0f };
		glm::vec3 m_eulerAngles{ 0.0f }; // Pitch (X), yaw (Y), roll (Z).
		glm::vec3 m_scale{ 1.0f };
		glm::quat m_rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		mutable glm::mat4 m_localMatrix{ 1.0f };
		mutable bool m_dirty = true;
	};

	/**
	 * \brief One object's transform as the storage buffer holds it.
	 *
	 * The normal matrix is the inverse transpose of the model matrix's upper 3x3, widened back to 4x4 so both members keep the same alignment on every
	 * backend. A non uniform scale is what makes it necessary: a normal put through the model matrix comes out pointing somewhere else.
	 */
	struct TransformGpu final
	{
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
	};

	/**
	 * \brief Builds the storage buffer form of a world matrix, deriving the normal matrix from it.
	 */
	[[nodiscard]] TransformGpu MakeTransformGpu(const glm::mat4 & worldMatrix) noexcept;
} // namespace fw::scene

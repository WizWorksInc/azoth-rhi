// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fw::scene
{
	class Transform final
	{
	public:
		void SetPosition(const glm::vec3 & position) noexcept;

		void SetEulerAngles(const glm::vec3 & eulerDegrees) noexcept;

		void SetQuaternion(const glm::vec4 & quaternion) noexcept;

		void SetRotation(const glm::quat & rotation) noexcept;

		void SetScale(const glm::vec3 & scale) noexcept;

		[[nodiscard]] const glm::mat4 & GetLocalMatrix() const noexcept;

		[[nodiscard]] glm::vec3 GetPosition() const noexcept
		{
			return m_position;
		}

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
		glm::vec3 m_eulerAngles{ 0.0f };
		glm::vec3 m_scale{ 1.0f };
		glm::quat m_rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		mutable glm::mat4 m_localMatrix{ 1.0f };
		mutable bool m_dirty = true;
	};

	struct TransformGpu final
	{
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };
	};

	[[nodiscard]] TransformGpu MakeTransformGpu(const glm::mat4 & worldMatrix) noexcept;
}

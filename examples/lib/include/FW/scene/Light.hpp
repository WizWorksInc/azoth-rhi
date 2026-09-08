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

#include <cstdint>
#include <string>

namespace fw::scene
{
	enum class LightType : std::uint32_t
	{
		eDirectional = 0,

		ePoint = 1,

		eSpot = 2,
	};

	struct Light final
	{
		std::string name;
		LightType type = LightType::eDirectional;

		glm::vec3 position{ 0.0f };

		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };

		glm::vec3 color{ 1.0f };
		float intensity = 1.0f;

		float range = 0.0f;

		float innerConeAngle = 0.0f;
		float outerConeAngle = 0.7853982f;
	};

	struct LightGpu final
	{
		glm::vec4 positionAndType{ 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 directionAndRange{ 0.0f, -1.0f, 0.0f, 0.0f };

		glm::vec4 colorAndInnerCos{ 1.0f, 1.0f, 1.0f, 1.0f };

		glm::vec4 outerCosAndPadding{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	[[nodiscard]] LightGpu MakeLightGpu(const Light & light) noexcept;
}

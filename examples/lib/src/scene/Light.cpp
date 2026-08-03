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

#include "FW/scene/Light.hpp"

#include <cmath>

namespace fw::scene
{
	LightGpu MakeLightGpu(const Light & light) noexcept
	{
		// Normalized here so a shader can dot against it without checking, and so a light left at a direction of zero does not hand one back full of NaN.
		const float lengthSquared = glm::dot(light.direction, light.direction);
		const glm::vec3 direction = lengthSquared > 1e-12f ? light.direction / std::sqrt(lengthSquared) : glm::vec3{ 0.0f, -1.0f, 0.0f };

		return LightGpu{
			.positionAndType	= glm::vec4{ light.position, static_cast<float>(light.type) },
			.directionAndRange	= glm::vec4{ direction, light.range },
			.colorAndInnerCos	= glm::vec4{ light.color * light.intensity, std::cos(light.innerConeAngle) },
			.outerCosAndPadding = glm::vec4{ std::cos(light.outerConeAngle), 0.0f, 0.0f, 0.0f },
		};
	}
} // namespace fw::scene

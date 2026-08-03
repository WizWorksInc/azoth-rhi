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

#include <cstdint>
#include <string>

namespace fw::scene
{
	enum class LightType : std::uint32_t
	{
		/**
		 * \brief Infinitely far away, so only its direction matters. The sun.
		 */
		eDirectional = 0,

		/**
		 * \brief Radiates from a position in every direction.
		 */
		ePoint = 1,

		/**
		 * \brief A point light narrowed to a cone.
		 */
		eSpot = 2,
	};

	/**
	 * \brief What a light is, in the units glTF's punctual lights extension uses.
	 *
	 * Intensity means different things by type, which is the part worth knowing before wondering why a point light looks wrong next to a directional one:
	 * lux for a directional light, candela for the other two.
	 */
	struct Light final
	{
		std::string name;
		LightType type = LightType::eDirectional;

		glm::vec3 position{ 0.0f };

		/**
		 * \brief Which way the light points, for a directional or spot light. Ignored by a point light.
		 */
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };

		glm::vec3 color{ 1.0f };
		float intensity = 1.0f;

		/**
		 * \brief How far a point or spot light reaches. Zero means it falls off forever, which is what glTF's absent range means.
		 */
		float range = 0.0f;

		/**
		 * \brief The cone of a spot light, in radians from its axis. Inside the inner angle it is at full strength, and it falls to nothing by the outer.
		 */
		float innerConeAngle = 0.0f;
		float outerConeAngle = 0.7853982f;
	};

	/**
	 * \brief One light as a storage buffer holds it.
	 *
	 * Padded to whole vec4s so the layout is the same on every backend without a shader having to agree about how tightly a vec3 packs.
	 */
	struct LightGpu final
	{
		glm::vec4 positionAndType{ 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 directionAndRange{ 0.0f, -1.0f, 0.0f, 0.0f };

		/**
		 * \brief The colour scaled by intensity in rgb, and the cosine of the inner cone angle in w.
		 */
		glm::vec4 colorAndInnerCos{ 1.0f, 1.0f, 1.0f, 1.0f };

		/**
		 * \brief The cosine of the outer cone angle in x, and room for whatever a sample wants in the rest.
		 */
		glm::vec4 outerCosAndPadding{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	/**
	 * \brief Packs a light into the form above, folding intensity into the colour and the cone angles into their cosines.
	 *
	 * The cosines are what a shader compares a dot product against, so taking them here keeps two trigonometric calls out of every pixel.
	 */
	[[nodiscard]] LightGpu MakeLightGpu(const Light & light) noexcept;
} // namespace fw::scene

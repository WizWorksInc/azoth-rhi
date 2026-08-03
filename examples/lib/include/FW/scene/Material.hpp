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

#include "FW/utility/AppendBytes.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace fw::scene
{
	enum class MaterialComponent : std::uint8_t
	{
		eDiffuseColorVec4 = 0,
		eAlbedoColorVec4,
		eSpecularColorVec4,

		eAmbientStrengthFloat,
		eShininessFloat,
		eRoughnessFloat,
		eMetallicFloat,
		eSubSurfaceFloat,
		eSpecularStrengthFloat,
		eSpecularTintFloat,
		eAnisotropicFloat,
		eSheenFloat,
		eSheenTintFloat,
		eClearCoatFloat,
		eClearCoatGlossFloat,
		eReflectivityFloat,
		eOpacityFloat,
		eUVScaleFloat,

		eDiffuseMapTexture,
		eAlbedoMapTexture,
		eSpecularMapTexture,
		eNormalMapTexture,
		eHeightMapTexture,
		eRoughnessMapTexture,
		eMetallicMapTexture,
		eMetallicRoughnessMapTexture, // G Channel is Roughness, B Channel is Metallic.
		eOpacityMapTexture,
		eEmissiveMapTexture,
		eAmbientOcclusionMapTexture,

		eFlipNormalsInt,
	};

	struct Material
	{
		glm::vec4 diffuseColor	= glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 albedoColor	= glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 specularColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };

		float ambientStrength  = 0.05f;
		float shininess		   = 128.0f;
		float roughness		   = 0.5f;
		float metallic		   = 0.5f;
		float subsurface	   = 0.0f;
		float specularStrength = 0.5f;
		float specularTin	   = 0.0f;
		float anisotropic	   = 0.0f;
		float sheen			   = 0.0f;
		float sheenTint		   = 0.0f;
		float clearcoat		   = 0.0f;
		float clearcoatGloss   = 1.0f;
		float reflectivity	   = 0.0f;
		float opacity		   = 1.0f;
		float uvScale		   = 1.0f;

		int diffuseMap			 = -1;
		int albedoMap			 = -1;
		int specularMap			 = -1;
		int normalMap			 = -1;
		int heightMap			 = -1;
		int roughnessMap		 = -1;
		int metallicMap			 = -1;
		int metallicRoughnessMap = -1;
		int opacityMap			 = -1;
		int emissiveMap			 = -1;
		int ambientOcclusionMap	 = -1;

		int flipNormals = 0; // This is a bool as an int.
	};

	inline std::vector<std::uint8_t> SerializeMaterial(const Material & material, const std::vector<MaterialComponent> & enabledMaterialComponents)
	{
		std::vector<std::uint8_t> buffer{};

		for (const MaterialComponent component : enabledMaterialComponents)
		{
			switch (component)
			{
			case MaterialComponent::eDiffuseColorVec4: util::AppendBytes(buffer, material.diffuseColor); break;

			case MaterialComponent::eAlbedoColorVec4: util::AppendBytes(buffer, material.albedoColor); break;

			case MaterialComponent::eSpecularColorVec4: util::AppendBytes(buffer, material.specularColor); break;

			case MaterialComponent::eAmbientStrengthFloat: util::AppendBytes(buffer, material.ambientStrength); break;

			case MaterialComponent::eShininessFloat: util::AppendBytes(buffer, material.shininess); break;

			case MaterialComponent::eRoughnessFloat: util::AppendBytes(buffer, material.roughness); break;

			case MaterialComponent::eMetallicFloat: util::AppendBytes(buffer, material.metallic); break;

			case MaterialComponent::eSubSurfaceFloat: util::AppendBytes(buffer, material.subsurface); break;

			case MaterialComponent::eSpecularStrengthFloat: util::AppendBytes(buffer, material.specularStrength); break;

			case MaterialComponent::eSpecularTintFloat: util::AppendBytes(buffer, material.specularTin); break;

			case MaterialComponent::eAnisotropicFloat: util::AppendBytes(buffer, material.anisotropic); break;

			case MaterialComponent::eSheenFloat: util::AppendBytes(buffer, material.sheen); break;

			case MaterialComponent::eSheenTintFloat: util::AppendBytes(buffer, material.sheenTint); break;

			case MaterialComponent::eClearCoatFloat: util::AppendBytes(buffer, material.clearcoat); break;

			case MaterialComponent::eClearCoatGlossFloat: util::AppendBytes(buffer, material.clearcoatGloss); break;

			case MaterialComponent::eReflectivityFloat: util::AppendBytes(buffer, material.reflectivity); break;

			case MaterialComponent::eOpacityFloat: util::AppendBytes(buffer, material.opacity); break;

			case MaterialComponent::eUVScaleFloat: util::AppendBytes(buffer, material.uvScale); break;

			case MaterialComponent::eDiffuseMapTexture: util::AppendBytes(buffer, material.diffuseMap); break;

			case MaterialComponent::eAlbedoMapTexture: util::AppendBytes(buffer, material.albedoMap); break;

			case MaterialComponent::eSpecularMapTexture: util::AppendBytes(buffer, material.specularMap); break;

			case MaterialComponent::eNormalMapTexture: util::AppendBytes(buffer, material.normalMap); break;

			case MaterialComponent::eHeightMapTexture: util::AppendBytes(buffer, material.heightMap); break;

			case MaterialComponent::eRoughnessMapTexture: util::AppendBytes(buffer, material.roughnessMap); break;

			case MaterialComponent::eMetallicMapTexture: util::AppendBytes(buffer, material.metallicMap); break;

			case MaterialComponent::eMetallicRoughnessMapTexture: util::AppendBytes(buffer, material.metallicRoughnessMap); break;

			case MaterialComponent::eOpacityMapTexture: util::AppendBytes(buffer, material.opacityMap); break;

			case MaterialComponent::eEmissiveMapTexture: util::AppendBytes(buffer, material.emissiveMap); break;

			case MaterialComponent::eAmbientOcclusionMapTexture: util::AppendBytes(buffer, material.ambientOcclusionMap); break;

			case MaterialComponent::eFlipNormalsInt: util::AppendBytes(buffer, material.flipNormals); break;
			}
		}

		// Enforce 16 byte alignment
		constexpr std::size_t alignment = 16uz;
		if (const std::size_t remainder = buffer.size() % alignment; remainder != 0uz)
		{
			const std::size_t padding = alignment - remainder;
			buffer.insert(buffer.end(), padding, 0uz);
		}

		return buffer;
	}

	/**
	 * \brief Bytes one material occupies once serialized, which is the stride of the material storage buffer.
	 *
	 * Derived by serializing a default material and not by adding up component sizes, so it cannot drift from what SerializeMaterial writes.
	 */
	[[nodiscard]] inline std::size_t MaterialStride(const std::vector<MaterialComponent> & enabledMaterialComponents)
	{
		return SerializeMaterial(Material{}, enabledMaterialComponents).size();
	}
} // namespace fw::scene

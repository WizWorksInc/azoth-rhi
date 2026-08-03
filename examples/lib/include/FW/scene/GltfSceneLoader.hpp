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

#include "Scene.hpp"
#include "SceneObject.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fw::scene
{
	/**
	 * \brief What loading a glTF document produced.
	 */
	struct GltfLoadResult final
	{
		/**
		 * \brief The document's top level nodes, already added to the scene as roots.
		 */
		std::vector<std::shared_ptr<SceneObject>> roots;

		std::uint32_t meshCount	   = 0;
		std::uint32_t textureCount = 0;

		/**
		 * \brief What went wrong, when this reports no roots.
		 */
		std::string error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return error.empty();
		}
	};

	/**
	 * \brief How a document is turned into scene objects.
	 */
	struct GltfLoadOptions final
	{
		/**
		 * \brief Prefix put on every object's name, for telling two copies of the same document apart.
		 */
		std::string namePrefix;

		/**
		 * \brief Loads the document's textures and points each material at the ones it names.
		 *
		 * Needs a scene whose image storage came up, which means a config carrying a transfer queue and pool. False leaves every material's texture fields at
		 * -1 and skips the decode, which is what a sample drawing untextured wants.
		 */
		bool loadTextures = true;

		/**
		 * \brief Builds a mip chain for every texture loaded.
		 */
		bool mipmapTextures = true;

		/**
		 * \brief Tags every object this load created, so a later traversal can pick the document's objects out.
		 */
		std::string tag;
	};

	/**
	 * \brief Loads a glTF or GLB document into a scene, one object per node that carries geometry.
	 *
	 * A node's world transform comes from the document's own parent chain and the hierarchy is rebuilt as scene objects, not baked into the vertices.
	 * Each primitive becomes its own object, since a primitive is the unit that carries one material.
	 *
	 * \param path A relative path is resolved with fw::util::AssetPath.
	 */
	[[nodiscard]] GltfLoadResult LoadGltf(Scene & scene, const std::filesystem::path & path, const GltfLoadOptions & options = {});
} // namespace fw::scene

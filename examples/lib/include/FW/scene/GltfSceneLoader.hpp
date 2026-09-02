// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
	struct GltfLoadResult final
	{
		std::vector<std::shared_ptr<SceneObject>> roots;

		std::uint32_t meshCount	   = 0;
		std::uint32_t textureCount = 0;

		std::string error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return error.empty();
		}
	};

	struct GltfLoadOptions final
	{
		std::string namePrefix;

		bool loadTextures = true;

		bool mipmapTextures = true;

		std::string tag;
	};

	[[nodiscard]] GltfLoadResult LoadGltf(Scene & scene, const std::filesystem::path & path, const GltfLoadOptions & options = {});
}

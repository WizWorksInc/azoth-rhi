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

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fw::util
{
	/**
	 * \brief Finds a read-only file relative to the executable's directory.
	 *
	 * The search starts beside the executable and walks up toward the filesystem root, never against the working directory.
	 *
	 * \param relative Resolved against whichever directory is being probed, not the working directory.
	 * \param maxDepth How many parent directories above the executable to try. Zero probes only the executable's own directory.
	 * \return The absolute path of the first match, or an empty path when nothing matched.
	 */
	[[nodiscard]] std::filesystem::path FindResource(const std::filesystem::path & relative, int maxDepth = 8);

	/**
	 * \brief Resolves a path under an assets directory, which is where a sample's data is staged beside its executable.
	 *
	 * \param relative Path below the assets root, such as "deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf".
	 * \return The absolute path of the asset, or an empty path when it was not found.
	 */
	[[nodiscard]] std::filesystem::path AssetPath(const std::filesystem::path & relative);

	/**
	 * \brief Reads a whole file into memory.
	 *
	 * \return The file's bytes, or an empty vector when it could not be opened or read. An empty file reads as an empty vector too, so a caller that needs to
	 * tell the two apart should check the path exists first.
	 */
	[[nodiscard]] std::vector<std::uint8_t> ReadFile(const std::filesystem::path & path);

	/**
	 * \brief Reads a staged asset as text, which is how the samples get at their shader sources.
	 *
	 * \param relative Path below the assets root, such as "hello_triangle/shaders/triangle.slang".
	 * \param error Set to why the read failed, and left alone otherwise.
	 * \return The file's contents, or an empty string when it was not staged or could not be read.
	 */
	[[nodiscard]] std::string LoadTextAsset(const std::filesystem::path & relative, std::string & error);
} // namespace fw::util

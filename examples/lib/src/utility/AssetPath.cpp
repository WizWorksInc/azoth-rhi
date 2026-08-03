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

#include "FW/utility/AssetPath.hpp"

#include "FW/utility/ExecutablePath.hpp"

#include <azoth/rhi/core/platform.hpp>

#include <format>
#include <fstream>
#include <string>
#include <system_error>

namespace fw::util
{
	namespace
	{
		constexpr auto kAssetsDirectory = "assets";
	} // namespace

	std::filesystem::path FindResource(const std::filesystem::path & relative, int maxDepth)
	{
		// An empty relative path joins to the directory being probed, which exists, so the search would hand back a directory and call it a match.
		if (relative.empty())
		{
			return {};
		}

		// A negative depth would skip the loop body entirely and miss a file sitting right beside the executable.
		maxDepth = maxDepth < 0 ? 0 : maxDepth;

		std::error_code ec;
		std::filesystem::path directory = ExecutableDirectory();

#ifdef AZOTH_RHI_OS_MAC
		// An app bundle runs its executable out of Contents/MacOS and keeps read-only data in Contents/Resources. Probing there first is what lets a bundle
		// that has been moved stay self contained. The walk below still covers a plain binary in a build tree.
		if (directory.filename() == "MacOS")
		{
			if (std::filesystem::path resources = directory.parent_path() / "Resources" / relative; std::filesystem::exists(resources, ec))
			{
				return resources;
			}
		}
#endif

		for (int depth = 0; depth <= maxDepth; ++depth)
		{
			if (std::filesystem::path candidate = directory / relative; std::filesystem::exists(candidate, ec))
			{
				return candidate;
			}

			std::filesystem::path parent = directory.parent_path();

			// A root directory is its own parent, which would otherwise spin here until the depth ran out.
			if (parent.empty() || parent == directory)
			{
				break;
			}

			directory = std::move(parent);
		}

		return {};
	}

	std::filesystem::path AssetPath(const std::filesystem::path & relative)
	{
		return FindResource(std::filesystem::path{ kAssetsDirectory } / relative);
	}

	std::vector<std::uint8_t> ReadFile(const std::filesystem::path & path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return {};
		}

		// Sized from the seek, not grown while reading, so one allocation holds the whole file.
		const std::streamoff size = file.tellg();
		if (size < 0)
		{
			return {};
		}

		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		file.seekg(0);
		if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), size)) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		{
			return {};
		}

		return bytes;
	}

	std::string LoadTextAsset(const std::filesystem::path & relative, std::string & error)
	{
		const std::filesystem::path resolved = AssetPath(relative);
		if (resolved.empty())
		{
			error = std::format("{} was not staged beside the executable", relative.generic_string());
			return {};
		}

		const std::vector<std::uint8_t> bytes = ReadFile(resolved);
		if (bytes.empty())
		{
			error = std::format("{} could not be read, or is empty", resolved.generic_string());
			return {};
		}

		return { reinterpret_cast<const char *>(bytes.data()), bytes.size() }; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}
} // namespace fw::util

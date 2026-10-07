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

#include <cstdint> // NOLINT
#include <filesystem>
#include <string>
#include <vector>

namespace fw::util
{
	[[nodiscard]] std::filesystem::path FindResource(const std::filesystem::path & relative, int maxDepth = 8);

	[[nodiscard]] std::filesystem::path AssetPath(const std::filesystem::path & relative);

	[[nodiscard]] std::vector<std::uint8_t> ReadFile(const std::filesystem::path & path);

	[[nodiscard]] std::string LoadTextAsset(const std::filesystem::path & relative, std::string & error);
} // namespace fw::util

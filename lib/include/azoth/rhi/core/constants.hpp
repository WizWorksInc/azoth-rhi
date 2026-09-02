// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <limits>

namespace azo::rhi
{

	inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

	inline constexpr std::uint32_t kAllMips = std::numeric_limits<std::uint32_t>::max();

	inline constexpr std::uint32_t kAllLayers = std::numeric_limits<std::uint32_t>::max();

}

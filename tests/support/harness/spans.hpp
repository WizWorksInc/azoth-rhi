// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>

namespace azo::rhi::test
{

	template <class T, std::size_t Extent>
	[[nodiscard]] constexpr T & At(const std::span<T, Extent> span, const std::size_t index)
	{
		if (index >= span.size())
		{
			throw std::out_of_range("span index out of range");
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the line above is the bounds check.
		return span[index];
	}

}

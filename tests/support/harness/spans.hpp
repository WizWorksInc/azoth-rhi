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

#include <cstddef>
#include <span>
#include <stdexcept>

namespace azo::rhi::test
{

	/**
	 * \brief The checked element access std::span does not carry until C++26.
	 *
	 * Tests reach a span through this, not through operator[] so an index past the end ends the case with a diagnostic instead of reading whatever followed the
	 * span. The library indexes spans directly, where the bound is already proved at the call site.
	 */
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

} // namespace azo::rhi::test

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/build_config.hpp"

#include <atomic>
#include <cstdint>

namespace azo::rhi
{

	namespace
	{

		constexpr std::uint8_t kUnsettled = 0;

		std::atomic<std::uint8_t> g_ClipSpace{ kUnsettled };

		[[nodiscard]] constexpr std::uint8_t encode(const ClipSpaceConvention convention) noexcept
		{
			return static_cast<std::uint8_t>(static_cast<std::uint8_t>(convention) + 1);
		}

	}

	bool set_clip_space(const ClipSpaceConvention convention) noexcept
	{
		std::uint8_t expected = kUnsettled;
		return g_ClipSpace.compare_exchange_strong(expected, encode(convention), std::memory_order_relaxed);
	}

	ClipSpaceConvention get_clip_space() noexcept
	{
		return g_ClipSpace.load(std::memory_order_relaxed) == encode(ClipSpaceConvention::eYDown) ? ClipSpaceConvention::eYDown : ClipSpaceConvention::eYUp;
	}

}

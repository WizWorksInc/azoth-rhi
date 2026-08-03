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

#include "azoth/rhi/core/build_config.hpp"

#include <atomic>
#include <cstdint>

namespace azo::rhi
{

	namespace
	{

		/*
		 * Stored as the convention plus one, leaving zero to mean nothing has settled it. Keeping that distinction in the same word is what lets the first
		 * caller claim it in one compare-exchange and not a flag and a value that a second thread could land between.
		 *
		 * Out of line and not an inline variable so a shared build has one copy. Two would leave the backends flipping against theirs while the application
		 * projected into its own.
		 */
		constexpr std::uint8_t kUnsettled = 0;

		std::atomic<std::uint8_t> g_clipSpace{ kUnsettled };

		[[nodiscard]] constexpr std::uint8_t Encode(const ClipSpaceConvention convention) noexcept
		{
			return static_cast<std::uint8_t>(static_cast<std::uint8_t>(convention) + 1);
		}

	} // namespace

	bool SetClipSpace(const ClipSpaceConvention convention) noexcept
	{
		std::uint8_t expected = kUnsettled;
		return g_clipSpace.compare_exchange_strong(expected, Encode(convention), std::memory_order_relaxed);
	}

	ClipSpaceConvention GetClipSpace() noexcept
	{
		/*
		 * Relaxed carries no data besides the word itself and the contract is that it is settled before the first device exists. Whatever brought that
		 * device up already ordered this store against the threads that record against it so there is nothing here for an acquire to pair with.
		 */
		return g_clipSpace.load(std::memory_order_relaxed) == Encode(ClipSpaceConvention::eYDown) ? ClipSpaceConvention::eYDown : ClipSpaceConvention::eYUp;
	}

} // namespace azo::rhi

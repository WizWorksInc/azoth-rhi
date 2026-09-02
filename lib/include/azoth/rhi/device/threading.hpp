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

namespace azo::rhi
{

	struct FiberId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(FiberId lhs, FiberId rhs) noexcept = default;
	};

	enum class ThreadingMode : std::uint8_t
	{
		eSingleThreaded,

		eThreads,

		eCooperative,
	};

	struct SyncOps final
	{
		void * context											 = nullptr;
		void * (*create)(void * context) noexcept				 = nullptr;
		void (*destroy)(void * context, void * lock) noexcept	 = nullptr;
		void (*acquire)(void * context, void * lock) noexcept	 = nullptr;
		bool (*tryAcquire)(void * context, void * lock) noexcept = nullptr;
		void (*release)(void * context, void * lock) noexcept	 = nullptr;

		FiberId (*currentFiber)(void * context) noexcept = nullptr;

		[[nodiscard]] constexpr bool IsComplete() const noexcept
		{
			return create != nullptr && destroy != nullptr && acquire != nullptr && tryAcquire != nullptr && release != nullptr;
		}
	};

}

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

#include "azoth/base/attr/force_inline.hpp"
#include "azoth/base/config/platform.hpp"

#include <atomic>

#if AZOTH_CPU_X64
	#include <emmintrin.h>
#elif AZOTH_CPU_ARM && AZOTH_COMPILER_MSVC
	#include <intrin.h>
#endif

namespace azo::rhi
{
	AZO_FORCE_INLINE void cpu_spin_hint() noexcept
	{
#if AZOTH_CPU_X64
		_mm_pause();
#elif AZOTH_CPU_ARM
	#if AZOTH_COMPILER_MSVC
		__isb(_ARM64_BARRIER_SY);
	#else
		__asm__ __volatile__("isb" ::: "memory"); // NOLINT(hicpp-no-assembler): deliberate architecture spin hint
	#endif
#endif
	}

	class SpinLock
	{
	public:
		void lock() noexcept
		{
			while (m_locked.exchange(true, std::memory_order_acquire))
			{
				while (m_locked.load(std::memory_order_relaxed))
				{
					cpu_spin_hint();
				}
			}
		}

		[[nodiscard]] bool try_lock() noexcept
		{
			return !m_locked.load(std::memory_order_relaxed) && !m_locked.exchange(true, std::memory_order_acquire);
		}

		void unlock() noexcept
		{
			m_locked.store(false, std::memory_order_release);
		}

	private:
		std::atomic<bool> m_locked{ false };
	};
}

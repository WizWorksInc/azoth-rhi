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

/**
 * \file
 * \brief Device threading policy and cooperative synchronization callbacks.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Opaque cooperative task identity used by profiler fiber tracking.
	 */
	struct FiberId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(FiberId lhs, FiberId rhs) noexcept = default;
	};

	/**
	 * \brief Calling-side threading policy for one device.
	 *
	 * Creates, destroys, and validation use this policy. Command-list recording is not guarded because a command list is owned by one host thread for
	 * its recording lifetime.
	 */
	enum class ThreadingMode : std::uint8_t
	{
		/**
		 * \brief No guards.
		 *
		 * The caller promises single-threaded device access. Concurrent access races without reporting an error.
		 */
		eSingleThreaded,

		/**
		 * \brief Preemptive OS threads.
		 *
		 * The RHI guards device-owned state with its own spin locks.
		 */
		eThreads,

		/**
		 * \brief Cooperative scheduling.
		 *
		 * The host supplies lock primitives through SyncOps so waits can yield fibers instead of blocking workers.
		 */
		eCooperative,
	};

	/**
	 * \brief Cooperative lock operations installed per device.
	 *
	 * The context and created locks are owned by the host scheduler. The RHI only stores the callbacks and opaque pointers for the device lifetime. Per-device
	 * installation lets several consumers in one process use different schedulers.
	 *
	 * \attention release must be valid from a different OS thread than the matching acquire because a fiber may migrate between them.
	 */
	struct SyncOps final
	{
		void * context											 = nullptr;
		void * (*create)(void * context) noexcept				 = nullptr;
		void (*destroy)(void * context, void * lock) noexcept	 = nullptr;
		void (*acquire)(void * context, void * lock) noexcept	 = nullptr;
		bool (*tryAcquire)(void * context, void * lock) noexcept = nullptr;
		void (*release)(void * context, void * lock) noexcept	 = nullptr;

		/**
		 * \brief Returns the cooperative task currently running on this worker.
		 *
		 * Zero means no fiber is active. Null means the host does not provide profiler fiber tracking.
		 */
		FiberId (*currentFiber)(void * context) noexcept = nullptr;

		/**
		 * \brief Returns true when all required lock operations are installed.
		 *
		 * currentFiber is optional. A partially filled required set is refused during device creation.
		 */
		[[nodiscard]] constexpr bool IsComplete() const noexcept
		{
			return create != nullptr && destroy != nullptr && acquire != nullptr && tryAcquire != nullptr && release != nullptr;
		}
	};

} // namespace azo::rhi

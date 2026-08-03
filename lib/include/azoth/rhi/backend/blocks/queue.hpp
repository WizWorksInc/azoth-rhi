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
 * \brief Queue-level ABI blocks published by backend queue objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Required backend queue table for submission, idle waits, timeline operations, and debug labels.
	 *
	 * \note Queue family indices are backend-native indices. They are exposed for diagnostics and interop, not for choosing an RHI queue type.
	 */
	struct QueueApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueueApi), .version = 1 };

		/// Queue identity.

		QueueType (*getType)(void * impl) noexcept			  = nullptr;
		std::uint32_t (*getFamilyIndex)(void * impl) noexcept = nullptr;

		/// Submission.

		/**
		 * \brief Submits recorded work and synchronization operations to the backend queue.
		 *
		 * \attention The backend must either consume SubmitDesc contents during this call or retain its own copies of anything needed after the call returns.
		 */
		bool (*submit)(void * impl, const SubmitDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Blocks until all work already submitted to this queue has completed.
		 */
		bool (*waitIdle)(void * impl, Error * error) noexcept = nullptr;

		/// Timeline synchronization.

		/**
		 * \brief Reads the highest completed value observed for a timeline on this queue.
		 */
		bool (*getCompletedValue)(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Blocks until a timeline reaches value or the timeout expires.
		 *
		 * \note timeoutNanoseconds is an absolute duration in nanoseconds. The backend decides whether zero means poll or immediate timeout.
		 */
		bool (*wait)(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Signals a timeline from the queue without submitting command-list work.
		 */
		bool (*signal)(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept = nullptr;

		/// Debug labels.

		/**
		 * \brief Begins a queue debug-label scope.
		 *
		 * \note Color is packed as 0xRRGGBBAA. Zero requests the backend default.
		 */
		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the current queue debug-label scope.
		 */
		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional backend queue table for sparse resource binding.
	 */
	struct SparseApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SparseApi), .version = 1 };

		/**
		 * \brief Applies sparse memory bindings through the queue's sparse-binding path.
		 *
		 * \attention Sparse binding has queue-ordering semantics, so the backend must honor waits and signals encoded in SparseBindDesc.
		 */
		bool (*bindSparse)(void * impl, const SparseBindDesc & desc, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

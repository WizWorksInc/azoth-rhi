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

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Callbacks for queue submission, synchronization and debug labels.
	 */
	struct QueueApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(QueueApi), .version = 1 };

		/**
		 * \brief Returns the queue's type.
		 * \param impl Backend queue instance.
		 * \return Queue type.
		 */
		QueueType (*getType)(void * impl) noexcept = nullptr;

		/**
		 * \brief Submits command lists with synchronization dependencies.
		 * \param impl Backend queue instance.
		 * \param desc Command lists, timeline waits and signals, and swapchain synchronization.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*submit)(void * impl, const SubmitDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Waits on the host until previously submitted queue work completes.
		 * \param impl Backend queue instance.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*waitIdle)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries a timeline's completed value.
		 * \param impl Backend queue instance.
		 * \param timeline Timeline handle.
		 * \param[out] out Output completed timeline value.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getCompletedValue)(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Waits on the host until a timeline reaches the requested value.
		 * \param impl Backend queue instance.
		 * \param timeline Timeline handle.
		 * \param value Timeline value to wait for.
		 * \param timeoutNanoseconds Wait timeout in nanoseconds.
		 * \param[out] error Optional output for failure details.
		 * \return True when the value is reached, false on timeout or failure.
		 */
		bool (*wait)(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Signals a timeline from the host.
		 * \param impl Backend queue instance.
		 * \param timeline Timeline handle.
		 * \param value Timeline value to signal.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*signal)(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins a debug label scope on the queue.
		 * \param impl Backend queue instance.
		 * \param name Debug label name.
		 * \param color Packed RGBA color, with red in the most significant byte.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the queue's current debug label scope.
		 * \param impl Backend queue instance.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for submitting sparse resource memory bindings.
	 */
	struct SparseApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(SparseApi), .version = 1 };

		/**
		 * \brief Submits sparse buffer and texture memory bindings.
		 * \param impl Backend queue instance.
		 * \param desc Memory bindings, with timeline waits and signals.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*bindSparse)(void * impl, const SparseBindDesc & desc, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

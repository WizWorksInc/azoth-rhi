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

	struct QueueApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueueApi), .version = 1 };

		/**
		 * \brief Returns the queue's type.
		 */
		QueueType (*getType)(void * impl) noexcept = nullptr;

		/**
		 * \brief Submits command lists with synchronization dependencies.
		 */
		bool (*submit)(void * impl, const SubmitDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Waits on the host until previously submitted queue work completes.
		 */
		bool (*waitIdle)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries a timeline's completed value.
		 */
		bool (*getCompletedValue)(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Waits on the host until a timeline reaches the requested value.
		 * \return True when the value is reached, false on timeout or failure.
		 */
		bool (*wait)(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Signals a timeline from the host.
		 */
		bool (*signal)(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept = nullptr;

		/**
		 * \brief Uses packed RGBA colors, with red in the most significant byte.
		 */
		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the queue's current debug label scope.
		 */
		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	struct SparseApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SparseApi), .version = 1 };

		/**
		 * \brief Submits sparse buffer and texture memory bindings.
		 */
		bool (*bindSparse)(void * impl, const SparseBindDesc & desc, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

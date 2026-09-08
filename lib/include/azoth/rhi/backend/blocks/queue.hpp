// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		QueueType (*getType)(void * impl) noexcept = nullptr;

		bool (*submit)(void * impl, const SubmitDesc & desc, Error * error) noexcept = nullptr;

		bool (*waitIdle)(void * impl, Error * error) noexcept = nullptr;

		bool (*getCompletedValue)(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept = nullptr;

		bool (*wait)(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		bool (*signal)(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept = nullptr;

		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	struct SparseApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SparseApi), .version = 1 };

		bool (*bindSparse)(void * impl, const SparseBindDesc & desc, Error * error) noexcept = nullptr;
	};

}

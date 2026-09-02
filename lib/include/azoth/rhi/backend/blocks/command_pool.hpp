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

namespace azo::rhi
{

	struct CommandPoolApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(CommandPoolApi), .version = 1 };

		void * (*allocate)(void * impl, CString debugName, Error * error) noexcept = nullptr;

		bool (*reset)(void * impl, RetirePoint safeAfter, Error * error) noexcept = nullptr;
	};

}

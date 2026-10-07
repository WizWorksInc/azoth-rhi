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

namespace azo::rhi
{
	struct CommandPoolApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(CommandPoolApi),
			.version  = 1,
		};

		/**
		 * \brief Returns the allocated backend command list, or nullptr on failure.
		 * \param impl Command pool.
		 * \param debugName Optional debug name for the command list.
		 * \param[out] error Optional error details.
		 */
		void * (*allocate)(void * impl, CString debugName, Error * error) noexcept = nullptr;

		/**
		 * \brief Resets the pool for reuse.
		 * \param impl Command pool.
		 * \param safeAfter Retire point for the pool's submitted work.
		 * \param[out] error Optional error details.
		 */
		bool (*reset)(void * impl, RetirePoint safeAfter, Error * error) noexcept = nullptr;
	};
} // namespace azo::rhi

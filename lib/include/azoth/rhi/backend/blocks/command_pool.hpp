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
 * \brief Command-pool ABI block published by backend command-pool objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

namespace azo::rhi
{

	/**
	 * \brief Backend table for command-list allocation and command-pool storage reuse.
	 *
	 * Command lists allocated from a pool remain tied to that pool's backend storage. reset may recycle that storage only after the supplied retire point.
	 */
	struct CommandPoolApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(CommandPoolApi), .version = 1 };

		/**
		 * \brief Allocates one backend command-list object from the pool.
		 *
		 * \note debugName is optional diagnostic text and may be null.
		 */
		void * (*allocate)(void * impl, CString debugName, Error * error) noexcept = nullptr;

		/**
		 * \brief Recycles reusable command storage owned by the pool.
		 *
		 * \attention safeAfter must identify a point where every command list using this pool's reusable backend storage is no longer used by the GPU.
		 */
		bool (*reset)(void * impl, RetirePoint safeAfter, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

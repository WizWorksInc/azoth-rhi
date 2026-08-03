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
 * \brief Descriptor-arena ABI block published by backend descriptor-arena objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

namespace azo::rhi
{

	/**
	 * \brief Backend table for descriptor-set allocation and descriptor-arena storage reuse.
	 *
	 * Descriptor arenas own backend descriptor storage used by descriptor sets allocated from them. reset may recycle that storage only after the supplied
	 * retire point.
	 */
	struct DescriptorArenaApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(DescriptorArenaApi), .version = 1 };

		/**
		 * \brief Allocates one descriptor set from arena-owned storage.
		 *
		 * \note Returns an invalid DescriptorSetHandle when the arena cannot satisfy the request.
		 */
		DescriptorSetHandle (*allocate)(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Recycles reusable descriptor storage owned by the arena.
		 *
		 * \attention safeAfter must identify a point where every descriptor set using recycled arena storage is no longer used by the GPU.
		 */
		bool (*reset)(void * impl, RetirePoint safeAfter, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

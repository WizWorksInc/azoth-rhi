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
 * \brief Native-object unwrapping ABI block for backend decorators.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

namespace azo::rhi
{

	/**
	 * \brief Optional table for retrieving the backend object wrapped by a decorator.
	 *
	 * \attention inner must return the next object in the wrapper chain, not the wrapper object passed as impl.
	 * \note The returned pointer must not be null.
	 */
	struct NativeObjectApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(NativeObjectApi), .version = 1 };

		void * (*inner)(void * impl) noexcept = nullptr;
	};

} // namespace azo::rhi

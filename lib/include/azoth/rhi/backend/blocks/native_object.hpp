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

	/**
	 * \brief Callback for accessing a wrapped backend object.
	 */
	struct NativeObjectApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(NativeObjectApi), .version = 1 };

		/**
		 * \brief Returns the object immediately beneath this wrapper.
		 * \param impl Backend wrapper instance.
		 * \return Borrowed pointer to the wrapped backend object.
		 */
		void * (*inner)(void * impl) noexcept = nullptr;
	};

} // namespace azo::rhi

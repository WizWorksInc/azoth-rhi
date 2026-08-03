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
 * \brief Keeping a creation description around for the queries that answer from it.
 */

#include "azoth/rhi/resources/resources.hpp"

namespace azo::rhi::detail
{
	/**
	 * \brief A creation description in the form a backend may hold past the call that carried it.
	 *
	 * Every field of TextureDesc is a value except debugName, which is borrowed for the duration of creation. Storing the description as it arrived would leave
	 * a backend holding that pointer and handing it back after the caller's string went away.
	 *
	 * Not a general deep copy. One field has a lifetime the rest do not.
	 */
	[[nodiscard]] constexpr TextureDesc Recorded(TextureDesc desc) noexcept
	{
		desc.debugName = nullptr;
		return desc;
	}

	/**
	 * \brief The buffer form, for the same reason and with the same one field dropped.
	 */
	[[nodiscard]] constexpr BufferDesc Recorded(BufferDesc desc) noexcept
	{
		desc.debugName = nullptr;
		return desc;
	}
} // namespace azo::rhi::detail

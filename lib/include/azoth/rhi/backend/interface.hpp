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
 * \brief ABI identity types for backend objects and interface blocks.
 */

#include "azoth/rhi/core/hash.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <string_view>

namespace azo::rhi
{

	/**
	 * \brief Stable identity for one backend interface block.
	 */
	struct InterfaceId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(InterfaceId lhs, InterfaceId rhs) noexcept = default;
	};

	/**
	 * \brief Builds a stable interface id from a namespaced block name.
	 *
	 * \param name Fully namespaced ABI block name. It must not change unless the block identity itself changes.
	 */
	[[nodiscard]] consteval InterfaceId MakeInterfaceId(const std::string_view name) noexcept
	{
		return InterfaceId{ hash::Fnv1a64Hash(name) };
	}

	/**
	 * \brief ABI header stored at the start of every interface block.
	 *
	 * byteSize is the number of bytes the backend actually published, so newer readers can reject shorter tables before reading past their layout.
	 * \note version changes only when an existing entry changes meaning. Appending entries changes byteSize without requiring a new version.
	 */
	struct InterfaceHeader final
	{
		std::uint32_t byteSize = 0;
		std::uint32_t version  = 0;
	};

	/**
	 * \brief ABI header stored as the first word of every backend object.
	 */
	struct BackendObject final
	{
		/**
		 * \brief Queries one interface block published by this object.
		 *
		 * \param minVersion Minimum ABI version accepted by the caller.
		 * \return Interface block table, or nullptr when the id or requested version is unsupported.
		 * \attention For the lifetime of object, the same id and minVersion must return the same pointer. This function must tolerate concurrent calls.
		 */
		const void * (*queryInterface)(void * object, InterfaceId id, std::uint32_t minVersion) noexcept = nullptr;
	};

} // namespace azo::rhi

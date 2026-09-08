// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/hash.hpp"

#include <cstdint>
#include <string_view>

namespace azo::rhi
{

	struct InterfaceId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(InterfaceId lhs, InterfaceId rhs) noexcept = default;
	};

	[[nodiscard]] consteval InterfaceId MakeInterfaceId(const std::string_view name) noexcept
	{
		return InterfaceId{ hash::Fnv1a64Hash(name) };
	}

	struct InterfaceHeader final
	{
		std::uint32_t byteSize = 0;
		std::uint32_t version  = 0;
	};

	struct BackendObject final
	{
		const void * (*queryInterface)(void * object, InterfaceId id, std::uint32_t minVersion) noexcept = nullptr;
	};

}

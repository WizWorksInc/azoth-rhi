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

#include "azoth/rhi/core/hash.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint> // NOLINT
#include <string_view>

namespace azo::rhi
{

	struct InterfaceId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(InterfaceId lhs, InterfaceId rhs) noexcept = default;
	};

	[[nodiscard]] consteval InterfaceId make_interface_id(const std::string_view name) noexcept
	{
		return InterfaceId{ hash::fnv1a64_hash(name) };
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

} // namespace azo::rhi

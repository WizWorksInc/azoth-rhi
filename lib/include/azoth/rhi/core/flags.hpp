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

#include <cstdint> // NOLINT
#include <type_traits>

namespace azo::rhi
{

	template <class E>
	concept FlagEnum = std::is_enum_v<E> && requires { typename std::underlying_type_t<E>; };

	template <FlagEnum E>
	class Flags final
	{
	public:
		using Underlying = std::underlying_type_t<E>;

		constexpr Flags() noexcept = default;

		// Intentionally implicit so one enum value can be passed where Flags<E> is expected. NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
		constexpr Flags(E value) noexcept : m_bits(static_cast<Underlying>(value)) {}

		constexpr explicit Flags(Underlying bits) noexcept : m_bits(bits) {}

		[[nodiscard]] constexpr Underlying bits() const noexcept
		{
			return m_bits;
		}

		[[nodiscard]] constexpr bool empty() const noexcept
		{
			return m_bits == 0;
		}

		[[nodiscard]] constexpr bool contains(E value) const noexcept
		{
			const auto bit = static_cast<Underlying>(value);
			return (m_bits & bit) == bit;
		}

		[[nodiscard]] constexpr bool contains(Flags other) const noexcept
		{
			return (m_bits & other.m_bits) == other.m_bits;
		}

		constexpr Flags & operator|=(Flags other) noexcept
		{
			m_bits |= other.m_bits;
			return *this;
		}

		constexpr Flags & operator&=(Flags other) noexcept
		{
			m_bits &= other.m_bits;
			return *this;
		}

		constexpr Flags & operator^=(Flags other) noexcept
		{
			m_bits ^= other.m_bits;
			return *this;
		}

		[[nodiscard]] friend constexpr Flags operator|(Flags lhs, Flags rhs) noexcept
		{
			return Flags(lhs.m_bits | rhs.m_bits);
		}

		[[nodiscard]] friend constexpr Flags operator&(Flags lhs, Flags rhs) noexcept
		{
			return Flags(lhs.m_bits & rhs.m_bits);
		}

		[[nodiscard]] friend constexpr Flags operator^(Flags lhs, Flags rhs) noexcept
		{
			return Flags(lhs.m_bits ^ rhs.m_bits);
		}

		[[nodiscard]] friend constexpr Flags operator~(Flags value) noexcept
		{
			return Flags(static_cast<Underlying>(~value.m_bits));
		}

		[[nodiscard]] friend constexpr bool operator==(Flags lhs, Flags rhs) noexcept = default;

	private:
		Underlying m_bits = 0;
	};

} // namespace azo::rhi

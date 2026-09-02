// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
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

		// Intentionally implicit so one enum value can be passed where Flags<E> is expected. NOLINTNEXTLINE(hicpp-explicit-conversions)
		constexpr Flags(E value) noexcept : m_bits(static_cast<Underlying>(value)) {}

		constexpr explicit Flags(Underlying bits) noexcept : m_bits(bits) {}

		[[nodiscard]] constexpr Underlying Bits() const noexcept
		{
			return m_bits;
		}

		[[nodiscard]] constexpr bool Empty() const noexcept
		{
			return m_bits == 0;
		}

		[[nodiscard]] constexpr bool Contains(E value) const noexcept
		{
			const Underlying bit = static_cast<Underlying>(value);
			return (m_bits & bit) == bit;
		}

		[[nodiscard]] constexpr bool Contains(Flags other) const noexcept
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

}

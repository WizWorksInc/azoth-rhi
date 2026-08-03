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
 * \brief Type-safe scoped-enum flag masks.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <type_traits>

namespace azo::rhi
{

	/**
	 * \brief Enum type accepted by Flags.
	 *
	 * This only verifies that E is an enum. Public flag enums still need an unsigned underlying type and power-of-two values by convention.
	 */
	template <class E>
	concept FlagEnum = std::is_enum_v<E> && requires { typename std::underlying_type_t<E>; };

	/**
	 * \brief Type-safe bitmask over a scoped enum.
	 *
	 * Use Flags<E> for public bitmask parameters instead of raw integers.
	 * \attention Raw bit construction is not validated. Pass only bits already in the enum's flag domain.
	 */
	template <FlagEnum E>
	class Flags final
	{
	public:
		using Underlying = std::underlying_type_t<E>;

		constexpr Flags() noexcept = default;

		// Intentionally implicit so one enum value can be passed where Flags<E> is expected. NOLINTNEXTLINE(hicpp-explicit-conversions)
		constexpr Flags(E value) noexcept : m_bits(static_cast<Underlying>(value)) {}

		/**
		 * \brief Wraps a native or serialized bit pattern that is already in the flag domain.
		 */
		constexpr explicit Flags(Underlying bits) noexcept : m_bits(bits) {}

		[[nodiscard]] constexpr Underlying Bits() const noexcept
		{
			return m_bits;
		}

		[[nodiscard]] constexpr bool Empty() const noexcept
		{
			return m_bits == 0;
		}

		/**
		 * \brief Returns true when all bits in value are present.
		 *
		 * \note A zero-valued enum returns true because the empty set is contained by every mask.
		 */
		[[nodiscard]] constexpr bool Contains(E value) const noexcept
		{
			const Underlying bit = static_cast<Underlying>(value);
			return (m_bits & bit) == bit;
		}

		/**
		 * \brief Returns true when all bits in other are present.
		 *
		 * \note An empty mask returns true because the empty set is contained by every mask.
		 */
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

		/**
		 * \brief Complement over the whole underlying type, for masking a set down and not for holding.
		 *
		 * \attention The result carries every bit the enum does not name, since nothing here knows which of them are in the flag domain. Use it as the
		 * right side of an and, where the left side decides which bits survive, and never keeping it or testing it on its own.
		 */
		[[nodiscard]] friend constexpr Flags operator~(Flags value) noexcept
		{
			return Flags(static_cast<Underlying>(~value.m_bits));
		}

		[[nodiscard]] friend constexpr bool operator==(Flags lhs, Flags rhs) noexcept = default;

	private:
		Underlying m_bits = 0;
	};

} // namespace azo::rhi

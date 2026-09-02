// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace azo::rhi::hash
{
	inline constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
	inline constexpr std::uint64_t kFnv1a64Prime	   = 1099511628211ULL;
	inline constexpr std::uint32_t kFnv1a32OffsetBasis = 2166136261u;
	inline constexpr std::uint32_t kFnv1a32Prime	   = 16777619u;

	struct Fnv1a64 final
	{
		std::uint64_t state = kFnv1a64OffsetBasis;

		constexpr Fnv1a64 & Mix(std::uint64_t value) noexcept
		{
			state ^= value;
			state *= kFnv1a64Prime;
			return *this;
		}

		constexpr Fnv1a64 & MixBytes(std::span<const std::byte> bytes) noexcept
		{
			for (const std::byte b : bytes)
			{
				state ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(b));
				state *= kFnv1a64Prime;
			}
			return *this;
		}

		[[nodiscard]] constexpr std::uint64_t Value() const noexcept
		{
			return state;
		}
	};

	[[nodiscard]] constexpr std::uint64_t Fnv1a64Hash(std::string_view text) noexcept
	{
		Fnv1a64 h;
		for (const char c : text)
		{
			h.Mix(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
		}
		return h.Value();
	}

	[[nodiscard]] constexpr std::uint64_t Fnv1a64Hash(std::span<const std::byte> bytes) noexcept
	{
		return Fnv1a64{}.MixBytes(bytes).Value();
	}

	[[nodiscard]] constexpr std::uint32_t Fnv1a32Hash(std::string_view text) noexcept
	{
		std::uint32_t hash = kFnv1a32OffsetBasis;
		for (const char c : text)
		{
			hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
			hash *= kFnv1a32Prime;
		}
		return hash;
	}
}

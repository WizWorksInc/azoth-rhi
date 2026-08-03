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

#include "azoth/rhi/core/hash.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rhi = azo::rhi;

namespace
{

	TEST(Fnv1a64, MatchesThePublishedVectors)
	{
		static_assert(rhi::hash::Fnv1a64Hash("") == 0xcbf29ce484222325ULL, "the empty string has to fold to the offset basis");
		static_assert(rhi::hash::Fnv1a64Hash("a") == 0xaf63dc4c8601ec8cULL);
		static_assert(rhi::hash::Fnv1a64Hash("foobar") == 0x85944171f73967e8ULL);

		EXPECT_EQ(rhi::hash::Fnv1a64Hash(""), rhi::hash::kFnv1a64OffsetBasis);
		EXPECT_EQ(rhi::hash::Fnv1a64Hash("a"), 0xaf63dc4c8601ec8cULL);
		EXPECT_EQ(rhi::hash::Fnv1a64Hash("foobar"), 0x85944171f73967e8ULL);
	}

	TEST(Fnv1a32, MatchesThePublishedVectors)
	{
		static_assert(rhi::hash::Fnv1a32Hash("") == 0x811c9dc5u);
		static_assert(rhi::hash::Fnv1a32Hash("a") == 0xe40c292cu);
		static_assert(rhi::hash::Fnv1a32Hash("foobar") == 0xbf9cf968u);

		EXPECT_EQ(rhi::hash::Fnv1a32Hash(""), rhi::hash::kFnv1a32OffsetBasis);
		EXPECT_EQ(rhi::hash::Fnv1a32Hash("foobar"), 0xbf9cf968u);
	}

	TEST(Fnv1a64, FoldsBytesUnsignedSoThePlatformCharSignDoesNotShow)
	{
		// char is signed on x86-64 Linux and macOS and unsigned on arm64 Linux. A hash that sign-extended would give two different ids for the same name
		// depending on the build machine, which is exactly the failure that makes a serialized rhi::GraphicsApiId unreadable elsewhere.
		constexpr std::string_view highBit = "\x80\xff";

		std::uint64_t expected = rhi::hash::kFnv1a64OffsetBasis;
		for (const std::uint8_t byte : std::array<std::uint8_t, 2>{ 0x80u, 0xffu })
		{
			expected ^= byte;
			expected *= rhi::hash::kFnv1a64Prime;
		}

		EXPECT_EQ(rhi::hash::Fnv1a64Hash(highBit), expected);
	}

	TEST(Fnv1a64, IncrementalMixMatchesTheOneShotHash)
	{
		constexpr std::string_view text = "azoth.rhi.pipeline";

		rhi::hash::Fnv1a64 incremental;
		for (const char c : text)
		{
			incremental.Mix(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
		}

		EXPECT_EQ(incremental.Value(), rhi::hash::Fnv1a64Hash(text));
	}

	TEST(Fnv1a64, MixIsOrderSensitive)
	{
		const std::uint64_t forward = rhi::hash::Fnv1a64{}.Mix(1).Mix(2).Value();
		const std::uint64_t reverse = rhi::hash::Fnv1a64{}.Mix(2).Mix(1).Value();

		EXPECT_NE(forward, reverse);
	}

	TEST(Fnv1a64, MixBytesMatchesHashingTheSameBytesAsText)
	{
		constexpr std::string_view text = "blob";
		const std::span<const std::byte> bytes{ reinterpret_cast<const std::byte *>(text.data()),
			text.size() }; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

		EXPECT_EQ(rhi::hash::Fnv1a64Hash(bytes), rhi::hash::Fnv1a64Hash(text));
	}

	TEST(Fnv1a64, EmptyByteRunLeavesTheStateAtTheOffsetBasis)
	{
		EXPECT_EQ(rhi::hash::Fnv1a64Hash(std::span<const std::byte>{}), rhi::hash::kFnv1a64OffsetBasis);
	}

	TEST(Fnv1a64, DistinctNamesDoNotCollideAcrossTheIdSpaceWeActuallyUse)
	{
		constexpr std::array names{ std::string_view{ "azoth.rhi.vulkan" },
			std::string_view{ "azoth.rhi.d3d12" },
			std::string_view{ "azoth.rhi.metal" },
			std::string_view{ "azoth.rhi.null" } };

		for (std::size_t lhs = 0; lhs < names.size(); ++lhs)
		{
			for (std::size_t rhs = lhs + 1; rhs < names.size(); ++rhs)
			{
				EXPECT_NE(rhi::hash::Fnv1a64Hash(names[lhs]), rhi::hash::Fnv1a64Hash(names[rhs])) << names[lhs] << " collides with " << names[rhs];
			}
		}
	}

} // namespace

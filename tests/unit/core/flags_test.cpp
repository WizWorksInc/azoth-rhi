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

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	enum class Sample : std::uint32_t // NOLINT(performance-enum-size): stands in for the library's flag enums, which are this wide.
	{
		eNone	= 0,
		eFirst	= 1u << 0u,
		eSecond = 1u << 1u,
		eThird	= 1u << 2u,
	};

	using SampleFlags = rhi::Flags<Sample>;

	TEST(FlagsType, IsATrivialValueTypeOverTheEnumUnderlyingType)
	{
		static_assert(std::same_as<SampleFlags::Underlying, std::uint32_t>);
		static_assert(sizeof(SampleFlags) == sizeof(std::uint32_t));
		static_assert(std::is_trivially_copyable_v<SampleFlags>);

		SUCCEED();
	}

	TEST(FlagsType, AcceptsOnlyEnums)
	{
		static_assert(rhi::FlagEnum<Sample>);
		static_assert(rhi::FlagEnum<rhi::BufferUsage>);
		static_assert(rhi::FlagEnum<rhi::TextureUsage>);
		static_assert(rhi::FlagEnum<rhi::Stage>);
		static_assert(rhi::FlagEnum<rhi::ResourceUse>);
		static_assert(rhi::FlagEnum<rhi::ShaderStage>);
		static_assert(!rhi::FlagEnum<int>);
		static_assert(!rhi::FlagEnum<unsigned>);

		SUCCEED();
	}

	TEST(FlagsValue, DefaultsToEmpty)
	{
		constexpr SampleFlags flags;

		static_assert(flags.empty());
		static_assert(flags.bits() == 0);
		static_assert(!flags.contains(Sample::eFirst));

		EXPECT_TRUE(flags.empty());
	}

	TEST(FlagsValue, ConvertsImplicitlyFromOneEnumerator)
	{
		static_assert(std::convertible_to<Sample, SampleFlags>);

		constexpr SampleFlags flags = Sample::eSecond;
		static_assert(flags.contains(Sample::eSecond));
		static_assert(!flags.contains(Sample::eFirst));

		EXPECT_FALSE(flags.empty());
	}

	TEST(FlagsValue, ConstructsExplicitlyFromARawBitPattern)
	{
		static_assert(!std::convertible_to<std::uint32_t, SampleFlags>);

		constexpr SampleFlags flags{ 0b101u };
		static_assert(flags.contains(Sample::eFirst));
		static_assert(flags.contains(Sample::eThird));
		static_assert(!flags.contains(Sample::eSecond));

		EXPECT_EQ(flags.bits(), 0b101u);
	}

	TEST(FlagsContains, IsASubsetTestAndNotAnIntersectionTest)
	{
		constexpr SampleFlags one  = Sample::eFirst;
		constexpr SampleFlags both = SampleFlags(Sample::eFirst) | Sample::eSecond;

		static_assert(both.contains(one));
		static_assert(!one.contains(both), "a mask holding one bit must not claim to contain two");

		EXPECT_TRUE(both.contains(one));
		EXPECT_FALSE(one.contains(both));
	}

	TEST(FlagsContains, TreatsTheEmptyMaskAsContainedByEverything)
	{
		constexpr SampleFlags empty;
		constexpr SampleFlags populated = Sample::eFirst;

		static_assert(populated.contains(empty));
		static_assert(empty.contains(empty));
		static_assert(!empty.contains(populated));

		SUCCEED();
	}

	TEST(FlagsOperators, CombineIntersectAndToggle)
	{
		constexpr SampleFlags first	 = Sample::eFirst;
		constexpr SampleFlags second = Sample::eSecond;

		static_assert((first | second).bits() == 0b011u);
		static_assert((first & second).empty());
		static_assert(((first | second) & first) == first);
		static_assert((first ^ first).empty());
		static_assert((first ^ second).bits() == 0b011u);

		SUCCEED();
	}

	TEST(FlagsOperators, CompoundFormsMutateInPlace)
	{
		SampleFlags flags = Sample::eFirst;

		flags |= Sample::eSecond;
		EXPECT_TRUE(flags.contains(Sample::eFirst));
		EXPECT_TRUE(flags.contains(Sample::eSecond));

		flags &= Sample::eSecond;
		EXPECT_FALSE(flags.contains(Sample::eFirst));
		EXPECT_TRUE(flags.contains(Sample::eSecond));

		flags ^= Sample::eSecond;
		EXPECT_TRUE(flags.empty());
	}

	TEST(FlagsValue, ComparesByBitsRatherThanByConstructionPath)
	{
		constexpr SampleFlags built = SampleFlags(Sample::eFirst) | Sample::eThird;
		constexpr SampleFlags raw{ 0b101u };

		static_assert(built == raw);
		static_assert(SampleFlags{} != built);

		SUCCEED();
	}

	TEST(FlagsInPublicDescs, HoldTheDefaultsTheHeadersDocument)
	{
		constexpr rhi::BufferDesc buffer{};
		EXPECT_TRUE(buffer.usage.empty());

		constexpr rhi::TextureDesc texture{};
		EXPECT_TRUE(texture.usage.empty());

		constexpr rhi::TextureSubresourceRange range{};
		EXPECT_TRUE(range.aspects.contains(rhi::TextureAspect::eColor));
		EXPECT_FALSE(range.aspects.contains(rhi::TextureAspect::eDepth));

		constexpr rhi::DescriptorBinding binding{};
		EXPECT_TRUE(binding.stages.contains(rhi::ShaderStage::eVertex));
		EXPECT_TRUE(binding.stages.contains(rhi::ShaderStage::eCompute));
	}

	TEST(FlagsInPublicDescs, KeepTheWideAliasesDistinctFromTheStagesTheyCover)
	{
		constexpr rhi::Flags<rhi::Stage> alias = rhi::Stage::eAllGraphics;
		EXPECT_FALSE(alias.contains(rhi::Stage::eVertexWork));
		EXPECT_FALSE(alias.contains(rhi::Stage::eFragmentShading));
	}

	TEST(FlagsInPublicDescs, SurviveTheFullSixtyFourBitStageDomain)
	{
		static_assert(std::same_as<rhi::Flags<rhi::Stage>::Underlying, std::uint64_t>);

		constexpr rhi::Flags<rhi::Stage> wide = rhi::Flags<rhi::Stage>(rhi::Stage::eAllCommands) | rhi::Stage::eAccelBuild;
		EXPECT_TRUE(wide.contains(rhi::Stage::eAllCommands));
		EXPECT_TRUE(wide.contains(rhi::Stage::eAccelBuild));
	}

}

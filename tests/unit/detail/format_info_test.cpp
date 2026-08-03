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

#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"

#include <gtest/gtest.h>

namespace rhi = azo::rhi;

namespace
{

	TEST(FormatInfo, UncompressedFormatsAreOneTexelBlocks)
	{
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eR8UNorm).bytesPerBlock, 1u);
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eD16UNorm).bytesPerBlock, 2u);
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eRGBA8UNorm).bytesPerBlock, 4u);
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eRGBA16Float).bytesPerBlock, 8u);
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eRGBA32Float).bytesPerBlock, 16u);
		EXPECT_FALSE(rhi::detail::IsCompressedFormat(rhi::Format::eRGBA8UNorm));
	}

	TEST(FormatInfo, ThreeChannelFloatIsTwelveBytesNotFour)
	{
		EXPECT_EQ(rhi::detail::BlockInfoOf(rhi::Format::eRGB32Float).bytesPerBlock, 12u);
	}

	TEST(FormatInfo, BlockCompressedFormatsCarryTheirBlockShape)
	{
		for (const rhi::Format format : { rhi::Format::eBC1RGBAUNorm, rhi::Format::eBC3UNorm, rhi::Format::eBC7Srgb, rhi::Format::eBC6HSFloat })
		{
			const rhi::detail::FormatBlockInfo info = rhi::detail::BlockInfoOf(format);
			EXPECT_EQ(info.blockWidth, 4u);
			EXPECT_EQ(info.blockHeight, 4u);
			EXPECT_TRUE(rhi::detail::IsCompressedFormat(format));
		}
	}

	TEST(FormatInfo, BC1IsHalfTheRowPitchOfTheOtherBlockFormats)
	{
		constexpr std::uint32_t width = 64;

		EXPECT_EQ(rhi::detail::TightRowPitch(rhi::Format::eBC1RGBAUNorm, width), 128u) << "16 blocks of 8 bytes";
		EXPECT_EQ(rhi::detail::TightRowPitch(rhi::Format::eBC7UNorm, width), 256u) << "16 blocks of 16 bytes";
		EXPECT_EQ(rhi::detail::TightRowPitch(rhi::Format::eRGBA8UNorm, width), 256u) << "the coincidence BC7 shares";
	}

	TEST(FormatInfo, PartialBlocksRoundUp)
	{
		EXPECT_EQ(rhi::detail::BlockColumns(rhi::Format::eBC1RGBAUNorm, 1), 1u);
		EXPECT_EQ(rhi::detail::BlockColumns(rhi::Format::eBC1RGBAUNorm, 5), 2u);
		EXPECT_EQ(rhi::detail::BlockRows(rhi::Format::eBC1RGBAUNorm, 5), 2u);
	}

	TEST(FormatInfo, SlicePitchCountsBlockRowsNotTexelRows)
	{
		EXPECT_EQ(rhi::detail::TightSlicePitch(rhi::Format::eBC1RGBAUNorm, 64, 64), 128u * 16u);
		EXPECT_EQ(rhi::detail::TightSlicePitch(rhi::Format::eRGBA8UNorm, 64, 64), 256u * 64u);
	}

	TEST(FormatInfo, CombinedDepthStencilHasNoSingleLinearLayout)
	{
		EXPECT_FALSE(rhi::detail::HasLinearLayout(rhi::Format::eD24UNormS8UInt));
		EXPECT_FALSE(rhi::detail::HasLinearLayout(rhi::Format::eD32FloatS8UInt));
		EXPECT_TRUE(rhi::detail::HasLinearLayout(rhi::Format::eD32Float)) << "depth only is a plain linear format";
	}

	TEST(Subresource, CountSentinelsResolveAgainstTheResource)
	{
		const rhi::TextureSubresourceRange range{
			.baseMip	= 0,
			.mipCount	= rhi::kAllMips,
			.baseLayer	= 0,
			.layerCount = rhi::kAllLayers,
		};
		const rhi::detail::ResolvedSubresourceRange resolved = rhi::detail::ResolveSubresourceRange(range, 8, 6);

		EXPECT_EQ(resolved.mipCount, 8u);
		EXPECT_EQ(resolved.layerCount, 6u);
		EXPECT_TRUE(rhi::detail::CoversWholeTexture(resolved, 8, 6));
	}

	TEST(Subresource, ASentinelOnASubrangeResolvesToWhatRemains)
	{
		const rhi::TextureSubresourceRange range{
			.baseMip	= 2,
			.mipCount	= rhi::kAllMips,
			.baseLayer	= 3,
			.layerCount = 1,
		};
		const rhi::detail::ResolvedSubresourceRange resolved = rhi::detail::ResolveSubresourceRange(range, 8, 6);

		EXPECT_EQ(resolved.mipCount, 6u) << "8 mips less the 2 skipped";
		EXPECT_EQ(resolved.layerCount, 1u);
		EXPECT_FALSE(rhi::detail::CoversWholeTexture(resolved, 8, 6));
	}

	TEST(Subresource, AnOverlongExplicitCountIsClamped)
	{
		const rhi::TextureSubresourceRange range{
			.baseMip	= 1,
			.mipCount	= 99,
			.baseLayer	= 0,
			.layerCount = 99,
		};
		const rhi::detail::ResolvedSubresourceRange resolved = rhi::detail::ResolveSubresourceRange(range, 4, 2);

		EXPECT_EQ(resolved.mipCount, 3u);
		EXPECT_EQ(resolved.layerCount, 2u);
	}

	TEST(Subresource, ABaseBeyondTheResourceLeavesNothingToDo)
	{
		const rhi::TextureSubresourceRange range{
			.baseMip	= 9,
			.mipCount	= rhi::kAllMips,
			.baseLayer	= 0,
			.layerCount = 1,
		};
		EXPECT_TRUE(rhi::detail::ResolveSubresourceRange(range, 8, 6).IsEmpty());
	}

} // namespace

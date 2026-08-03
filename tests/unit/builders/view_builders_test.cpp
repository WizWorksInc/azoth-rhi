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

#include "azoth/rhi/builders/texture_view_builders.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace rhi = azo::rhi;

namespace
{

	TEST(TextureViewBuilder, DefaultsToTheFirstMipAndLayerOfATwoDimensionalColorTexture)
	{
		const rhi::TextureViewDesc desc = rhi::TextureViewBuilder{}.Build();

		EXPECT_EQ(desc.type, rhi::TextureViewType::eTex2D);
		EXPECT_EQ(desc.format, rhi::Format::eUndefined) << "an unset view format has to inherit from the source texture";
		EXPECT_TRUE(desc.range.aspects.Contains(rhi::TextureAspect::eColor));
		EXPECT_EQ(desc.range.baseMip, 0u);
		EXPECT_EQ(desc.range.mipCount, 1u);
		EXPECT_EQ(desc.range.baseLayer, 0u);
		EXPECT_EQ(desc.range.layerCount, 1u);
	}

	TEST(TextureViewBuilder, SlicesMipsAndLayersIndependently)
	{
		const rhi::TextureViewDesc desc = rhi::TextureViewBuilder{}.Type(rhi::TextureViewType::eTex2DArray).Mips(2, 3).Layers(4, 2).Build();

		EXPECT_EQ(desc.range.baseMip, 2u);
		EXPECT_EQ(desc.range.mipCount, 3u);
		EXPECT_EQ(desc.range.baseLayer, 4u);
		EXPECT_EQ(desc.range.layerCount, 2u);
	}

	TEST(TextureViewBuilder, RangeReplacesEverythingTheSlicersWouldHaveSet)
	{
		const rhi::TextureViewDesc desc = rhi::TextureViewBuilder{}
											  .Mips(5, 5)
											  .Range(rhi::TextureSubresourceRange{
												  .aspects	  = rhi::TextureAspect::eDepth,
												  .baseMip	  = 0,
												  .mipCount	  = 1,
												  .baseLayer  = 0,
												  .layerCount = 1,
											  })
											  .Build();

		EXPECT_EQ(desc.range.baseMip, 0u);
		EXPECT_EQ(desc.range.mipCount, 1u);
		EXPECT_TRUE(desc.range.aspects.Contains(rhi::TextureAspect::eDepth));
		EXPECT_FALSE(desc.range.aspects.Contains(rhi::TextureAspect::eColor));
	}

	TEST(TextureViewBuilder, SelectsTheDepthAspectForADepthStencilView)
	{
		const rhi::TextureViewDesc depth = rhi::TextureViewBuilder{}.Format(rhi::Format::eD32FloatS8UInt).Aspects(rhi::TextureAspect::eDepth).Build();

		EXPECT_TRUE(depth.range.aspects.Contains(rhi::TextureAspect::eDepth));
		EXPECT_FALSE(depth.range.aspects.Contains(rhi::TextureAspect::eStencil));

		const rhi::TextureViewDesc stencil = rhi::TextureViewBuilder{}.Format(rhi::Format::eD32FloatS8UInt).Aspects(rhi::TextureAspect::eStencil).Build();
		EXPECT_TRUE(stencil.range.aspects.Contains(rhi::TextureAspect::eStencil));
	}

	TEST(TextureViewBuilder, CarriesADebugNameIntoTheDesc)
	{
		rhi::TextureViewBuilder builder;
		builder.DebugName("azoth.rhi.test.view");

		const rhi::TextureViewDesc desc = builder.Build();
		ASSERT_NE(desc.debugName, nullptr);
		EXPECT_STREQ(desc.debugName, "azoth.rhi.test.view");

		EXPECT_EQ(rhi::TextureViewBuilder{}.Build().debugName, nullptr);
	}

	TEST(SamplerBuilder, DefaultsToTrilinearRepeatWithNoComparison)
	{
		const rhi::SamplerDesc desc = rhi::SamplerBuilder{}.Build();

		EXPECT_EQ(desc.magFilter, rhi::Filter::eLinear);
		EXPECT_EQ(desc.minFilter, rhi::Filter::eLinear);
		EXPECT_EQ(desc.mipmapMode, rhi::MipmapMode::eLinear);
		EXPECT_EQ(desc.addressU, rhi::AddressMode::eRepeat);
		EXPECT_EQ(desc.addressV, rhi::AddressMode::eRepeat);
		EXPECT_EQ(desc.addressW, rhi::AddressMode::eRepeat);
		EXPECT_FLOAT_EQ(desc.mipLodBias, 0.0f);
		EXPECT_FALSE(desc.anisotropyEnable);
		EXPECT_FLOAT_EQ(desc.maxAnisotropy, 1.0f);
		EXPECT_FALSE(desc.compareEnable);
		EXPECT_FLOAT_EQ(desc.minLod, 0.0f);
		EXPECT_FLOAT_EQ(desc.maxLod, 1000.0f);
		EXPECT_EQ(desc.borderColor, rhi::BorderColor::eOpaqueBlackFloat);
	}

	TEST(SamplerBuilder, FilterShorthandsSetBothEndsOfTheFilterPair)
	{
		const rhi::SamplerDesc nearest = rhi::SamplerBuilder{}.Nearest().Build();
		EXPECT_EQ(nearest.magFilter, rhi::Filter::eNearest);
		EXPECT_EQ(nearest.minFilter, rhi::Filter::eNearest);

		const rhi::SamplerDesc linear = rhi::SamplerBuilder{}.Nearest().Linear().Build();
		EXPECT_EQ(linear.magFilter, rhi::Filter::eLinear);
		EXPECT_EQ(linear.minFilter, rhi::Filter::eLinear);

		const rhi::SamplerDesc mixed = rhi::SamplerBuilder{}.Filter(rhi::Filter::eNearest, rhi::Filter::eLinear).Build();
		EXPECT_EQ(mixed.magFilter, rhi::Filter::eNearest);
		EXPECT_EQ(mixed.minFilter, rhi::Filter::eLinear);
	}

	TEST(SamplerBuilder, AddressAllSetsEveryAxisAndAddressSetsThemSeparately)
	{
		const rhi::SamplerDesc clamped = rhi::SamplerBuilder{}.AddressAll(rhi::AddressMode::eClampToEdge).Build();
		EXPECT_EQ(clamped.addressU, rhi::AddressMode::eClampToEdge);
		EXPECT_EQ(clamped.addressV, rhi::AddressMode::eClampToEdge);
		EXPECT_EQ(clamped.addressW, rhi::AddressMode::eClampToEdge);

		const rhi::SamplerDesc mixed =
			rhi::SamplerBuilder{}.Address(rhi::AddressMode::eRepeat, rhi::AddressMode::eClampToBorder, rhi::AddressMode::eMirroredRepeat).Build();
		EXPECT_EQ(mixed.addressU, rhi::AddressMode::eRepeat);
		EXPECT_EQ(mixed.addressV, rhi::AddressMode::eClampToBorder);
		EXPECT_EQ(mixed.addressW, rhi::AddressMode::eMirroredRepeat);
	}

	TEST(SamplerBuilder, TakesTheLodRangeAndBiasTogether)
	{
		const rhi::SamplerDesc desc = rhi::SamplerBuilder{}.Lod(1.0f, 8.0f, -0.5f).Build();

		EXPECT_FLOAT_EQ(desc.minLod, 1.0f);
		EXPECT_FLOAT_EQ(desc.maxLod, 8.0f);
		EXPECT_FLOAT_EQ(desc.mipLodBias, -0.5f);
	}

	TEST(SamplerBuilder, EnablesAnisotropyAndComparisonAlongsideTheirValues)
	{
		const rhi::SamplerDesc anisotropic = rhi::SamplerBuilder{}.Anisotropy(16.0f).Build();
		EXPECT_TRUE(anisotropic.anisotropyEnable);
		EXPECT_FLOAT_EQ(anisotropic.maxAnisotropy, 16.0f);

		const rhi::SamplerDesc shadow = rhi::SamplerBuilder{}.Compare(rhi::CompareOp::eLessOrEqual).Build();
		EXPECT_TRUE(shadow.compareEnable);
		EXPECT_EQ(shadow.compareOp, rhi::CompareOp::eLessOrEqual);

		const rhi::SamplerDesc disabled = rhi::SamplerBuilder{}.Anisotropy(16.0f, false).Build();
		EXPECT_FALSE(disabled.anisotropyEnable);
		EXPECT_FLOAT_EQ(disabled.maxAnisotropy, 16.0f);
	}

	TEST(SamplerBuilder, CarriesTheBorderColorUsedByClampToBorder)
	{
		const rhi::SamplerDesc desc = rhi::SamplerBuilder{}.AddressAll(rhi::AddressMode::eClampToBorder).Border(rhi::BorderColor::eOpaqueWhiteFloat).Build();

		EXPECT_EQ(desc.addressU, rhi::AddressMode::eClampToBorder);
		EXPECT_EQ(desc.borderColor, rhi::BorderColor::eOpaqueWhiteFloat);
	}

} // namespace

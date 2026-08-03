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

#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class TextureTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(TextureTest);

	TEST_P(TextureTest, CreatesAndDestroysASampledTexture)
	{
		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);

		ASSERT_TRUE(test::Ok(texture.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, CreatesTheAttachmentShapesARendererNeedsOnItsFirstFrame)
	{
		rhi::Error error{};

		for (const rhi::TextureDesc & desc : { test::samples::ColorTarget2D(), test::samples::DepthTarget2D(), test::samples::MippedTexture2D() })
		{
			const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
			EXPECT_TRUE(test::Ok(texture.IsValid(), error)) << "a baseline texture shape was refused";
			if (texture.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
			}
		}
	}

	TEST_P(TextureTest, ReportsWhatATextureWasCreatedWith)
	{
		rhi::Error error{};
		const rhi::TextureDesc desc		 = test::samples::MippedTexture2D();
		const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureInfo info{};
		ASSERT_TRUE(test::Ok(Dev().GetTextureInfo(texture, info, error), error));

		EXPECT_EQ(info.desc.format, desc.format);
		EXPECT_EQ(info.desc.width, desc.width);
		EXPECT_EQ(info.desc.height, desc.height);
		EXPECT_EQ(info.desc.mipLevels, desc.mipLevels);
		EXPECT_EQ(info.desc.arrayLayers, desc.arrayLayers);
		EXPECT_EQ(info.desc.type, desc.type);
		EXPECT_EQ(info.desc.usage, desc.usage);

		EXPECT_EQ(info.desc.debugName, nullptr) << "a backend kept the caller's debug name past the call that lent it";

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, ReportsWhatABufferWasCreatedWith)
	{
		rhi::Error error{};
		const rhi::BufferDesc desc{
			.size	   = 4096,
			.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopyDst,
			.debugName = "azoth.rhi.test.describedBuffer",
		};

		const rhi::BufferHandle buffer = Dev().CreateBuffer(desc, error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		rhi::BufferInfo info{};
		ASSERT_TRUE(test::Ok(Dev().GetBufferInfo(buffer, info, error), error));

		EXPECT_EQ(info.desc.size, desc.size);
		EXPECT_EQ(info.desc.usage, desc.usage);

		EXPECT_TRUE(info.allocationSize == 0 || info.allocationSize >= desc.size)
			<< "a buffer that owns an allocation reported one smaller than the size it was created with: " << info.allocationSize;

		EXPECT_EQ(info.desc.debugName, nullptr) << "a backend kept the caller's debug name past the call that lent it";

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(TextureTest, RefusesToDescribeABufferThisDeviceNeverCreated)
	{
		rhi::Error error{};
		rhi::BufferInfo info{};

		EXPECT_TRUE(test::Failed(Dev().GetBufferInfo(rhi::BufferHandle{}, info, error), error, rhi::ErrorCode::eInvalidHandle));
		EXPECT_EQ(info.desc.size, 0u);
		EXPECT_EQ(info.allocationSize, 0u);
	}

	TEST_P(TextureTest, RefusesToDescribeATextureThisDeviceNeverCreated)
	{
		rhi::Error error{};
		rhi::TextureInfo info{};

		EXPECT_TRUE(test::Failed(Dev().GetTextureInfo(rhi::TextureHandle{}, info, error), error, rhi::ErrorCode::eInvalidHandle));

		EXPECT_EQ(info.desc.format, rhi::Format::eUndefined);
		EXPECT_EQ(info.allocationSize, 0u);
	}

	TEST_P(TextureTest, CreatesTheTextureTypesTheEnumDeclares)
	{
		rhi::Error error{};

		rhi::TextureDesc oneDimensional = test::samples::SampledTexture2D();
		oneDimensional.type				= rhi::TextureType::eTex1D;
		oneDimensional.height			= 1;

		rhi::TextureDesc volume = test::samples::SampledTexture2D(16);
		volume.type				= rhi::TextureType::eTex3D;
		volume.depth			= 16;

		rhi::TextureDesc cube = test::samples::SampledTexture2D(32);
		cube.type			  = rhi::TextureType::eTexCube;
		cube.arrayLayers	  = 6;

		for (const rhi::TextureDesc & desc : { oneDimensional, volume, cube })
		{
			const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
			EXPECT_TRUE(test::Ok(texture.IsValid(), error)) << "texture type " << static_cast<int>(desc.type) << " was refused";
			if (texture.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
			}
		}
	}

	TEST_P(TextureTest, CreatesAViewOverAnExistingTexture)
	{
		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, CreatesSeveralViewsOverOneTexture)
	{
		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::MippedTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc wholeDesc	   = test::samples::FullTextureView();
		wholeDesc.range.mipCount		   = 4;
		const rhi::TextureViewHandle whole = Dev().CreateTextureView(texture, wholeDesc, error);
		ASSERT_TRUE(test::Ok(whole.IsValid(), error));

		rhi::TextureViewDesc singleMip		= test::samples::FullTextureView();
		singleMip.range.baseMip				= 2;
		singleMip.range.mipCount			= 1;
		const rhi::TextureViewHandle sliced = Dev().CreateTextureView(texture, singleMip, error);
		ASSERT_TRUE(test::Ok(sliced.IsValid(), error));

		EXPECT_NE(whole, sliced) << "two views of the same texture share a handle";

		EXPECT_TRUE(test::Ok(Dev().Destroy(sliced, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(whole, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, ViewsALinearTextureAsSrgbWhenItAllowedFormatViews)
	{
		rhi::Error error{};
		rhi::TextureDesc desc			 = test::samples::SampledTexture2D();
		desc.allowFormatViews			 = true;
		const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc viewDesc = test::samples::FullTextureView();
		viewDesc.format				  = rhi::Format::eRGBA8Srgb;

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, viewDesc, error);
		EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "a texture that allows format views refused one";
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "an sRGB view of a linear texture tripped native validation");

		if (view.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, RefusesAFormatViewOfATextureThatDidNotAllowOne)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "texture formats (the null backend models no resource state)");

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc viewDesc = test::samples::FullTextureView();
		viewDesc.format				  = rhi::Format::eRGBA8Srgb;

		rhi::Error viewError{};
		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, viewDesc, viewError);
		EXPECT_FALSE(view.IsValid()) << "a view reinterpreted a texture that never allowed format views";
		EXPECT_EQ(viewError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, ViewsATextureAtItsOwnFormatWithoutAllowingFormatViews)
	{
		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc sameFormat = test::samples::FullTextureView();
		sameFormat.format				= rhi::Format::eRGBA8UNorm;

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, sameFormat, error);
		EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "naming the texture's own format was treated as a reinterpretation";

		if (view.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, SwizzlesASampledViewWhenTheDeviceReportsIt)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTextureViewSwizzle, "texture view swizzle");

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(1024, rhi::Format::eR8UNorm), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc viewDesc = test::samples::FullTextureView();
		viewDesc.swizzle			  = rhi::ComponentMapping{
			.r = rhi::ComponentSwizzle::eR,
			.g = rhi::ComponentSwizzle::eR,
			.b = rhi::ComponentSwizzle::eR,
			.a = rhi::ComponentSwizzle::eOne,
		};

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, viewDesc, error);
		EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "a sampled texture refused a swizzled view";
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a swizzled sampled view tripped native validation");

		if (view.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, TreatsAnExplicitSelfMappingAsTheIdentitySwizzle)
	{
		// Vulkan defines naming a channel's own component as equivalent to the identity enumerant, so this must not be read as a reinterpretation and
		// must not be refused on a storage texture the way a real swizzle would be.
		static_assert(rhi::ComponentMapping{}.IsIdentity());
		static_assert(rhi::ComponentMapping{
			.r = rhi::ComponentSwizzle::eR, .g = rhi::ComponentSwizzle::eG, .b = rhi::ComponentSwizzle::eB, .a = rhi::ComponentSwizzle::eA }
				.IsIdentity());
		static_assert(!rhi::ComponentMapping{ .r = rhi::ComponentSwizzle::eG }.IsIdentity());
		static_assert(!rhi::ComponentMapping{ .a = rhi::ComponentSwizzle::eOne }.IsIdentity());

		rhi::Error error{};
		rhi::TextureDesc storage = test::samples::SampledTexture2D();
		storage.usage			 = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eStorage;

		const rhi::TextureHandle texture = Dev().CreateTexture(storage, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc viewDesc = test::samples::FullTextureView();
		viewDesc.swizzle			  = rhi::ComponentMapping{
			.r = rhi::ComponentSwizzle::eR, .g = rhi::ComponentSwizzle::eG, .b = rhi::ComponentSwizzle::eB, .a = rhi::ComponentSwizzle::eA
		};

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, viewDesc, error);
		EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "an explicit self-mapping was refused as a swizzle";

		if (view.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, RefusesASwizzledViewThatCouldBeBoundAsStorageOrAnAttachment)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTextureViewSwizzle, "texture view swizzle");
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "texture usage (the null backend models no resource state)");

		rhi::Error error{};
		rhi::TextureDesc storage = test::samples::SampledTexture2D();
		storage.usage			 = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eStorage;

		const rhi::TextureHandle texture = Dev().CreateTexture(storage, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::TextureViewDesc wide = test::samples::FullTextureView();
		wide.swizzle			  = rhi::ComponentMapping{ .a = rhi::ComponentSwizzle::eOne };

		rhi::Error wideError{};
		const rhi::TextureViewHandle refused = Dev().CreateTextureView(texture, wide, wideError);
		EXPECT_FALSE(refused.IsValid()) << "a swizzled view was allowed to inherit storage usage";
		EXPECT_EQ(wideError.code, rhi::ErrorCode::eInvalidArgument);

		rhi::TextureViewDesc narrowed = wide;
		narrowed.usage				  = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled);

		const rhi::TextureViewHandle allowed = Dev().CreateTextureView(texture, narrowed, error);
		EXPECT_TRUE(test::Ok(allowed.IsValid(), error)) << "narrowing the view usage to sampled did not permit the swizzle";

		if (allowed.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(allowed, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, DescribesMultiPlanarFormatsIndependentlyOfAnyDevice)
	{
		static_assert(rhi::PlaneCountOf(rhi::Format::eRGBA8UNorm) == 1);
		static_assert(!rhi::IsMultiPlanarFormat(rhi::Format::eRGBA8UNorm));

		static_assert(rhi::PlaneCountOf(rhi::Format::eG8B8R8Biplanar420UNorm) == 2);
		static_assert(rhi::IsMultiPlanarFormat(rhi::Format::eG8B8R8Biplanar420UNorm));
		static_assert(rhi::PlaneFormatOf(rhi::Format::eG8B8R8Biplanar420UNorm, 0) == rhi::Format::eR8UNorm);
		static_assert(rhi::PlaneFormatOf(rhi::Format::eG8B8R8Biplanar420UNorm, 1) == rhi::Format::eRG8UNorm);
		static_assert(rhi::PlaneFormatOf(rhi::Format::eG8B8R8Biplanar420UNorm, 2) == rhi::Format::eUndefined);

		static_assert(rhi::PlaneExtentDivisorOf(rhi::Format::eG8B8R8Biplanar420UNorm, 0) == 1);
		static_assert(rhi::PlaneExtentDivisorOf(rhi::Format::eG8B8R8Biplanar420UNorm, 1) == 2);

		static_assert(rhi::PlaneCountOf(rhi::Format::eG8B8R8Triplanar420UNorm) == 3);
		static_assert(rhi::PlaneFormatOf(rhi::Format::eG8B8R8Triplanar420UNorm, 2) == rhi::Format::eR8UNorm);

		static_assert(rhi::PlaneIndexOf(rhi::Flags<rhi::TextureAspect>(rhi::TextureAspect::eColor)) == rhi::kNoPlane);
		static_assert(rhi::PlaneIndexOf(rhi::Flags<rhi::TextureAspect>(rhi::TextureAspect::ePlane1)) == 1);
	}

	TEST_P(TextureTest, ViewsOnePlaneOfAMultiPlanarTextureAsItsOwnSinglePlaneFormat)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsMultiPlanarFormats, "multi-planar formats");

		rhi::Error error{};
		rhi::TextureDesc video = test::samples::SampledTexture2D();
		video.format		   = rhi::Format::eG8B8R8Biplanar420UNorm;

		const rhi::TextureHandle texture = Dev().CreateTexture(video, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		for (const rhi::TextureAspect aspect : { rhi::TextureAspect::ePlane0, rhi::TextureAspect::ePlane1 })
		{
			rhi::TextureViewDesc planeView = test::samples::FullTextureView();
			planeView.range.aspects		   = rhi::Flags<rhi::TextureAspect>(aspect);

			const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, planeView, error);
			EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "a plane view of a biplanar texture was refused";
			AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a plane view tripped native validation");

			if (view.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
			}
		}

		rhi::TextureViewDesc missingPlane = test::samples::FullTextureView();
		missingPlane.range.aspects		  = rhi::Flags<rhi::TextureAspect>(rhi::TextureAspect::ePlane2);

		rhi::Error planeError{};
		const rhi::TextureViewHandle refused = Dev().CreateTextureView(texture, missingPlane, planeError);
		if (!IsNullBackend())
		{
			EXPECT_FALSE(refused.IsValid()) << "a view named a plane the format does not have";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, RefusesAMultiPlanarTextureWhereTheBackendCannotCreateOne)
	{
		// Metal builds textures from a descriptor and MTLPixelFormat has no multi-planar member, so the honest answer is a refusal at creation rather
		// than a texture that cannot be sampled.
		AZO_RHI_REQUIRE_CAP(!Caps().supportsMultiPlanarFormats, "a backend without multi-planar creation");

		rhi::Error error{};
		rhi::TextureDesc video = test::samples::SampledTexture2D();
		video.format		   = rhi::Format::eG8B8R8Biplanar420UNorm;

		const rhi::TextureHandle texture = Dev().CreateTexture(video, error);
		EXPECT_FALSE(texture.IsValid()) << "a backend reporting no multi-planar support created one anyway";
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFormat);
	}

	TEST_P(TextureTest, SamplesAMultiPlanarTextureThroughAYcbcrConversionWhereTheDeviceHasOne)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsSamplerYcbcrConversion, "sampler Y'CbCr conversion");

		rhi::Error error{};
		rhi::TextureDesc video = test::samples::SampledTexture2D();
		video.format		   = rhi::Format::eG8B8R8Biplanar420UNorm;

		const rhi::TextureHandle texture = Dev().CreateTexture(video, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		const rhi::SamplerYcbcrConversionDesc conversion{
			.format = rhi::Format::eG8B8R8Biplanar420UNorm,
			.model	= rhi::YcbcrModel::eYcbcr709,
			.range	= rhi::YcbcrRange::eNarrow,
		};

		rhi::TextureViewDesc viewDesc = test::samples::FullTextureView();
		viewDesc.ycbcrConversion	  = &conversion;

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, viewDesc, error);
		EXPECT_TRUE(test::Ok(view.IsValid(), error)) << "a view carrying a Y'CbCr conversion was refused";

		// Vulkan pins a conversion sampler to edge clamping with no anisotropy and no comparison, so this is the only shape one can take.
		rhi::SamplerDesc samplerDesc = test::samples::LinearSampler();
		samplerDesc.addressU		 = rhi::AddressMode::eClampToEdge;
		samplerDesc.addressV		 = rhi::AddressMode::eClampToEdge;
		samplerDesc.addressW		 = rhi::AddressMode::eClampToEdge;
		samplerDesc.ycbcrConversion	 = &conversion;

		const rhi::SamplerHandle sampler = Dev().CreateSampler(samplerDesc, error);
		EXPECT_TRUE(test::Ok(sampler.IsValid(), error)) << "a sampler carrying a Y'CbCr conversion was refused";
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a Y'CbCr conversion tripped native validation");

		if (sampler.IsValid())
		{
			const std::array<rhi::SamplerHandle, 1> immutable{ sampler };
			const std::array<rhi::DescriptorBinding, 1> bindings{
				rhi::DescriptorBinding{
					.binding		   = 0,
					.type			   = rhi::DescriptorType::eCombinedImageSampler,
					.count			   = 1,
					.stages			   = rhi::ShaderStage::eFragment,
					.immutableSamplers = immutable,
				},
			};

			const rhi::DescriptorSetLayoutHandle layout = Dev().CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings }, error);
			EXPECT_TRUE(test::Ok(layout.IsValid(), error)) << "a layout baking in a Y'CbCr sampler was refused";
			AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "an immutable Y'CbCr sampler tripped native validation");

			if (layout.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
			}
			EXPECT_TRUE(test::Ok(Dev().Destroy(sampler, {}, error), error));
		}
		if (view.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, RefusesAWholeTextureViewOfAMultiPlanarFormatWithNoConversion)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsMultiPlanarFormats, "multi-planar formats");
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "texture formats (the null backend models no resource state)");

		rhi::Error error{};
		rhi::TextureDesc video = test::samples::SampledTexture2D();
		video.format		   = rhi::Format::eG8B8R8Biplanar420UNorm;

		const rhi::TextureHandle texture = Dev().CreateTexture(video, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::Error viewError{};
		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, test::samples::FullTextureView(), viewError);
		EXPECT_FALSE(view.IsValid()) << "a whole-texture view of a multi-planar format was allowed with no conversion";
		EXPECT_EQ(viewError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(TextureTest, RefusesAYcbcrConversionWhereTheBackendHasNoSuchObject)
	{
		AZO_RHI_REQUIRE_CAP(!Caps().supportsSamplerYcbcrConversion, "a backend without sampler Y'CbCr conversion");
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "sampler state (the null backend models none)");

		// Direct3D 12 and Metal have no equivalent object, so asking for one is refused, not quietly sampling the raw planes as if it were RGB.
		const rhi::SamplerYcbcrConversionDesc conversion{ .format = rhi::Format::eG8B8R8Biplanar420UNorm };

		rhi::SamplerDesc samplerDesc = test::samples::LinearSampler();
		samplerDesc.ycbcrConversion	 = &conversion;

		rhi::Error error{};
		const rhi::SamplerHandle sampler = Dev().CreateSampler(samplerDesc, error);
		EXPECT_FALSE(sampler.IsValid()) << "a backend with no conversion object accepted one";
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature);
	}

	/*
	 * Both of these reached a driver assertion before they were checked here. Metal refuses neither by returning an error, it asserts inside validateWithDevice
	 * and takes the process down, and Vulkan on MoltenVK builds the same descriptor and dies the same way. The Null backend accepted both, which is why a run
	 * with no GPU never saw it.
	 */
	TEST_P(TextureTest, RefusesATextureWithAZeroDimension)
	{
		for (const char * axis : { "width", "height", "depth" })
		{
			rhi::TextureDesc desc = test::samples::SampledTexture2D();
			if (std::string_view(axis) == "width")
			{
				desc.width = 0;
			}
			else if (std::string_view(axis) == "height")
			{
				desc.height = 0;
			}
			else
			{
				desc.depth = 0;
			}

			rhi::Error error{};
			const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
			EXPECT_FALSE(texture.IsValid()) << "a texture with a zero " << axis << " was accepted";
			EXPECT_EQ(error.code, rhi::ErrorCode::eInvalidArgument) << "zero " << axis;
		}
	}

	TEST_P(TextureTest, RefusesMoreMipLevelsThanTheExtentCanHold)
	{
		rhi::TextureDesc desc = test::samples::SampledTexture2D(16);

		// Sixteen texels give five levels counting the base, so six is the first that cannot exist.
		desc.mipLevels = 6;

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
		EXPECT_FALSE(texture.IsValid()) << "a 16x16 texture was given a sixth mip level";
		EXPECT_EQ(error.code, rhi::ErrorCode::eInvalidArgument);
	}

	TEST_P(TextureTest, RefusesToViewATextureThatWasNeverCreated)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::TextureViewHandle view = Dev().CreateTextureView(
			rhi::TextureHandle{
				.index		= 9999,
				.generation = 3,
			},
			test::samples::FullTextureView(),
			error);

		EXPECT_FALSE(view.IsValid()) << "a view was created over a texture handle the device never issued";
	}

	TEST_P(TextureTest, RefusesToViewADestroyedTexture)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));
		ASSERT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));

		rhi::Error viewError{};
		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, test::samples::FullTextureView(), viewError);
		EXPECT_FALSE(view.IsValid()) << "a view was created over a destroyed texture";
	}

	TEST_P(TextureTest, CreatesAndDestroysASampler)
	{
		rhi::Error error{};
		const rhi::SamplerHandle sampler = Dev().CreateSampler(test::samples::LinearSampler(), error);

		ASSERT_TRUE(test::Ok(sampler.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(sampler, {}, error), error));
	}

	TEST_P(TextureTest, CreatesAComparisonSamplerForShadowLookups)
	{
		rhi::Error error{};

		rhi::SamplerDesc shadow = test::samples::LinearSampler();
		shadow.compareEnable	= true;
		shadow.compareOp		= rhi::CompareOp::eLessOrEqual;
		shadow.addressU			= rhi::AddressMode::eClampToEdge;
		shadow.addressV			= rhi::AddressMode::eClampToEdge;

		const rhi::SamplerHandle sampler = Dev().CreateSampler(shadow, error);
		ASSERT_TRUE(test::Ok(sampler.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(sampler, {}, error), error));
	}

	TEST_P(TextureTest, HonoursAnisotropyOnlyWhenTheDeviceReportsIt)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsAnisotropy, "anisotropic filtering");

		rhi::Error error{};
		rhi::SamplerDesc desc = test::samples::LinearSampler();
		desc.anisotropyEnable = true;
		desc.maxAnisotropy	  = 4.0f;

		const rhi::SamplerHandle sampler = Dev().CreateSampler(desc, error);
		ASSERT_TRUE(test::Ok(sampler.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(sampler, {}, error), error));
	}

	TEST_P(TextureTest, ReportsAMemoryFootprintForATextureDesc)
	{
		rhi::Error error{};
		rhi::MemoryInfo info{};

		if (!Dev().GetTextureMemoryInfo(test::samples::SampledTexture2D(), info, error))
		{
			GTEST_SKIP() << "this backend does not report texture memory info: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(true, error));
	}

	TEST_P(TextureTest, KeepsTextureViewAndSamplerHandleDomainsApart)
	{
		static_assert(!std::is_convertible_v<rhi::TextureHandle, rhi::TextureViewHandle>);
		static_assert(!std::is_convertible_v<rhi::TextureViewHandle, rhi::SamplerHandle>);

		SUCCEED();
	}

} // namespace

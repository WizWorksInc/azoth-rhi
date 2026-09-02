// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BlendableFormatProbe : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BlendableFormatProbe);

	constexpr std::string_view kSource = R"(
		#include <metal_stdlib>
		using namespace metal;
		vertex float4 vertexMain() { return float4(0.0, 0.0, 0.0, 1.0); }
		fragment float4 fragmentMain() { return float4(1.0, 0.0, 0.0, 1.0); }
	)";

	[[nodiscard]] bool BlendPipelineBuilds(rhi::Device device, const rhi::Format format, rhi::Error & error)
	{
		rhi::PipelineLayoutHandle layout = device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.probe.layout" }, error);
		if (!layout.IsValid())
		{
			return false;
		}

		rhi::ShaderBinary vertex{};
		vertex.stage	  = rhi::ShaderStage::eVertex;
		vertex.format	  = rhi::ShaderBinaryFormat::eBackendNative;
		vertex.isSource	  = true;
		vertex.entryPoint = "vertexMain";
		vertex.data		  = kSource.data();
		vertex.size		  = kSource.size();

		rhi::ShaderBinary fragment = vertex;
		fragment.stage			   = rhi::ShaderStage::eFragment;
		fragment.entryPoint		   = "fragmentMain";

		const std::array shaders{ vertex, fragment };
		const rhi::VertexInputDesc vertexInput{};

		rhi::GraphicsPipelineDesc desc{};
		desc.layout									  = layout;
		desc.vertexInput							  = &vertexInput;
		desc.shaders								  = shaders;
		desc.renderTarget.colorFormats[0]			  = format;
		desc.renderTarget.colorFormatCount			  = 1;
		desc.blend.attachmentCount					  = 1;
		desc.blend.attachments[0].blendEnable		  = true;
		desc.blend.attachments[0].srcColorBlendFactor = rhi::BlendFactor::eSrcAlpha;
		desc.blend.attachments[0].dstColorBlendFactor = rhi::BlendFactor::eOneMinusSrcAlpha;
		desc.debugName								  = "azoth.rhi.probe.blend";

		const rhi::GraphicsPipelineHandle pipeline = device.CreateGraphicsPipeline(desc, error);
		const bool built						   = pipeline.IsValid();

		rhi::Error cleanup{};
		if (built)
		{
			static_cast<void>(device.Destroy(pipeline, {}, cleanup));
		}

		static_cast<void>(device.Destroy(layout, {}, cleanup));
		return built;
	}

	TEST_P(BlendableFormatProbe, ABlendingPipelineIsBuiltExactlyWhenTheFormatIsAdvertisedAsBlendable)
	{
		if (Caps().apiId != rhi::MetalApi::id && Caps().apiId != rhi::Metal4Api::id)
		{
			GTEST_SKIP() << "this probe compiles MSL source, so it only runs on the Metal backends";
		}

		rhi::DeviceDesc unguarded = test::DefaultDeviceDesc();
		unguarded.validation	  = rhi::ValidationMode::eOff;

		const test::DeviceHarness driver{ CurrentBackend(), unguarded };
		ASSERT_TRUE(test::Ok(driver.IsValid(), driver.GetError()))
			<< "no unvalidated device, so the answer below would come from the decorator rather than from Metal";

		rhi::Error control{};
		ASSERT_TRUE(test::Ok(BlendPipelineBuilds(driver.Get(), rhi::Format::eRGBA8UNorm, control), control))
			<< "blending was refused on a format nobody doubts, so a refusal on RGB9E5 would prove nothing";

		constexpr std::array probed{ rhi::Format::eRGB9E5Float, rhi::Format::eR32UInt, rhi::Format::eBC7UNorm };
		for (const rhi::Format format : probed)
		{
			const rhi::FormatSupport support = driver.Get().GetFormatSupport(format);

			rhi::Error error{};
			const bool built = BlendPipelineBuilds(driver.Get(), format, error);

			EXPECT_EQ(built, support.blendable) << "format " << static_cast<int>(format) << ": GetFormatSupport says blendable=" << support.blendable
												<< " but building a blending pipeline on it returned " << built << ": " << test::Describe(error);
		}
	}

} // namespace

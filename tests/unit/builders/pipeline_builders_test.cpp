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

#include "azoth/rhi/builders/pipeline_builders.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rhi = azo::rhi;

namespace
{

	void ExpectSame(const rhi::VertexBindingDesc & built, const rhi::VertexBindingDesc & direct, const char * what)
	{
		EXPECT_EQ(built.binding, direct.binding) << what;
		EXPECT_EQ(built.stride, direct.stride) << what;
		EXPECT_EQ(built.perInstance, direct.perInstance) << what;
	}

	void ExpectSame(const rhi::VertexAttributeDesc & built, const rhi::VertexAttributeDesc & direct, const char * what)
	{
		EXPECT_EQ(built.location, direct.location) << what;
		EXPECT_EQ(built.binding, direct.binding) << what;
		EXPECT_EQ(built.format, direct.format) << what;
		EXPECT_EQ(built.offset, direct.offset) << what;
	}

	void ExpectSame(const rhi::ShaderBinary & built, const rhi::ShaderBinary & direct, const char * what)
	{
		EXPECT_EQ(built.stage, direct.stage) << what;
		EXPECT_EQ(built.format, direct.format) << what;
		EXPECT_EQ(built.data, direct.data) << what;
		EXPECT_EQ(built.size, direct.size) << what;
		EXPECT_STREQ(built.entryPoint, direct.entryPoint) << what;
	}

	void ExpectSame(const rhi::RayTracingShaderGroupDesc & built, const rhi::RayTracingShaderGroupDesc & direct, const char * what)
	{
		EXPECT_EQ(built.type, direct.type) << what;
		EXPECT_EQ(built.generalShader, direct.generalShader) << what;
		EXPECT_EQ(built.closestHitShader, direct.closestHitShader) << what;
		EXPECT_EQ(built.anyHitShader, direct.anyHitShader) << what;
		EXPECT_EQ(built.intersectionShader, direct.intersectionShader) << what;
	}

	template <typename T>
	void ExpectSameElements(const std::span<const T> built, const std::span<const T> direct, const char * what)
	{
		ASSERT_EQ(built.size(), direct.size()) << what;
		for (std::size_t i = 0; i < built.size(); ++i)
		{
			ExpectSame(built[i], direct[i], what);
		}
	}

	[[nodiscard]] rhi::Built<rhi::ShaderBinaryBuilder> MakeShader(const rhi::ShaderStage stage, const void * data, const std::size_t size)
	{
		return rhi::ShaderBinaryBuilder{}.Stage(stage).SpirV().Data(data, size).EntryPoint("main").Build();
	}

	TEST(GraphicsPipelineBuilder, DefaultsToAnEmptyDescRatherThanAUsableOne)
	{
		const auto empty					 = rhi::GraphicsPipelineBuilder{}.Build();
		const rhi::GraphicsPipelineDesc desc = empty.Desc();

		EXPECT_FALSE(desc.layout.IsValid());
		EXPECT_TRUE(desc.shaders.empty());
		ASSERT_NE(desc.vertexInput, nullptr) << "a builder that was not told otherwise still sources vertices";
		EXPECT_TRUE(desc.vertexInput->bindings.empty());
		EXPECT_TRUE(desc.vertexInput->attributes.empty());
		EXPECT_EQ(desc.vertexInput->topology, rhi::PrimitiveTopology::eTriangleList);
		EXPECT_FALSE(desc.vertexInput->primitiveRestartEnable);
		EXPECT_EQ(desc.vertexInput->patchControlPoints, 0u);
		EXPECT_EQ(desc.renderTarget.colorFormatCount, 0u);
		EXPECT_EQ(desc.renderTarget.depthStencilFormat, rhi::Format::eUndefined);
		EXPECT_EQ(desc.renderTarget.samples, rhi::SampleCount::e1);
		EXPECT_EQ(desc.renderTarget.sampleMask, 0xffffffffu);
		EXPECT_FALSE(desc.renderTarget.alphaToCoverageEnable);
		EXPECT_TRUE(desc.dynamicStates.Empty());
		EXPECT_EQ(desc.debugName, nullptr);
	}

	TEST(GraphicsPipelineBuilder, ProducesTheSameDescriptionAsWritingOneDirectly)
	{
		const std::array<std::uint32_t, 4> vertexCode{ 1, 2, 3, 4 };
		const std::array<std::uint32_t, 4> fragmentCode{ 5, 6, 7, 8 };

		const auto vertexHeld			 = MakeShader(rhi::ShaderStage::eVertex, vertexCode.data(), sizeof(vertexCode));
		const rhi::ShaderBinary vertex	 = vertexHeld.Desc();
		const auto fragmentHeld			 = MakeShader(rhi::ShaderStage::eFragment, fragmentCode.data(), sizeof(fragmentCode));
		const rhi::ShaderBinary fragment = fragmentHeld.Desc();

		const rhi::VertexBindingDesc binding	= rhi::VertexBindingBuilder{}.Binding(0).Stride(32).Build();
		const rhi::VertexAttributeDesc position = rhi::VertexAttributeBuilder{}.Location(0).Binding(0).Format(rhi::Format::eRGB32Float).Offset(0).Build();
		const rhi::VertexAttributeDesc normal	= rhi::VertexAttributeBuilder{}.Location(1).Binding(0).Format(rhi::Format::eRGB32Float).Offset(12).Build();

		const rhi::RasterStateDesc raster = rhi::RasterStateBuilder{}.Fill(rhi::FillMode::eSolid).Cull(rhi::CullMode::eBack).DepthBias(1.0f, 2.0f).Build();
		const rhi::DepthStencilStateDesc depthStencil = rhi::DepthStencilStateBuilder{}.DepthTest().DepthWrite().DepthCompare(rhi::CompareOp::eLess).Build();
		const rhi::BlendStateDesc blend				  = rhi::BlendStateBuilder{}.Attachment(rhi::ColorBlendAttachmentBuilder{}.Enable().Build()).Build();

		const std::array<rhi::ShaderBinary, 2> shaders{ vertex, fragment };
		const std::array<rhi::VertexBindingDesc, 1> bindings{ binding };
		const std::array<rhi::VertexAttributeDesc, 2> attributes{ position, normal };

		const rhi::GraphicsPipelineBuilder builder = rhi::GraphicsPipelineBuilder{}
														 .Shaders(shaders)
														 .VertexBindings(bindings)
														 .VertexAttributes(attributes)
														 .Topology(rhi::PrimitiveTopology::eTriangleStrip)
														 .PrimitiveRestart()
														 .Raster(raster)
														 .DepthStencil(depthStencil)
														 .Blend(blend)
														 .ColorFormat(rhi::Format::eRGBA8UNorm)
														 .DepthStencilFormat(rhi::Format::eD32Float)
														 .Samples(rhi::SampleCount::e4)
														 .SampleMask(0x0000ffffu)
														 .AlphaToCoverage()
														 .DynamicViewportScissor()
														 .DebugName("gbuffer");
		const auto held							   = builder.Build();
		const rhi::GraphicsPipelineDesc built	   = held.Desc();

		rhi::GraphicsPipelineDesc direct{};
		direct.shaders = shaders;
		rhi::VertexInputDesc directInput{};
		directInput.bindings					  = bindings;
		directInput.attributes					  = attributes;
		directInput.topology					  = rhi::PrimitiveTopology::eTriangleStrip;
		directInput.primitiveRestartEnable		  = true;
		direct.vertexInput						  = &directInput;
		direct.raster							  = raster;
		direct.depthStencil						  = depthStencil;
		direct.blend							  = blend;
		direct.renderTarget.colorFormats[0]		  = rhi::Format::eRGBA8UNorm;
		direct.renderTarget.colorFormatCount	  = 1;
		direct.renderTarget.depthStencilFormat	  = rhi::Format::eD32Float;
		direct.renderTarget.samples				  = rhi::SampleCount::e4;
		direct.renderTarget.sampleMask			  = 0x0000ffffu;
		direct.renderTarget.alphaToCoverageEnable = true;
		direct.dynamicStates					  = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
		direct.debugName						  = "gbuffer";

		ExpectSameElements(built.shaders, direct.shaders, "shaders");
		ASSERT_NE(built.vertexInput, nullptr);
		ExpectSameElements(built.vertexInput->bindings, direct.vertexInput->bindings, "vertex bindings");
		ExpectSameElements(built.vertexInput->attributes, direct.vertexInput->attributes, "vertex attributes");

		EXPECT_EQ(built.layout, direct.layout);
		EXPECT_EQ(built.vertexInput->topology, direct.vertexInput->topology);
		EXPECT_EQ(built.vertexInput->primitiveRestartEnable, direct.vertexInput->primitiveRestartEnable);
		EXPECT_EQ(built.raster.fillMode, direct.raster.fillMode);
		EXPECT_EQ(built.raster.cullMode, direct.raster.cullMode);
		EXPECT_EQ(built.raster.depthBiasConstantFactor, direct.raster.depthBiasConstantFactor);
		EXPECT_EQ(built.raster.depthBiasSlopeFactor, direct.raster.depthBiasSlopeFactor);
		EXPECT_EQ(built.depthStencil.depthTestEnable, direct.depthStencil.depthTestEnable);
		EXPECT_EQ(built.depthStencil.depthWriteEnable, direct.depthStencil.depthWriteEnable);
		EXPECT_EQ(built.depthStencil.depthCompareOp, direct.depthStencil.depthCompareOp);
		EXPECT_EQ(built.blend.attachmentCount, direct.blend.attachmentCount);
		EXPECT_EQ(built.blend.attachments[0].blendEnable, direct.blend.attachments[0].blendEnable);
		EXPECT_EQ(built.renderTarget.colorFormats, direct.renderTarget.colorFormats);
		EXPECT_EQ(built.renderTarget.colorFormatCount, direct.renderTarget.colorFormatCount);
		EXPECT_EQ(built.renderTarget.depthStencilFormat, direct.renderTarget.depthStencilFormat);
		EXPECT_EQ(built.renderTarget.samples, direct.renderTarget.samples);
		EXPECT_EQ(built.renderTarget.sampleMask, direct.renderTarget.sampleMask);
		EXPECT_EQ(built.renderTarget.alphaToCoverageEnable, direct.renderTarget.alphaToCoverageEnable);
		EXPECT_EQ(built.pipelineCache, direct.pipelineCache);
		EXPECT_EQ(built.dynamicStates, direct.dynamicStates);
		EXPECT_STREQ(built.debugName, direct.debugName);
	}

	TEST(GraphicsPipelineBuilder, ColorFormatAppendsWhileTheIndexedFormOverwrites)
	{
		// Getting these two backwards at a call site silently changes which attachment a format lands on, and the pipeline still creates.
		const auto appendedHeld = rhi::GraphicsPipelineBuilder{}.ColorFormat(rhi::Format::eRGBA8UNorm).ColorFormat(rhi::Format::eRG16Float).Build();
		const rhi::GraphicsPipelineDesc appended = appendedHeld.Desc();
		EXPECT_EQ(appended.renderTarget.colorFormatCount, 2u);
		EXPECT_EQ(appended.renderTarget.colorFormats[0], rhi::Format::eRGBA8UNorm);
		EXPECT_EQ(appended.renderTarget.colorFormats[1], rhi::Format::eRG16Float);

		const auto placedHeld = rhi::GraphicsPipelineBuilder{}.ColorFormat(rhi::Format::eRGBA8UNorm).ColorFormat(0, rhi::Format::eRG16Float).Build();
		const rhi::GraphicsPipelineDesc placed = placedHeld.Desc();
		EXPECT_EQ(placed.renderTarget.colorFormatCount, 1u);
		EXPECT_EQ(placed.renderTarget.colorFormats[0], rhi::Format::eRG16Float);
	}

	TEST(GraphicsPipelineBuilder, ShaderAppendsWhileShadersReplaces)
	{
		const std::array<std::uint32_t, 2> code{ 9, 10 };
		const auto vertexHeld			 = MakeShader(rhi::ShaderStage::eVertex, code.data(), sizeof(code));
		const rhi::ShaderBinary vertex	 = vertexHeld.Desc();
		const auto fragmentHeld			 = MakeShader(rhi::ShaderStage::eFragment, code.data(), sizeof(code));
		const rhi::ShaderBinary fragment = fragmentHeld.Desc();

		rhi::GraphicsPipelineBuilder appending;
		appending.Shader(vertex).Shader(fragment);
		const auto appended = appending.Build();
		EXPECT_EQ(appended.Desc().shaders.size(), 2u);

		const std::array<rhi::ShaderBinary, 1> only{ fragment };
		rhi::GraphicsPipelineBuilder replacing;
		replacing.Shader(vertex).Shaders(only);

		const auto replacedHeld					 = replacing.Build();
		const rhi::GraphicsPipelineDesc replaced = replacedHeld.Desc();
		ASSERT_EQ(replaced.shaders.size(), 1u);
		EXPECT_EQ(replaced.shaders[0].stage, rhi::ShaderStage::eFragment);
	}

	TEST(GraphicsPipelineBuilder, TheDescBorrowsTheBuildersStorageRatherThanOwningIt)
	{
		const std::array<std::uint32_t, 2> code{ 16, 17 };
		const auto vertexHeld			 = MakeShader(rhi::ShaderStage::eVertex, code.data(), sizeof(code));
		const rhi::ShaderBinary vertex	 = vertexHeld.Desc();
		const auto fragmentHeld			 = MakeShader(rhi::ShaderStage::eFragment, code.data(), sizeof(code));
		const rhi::ShaderBinary fragment = fragmentHeld.Desc();

		rhi::GraphicsPipelineBuilder builder;
		builder.Shader(vertex);

		const auto beforeHeld				   = builder.Build();
		const rhi::GraphicsPipelineDesc before = beforeHeld.Desc();
		ASSERT_EQ(before.shaders.size(), 1u);
		EXPECT_EQ(before.shaders[0].stage, rhi::ShaderStage::eVertex);

		builder.Shader(fragment);

		const auto afterHeld				  = builder.Build();
		const rhi::GraphicsPipelineDesc after = afterHeld.Desc();
		ASSERT_EQ(after.shaders.size(), 2u);
		EXPECT_EQ(after.shaders[1].stage, rhi::ShaderStage::eFragment);
	}

	TEST(GraphicsPipelineBuilder, AddDynamicStateAccumulatesWhileDynamicStatesReplaces)
	{
		const auto accumulatedHeld =
			rhi::GraphicsPipelineBuilder{}.AddDynamicState(rhi::DynamicState::eViewport).AddDynamicState(rhi::DynamicState::eDepthBias).Build();
		const rhi::GraphicsPipelineDesc accumulated = accumulatedHeld.Desc();
		EXPECT_TRUE(accumulated.dynamicStates.Contains(rhi::DynamicState::eViewport));
		EXPECT_TRUE(accumulated.dynamicStates.Contains(rhi::DynamicState::eDepthBias));

		const auto replacedHeld					 = rhi::GraphicsPipelineBuilder{}
													   .AddDynamicState(rhi::DynamicState::eViewport)
													   .DynamicStates(rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eStencilReference))
													   .Build();
		const rhi::GraphicsPipelineDesc replaced = replacedHeld.Desc();
		EXPECT_FALSE(replaced.dynamicStates.Contains(rhi::DynamicState::eViewport));
		EXPECT_TRUE(replaced.dynamicStates.Contains(rhi::DynamicState::eStencilReference));
	}

	TEST(GraphicsPipelineBuilder, DynamicViewportScissorNamesBothAndNothingElse)
	{
		const auto held						 = rhi::GraphicsPipelineBuilder{}.DynamicViewportScissor().Build();
		const rhi::GraphicsPipelineDesc desc = held.Desc();

		EXPECT_EQ(desc.dynamicStates, rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor);
	}

	TEST(GraphicsPipelineBuilder, AnEmptyDebugNameStaysNullRatherThanPointingAtAnEmptyString)
	{
		const auto unnamed = rhi::GraphicsPipelineBuilder{}.Build();
		EXPECT_EQ(unnamed.Desc().debugName, nullptr);

		const auto emptyName = rhi::GraphicsPipelineBuilder{}.DebugName("").Build();
		EXPECT_EQ(emptyName.Desc().debugName, nullptr);

		const auto named = rhi::GraphicsPipelineBuilder{}.DebugName("shadow").Build();
		EXPECT_STREQ(named.Desc().debugName, "shadow");
	}

	TEST(ComputePipelineBuilder, ProducesTheSameDescriptionAsWritingOneDirectly)
	{
		const std::array<std::uint32_t, 3> code{ 11, 12, 13 };
		const auto shaderHeld		   = MakeShader(rhi::ShaderStage::eCompute, code.data(), sizeof(code));
		const rhi::ShaderBinary shader = shaderHeld.Desc();

		const rhi::ComputePipelineBuilder builder = rhi::ComputePipelineBuilder{}.Shader(shader).DebugName("cull");
		const auto held							  = builder.Build();
		const rhi::ComputePipelineDesc built	  = held.Desc();

		rhi::ComputePipelineDesc direct{};
		direct.shader	 = shader;
		direct.debugName = "cull";

		EXPECT_EQ(built.layout, direct.layout);
		ExpectSame(built.shader, direct.shader, "shader");
		EXPECT_EQ(built.pipelineCache, direct.pipelineCache);
		EXPECT_STREQ(built.debugName, direct.debugName);
	}

	TEST(RayTracingPipelineBuilder, ProducesTheSameDescriptionAsWritingOneDirectly)
	{
		const std::array<std::uint32_t, 2> code{ 14, 15 };
		const auto raygenHeld		   = MakeShader(rhi::ShaderStage::eRayGeneration, code.data(), sizeof(code));
		const rhi::ShaderBinary raygen = raygenHeld.Desc();
		const auto missHeld			   = MakeShader(rhi::ShaderStage::eMiss, code.data(), sizeof(code));
		const rhi::ShaderBinary miss   = missHeld.Desc();

		rhi::RayTracingShaderGroupDesc rayGenGroup{};
		rayGenGroup.type		  = rhi::RayTracingShaderGroupType::eRayGeneration;
		rayGenGroup.generalShader = 0;

		const std::array<rhi::ShaderBinary, 2> shaders{ raygen, miss };
		const std::array<rhi::RayTracingShaderGroupDesc, 1> groups{ rayGenGroup };

		const rhi::RayTracingPipelineBuilder builder =
			rhi::RayTracingPipelineBuilder{}.Shaders(shaders).Groups(groups).MaxRayRecursionDepth(3).MaxPayloadBytes(32).MaxAttributeBytes(8).DebugName(
				"primary rays");
		const auto held							= builder.Build();
		const rhi::RayTracingPipelineDesc built = held.Desc();

		rhi::RayTracingPipelineDesc direct{};
		direct.shaders				= shaders;
		direct.groups				= groups;
		direct.maxRayRecursionDepth = 3;
		direct.maxPayloadBytes		= 32;
		direct.maxAttributeBytes	= 8;
		direct.debugName			= "primary rays";

		ExpectSameElements(built.shaders, direct.shaders, "shaders");
		ExpectSameElements(built.groups, direct.groups, "groups");

		EXPECT_EQ(built.layout, direct.layout);
		EXPECT_EQ(built.maxRayRecursionDepth, direct.maxRayRecursionDepth);
		EXPECT_EQ(built.maxPayloadBytes, direct.maxPayloadBytes);
		EXPECT_EQ(built.maxAttributeBytes, direct.maxAttributeBytes);
		EXPECT_EQ(built.pipelineCache, direct.pipelineCache);
		EXPECT_STREQ(built.debugName, direct.debugName);
	}

	TEST(PipelineCacheBuilder, ReferencesInitialDataRatherThanCopyingIt)
	{
		const std::array<std::byte, 4> blob{};
		const auto held					  = rhi::PipelineCacheBuilder{}.InitialData(blob.data(), blob.size()).DebugName("cache").Build();
		const rhi::PipelineCacheDesc desc = held.Desc();

		EXPECT_EQ(desc.initialData, blob.data());
		EXPECT_EQ(desc.initialSize, blob.size());
		EXPECT_STREQ(desc.debugName, "cache");
	}

	TEST(ShaderBinaryBuilder, FormatShorthandsMatchTheEnumeratorsTheyName)
	{
		const auto spirv = rhi::ShaderBinaryBuilder{}.SpirV().Build();
		const auto dxil	 = rhi::ShaderBinaryBuilder{}.Dxil().Build();

		EXPECT_EQ(spirv.Desc().format, rhi::ShaderBinaryFormat::eSpirV);
		EXPECT_EQ(dxil.Desc().format, rhi::ShaderBinaryFormat::eDxil);
	}

} // namespace

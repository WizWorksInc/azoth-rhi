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

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class PipelineTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(PipelineTest);

	TEST_P(PipelineTest, CreatesAndDestroysAPipelineCache)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPipelineCache || IsNullBackend(), "pipeline caches");

		rhi::Error error{};
		const rhi::PipelineCacheHandle cache = Dev().CreatePipelineCache(rhi::PipelineCacheDesc{ .debugName = "azoth.rhi.test.cache" }, error);

		ASSERT_TRUE(test::Ok(cache.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(cache, {}, error), error));
	}

	TEST_P(PipelineTest, RejectsCacheDataFromSomewhereElseRatherThanTrustingIt)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPipelineCache || IsNullBackend(), "pipeline caches");

		constexpr std::array<std::byte, 32> junk{};

		rhi::Error error{};
		const rhi::PipelineCacheHandle cache = Dev().CreatePipelineCache(
			rhi::PipelineCacheDesc{
				.initialData = junk.data(),
				.initialSize = junk.size(),
				.debugName	 = "azoth.rhi.test.foreignCache",
			},
			error);

		if (!cache.IsValid())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error)) << "cache data was rejected with no diagnostic";
			SUCCEED() << "the backend rejected foreign cache data outright";
			return;
		}

		rhi::PipelineCacheData readBack{};
		if (Dev().GetPipelineCacheData(cache, readBack, error))
		{
			EXPECT_TRUE(readBack.data != nullptr || readBack.size == 0) << "the cache reported a size for a null blob";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(cache, {}, error), error));
	}

	TEST_P(PipelineTest, ReadsBackCacheDataOrSaysItCannot)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPipelineCache || IsNullBackend(), "pipeline caches");

		rhi::Error error{};
		const rhi::PipelineCacheHandle cache = Dev().CreatePipelineCache(rhi::PipelineCacheDesc{ .debugName = "azoth.rhi.test.cache" }, error);
		ASSERT_TRUE(test::Ok(cache.IsValid(), error));

		rhi::PipelineCacheData data{};
		if (!Dev().GetPipelineCacheData(cache, data, error))
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			static_cast<void>(Dev().Destroy(cache, {}, error));
			GTEST_SKIP() << "this backend does not serialize pipeline caches: " << test::Describe(error);
		}

		EXPECT_TRUE(data.data != nullptr || data.size == 0);

		EXPECT_TRUE(test::Ok(Dev().Destroy(cache, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesAGraphicsPipelineWithNoShadersRatherThanProducingOne)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		rhi::GraphicsPipelineDesc desc{};
		desc.layout	   = layout;
		desc.debugName = "azoth.rhi.test.emptyPipeline";

		rhi::Error pipelineError{};
		const rhi::GraphicsPipelineHandle pipeline = Dev().CreateGraphicsPipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a graphics pipeline was created with no shader stages";
		EXPECT_TRUE(test::ErrorIsPopulated(pipelineError));

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, TheReportedShaderFormatIsTheOneItActuallyTakes)
	{
		/*
		 * A caller compiles for whatever the device says it takes, so the answer has to be the truth and not a default nobody set. Before this, picking a
		 * target meant a table of backend to format outside the RHI.
		 *
		 * Four bytes are not a shader in any encoding, so no device builds one from this. What is checked is that the refusal is about the bytes and not the
		 * format, and that a device turns source down without reading it.
		 */
		constexpr std::array<std::byte, 4> notAShader{};

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const rhi::ShaderBinary shader{
			.stage			 = rhi::ShaderStage::eCompute,
			.format			 = Caps().shaderBinaryFormat,
			.data			 = notAShader.data(),
			.size			 = notAShader.size(),
			.threadgroupSize = { .x = 1, .y = 1, .z = 1 },
		};

		rhi::Error shaderError{};
		const rhi::ComputePipelineHandle pipeline =
			Dev().CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = shader, .debugName = "azoth.rhi.test.format" }, shaderError);

		// The Null backend hands the bytes to nobody, so malformed ones are not wrong there and it builds a pipeline from them. Every backend that reaches a
		// driver refuses.
		if (!IsNullBackend())
		{
			EXPECT_FALSE(pipeline.IsValid());
			EXPECT_TRUE(test::ErrorIsPopulated(shaderError)) << "malformed bytes were refused with no diagnostic";
		}

		// And the other half: a device that says it cannot compile source has to refuse source, not read it as compiled bytes.
		if (!Caps().supportsShaderSource)
		{
			rhi::ShaderBinary asSource = shader;
			asSource.isSource		   = true;

			rhi::Error sourceError{};
			EXPECT_FALSE(
				Dev().CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = asSource, .debugName = "azoth.rhi.test.source" }, sourceError)
					.IsValid());
			EXPECT_EQ(sourceError.code, rhi::ErrorCode::eUnsupportedFormat) << "a device reporting no source support took a source binary anyway";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesADescNamingMoreColorAttachmentsThanItCanHold)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const rhi::VertexInputDesc vertexInput{};
		rhi::GraphicsPipelineDesc desc{};
		desc.layout		 = layout;
		desc.vertexInput = &vertexInput;
		desc.debugName	 = "azoth.rhi.test.overrunPipeline";

		desc.renderTarget.colorFormatCount = static_cast<std::uint32_t>(desc.renderTarget.colorFormats.size()) + 1;

		rhi::Error colorError{};
		EXPECT_FALSE(Dev().CreateGraphicsPipeline(desc, colorError).IsValid()) << "a colorFormatCount past the array was accepted";
		EXPECT_EQ(colorError.code, rhi::ErrorCode::eInvalidArgument);

		desc.renderTarget.colorFormatCount = 0;
		desc.blend.attachmentCount		   = static_cast<std::uint32_t>(desc.blend.attachments.size()) + 1;

		rhi::Error blendError{};
		EXPECT_FALSE(Dev().CreateGraphicsPipeline(desc, blendError).IsValid()) << "an attachmentCount past the array was accepted";
		EXPECT_EQ(blendError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, ConservativeRasterIsRefusedExactlyWhenTheDeviceReportsNoTier)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const rhi::VertexInputDesc vertexInput{};
		rhi::GraphicsPipelineDesc desc{};
		desc.layout						 = layout;
		desc.vertexInput				 = &vertexInput;
		desc.raster.conservativeRasterEnable = true;
		desc.debugName					 = "azoth.rhi.test.conservativePipeline";

		rhi::Error pipelineError{};
		const rhi::GraphicsPipelineHandle pipeline = Dev().CreateGraphicsPipeline(desc, pipelineError);
		EXPECT_FALSE(pipeline.IsValid()) << "this pipeline has no stages, so nothing here should have produced one";

		if (Caps().conservativeRasterTier == rhi::ConservativeRasterTier::eNone)
		{
			EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eUnsupportedFeature) << "a device reporting no tier accepted the request past its capability gate";
		}
		else
		{
			EXPECT_NE(pipelineError.code, rhi::ErrorCode::eUnsupportedFeature)
				<< "a device reporting tier " << static_cast<int>(Caps().conservativeRasterTier) << " refused the request as unsupported anyway";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesAShaderBinaryInAContainerItCannotConsume)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a native shader container");

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const std::array<std::uint32_t, 4> bytes{ 1, 2, 3, 4 };

		const rhi::ShaderBinaryFormat wrong = Caps().apiId == rhi::VulkanApi::id ? rhi::ShaderBinaryFormat::eDxil : rhi::ShaderBinaryFormat::eSpirV;

		rhi::ComputePipelineDesc desc{};
		desc.layout					= layout;
		desc.shader.stage			= rhi::ShaderStage::eCompute;
		desc.shader.format			= wrong;
		desc.shader.data			= bytes.data();
		desc.shader.size			= sizeof(bytes);
		desc.shader.threadgroupSize = { .x = 8, .y = 8, .z = 1 };
		desc.debugName				= "azoth.rhi.test.wrongContainer";

		rhi::Error pipelineError{};
		const rhi::ComputePipelineHandle pipeline = Dev().CreateComputePipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a shader in a container this backend cannot consume was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(pipelineError));

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesAComputePipelineThatDidNotStateItsThreadgroupSize)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		rhi::ComputePipelineDesc desc{};
		desc.layout		  = layout;
		desc.shader.stage = rhi::ShaderStage::eCompute;
		desc.debugName	  = "azoth.rhi.test.sizelessCompute";

		rhi::Error pipelineError{};
		const rhi::ComputePipelineHandle pipeline = Dev().CreateComputePipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a compute pipeline was created without a threadgroup size";
		EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	[[nodiscard]] rhi::ShaderBinaryFormat NativeShaderFormat(const rhi::GraphicsApiId api)
	{
		if (api == rhi::VulkanApi::id)
		{
			return rhi::ShaderBinaryFormat::eSpirV;
		}

		return api == rhi::D3D12Api::id ? rhi::ShaderBinaryFormat::eDxil : rhi::ShaderBinaryFormat::eBackendNative;
	}

	[[nodiscard]] rhi::DescriptorSetLayoutHandle MakeOneBindingSetLayout(rhi::Device device, rhi::Error & error)
	{
		static constexpr std::array<rhi::DescriptorBinding, 1> bindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer },
		};

		return device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.test.mappedSet" }, error);
	}

	TEST_P(PipelineTest, RefusesAShaderBinaryWhoseBindingMapDisagreesWithTheLayout)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that resolves native binding slots");

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = MakeOneBindingSetLayout(Dev(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const std::array<rhi::DescriptorSetLayoutHandle, 1> sets{ setLayout };
		const rhi::PipelineLayoutHandle layout =
			Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = sets, .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		static constexpr std::array<rhi::DescriptorBinding, 1> abiBindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer },
		};
		const std::array<rhi::DescriptorSetLayoutDesc, 1> abiSets{ rhi::DescriptorSetLayoutDesc{ .bindings = abiBindings } };
		const rhi::NativeBinding actual = rhi::NativeBindingFor(Caps().apiId, Caps().bindingTier, rhi::ShaderAbiLayout{ .sets = abiSets }, 0, 0);
		ASSERT_TRUE(actual.exists) << "the ABI has no slot for a binding the layout declares";

		rhi::NativeSlot moved = actual.resource;
		moved.index += 1;

		const std::array<rhi::ShaderBindingEntry, 1> entries{
			rhi::ShaderBindingEntry{ .set = 0, .binding = 0, .resource = moved },
		};
		const rhi::ShaderBindingMap map{ .bindings = entries };

		const std::array<std::uint32_t, 4> bytes{ 1, 2, 3, 4 };

		rhi::ComputePipelineDesc desc{};
		desc.layout					= layout;
		desc.shader.stage			= rhi::ShaderStage::eCompute;
		desc.shader.format			= NativeShaderFormat(Caps().apiId);
		desc.shader.data			= bytes.data();
		desc.shader.size			= sizeof(bytes);
		desc.shader.threadgroupSize = { .x = 8, .y = 8, .z = 1 };
		desc.shader.bindingMap		= &map;
		desc.debugName				= "azoth.rhi.test.movedBinding";

		rhi::Error pipelineError{};
		const rhi::ComputePipelineHandle pipeline = Dev().CreateComputePipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a shader claiming a slot the layout does not bind was accepted";
		EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesAShaderBinaryBuiltAgainstAnAbiRevisionThisBuildDoesNotImplement)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that resolves native binding slots");

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const rhi::ShaderBindingMap map{ .abi = rhi::ShaderAbiVersion{ rhi::kShaderAbiVersion.value + 1 } };

		const std::array<std::uint32_t, 4> bytes{ 1, 2, 3, 4 };

		rhi::ComputePipelineDesc desc{};
		desc.layout					= layout;
		desc.shader.stage			= rhi::ShaderStage::eCompute;
		desc.shader.format			= NativeShaderFormat(Caps().apiId);
		desc.shader.data			= bytes.data();
		desc.shader.size			= sizeof(bytes);
		desc.shader.threadgroupSize = { .x = 8, .y = 8, .z = 1 };
		desc.shader.bindingMap		= &map;
		desc.debugName				= "azoth.rhi.test.futureAbi";

		rhi::Error pipelineError{};
		const rhi::ComputePipelineHandle pipeline = Dev().CreateComputePipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a shader built against another revision of the binding ABI was accepted";
		EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eUnsupportedFormat);

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesABindingMapNamingABindingTheLayoutNeverDeclared)
	{
		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that resolves native binding slots");

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		const std::array<rhi::ShaderBindingEntry, 1> entries{
			rhi::ShaderBindingEntry{ .set = 0, .binding = 0, .resource = rhi::NativeSlot{ .space = 0, .index = 0 } },
		};
		const rhi::ShaderBindingMap map{ .bindings = entries };

		const std::array<std::uint32_t, 4> bytes{ 1, 2, 3, 4 };

		rhi::ComputePipelineDesc desc{};
		desc.layout					= layout;
		desc.shader.stage			= rhi::ShaderStage::eCompute;
		desc.shader.format			= NativeShaderFormat(Caps().apiId);
		desc.shader.data			= bytes.data();
		desc.shader.size			= sizeof(bytes);
		desc.shader.threadgroupSize = { .x = 8, .y = 8, .z = 1 };
		desc.shader.bindingMap		= &map;
		desc.debugName				= "azoth.rhi.test.strayBinding";

		rhi::Error pipelineError{};
		const rhi::ComputePipelineHandle pipeline = Dev().CreateComputePipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a shader claiming a binding the layout never declared was accepted";
		EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eInvalidArgument);

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, RefusesAGraphicsPipelineWithNoVertexInputByName)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		rhi::GraphicsPipelineDesc desc{};
		desc.layout		 = layout;
		desc.vertexInput = nullptr;
		desc.debugName	 = "azoth.rhi.test.meshlessPipeline";

		rhi::Error pipelineError{};
		const rhi::GraphicsPipelineHandle pipeline = Dev().CreateGraphicsPipeline(desc, pipelineError);

		EXPECT_FALSE(pipeline.IsValid()) << "a graphics pipeline was created with no vertex input";

		EXPECT_EQ(pipelineError.code, rhi::ErrorCode::eUnsupportedFeature) << "refused, but not as a missing capability";

		EXPECT_TRUE(test::Ok(Dev().Destroy(layout, {}, error), error));
	}

	TEST_P(PipelineTest, ReportsRayTracingSupportHonestly)
	{
		rhi::Error error{};

		rhi::AccelerationStructureDesc structureDesc{};
		structureDesc.debugName							 = "azoth.rhi.test.accelerationStructure";
		const rhi::AccelerationStructureHandle structure = Dev().CreateAccelerationStructure(structureDesc, error);

		rhi::RayTracingPipelineDesc pipelineDesc{};
		pipelineDesc.debugName						 = "azoth.rhi.test.rayTracingPipeline";
		const rhi::RayTracingPipelineHandle pipeline = Dev().CreateRayTracingPipeline(pipelineDesc, error);

		if (!Caps().supportsRayTracing && !IsNullBackend())
		{
			EXPECT_FALSE(structure.IsValid()) << "a backend reporting no ray tracing still created an acceleration structure";
			EXPECT_FALSE(pipeline.IsValid()) << "a backend reporting no ray tracing still created a ray tracing pipeline";
			return;
		}

		if (structure.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(structure, {}, error), error));
		}
		if (pipeline.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(pipeline, {}, error), error));
		}
	}

	TEST_P(PipelineTest, CreatesAQueryPoolOfEachTypeTheDeviceReports)
	{
		rhi::Error error{};

		if (Caps().supportsTimestampQueries || IsNullBackend())
		{
			const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(), error);
			EXPECT_TRUE(test::Ok(pool.IsValid(), error));
			if (pool.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
			}
		}

		rhi::QueryPoolDesc occlusion{};
		occlusion.type		 = rhi::QueryType::eOcclusion;
		occlusion.queryCount = 4;
		occlusion.debugName	 = "azoth.rhi.test.occlusionPool";

		const rhi::QueryPoolHandle occlusionPool = Dev().CreateQueryPool(occlusion, error);
		if (occlusionPool.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(occlusionPool, {}, error), error));
		}
	}

	TEST_P(PipelineTest, CreatesAPipelineStatisticsPoolOnlyWhenItSaysItCan)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPipelineStatisticsQueries || IsNullBackend(), "pipeline statistics queries");

		rhi::Error error{};
		rhi::QueryPoolDesc desc{};
		desc.type		= rhi::QueryType::ePipelineStatistics;
		desc.queryCount = 4;
		desc.statistics =
			rhi::Flags<rhi::PipelineStatistic>(rhi::PipelineStatistic::eVertexShaderInvocations) | rhi::PipelineStatistic::eFragmentShaderInvocations;
		desc.debugName = "azoth.rhi.test.statisticsPool";

		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(desc, error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

	TEST_P(PipelineTest, CalibratesTimestampsOrSaysItCannot)
	{
		rhi::Error error{};
		rhi::TimestampCalibration calibration{};

		if (!Dev().CalibrateTimestamp(rhi::QueueType::eGraphics, calibration, error))
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			GTEST_SKIP() << "this backend does not calibrate timestamps: " << test::Describe(error);
		}

		EXPECT_EQ(calibration.queueType, rhi::QueueType::eGraphics) << "the calibration came back describing a different queue";
		EXPECT_GT(calibration.gpuPeriodNanoseconds, 0.0f) << "a timestamp period of zero would make every duration zero";
	}

	TEST_P(PipelineTest, KeepsThePipelineHandleDomainsApart)
	{
		static_assert(!std::is_convertible_v<rhi::GraphicsPipelineHandle, rhi::ComputePipelineHandle>);
		static_assert(!std::is_convertible_v<rhi::ComputePipelineHandle, rhi::RayTracingPipelineHandle>);
		static_assert(!std::is_convertible_v<rhi::PipelineLayoutHandle, rhi::GraphicsPipelineHandle>);

		SUCCEED();
	}

} // namespace

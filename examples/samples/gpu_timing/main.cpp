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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/shader/SlangCompiler.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"
#include "FW/utility/Timer.hpp"
#include "tracy_lifetime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint32_t kExtent	   = 256;
	constexpr std::uint32_t kThreads   = 4096;
	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	constexpr std::uint32_t kIterations = 20000;

	struct Params final
	{
		std::uint32_t iterations = kIterations;
	};

	enum class Slot : std::uint32_t
	{
		eSubmitBegin = 0,
		eComputeBegin,
		eComputeEnd,
		eRenderBegin,
		eRenderEnd,
		eSubmitEnd,
		eCount,
	};

	constexpr std::uint32_t kTimestampCount = static_cast<std::uint32_t>(Slot::eCount);
	constexpr std::uint64_t kOcclusionByte	= static_cast<std::uint64_t>(kTimestampCount) * sizeof(std::uint64_t);
	constexpr std::uint64_t kStatisticByte	= kOcclusionByte + sizeof(std::uint64_t);
	constexpr std::uint64_t kResultBytes	= kStatisticByte + sizeof(std::uint64_t);

	[[nodiscard]] std::uint64_t At(const std::span<const std::uint64_t> results, const Slot slot)
	{
		return results[static_cast<std::uint32_t>(slot)];
	}

	[[nodiscard]] bool Taken(const std::uint64_t tick)
	{
		return tick != std::numeric_limits<std::uint64_t>::max();
	}

	[[nodiscard]] double Milliseconds(const std::uint64_t from, const std::uint64_t to, const float periodNanoseconds)
	{
		if (!Taken(from) || !Taken(to) || to < from)
		{
			return -1.0;
		}

		return static_cast<double>(to - from) * static_cast<double>(periodNanoseconds) / 1'000'000.0;
	}

	void Report(const char * what, const double milliseconds)
	{
		if (milliseconds < 0.0)
		{
			LOG_INFO(fw::Log(), "  {:<22} not sampled", what);
			return;
		}

		LOG_INFO(fw::Log(), "  {:<22} {:.3f} ms", what, milliseconds);
	}

}

int main(int argc, char ** argv)
{
	const azo::rhi::support::TracyLifetime tracyLifetime;

	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };

	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .debug_name("gpu_timing")
													  .headless()
													  .graphics_queue()
													  .prefer_feature(rhi::DeviceFeature::eTimestampQueries)
													  .prefer_feature(rhi::DeviceFeature::ePipelineStatisticsQueries)
													  .build(backends.registry(), backends.preferred_apis());
	if (!device)
	{
		return fw::ReportNoDevice(device.get_error());
	}

	rhi::Device dev				 = device.value().get();
	const rhi::DeviceCaps & caps = dev.get_caps();
	LOG_INFO(fw::Log(), "backend: {}", dev.get_graphics_api_name());

	if (!caps.supportsTimestampQueries)
	{
		LOG_INFO(fw::Log(), "this device reports no timestamp queries, so there is nothing here to measure with");
		return fw::kSkipExitCode;
	}

	LOG_INFO(
		fw::Log(),
		"timestamps in a rendering scope: {}, pipeline statistics: {}, calibration: {}",
		caps.supportsTimestampWritesInScope ? "yes" : "no",
		caps.supportsPipelineStatisticsQueries ? "yes" : "no",
		caps.supportsTimestampCalibration ? "yes" : "no"
	);

	fw::shader::SlangCompiler compiler;
	std::string why;
	if (!compiler.Open(dev.get_graphics_api_id(), why))
	{
		LOG_ERROR(fw::Log(), "{}", why);
		return fw::kSkipExitCode;
	}

	const rhi::ShaderBinary computeShader = compiler.Compile("gpu_timing/shaders/timed.slang", "integrateMain", rhi::ShaderStage::eCompute, why);
	const rhi::ShaderBinary vertexShader  = compiler.Compile("gpu_timing/shaders/timed.slang", "vertexMain", rhi::ShaderStage::eVertex, why);
	const rhi::ShaderBinary pixelShader	  = compiler.Compile("gpu_timing/shaders/timed.slang", "fragmentMain", rhi::ShaderStage::eFragment, why);
	if (computeShader.data == nullptr || vertexShader.data == nullptr || pixelShader.data == nullptr)
	{
		LOG_ERROR(fw::Log(), "{}", why);
		return 1;
	}

	rhi::Error error{};

	const std::array workBindings{
		rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .stages = rhi::ShaderStage::eCompute },
	};
	const rhi::DescriptorSetLayoutHandle workSetLayout =
		dev.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{ .bindings = workBindings, .debugName = "timing.workSet" }, error);

	const std::array workSetLayouts{ workSetLayout };
	const std::array workPushConstants{
		rhi::PushConstantRange{ .stages = rhi::ShaderStage::eCompute, .offset = 0, .size = sizeof(Params) },
	};
	const rhi::PipelineLayoutHandle computeLayout = dev.create_pipeline_layout(
		rhi::PipelineLayoutDesc{ .sets = workSetLayouts, .pushConstants = workPushConstants, .debugName = "timing.computeLayout" },
		error
	);

	const rhi::ComputePipelineHandle computePipeline =
		dev.create_compute_pipeline(rhi::ComputePipelineDesc{ .layout = computeLayout, .shader = computeShader, .debugName = "timing.computePipeline" }, error);

	const rhi::BufferHandle accumulator = dev.create_buffer(
		rhi::BufferDesc{
			.size	   = static_cast<std::uint64_t>(kThreads) * sizeof(float),
			.usage	   = rhi::BufferUsage::eStorage,
			.memory	   = rhi::MemoryUsage::eGpuOnly,
			.debugName = "timing.accumulator",
		},
		error
	);

	const rhi::TextureHandle target = dev.create_texture(
		rhi::TextureDesc{
			.type	   = rhi::TextureType::eTex2D,
			.format	   = rhi::Format::eRGBA8UNorm,
			.width	   = kExtent,
			.height	   = kExtent,
			.usage	   = rhi::TextureUsage::eColorAttachment,
			.debugName = "timing.target",
		},
		error
	);
	const rhi::TextureViewHandle targetView = dev.create_texture_view(target, rhi::TextureViewDesc{ .debugName = "timing.targetView" }, error);

	const rhi::PipelineLayoutHandle graphicsLayout = dev.create_pipeline_layout(rhi::PipelineLayoutDesc{ .debugName = "timing.graphicsLayout" }, error);

	const std::array graphicsShaders{ vertexShader, pixelShader };
	rhi::VertexInputDesc vertexInput{};
	rhi::GraphicsPipelineDesc graphicsDesc{};
	graphicsDesc.layout							 = graphicsLayout;
	graphicsDesc.shaders						 = graphicsShaders;
	graphicsDesc.vertexInput					 = &vertexInput;
	graphicsDesc.raster.cullMode				 = rhi::CullMode::eNone;
	graphicsDesc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
	graphicsDesc.renderTarget.colorFormatCount	 = 1;
	graphicsDesc.blend.attachmentCount			 = 1;
	graphicsDesc.dynamicStates					 = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
	graphicsDesc.debugName						 = "timing.graphicsPipeline";

	const rhi::GraphicsPipelineHandle graphicsPipeline = dev.create_graphics_pipeline(graphicsDesc, error);

	if (!workSetLayout.is_valid() || !computeLayout.is_valid() || !computePipeline.is_valid() || !accumulator.is_valid() || !target.is_valid() ||
		!targetView.is_valid() || !graphicsLayout.is_valid() || !graphicsPipeline.is_valid())
	{
		fw::ReportError("the pipelines and their resources were refused", error);
		return 1;
	}

	rhi::DescriptorArena arena = dev.create_descriptor_arena(
		rhi::DescriptorArenaDesc{ .type = rhi::DescriptorArenaType::ePersistent, .maxSets = 1, .maxDescriptors = 1, .debugName = "timing.arena" },
		error
	);
	const rhi::DescriptorSetHandle workSet = arena.allocate(rhi::DescriptorSetAllocDesc{ .layout = workSetLayout, .debugName = "timing.work" }, error);

	const std::array workWrites{
		rhi::DescriptorWriteBuffer{
			.set	 = workSet,
			.binding = 0,
			.type	 = rhi::DescriptorType::eStorageBuffer,
			.buffer	 = accumulator,
			.range	 = static_cast<std::uint64_t>(kThreads) * sizeof(float),
		},
	};
	if (!workSet.is_valid() || !dev.update_descriptors(std::span(workWrites), error))
	{
		fw::ReportError("the compute descriptors were refused", error);
		return 1;
	}

	const rhi::QueryPoolHandle timestamps =
		dev.create_query_pool(rhi::QueryPoolDesc{ .type = rhi::QueryType::eTimestamp, .queryCount = kTimestampCount, .debugName = "timing.timestamps" }, error);
	if (!timestamps.is_valid())
	{
		fw::ReportError("this device reports timestamp queries and then refused the pool", error);
		return 1;
	}

	rhi::Error occlusionError{};
	const rhi::QueryPoolHandle occlusion =
		dev.create_query_pool(rhi::QueryPoolDesc{ .type = rhi::QueryType::eOcclusion, .queryCount = 1, .debugName = "timing.occlusion" }, occlusionError);
	if (!occlusion.is_valid())
	{
		LOG_INFO(fw::Log(), "no occlusion pool: {}", occlusionError.message != nullptr ? occlusionError.message : "no diagnostic");
	}

	rhi::Error statisticsError{};
	rhi::QueryPoolHandle statistics{};
	if (caps.supportsPipelineStatisticsQueries)
	{
		statistics = dev.create_query_pool(
			rhi::QueryPoolDesc{
				.type		= rhi::QueryType::ePipelineStatistics,
				.queryCount = 1,
				.statistics = rhi::PipelineStatistic::eFragmentShaderInvocations,
				.debugName	= "timing.statistics",
			},
			statisticsError
		);
		if (!statistics.is_valid())
		{
			LOG_INFO(fw::Log(), "no statistics pool: {}", statisticsError.message != nullptr ? statisticsError.message : "no diagnostic");
		}
	}

	const rhi::BufferHandle results = dev.create_buffer(
		rhi::BufferDesc{
			.size	   = kResultBytes,
			.usage	   = rhi::BufferUsage::eCopyDst,
			.memory	   = rhi::MemoryUsage::eCpuReadback,
			.debugName = "timing.results",
		},
		error
	);

	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "timing.timeline" }, error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "timing.pool" }, error);
	rhi::CommandList list			   = pool.allocate("timing.frame", error);
	if (!results.is_valid() || !timeline.is_valid() || !queue.is_valid() || !list.is_valid() || !list.begin(error))
	{
		fw::ReportError("the submission objects were refused", error);
		return 1;
	}

	const Params params{ .iterations = kIterations };

	const std::array intoShaderWrite{
		rhi::BufferBarrier{
			.buffer = accumulator,
			.before = { .use = rhi::ResourceUse::eDiscard },
			.after	= { .use = rhi::ResourceUse::eStorageWrite, .stages = rhi::Stage::eCompute },
		},
	};
	const std::array intoAttachment{
		rhi::TextureBarrier{
			.texture = target,
			.before	 = { .use = rhi::ResourceUse::eDiscard },
			.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		},
	};

	bool recorded = list.reset_query_pool(timestamps, 0, kTimestampCount, error);
	if (occlusion.is_valid())
	{
		recorded = recorded && list.reset_query_pool(occlusion, 0, 1, error);
	}
	if (statistics.is_valid())
	{
		recorded = recorded && list.reset_query_pool(statistics, 0, 1, error);
	}

	const bool timeTheDispatch = caps.supportsTimestampWritesInScope;

	recorded = recorded && list.write_timestamp(timestamps, static_cast<std::uint32_t>(Slot::eSubmitBegin), rhi::Stage::eAllCommands, error) &&
			   list.barriers(rhi::BarrierBatch{ .buffers = intoShaderWrite }, error);

	if (timeTheDispatch)
	{
		recorded = recorded && list.write_timestamp(timestamps, static_cast<std::uint32_t>(Slot::eComputeBegin), rhi::Stage::eAllCommands, error);
	}

	recorded = recorded && list.set_compute_pipeline(computePipeline, error) && list.bind_descriptor_set(computeLayout, 0, workSet, {}, error) &&
			   list.push_constants(computeLayout, rhi::ShaderStage::eCompute, 0, sizeof(params), &params, error) &&
			   list.dispatch(kThreads / computeShader.threadgroupSize.x, 1, 1, error);

	if (timeTheDispatch)
	{
		recorded = recorded && list.write_timestamp(timestamps, static_cast<std::uint32_t>(Slot::eComputeEnd), rhi::Stage::eCompute, error);
	}

	recorded = recorded && list.barriers(rhi::BarrierBatch{ .textures = intoAttachment }, error);

	if (!recorded)
	{
		fw::ReportError("recording the dispatch failed", error);
		return 1;
	}

	const rhi::RenderingTimestampWrites scopeTimestamps{
		.pool		= timestamps,
		.beginQuery = static_cast<std::uint32_t>(Slot::eRenderBegin),
		.endQuery	= static_cast<std::uint32_t>(Slot::eRenderEnd),
	};

	const std::array colors{
		rhi::RenderingAttachment{
			.view  = targetView,
			.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.load  = rhi::LoadOp::eClear,
			.store = rhi::StoreOp::eStore,
		},
	};
	const rhi::BeginRenderingDesc rendering{
		.colors		= colors,
		.width		= kExtent,
		.height		= kExtent,
		.timestamps = &scopeTimestamps,
	};

	if (!list.begin_rendering(rendering, error))
	{
		fw::ReportError("the rendering scope was refused", error);
		return 1;
	}

	if (occlusion.is_valid())
	{
		recorded = recorded && list.begin_query(occlusion, 0, error);
	}
	if (statistics.is_valid())
	{
		recorded = recorded && list.begin_query(statistics, 0, error);
	}

	const rhi::Viewport viewport{ .width = static_cast<float>(kExtent), .height = static_cast<float>(kExtent) };
	const rhi::Rect2D scissor{ .width = kExtent, .height = kExtent };

	recorded = recorded && list.set_graphics_pipeline(graphicsPipeline, error) && list.set_viewport(viewport, error) && list.set_scissor(scissor, error) &&
			   list.draw(3, 1, 0, 0, error);

	if (statistics.is_valid())
	{
		recorded = recorded && list.end_query(statistics, 0, error);
	}
	if (occlusion.is_valid())
	{
		recorded = recorded && list.end_query(occlusion, 0, error);
	}

	list.end_rendering(error);

	recorded = recorded && list.write_timestamp(timestamps, static_cast<std::uint32_t>(Slot::eSubmitEnd), rhi::Stage::eAllCommands, error) &&
			   list.resolve_query_data(timestamps, 0, kTimestampCount, results, 0, error);

	if (occlusion.is_valid())
	{
		recorded = recorded && list.resolve_query_data(occlusion, 0, 1, results, kOcclusionByte, error);
	}
	if (statistics.is_valid())
	{
		recorded = recorded && list.resolve_query_data(statistics, 0, 1, results, kStatisticByte, error);
	}

	recorded = recorded && list.end(error);
	if (!recorded)
	{
		fw::ReportError("recording the render pass failed", error);
		return 1;
	}

	fw::util::Timer wall;
	wall.Start();

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	if (!queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "timing.submit" }, error) ||
		!queue.wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("the frame did not complete", error);
		return 1;
	}

	const double wallMilliseconds = wall.Stop<fw::util::Timer::Milliseconds>();

	const rhi::MappedMemory mapped = dev.map(results, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		fw::ReportError("the results could not be mapped", error);
		return 1;
	}

	if (!mapped.coherent && !dev.invalidate_mapped_range(results, 0, kResultBytes, error))
	{
		fw::ReportError("the results could not be invalidated", error);
		return 1;
	}

	std::array<std::uint64_t, kResultBytes / sizeof(std::uint64_t)> raw{};
	std::memcpy(raw.data(), mapped.data, kResultBytes);
	static_cast<void>(dev.unmap(results, error));

	rhi::TimestampCalibration calibration{};
	if (!dev.calibrate_timestamp(rhi::QueueType::eGraphics, calibration, error))
	{
		LOG_INFO(fw::Log(), "no calibration on this device, so ticks are converted through the default period of 1 ns");
	}

	LOG_INFO(fw::Log(), "");
	LOG_INFO(fw::Log(), "period {} ns per tick, calibrated pair: {}", calibration.gpuPeriodNanoseconds, calibration.calibrated ? "yes" : "no");

	const std::span<const std::uint64_t> results64(raw);
	if (timeTheDispatch)
	{
		Report("compute dispatch", Milliseconds(At(results64, Slot::eComputeBegin), At(results64, Slot::eComputeEnd), calibration.gpuPeriodNanoseconds));
	}
	else
	{
		LOG_INFO(fw::Log(), "  {:<22} not timeable here, this device takes no timestamp inside a scope and none can close one", "compute dispatch");
	}

	Report("render pass", Milliseconds(At(results64, Slot::eRenderBegin), At(results64, Slot::eRenderEnd), calibration.gpuPeriodNanoseconds));
	Report("whole submission", Milliseconds(At(results64, Slot::eSubmitBegin), At(results64, Slot::eSubmitEnd), calibration.gpuPeriodNanoseconds));
	Report("wall clock", wallMilliseconds);

	int status = 0;

	if (occlusion.is_valid())
	{
		const std::uint64_t visible = raw.at(kOcclusionByte / sizeof(std::uint64_t));
		const std::uint64_t covered = static_cast<std::uint64_t>(kExtent) * kExtent;
		LOG_INFO(fw::Log(), "  {:<22} {}, and {} pixels were covered", "occlusion", visible, covered);
		if (visible == 0)
		{
			LOG_ERROR(fw::Log(), "the occlusion query saw nothing pass, and a triangle covered the whole target");
			status = 1;
		}
	}

	if (statistics.is_valid())
	{
		LOG_INFO(fw::Log(), "  {:<22} {} invocations", "fragment shader", raw.at(kStatisticByte / sizeof(std::uint64_t)));
	}

	for (std::uint32_t slot = 0; slot < kTimestampCount; ++slot)
	{
		const bool skipped =
			!timeTheDispatch && (slot == static_cast<std::uint32_t>(Slot::eComputeBegin) || slot == static_cast<std::uint32_t>(Slot::eComputeEnd));
		if (!skipped && !Taken(raw.at(slot)))
		{
			LOG_ERROR(fw::Log(), "timestamp {} was never sampled", slot);
			status = 1;
		}
	}

	const rhi::DestroyDesc retired{
		.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
		.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
	};
	dev.destroy(results, retired, error);
	dev.destroy(timestamps, retired, error);
	if (occlusion.is_valid())
	{
		dev.destroy(occlusion, retired, error);
	}
	if (statistics.is_valid())
	{
		dev.destroy(statistics, retired, error);
	}
	dev.destroy(targetView, retired, error);
	dev.destroy(target, retired, error);
	dev.destroy(accumulator, retired, error);
	dev.collect_garbage(timeline, 1, error);
	dev.destroy(timeline, {}, error);

	return status;
}

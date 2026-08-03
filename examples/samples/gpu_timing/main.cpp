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

	// Enough that the dispatch is milliseconds and not a handful of ticks, which is what keeps the measurement above the cost of taking it.
	constexpr std::uint32_t kIterations = 20000;

	// What the shader's push constant holds.
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

	/*
	 * A sample that was never taken resolves to a sentinel and not to zero, Metal writing MTLCounterErrorValue for one. Left as it arrives by the backend so
	 * a caller can tell it from a plausible time, and tested for here so a difference taken across one is reported as missing instead of printed as a duration.
	 */
	[[nodiscard]] bool Taken(const std::uint64_t tick)
	{
		return tick != std::numeric_limits<std::uint64_t>::max();
	}

	// Turns a tick difference into milliseconds through the period the device reported.
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

} // namespace

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	// No Null backend: it reports no timestamp support and a timing sample against it would measure nothing.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };

	/*
	 * Asked for, not assumed. Queries are an opt-in device feature: an adapter that could time a frame still comes up unable to when nothing requested it, and
	 * DeviceCaps then honestly reports no. Preferred, not required, so a device still comes up on an adapter that cannot, and the caps test below is what
	 * decides whether there is anything to measure with.
	 */
	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .DebugName("gpu_timing")
													  .Headless()
													  .GraphicsQueue()
													  .PreferFeature(rhi::DeviceFeature::eTimestampQueries)
													  .PreferFeature(rhi::DeviceFeature::ePipelineStatisticsQueries)
													  .Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev				 = device.Value().Get();
	const rhi::DeviceCaps & caps = dev.GetCaps();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	if (!caps.supportsTimestampQueries)
	{
		LOG_INFO(fw::Log(), "this device reports no timestamp queries, so there is nothing here to measure with");
		return fw::kSkipExitCode;
	}

	LOG_INFO(fw::Log(),
		"timestamps in a rendering scope: {}, pipeline statistics: {}, calibration: {}",
		caps.supportsTimestampWritesInScope ? "yes" : "no",
		caps.supportsPipelineStatisticsQueries ? "yes" : "no",
		caps.supportsTimestampCalibration ? "yes" : "no");

	fw::shader::SlangCompiler compiler;
	std::string why;
	if (!compiler.Open(dev.GetGraphicsApiId(), why))
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

	// The compute half: one storage buffer the kernel writes, bound through a set, with the loop count arriving as a push constant.
	const std::array workBindings{
		rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .stages = rhi::ShaderStage::eCompute },
	};
	const rhi::DescriptorSetLayoutHandle workSetLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = workBindings, .debugName = "timing.workSet" }, error);

	const std::array workSetLayouts{ workSetLayout };
	const std::array workPushConstants{
		rhi::PushConstantRange{ .stages = rhi::ShaderStage::eCompute, .offset = 0, .size = sizeof(Params) },
	};
	const rhi::PipelineLayoutHandle computeLayout = dev.CreatePipelineLayout(
		rhi::PipelineLayoutDesc{ .sets = workSetLayouts, .pushConstants = workPushConstants, .debugName = "timing.computeLayout" }, error);

	const rhi::ComputePipelineHandle computePipeline = dev.CreateComputePipeline(
		rhi::ComputePipelineDesc{ .layout = computeLayout, .shader = computeShader, .debugName = "timing.computePipeline" }, error);

	const rhi::BufferHandle accumulator = dev.CreateBuffer(
		rhi::BufferDesc{
			.size	   = static_cast<std::uint64_t>(kThreads) * sizeof(float),
			.usage	   = rhi::BufferUsage::eStorage,
			.memory	   = rhi::MemoryUsage::eGpuOnly,
			.debugName = "timing.accumulator",
		},
		error);

	// The graphics half, drawing a triangle over the whole target so the occlusion count is the pixel count.
	const rhi::TextureHandle target = dev.CreateTexture(
		rhi::TextureDesc{
			.type	   = rhi::TextureType::eTex2D,
			.format	   = rhi::Format::eRGBA8UNorm,
			.width	   = kExtent,
			.height	   = kExtent,
			.usage	   = rhi::TextureUsage::eColorAttachment,
			.debugName = "timing.target",
		},
		error);
	const rhi::TextureViewHandle targetView = dev.CreateTextureView(target, rhi::TextureViewDesc{ .debugName = "timing.targetView" }, error);

	const rhi::PipelineLayoutHandle graphicsLayout = dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "timing.graphicsLayout" }, error);

	const std::array graphicsShaders{ vertexShader, pixelShader };
	rhi::VertexInputDesc vertexInput{};
	rhi::GraphicsPipelineDesc graphicsDesc{};
	graphicsDesc.layout							= graphicsLayout;
	graphicsDesc.shaders						= graphicsShaders;
	graphicsDesc.vertexInput					= &vertexInput;
	graphicsDesc.raster.cullMode				= rhi::CullMode::eNone;
	graphicsDesc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
	graphicsDesc.renderTarget.colorFormatCount	= 1;
	graphicsDesc.blend.attachmentCount			= 1;
	graphicsDesc.dynamicStates					= rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
	graphicsDesc.debugName						= "timing.graphicsPipeline";

	const rhi::GraphicsPipelineHandle graphicsPipeline = dev.CreateGraphicsPipeline(graphicsDesc, error);

	if (!workSetLayout.IsValid() || !computeLayout.IsValid() || !computePipeline.IsValid() || !accumulator.IsValid() || !target.IsValid() ||
		!targetView.IsValid() || !graphicsLayout.IsValid() || !graphicsPipeline.IsValid())
	{
		fw::ReportError("the pipelines and their resources were refused", error);
		return 1;
	}

	rhi::DescriptorArena arena = dev.CreateDescriptorArena(
		rhi::DescriptorArenaDesc{ .type = rhi::DescriptorArenaType::ePersistent, .maxSets = 1, .maxDescriptors = 1, .debugName = "timing.arena" }, error);
	const rhi::DescriptorSetHandle workSet = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = workSetLayout, .debugName = "timing.work" }, error);

	const std::array workWrites{
		rhi::DescriptorWriteBuffer{
			.set	 = workSet,
			.binding = 0,
			.type	 = rhi::DescriptorType::eStorageBuffer,
			.buffer	 = accumulator,
			.range	 = static_cast<std::uint64_t>(kThreads) * sizeof(float),
		},
	};
	if (!workSet.IsValid() || !dev.UpdateDescriptors(std::span(workWrites), error))
	{
		fw::ReportError("the compute descriptors were refused", error);
		return 1;
	}

	/*
	 * Three pools, asked for one at a time so a backend that has only the first still runs the rest of the sample. An invalid handle here is a decline and not a
	 * fault: the create call reported why, and everything below tests the handle before recording against it.
	 */
	const rhi::QueryPoolHandle timestamps = dev.CreateQueryPool(
		rhi::QueryPoolDesc{ .type = rhi::QueryType::eTimestamp, .queryCount = kTimestampCount, .debugName = "timing.timestamps" }, error);
	if (!timestamps.IsValid())
	{
		fw::ReportError("this device reports timestamp queries and then refused the pool", error);
		return 1;
	}

	rhi::Error occlusionError{};
	const rhi::QueryPoolHandle occlusion =
		dev.CreateQueryPool(rhi::QueryPoolDesc{ .type = rhi::QueryType::eOcclusion, .queryCount = 1, .debugName = "timing.occlusion" }, occlusionError);
	if (!occlusion.IsValid())
	{
		LOG_INFO(fw::Log(), "no occlusion pool: {}", occlusionError.message != nullptr ? occlusionError.message : "no diagnostic");
	}

	/*
	 * One counter and not several. A statistics query with N counters enabled writes N values per query, which a resolve packing one 64-bit value per query has
	 * nowhere to put. Asking for exactly one keeps the destination layout the same as the other two pools'.
	 */
	rhi::Error statisticsError{};
	rhi::QueryPoolHandle statistics{};
	if (caps.supportsPipelineStatisticsQueries)
	{
		statistics = dev.CreateQueryPool(
			rhi::QueryPoolDesc{
				.type		= rhi::QueryType::ePipelineStatistics,
				.queryCount = 1,
				.statistics = rhi::PipelineStatistic::eFragmentShaderInvocations,
				.debugName	= "timing.statistics",
			},
			statisticsError);
		if (!statistics.IsValid())
		{
			LOG_INFO(fw::Log(), "no statistics pool: {}", statisticsError.message != nullptr ? statisticsError.message : "no diagnostic");
		}
	}

	const rhi::BufferHandle results = dev.CreateBuffer(
		rhi::BufferDesc{
			.size	   = kResultBytes,
			.usage	   = rhi::BufferUsage::eCopyDst,
			.memory	   = rhi::MemoryUsage::eCpuReadback,
			.debugName = "timing.results",
		},
		error);

	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "timing.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "timing.pool" }, error);
	rhi::CommandList list			   = pool.Allocate("timing.frame", error);
	if (!results.IsValid() || !timeline.IsValid() || !queue.IsValid() || !list.IsValid() || !list.Begin(error))
	{
		fw::ReportError("the submission objects were refused", error);
		return 1;
	}

	const Params params{ .iterations = kIterations };

	const std::array intoShaderWrite{
		rhi::BufferBarrier{
			.buffer = accumulator,
			.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
			.after	= { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
		},
	};
	const std::array intoAttachment{
		rhi::TextureBarrier{
			.texture = target,
			.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
			.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		},
	};

	bool recorded = list.ResetQueryPool(timestamps, 0, kTimestampCount, error);
	if (occlusion.IsValid())
	{
		recorded = recorded && list.ResetQueryPool(occlusion, 0, 1, error);
	}
	if (statistics.IsValid())
	{
		recorded = recorded && list.ResetQueryPool(statistics, 0, 1, error);
	}

	/*
	 * The dispatch, bracketed only where a loose write is legal around one.
	 *
	 * This is the asymmetry the sample exists to show. A rendering scope can always be timed, because BeginRenderingDesc::timestamps records on the sample
	 * points the hardware fixes when the scope opens. A dispatch has no equivalent, and a device reporting supportsTimestampWritesInScope false refuses loose
	 * writes inside one.
	 *
	 * Nor can the scope be closed early: Metal tracks its own hazards, so a barrier leaves the encoder open.
	 */
	const bool timeTheDispatch = caps.supportsTimestampWritesInScope;

	recorded = recorded && list.WriteTimestamp(timestamps, static_cast<std::uint32_t>(Slot::eSubmitBegin), rhi::PipelineStage::eAllCommands, error) &&
			   list.Barriers(rhi::BarrierBatch{ .buffers = intoShaderWrite }, error);

	if (timeTheDispatch)
	{
		recorded = recorded && list.WriteTimestamp(timestamps, static_cast<std::uint32_t>(Slot::eComputeBegin), rhi::PipelineStage::eAllCommands, error);
	}

	recorded = recorded && list.SetComputePipeline(computePipeline, error) && list.BindDescriptorSet(computeLayout, 0, workSet, {}, error) &&
			   list.PushConstants(computeLayout, rhi::ShaderStage::eCompute, 0, sizeof(params), &params, error) &&
			   list.Dispatch(kThreads / computeShader.threadgroupSize.x, 1, 1, error);

	if (timeTheDispatch)
	{
		recorded = recorded && list.WriteTimestamp(timestamps, static_cast<std::uint32_t>(Slot::eComputeEnd), rhi::PipelineStage::eComputeShader, error);
	}

	recorded = recorded && list.Barriers(rhi::BarrierBatch{ .textures = intoAttachment }, error);

	if (!recorded)
	{
		fw::ReportError("recording the dispatch failed", error);
		return 1;
	}

	/*
	 * The render pass, timed through the scope's own slots, not by two more loose writes. This is the portable form: it records on the sample points the
	 * hardware fixes when the scope opens, so it works whether or not this device would have taken a write inside.
	 */
	const rhi::RenderingTimestampWrites scopeTimestamps{
		.pool		= timestamps,
		.beginQuery = static_cast<std::uint32_t>(Slot::eRenderBegin),
		.endQuery	= static_cast<std::uint32_t>(Slot::eRenderEnd),
	};

	const std::array colors{
		rhi::RenderingAttachment{
			.view  = targetView,
			.state = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
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

	if (!list.BeginRendering(rendering, error))
	{
		fw::ReportError("the rendering scope was refused", error);
		return 1;
	}

	if (occlusion.IsValid())
	{
		recorded = recorded && list.BeginQuery(occlusion, 0, error);
	}
	if (statistics.IsValid())
	{
		recorded = recorded && list.BeginQuery(statistics, 0, error);
	}

	const rhi::Viewport viewport{ .width = static_cast<float>(kExtent), .height = static_cast<float>(kExtent) };
	const rhi::Rect2D scissor{ .width = kExtent, .height = kExtent };

	recorded = recorded && list.SetGraphicsPipeline(graphicsPipeline, error) && list.SetViewport(viewport, error) && list.SetScissor(scissor, error) &&
			   list.Draw(3, 1, 0, 0, error);

	if (statistics.IsValid())
	{
		recorded = recorded && list.EndQuery(statistics, 0, error);
	}
	if (occlusion.IsValid())
	{
		recorded = recorded && list.EndQuery(occlusion, 0, error);
	}

	list.EndRendering(error);

	recorded = recorded && list.WriteTimestamp(timestamps, static_cast<std::uint32_t>(Slot::eSubmitEnd), rhi::PipelineStage::eAllCommands, error) &&
			   list.ResolveQueryData(timestamps, 0, kTimestampCount, results, 0, error);

	if (occlusion.IsValid())
	{
		recorded = recorded && list.ResolveQueryData(occlusion, 0, 1, results, kOcclusionByte, error);
	}
	if (statistics.IsValid())
	{
		recorded = recorded && list.ResolveQueryData(statistics, 0, 1, results, kStatisticByte, error);
	}

	recorded = recorded && list.End(error);
	if (!recorded)
	{
		fw::ReportError("recording the render pass failed", error);
		return 1;
	}

	// The wall clock around the submission, which is the number a timestamp difference should be checked against and is never the same number.
	fw::util::Timer wall;
	wall.Start();

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "timing.submit" }, error) ||
		!queue.Wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("the frame did not complete", error);
		return 1;
	}

	const double wallMilliseconds = wall.Stop<fw::util::Timer::Milliseconds>();

	const rhi::MappedMemory mapped = dev.Map(results, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		fw::ReportError("the results could not be mapped", error);
		return 1;
	}

	if (!mapped.coherent && !dev.InvalidateMappedRange(results, 0, kResultBytes, error))
	{
		fw::ReportError("the results could not be invalidated", error);
		return 1;
	}

	std::array<std::uint64_t, kResultBytes / sizeof(std::uint64_t)> raw{};
	std::memcpy(raw.data(), mapped.data, kResultBytes);
	static_cast<void>(dev.Unmap(results, error));

	/*
	 * A tick is not a nanosecond. The period converts one to the other, and the calibrated pair beside it is what a CPU timeline and a GPU one have to be lined
	 * up through: subtracting a GPU timestamp from a CPU one without it subtracts unrelated clocks.
	 */
	rhi::TimestampCalibration calibration{};
	if (!dev.CalibrateTimestamp(rhi::QueueType::eGraphics, calibration, error))
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

	/*
	 * The triangle covered every pixel, so a precise count would be the pixel count. Only non-zero is checked because that is all the RHI promises: nothing in
	 * QueryPoolDesc or BeginQuery asks for a precise occlusion count, and Vulkan without VK_QUERY_CONTROL_PRECISE_BIT is free to answer any non-zero value for
	 * "something passed". MoltenVK answers 1. A caller wanting the count occlusion culling needs cannot ask for it yet.
	 */
	if (occlusion.IsValid())
	{
		const std::uint64_t visible	 = raw.at(kOcclusionByte / sizeof(std::uint64_t));
		const std::uint64_t covered	 = static_cast<std::uint64_t>(kExtent) * kExtent;
		LOG_INFO(fw::Log(), "  {:<22} {}, and {} pixels were covered", "occlusion", visible, covered);
		if (visible == 0)
		{
			LOG_ERROR(fw::Log(), "the occlusion query saw nothing pass, and a triangle covered the whole target");
			status = 1;
		}
	}

	if (statistics.IsValid())
	{
		LOG_INFO(fw::Log(), "  {:<22} {} invocations", "fragment shader", raw.at(kStatisticByte / sizeof(std::uint64_t)));
	}

	// Every timestamp the sample recorded has to have been taken, or the numbers above are differences across a sentinel.
	for (std::uint32_t slot = 0; slot < kTimestampCount; ++slot)
	{
		const bool skipped = !timeTheDispatch &&
							 (slot == static_cast<std::uint32_t>(Slot::eComputeBegin) || slot == static_cast<std::uint32_t>(Slot::eComputeEnd));
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
	dev.Destroy(results, retired, error);
	dev.Destroy(timestamps, retired, error);
	if (occlusion.IsValid())
	{
		dev.Destroy(occlusion, retired, error);
	}
	if (statistics.IsValid())
	{
		dev.Destroy(statistics, retired, error);
	}
	dev.Destroy(targetView, retired, error);
	dev.Destroy(target, retired, error);
	dev.Destroy(accumulator, retired, error);
	dev.CollectGarbage(timeline, 1, error);
	dev.Destroy(timeline, {}, error);

	return status;
}

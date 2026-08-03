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

/*
 * What the abstraction costs to record a command, against the same command recorded straight onto the same native recorder, in the same list, on the same
 * device.
 *
 * Both arms share everything but the call, so the difference between the two numbers is the RHI's dispatch and translation. This is a CPU recording
 * measurement: no GPU, no submission, no frame time.
 *
 * One benchmark is one shape, run a pass at a time with both arms in it. See shared/pass_plan.hpp and native/arms.hpp.
 */

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
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "native_delta/native/arms.hpp"
#include "shared/options.hpp"
#include "shared/pass_plan.hpp"
#include "shared/shapes.hpp"
#include "shared/spread_gate.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

using bench::Kind;
using bench::kMetalSource;
using bench::kPushConstantBytes;
using bench::kScratchBufferBytes;
using bench::kTargetExtent;
using bench::Workload;

namespace
{

	/*
	 * How long a pass may wait on the GPU before the run gives up on the machine.
	 *
	 * The recorded work still has to run and a driver that has stopped making progress leaves the wait spinning with nothing to report. An unbounded wait would
	 * just look hung and the timeout is here so it gets reported instead.
	 */
	constexpr std::uint64_t kPassTimeoutNanoseconds = 30'000'000'000;

	// What the arms are called in the report. The RHI arm is the reported time so the native one and the difference over it ride beside it as counters.
	constexpr const char * kNativeCounter	  = "native_ns";
	constexpr const char * kDeltaCounter	  = "delta_ns";
	constexpr const char * kDeltaShareCounter = "delta_pct";

	/*
	 * The most commands one pass may record per arm. A pass therefore records twice this.
	 *
	 * Below the recording benchmark's own ceiling because both arms land in one command list and a driver's recorded command storage is real memory. Also because
	 * eight shapes at two arms each is sixteen timed loops a repetition. What a pass actually records is at most this and usually less, the machine being measured
	 * for what it can afford.
	 */
	constexpr std::size_t kCommandCeiling = 2'000'000;

	// Enough recording in a repetition to average a scheduler over, where the run budget can afford it.
	constexpr double kPreferredMinTimeSeconds = 0.1;

	constexpr double kMaxSpreadPercent = 4.0;

	/*
	 * One timed RHI pass over one command shape, in nanoseconds.
	 *
	 * The shape is chosen outside the loop so the loop body is the one call a real recording loop would have. The accepted count leaves through the caller's sink
	 * because every entry reports a bool and discarding all of them would let a compiler drop the calls it can see through.
	 */
	[[nodiscard]] std::uint64_t RecordRhi(const Kind kind, rhi::CommandList & list, const Workload & work, const std::size_t commands, std::uint64_t & sink)
	{
		std::uint64_t accepted = 0;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		switch (kind)
		{
		case Kind::eSetViewport:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.SetViewport(work.viewport));
			}
			break;

		case Kind::eSetScissor:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.SetScissor(work.scissor));
			}
			break;

		case Kind::ePushConstants:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.PushConstants(work.layout,
					rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
					0,
					kPushConstantBytes,
					work.pushConstants.data()));
			}
			break;

		case Kind::eBindDescriptorSet:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.BindDescriptorSet(work.layout, 0, work.set));
			}
			break;

		case Kind::eSetGraphicsPipeline:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.SetGraphicsPipeline(work.pipeline));
			}
			break;

		case Kind::eDraw:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.Draw(3, 1, 0, 0));
			}
			break;

		case Kind::eDrawIndexed:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.DrawIndexed(3, 1, 0, 0, 0));
			}
			break;

		case Kind::eBarrier:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.Barriers(rhi::BarrierBatch{ .textures = work.holdBarrier }));
			}
			break;
		}
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

		sink += accepted;
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
	}

} // namespace

int main(int argc, char ** argv)
{
	/*
	 * Defaults put in front of what the caller wrote, each still a flag the command line overrides.
	 *
	 * Per-repetition rows are left out of the console report, since eight shapes at thirteen rows each is a wall, and --benchmark_out keeps them either way.
	 * The warm-up is this benchmark's own because what needs warming is the clock the core runs at, which eight shapes do not each pay for.
	 */
	std::array<std::string, 2> flagDefaults{ "--benchmark_repetitions=9", "--benchmark_display_aggregates_only=true" };

	std::vector<char *> args = bench::WithFlagDefaults(argc, argv, flagDefaults);

	// Read before Initialize, which takes the flags it recognises back out of the line as it parses them.
	const std::size_t repetitions = bench::FlagValue(args, "--benchmark_repetitions", 1);
	const bool ownMinTime		  = !bench::NamesFlag(args, "--benchmark_min_time");

	int argCount = static_cast<int>(args.size());
	benchmark::Initialize(&argCount, args.data(), bench::PrintHelp);

	bench::Options options{};
	options.commandCeiling	 = kCommandCeiling;
	options.maxSpreadPercent = kMaxSpreadPercent;

	// What ships. Turning it up measures what validation costs, which is a different question.
	options.validation = rhi::ValidationMode::eOff;
	if (!bench::ParseOptions(argCount, args.data(), options))
	{
		bench::PrintOwnOptions();
		return 2;
	}

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = options.backend } };
	if (options.backend != nullptr && !backends.HonoredRequest())
	{
		std::println("this build has no {} backend", options.backend);
		return 1;
	}

	rhi::DeviceDesc deviceDesc{};
	deviceDesc.validation		= options.validation;
	deviceDesc.requireSwapchain = false;
	deviceDesc.debugName		= "bench.nativeDelta";

	const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(deviceDesc);
	if (!device)
	{
		bench::ReportError("failed to create a device", device.GetError());
		return 1;
	}

	rhi::Device dev = device.Value().Get();
	rhi::Error error{};

	Workload work{};

	const rhi::TextureDesc targetDesc{
		.type	   = rhi::TextureType::eTex2D,
		.format	   = rhi::Format::eRGBA8UNorm,
		.width	   = kTargetExtent,
		.height	   = kTargetExtent,
		.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eColorAttachment),
		.debugName = "bench.colorTarget",
	};

	work.target						   = dev.CreateTexture(targetDesc, error);
	const rhi::TextureViewHandle view  = dev.CreateTextureView(work.target, rhi::TextureViewDesc{ .debugName = "bench.colorTargetView" }, error);
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "bench.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "bench.pool" }, error);
	if (!work.target.IsValid() || !view.IsValid() || !timeline.IsValid() || !queue.IsValid() || !pool.IsValid())
	{
		bench::ReportError("failed to create the recording resources", error);
		return 1;
	}

	// A set with one binding, since a set with none records no useResource on Metal and would compare the binding call against a stand-in for it.
	const rhi::BufferDesc scratchDesc{
		.size	   = kScratchBufferBytes,
		.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eIndex,
		.debugName = "bench.scratch",
	};
	work.scratch = dev.CreateBuffer(scratchDesc, error);

	const std::array bindings{ rhi::DescriptorBinding{
		.binding = 0,
		.type	 = rhi::DescriptorType::eStorageBuffer,
		.count	 = 1,
		.stages	 = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
	} };
	const rhi::DescriptorSetLayoutHandle setLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "bench.setLayout" }, error);

	const std::array setLayouts{ setLayout };
	const std::array pushRanges{ rhi::PushConstantRange{
		.stages = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
		.offset = 0,
		.size	= kPushConstantBytes,
	} };
	work.layout =
		dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushRanges, .debugName = "bench.pipelineLayout" }, error);

	rhi::DescriptorArena arena = dev.CreateDescriptorArena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= 1,
			.maxDescriptors = 1,
			.debugName		= "bench.arena",
		},
		error);
	if (!work.scratch.IsValid() || !setLayout.IsValid() || !work.layout.IsValid() || !arena.IsValid())
	{
		bench::ReportError("failed to create the binding resources", error);
		return 1;
	}

	work.set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .debugName = "bench.set" }, error);
	if (!work.set.IsValid())
	{
		bench::ReportError("failed to allocate the descriptor set", error);
		return 1;
	}

	const std::array writes{ rhi::DescriptorWriteBuffer{
		.set	 = work.set,
		.binding = 0,
		.type	 = rhi::DescriptorType::eStorageBuffer,
		.buffer	 = work.scratch,
	} };
	if (!dev.UpdateDescriptors(writes, error))
	{
		bench::ReportError("failed to write the descriptor set", error);
		return 1;
	}

	/*
	 * A pipeline, built when the device compiles shading language source, since the draws are only a legal recording with one bound. A draw with nothing bound
	 * faults inside the driver instead of costing less, so the shapes that need one report as not compared where there is none.
	 *
	 * Metal source because that is the one form a device here accepts. A shader toolchain behind the benchmark would make what it measures depend on the host's
	 * compiler.
	 */
	const rhi::DeviceCaps & caps = dev.GetCaps();
	if (caps.supportsShaderSource && caps.shaderBinaryFormat == rhi::ShaderBinaryFormat::eBackendNative)
	{
		const std::array shaders{
			rhi::ShaderBinary{
				.stage		= rhi::ShaderStage::eVertex,
				.format		= rhi::ShaderBinaryFormat::eBackendNative,
				.data		= kMetalSource.data(),
				.size		= kMetalSource.size(),
				.entryPoint = "vertexMain",
				.isSource	= true,
			},
			rhi::ShaderBinary{
				.stage		= rhi::ShaderStage::eFragment,
				.format		= rhi::ShaderBinaryFormat::eBackendNative,
				.data		= kMetalSource.data(),
				.size		= kMetalSource.size(),
				.entryPoint = "fragmentMain",
				.isSource	= true,
			},
		};

		const rhi::VertexInputDesc vertexInput{};
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.layout							 = work.layout;
		pipelineDesc.shaders						 = shaders;
		pipelineDesc.vertexInput					 = &vertexInput;
		pipelineDesc.raster.cullMode				 = rhi::CullMode::eNone;
		pipelineDesc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
		pipelineDesc.renderTarget.colorFormatCount	 = 1;
		pipelineDesc.blend.attachmentCount			 = 1;
		pipelineDesc.dynamicStates					 = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
		pipelineDesc.debugName						 = "bench.pipeline";

		work.pipeline = dev.CreateGraphicsPipeline(pipelineDesc, error);
		if (!work.pipeline.IsValid())
		{
			bench::ReportError("failed to create the pipeline the draws are recorded against", error);
			return 1;
		}
	}

	if (!bench::native::Prepare(dev, work))
	{
		std::println("no native arm for {}, so there is nothing to compare against", dev.GetGraphicsApiName());
		return 0;
	}

	// The before state is undefined every pass, since each pass clears the target anyway and a benchmark target has no contents worth keeping.
	const std::array toAttachment{ rhi::TextureBarrier{
		.texture = work.target,
		.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
		.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
	} };
	work.holdBarrier.at(0) = rhi::TextureBarrier{
		.texture = work.target,
		.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
	};

	const std::array colors{ rhi::RenderingAttachment{
		.view		= view,
		.state		= { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		.load		= rhi::LoadOp::eClear,
		.store		= rhi::StoreOp::eStore,
		.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
	} };
	const rhi::BeginRenderingDesc rendering{
		.colors = colors,
		.width	= kTargetExtent,
		.height = kTargetExtent,
	};

	benchmark::AddCustomContext("backend", std::string(dev.GetGraphicsApiName()));
	benchmark::AddCustomContext("validation", std::string(bench::ValidationName(options.validation)));
	benchmark::AddCustomContext("spread tolerance", std::to_string(static_cast<int>(options.maxSpreadPercent)) + " percent");

	std::uint64_t sink		= 0;
	std::uint64_t submitted = 0;

	/*
	 * One pass: a fresh list out of a reset pool, opened, both arms timed into it, closed, submitted and waited on. Only the two recording loops are timed, so
	 * the submit is in neither number.
	 *
	 * The submit is here because a driver holds a bounded set of command buffers, and recording without ever submitting exhausts it: Metal blocks forever after
	 * sixty-four passes.
	 */
	const auto onePass = [&](const Kind kind,
							 const std::size_t commands,
							 std::uint64_t & wallNanoseconds,
							 std::uint64_t & rhiNanoseconds,
							 std::uint64_t & nativeNanoseconds) -> bool
	{
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

		// Nothing is in flight past the point the last pass waited for so the pool is free to reset.
		if (!pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = submitted }, error))
		{
			bench::ReportError("failed to reset the command pool", error);
			return false;
		}

		rhi::CommandList list = pool.Allocate("bench.list", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			bench::ReportError("failed to begin recording", error);
			return false;
		}

		if (!list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error))
		{
			bench::ReportError("failed to move the target to its attachment state", error);
			return false;
		}

		if (bench::NeedsRenderingScope(kind) && !list.BeginRendering(rendering, error))
		{
			bench::ReportError("failed to open the rendering scope", error);
			return false;
		}

		/*
		 * Untimed setup for the two draw shapes, recorded once a pass instead of once a command.
		 *
		 * It is what makes the draws legal in both arms at once. Both record onto the encoder this bind leaves the pipeline on so the native arm needs no bind of
		 * its own to be a draw the driver will take.
		 */
		if ((kind == Kind::eDraw || kind == Kind::eDrawIndexed) &&
			(!list.SetGraphicsPipeline(work.pipeline, error) || !list.SetIndexBuffer(work.scratch, 0, false, error)))
		{
			bench::ReportError("failed to bind what a draw needs", error);
			return false;
		}

		rhiNanoseconds = RecordRhi(kind, list, work, commands, sink);

		if (!bench::native::Record(kind, list, work, commands, nativeNanoseconds))
		{
			return false;
		}

		if (bench::NeedsRenderingScope(kind) && !list.EndRendering(error))
		{
			bench::ReportError("failed to close the rendering scope", error);
			return false;
		}

		if (!list.End(error))
		{
			bench::ReportError("failed to close the command list", error);
			return false;
		}

		++submitted;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = submitted } };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = signals,
			.debugName	  = "bench.pass",
		};

		if (!queue.Submit(submit, error) || !queue.Wait(timeline, submitted, kPassTimeoutNanoseconds, error))
		{
			bench::ReportError("failed to drain the recorded pass, which a timeout here means the GPU did not finish it inside thirty seconds", error);
			return false;
		}

		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
		wallNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		return true;
	};

	// One shape's pass, wrapped in the signature the planner and the warm-up ask for.
	const auto probeShape = [&](const Kind kind)
	{
		return [&, kind](const std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds)
		{
			std::uint64_t nativeNanoseconds = 0;
			return onePass(kind, commands, wallNanoseconds, timedNanoseconds, nativeNanoseconds);
		};
	};

	/*
	 * Warming up once for the whole run, timed by the wall clock instead of counted in passes.
	 *
	 * What needs warming is the clock the core is running at. A laptop that starts at its low-power frequency needs time to ramp and the shapes that follow keep
	 * that time spent.
	 */
	if (!bench::WarmUp(options.warmupMilliseconds, options, probeShape(Kind::eSetViewport)))
	{
		return 1;
	}

	// The run budget is shared between the shapes that are actually compared so the ones this backend cannot reach do not each hold a share of it.
	std::size_t comparable = 0;
	for (const Kind kind : bench::kKinds)
	{
		comparable += static_cast<std::size_t>(bench::native::Gap(kind).empty());
	}

	bool passFailed = false;

	for (const Kind kind : bench::kKinds)
	{
		const std::string name = std::string("native_delta/") + std::string(bench::KindName(kind));

		// A shape whose native call needs an object the RHI resolved out of a handle and does not publish is reported as skipped, with the reason standing where
		// the figures would have been. Leaving it out of the report entirely would read as if it had never been asked for.
		if (const std::string_view gap = bench::native::Gap(kind); !gap.empty())
		{
			benchmark::RegisterBenchmark(name,
				[gap](benchmark::State & state)
				{
					state.SkipWithMessage(std::string(gap));
				})
				->UseManualTime()
				->Unit(benchmark::kNanosecond);
			continue;
		}

		const bench::PassPlan plan = bench::PlanPasses(options, comparable, repetitions, kPreferredMinTimeSeconds, probeShape(kind));
		if (plan.commands == 0)
		{
			return 1;
		}

		const std::size_t commandsAPass = plan.commands;

		auto * registered = benchmark::RegisterBenchmark(name,
			[&, kind, commandsAPass](benchmark::State & state)
			{
				// A benchmark that ran after another one failed would report against a device that is already answering errors.
				if (passFailed)
				{
					state.SkipWithError("an earlier pass failed, see the diagnostic above");
					return;
				}

				// How big a pass turned out to be here, which differs per shape because what a pass costs does.
				state.SetLabel(std::to_string(commandsAPass) + " commands an arm a pass");

				const std::uint64_t acceptedBefore = sink;

				std::uint64_t rhiTotal	  = 0;
				std::uint64_t nativeTotal = 0;

				while (state.KeepRunningBatch(static_cast<benchmark::IterationCount>(commandsAPass)))
				{
					std::uint64_t wallNanoseconds	= 0;
					std::uint64_t rhiNanoseconds	= 0;
					std::uint64_t nativeNanoseconds = 0;
					if (!onePass(kind, commandsAPass, wallNanoseconds, rhiNanoseconds, nativeNanoseconds))
					{
						passFailed = true;
						state.SkipWithError("a recorded pass failed, see the diagnostic above");
						return;
					}

					rhiTotal += rhiNanoseconds;
					nativeTotal += nativeNanoseconds;

					// The RHI arm is the reported time so the native arm beside it has to be a counter.
					state.SetIterationTime(static_cast<double>(rhiNanoseconds) / bench::kNanosecondsASecond);
				}

				if (sink == acceptedBefore)
				{
					passFailed = true;
					state.SkipWithError("no recorded command was accepted, so the measurement is not of a recording");
					return;
				}

				const double recorded = static_cast<double>(state.iterations());
				const double rhiNs	  = static_cast<double>(rhiTotal) / recorded;
				const double nativeNs = static_cast<double>(nativeTotal) / recorded;

				state.counters[kNativeCounter] = benchmark::Counter(nativeNs);
				state.counters[kDeltaCounter]  = benchmark::Counter(rhiNs - nativeNs);

				/*
				 * The share is against the native arm because that is the claim under test: a caller who dropped the RHI would pay the native number so the delta
				 * over it is what the abstraction costs them. Where the native arm records nothing the ratio has no denominator and is left out. It is left out
				 * for the same shape every repetition so the aggregates still line up.
				 */
				if (!bench::native::RecordsNothing(kind))
				{
					state.counters[kDeltaShareCounter] = benchmark::Counter((rhiNs - nativeNs) / nativeNs * 100.0);
				}
			});

		registered->UseManualTime()->Unit(benchmark::kNanosecond);

		// A minimum time named on the command line is one Google Benchmark cannot see past a benchmark that names its own so the plan gives way to it.
		if (ownMinTime)
		{
			registered->MinTime(plan.minTimeSeconds);
		}
	}

	bench::SpreadGate gate{ kNativeCounter };
	benchmark::RunSpecifiedBenchmarks(&gate);
	benchmark::Shutdown();

	std::println();
	std::println("  CPU recording only. No GPU time, no submission, no presentation, and no frame time is in any number here.");
	std::println("  Both arms record into one command list on one device, so the delta is the RHI's dispatch and translation and nothing else.");

	bench::native::Release();

	// Every pass waited for its own submission so nothing is in flight to defer around.
	constexpr rhi::DestroyDesc idle{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle };
	if (work.pipeline.IsValid())
	{
		dev.Destroy(work.pipeline, idle, error);
	}
	dev.Destroy(work.layout, idle, error);
	dev.Destroy(setLayout, idle, error);
	dev.Destroy(work.scratch, idle, error);
	dev.Destroy(view, idle, error);
	dev.Destroy(work.target, idle, error);
	dev.Destroy(timeline, idle, error);

	if (passFailed)
	{
		return 1;
	}

	if (gate.WorstSpreadPercent() > options.maxSpreadPercent)
	{
		std::println();
		std::println("a shape resolved no better than {:.2f} percent, so this machine cannot see a change smaller than that", gate.WorstSpreadPercent());
		return 1;
	}

	return 0;
}

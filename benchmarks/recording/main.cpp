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
 * What one recorded command costs before it reaches a driver. The workload is dynamic state inside a rendering scope, five entries a batch, so every backend
 * takes the identical workload.
 *
 * One iteration is one recorded command, run a pass at a time: a fresh list opened, timed, closed, submitted and waited on, with only the recording loop timed.
 * See shared/pass_plan.hpp. A run that spreads wider than the tolerance exits non-zero.
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
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "shared/options.hpp"
#include "shared/pass_plan.hpp"
#include "shared/spread_gate.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// SetViewport, SetScissor, SetBlendConstants, SetStencilReference, SetDepthBias.
	constexpr std::size_t kCommandsPerBatch = 5;

	constexpr std::uint32_t kTargetExtent = 64;

	/*
	 * How long a pass may wait on the GPU before the run gives up on the machine.
	 *
	 * The recorded work still has to run and a driver that has stopped making progress leaves the wait spinning with nothing to report. An unbounded wait would
	 * just look hung and the timeout is here so it gets reported instead.
	 */
	constexpr std::uint64_t kPassTimeoutNanoseconds = 30'000'000'000;

	// High enough that the pass budget is what decides the size of a pass on any machine that can afford it and low enough that a driver is not asked to hold a
	// gigabyte of recorded commands.
	constexpr std::size_t kCommandCeiling = 10'000'000;

	// A regression this is meant to see is a couple of percent, which asks for a repetition long enough to average a scheduler over. The run budget cuts it down
	// where the machine cannot afford it.
	constexpr double kPreferredMinTimeSeconds = 0.5;

	constexpr double kMaxSpreadPercent = 2.0;

	/*
	 * One timed pass, in nanoseconds, leaving in accepted the number of entries the list took.
	 *
	 * Every entry reports a bool and the caller checks the count against what it asked for, which catches a refused command before it is reported as a cheap one
	 * and keeps a compiler that can see through the calls from dropping them.
	 */
	[[nodiscard]] std::uint64_t RecordPass(rhi::CommandList & list, const std::size_t batches, std::uint64_t & accepted)
	{
		const rhi::Viewport viewport{
			.width	= static_cast<float>(kTargetExtent),
			.height = static_cast<float>(kTargetExtent),
		};
		const rhi::Rect2D scissor{ .width = kTargetExtent, .height = kTargetExtent };

		std::uint64_t taken = 0;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		for (std::size_t batch = 0; batch < batches; ++batch)
		{
			taken += static_cast<std::uint64_t>(list.SetViewport(viewport));
			taken += static_cast<std::uint64_t>(list.SetScissor(scissor));
			taken += static_cast<std::uint64_t>(list.SetBlendConstants(0.0f, 0.0f, 0.0f, 1.0f));
			taken += static_cast<std::uint64_t>(list.SetStencilReference(static_cast<std::uint32_t>(batch)));
			taken += static_cast<std::uint64_t>(list.SetDepthBias(0.0f, 0.0f, 0.0f));
		}
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

		accepted = taken;
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
	}

} // namespace

int main(int argc, char ** argv)
{
	// A default put in front of what the caller wrote. It is still a flag the command line overrides. The warm-up is this benchmark's own, see --warmup.
	std::array<std::string, 1> flagDefaults{ "--benchmark_repetitions=9" };

	std::vector<char *> args = bench::WithFlagDefaults(argc, argv, flagDefaults);

	// Read before Initialize, which takes the flags it recognises back out of the line as it parses them.
	const std::size_t repetitions = bench::FlagValue(args, "--benchmark_repetitions", 1);
	const bool ownMinTime		  = !bench::NamesFlag(args, "--benchmark_min_time");

	int argCount = static_cast<int>(args.size());
	benchmark::Initialize(&argCount, args.data(), bench::PrintHelp);

	bench::Options options{};
	options.commandCeiling	 = kCommandCeiling;
	options.maxSpreadPercent = kMaxSpreadPercent;
	options.validation		 = rhi::ValidationMode::eReleaseLight;
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
	deviceDesc.debugName		= "bench.recording";

	const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(deviceDesc);
	if (!device)
	{
		bench::ReportError("failed to create a device", device.GetError());
		return 1;
	}

	rhi::Device dev = device.Value().Get();
	rhi::Error error{};

	const rhi::TextureDesc targetDesc{
		.type	   = rhi::TextureType::eTex2D,
		.format	   = rhi::Format::eRGBA8UNorm,
		.width	   = kTargetExtent,
		.height	   = kTargetExtent,
		.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eColorAttachment),
		.debugName = "bench.colorTarget",
	};

	const rhi::TextureHandle target	   = dev.CreateTexture(targetDesc, error);
	const rhi::TextureViewHandle view  = dev.CreateTextureView(target, rhi::TextureViewDesc{ .debugName = "bench.colorTargetView" }, error);
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "bench.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "bench.pool" }, error);
	if (!target.IsValid() || !view.IsValid() || !timeline.IsValid() || !queue.IsValid() || !pool.IsValid())
	{
		bench::ReportError("failed to create the recording resources", error);
		return 1;
	}

	// The before state is undefined every pass, since each pass clears the target anyway and a benchmark target has no contents worth keeping.
	const std::array toAttachment{ rhi::TextureBarrier{
		.texture = target,
		.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
		.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
	} };
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

	std::uint64_t submitted = 0;

	/*
	 * One pass: a fresh list out of a reset pool, opened, timed, closed, submitted and waited on. The whole of it is the wall time and the recording loop inside
	 * it is the timed one so the submit is not in the number.
	 *
	 * The submit is here because a driver holds a bounded set of command buffers. Recording without ever submitting exhausts that set and the next allocation
	 * blocks forever, which Metal does after sixty-four passes.
	 */
	const auto onePass = [&](const std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds) -> bool
	{
		const std::size_t batches = std::max<std::size_t>(commands / kCommandsPerBatch, 1);

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

		if (!list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) || !list.BeginRendering(rendering, error))
		{
			bench::ReportError("failed to open the rendering scope", error);
			return false;
		}

		std::uint64_t accepted = 0;
		timedNanoseconds	   = RecordPass(list, batches, accepted);
		if (accepted != batches * kCommandsPerBatch)
		{
			std::println("a recorded command was refused, so the measurement is not of a full pass");
			return false;
		}

		if (!list.EndRendering(error) || !list.End(error))
		{
			bench::ReportError("failed to close the rendering scope", error);
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

	if (!bench::WarmUp(options.warmupMilliseconds, options, onePass))
	{
		return 1;
	}

	const bench::PassPlan plan = bench::PlanPasses(options, 1, repetitions, kPreferredMinTimeSeconds, onePass);
	if (plan.commands == 0)
	{
		return 1;
	}

	// A whole number of batches so what the pass records is what Google Benchmark is told an iteration of it was.
	const std::size_t commandsAPass = std::max<std::size_t>(plan.commands / kCommandsPerBatch, 1) * kCommandsPerBatch;

	benchmark::AddCustomContext("backend", std::string(dev.GetGraphicsApiName()));
	benchmark::AddCustomContext("validation", std::string(bench::ValidationName(options.validation)));
	benchmark::AddCustomContext("commands a pass", std::to_string(commandsAPass));
	benchmark::AddCustomContext("spread tolerance", std::to_string(static_cast<int>(options.maxSpreadPercent)) + " percent");

	bool passFailed = false;

	auto * registered = benchmark::RegisterBenchmark("recording/dynamic_state",
		[&](benchmark::State & state)
		{
			while (state.KeepRunningBatch(static_cast<benchmark::IterationCount>(commandsAPass)))
			{
				std::uint64_t wallNanoseconds  = 0;
				std::uint64_t timedNanoseconds = 0;
				if (!onePass(commandsAPass, wallNanoseconds, timedNanoseconds))
				{
					passFailed = true;
					state.SkipWithError("a recorded pass failed, see the diagnostic above");
					return;
				}

				state.SetIterationTime(static_cast<double>(timedNanoseconds) / bench::kNanosecondsASecond);
			}
		});

	registered->UseManualTime()->Unit(benchmark::kNanosecond);

	// A minimum time named on the command line is one Google Benchmark cannot see past a benchmark that names its own so the plan gives way to it.
	if (ownMinTime)
	{
		registered->MinTime(plan.minTimeSeconds);
	}

	bench::SpreadGate gate;
	benchmark::RunSpecifiedBenchmarks(&gate);
	benchmark::Shutdown();

	// Every pass waited for its own submission so nothing is in flight to defer around.
	const rhi::DestroyDesc idle{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle };
	dev.Destroy(view, idle, error);
	dev.Destroy(target, idle, error);
	dev.Destroy(timeline, idle, error);

	if (passFailed)
	{
		return 1;
	}

	if (gate.WorstSpreadPercent() > options.maxSpreadPercent)
	{
		std::println();
		std::println("this machine resolves no better than {:.2f} percent, so it cannot see a {:.0f} percent change",
			gate.WorstSpreadPercent(),
			options.maxSpreadPercent);
		return 1;
	}

	return 0;
}

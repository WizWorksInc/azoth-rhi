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

	constexpr std::uint64_t kPassTimeoutNanoseconds = 30'000'000'000;

	constexpr const char * kNativeCounter	  = "native_ns";
	constexpr const char * kDeltaCounter	  = "delta_ns";
	constexpr const char * kDeltaShareCounter = "delta_pct";

	constexpr std::size_t kCommandCeiling = 2'000'000;

	constexpr double kPreferredMinTimeSeconds = 0.1;

	constexpr double kMaxSpreadPercent = 4.0;

	[[nodiscard]] std::uint64_t RecordRhi(const Kind kind, rhi::CommandList & list, const Workload & work, const std::size_t commands, std::uint64_t & sink)
	{
		std::uint64_t accepted = 0;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		switch (kind)
		{
		case Kind::eSetViewport:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.set_viewport(work.viewport));
			}
			break;

		case Kind::eSetScissor:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.set_scissor(work.scissor));
			}
			break;

		case Kind::ePushConstants:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.push_constants(work.layout,
					rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
					0,
					kPushConstantBytes,
					work.pushConstants.data()));
			}
			break;

		case Kind::eBindDescriptorSet:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.bind_descriptor_set(work.layout, 0, work.set));
			}
			break;

		case Kind::eSetGraphicsPipeline:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.set_graphics_pipeline(work.pipeline));
			}
			break;

		case Kind::eDraw:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.draw(3, 1, 0, 0));
			}
			break;

		case Kind::eDrawIndexed:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.draw_indexed(3, 1, 0, 0, 0));
			}
			break;

		case Kind::eBarrier:
			for (std::size_t index = 0; index < commands; ++index)
			{
				accepted += static_cast<std::uint64_t>(list.barriers(rhi::BarrierBatch{ .textures = work.holdBarrier }));
			}
			break;
		}
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

		sink += accepted;
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
	}

}

int main(int argc, char ** argv)
{
	std::array<std::string, 2> flagDefaults{ "--benchmark_repetitions=9", "--benchmark_display_aggregates_only=true" };

	std::vector<char *> args = bench::WithFlagDefaults(argc, argv, flagDefaults);

	const std::size_t repetitions = bench::FlagValue(args, "--benchmark_repetitions", 1);
	const bool ownMinTime		  = !bench::NamesFlag(args, "--benchmark_min_time");

	int argCount = static_cast<int>(args.size());
	benchmark::Initialize(&argCount, args.data(), bench::PrintHelp);

	bench::Options options{};
	options.commandCeiling	 = kCommandCeiling;
	options.maxSpreadPercent = kMaxSpreadPercent;

	options.validation = rhi::ValidationMode::eOff;
	if (!bench::ParseOptions(argCount, args.data(), options))
	{
		bench::PrintOwnOptions();
		return 2;
	}

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = options.backend } };
	if (options.backend != nullptr && !backends.honored_request())
	{
		std::println("this build has no {} backend", options.backend);
		return 1;
	}

	rhi::DeviceDesc deviceDesc{};
	deviceDesc.validation		= options.validation;
	deviceDesc.requireSwapchain = false;
	deviceDesc.debugName		= "bench.nativeDelta";

	const rhi::Result<rhi::UniqueDevice> device = backends.create_device(deviceDesc);
	if (!device)
	{
		bench::ReportError("failed to create a device", device.get_error());
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

	work.target						   = dev.create_texture(targetDesc, error);
	const rhi::TextureViewHandle view  = dev.create_texture_view(work.target, rhi::TextureViewDesc{ .debugName = "bench.colorTargetView" }, error);
	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "bench.timeline" }, error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "bench.pool" }, error);
	if (!work.target.is_valid() || !view.is_valid() || !timeline.is_valid() || !queue.is_valid() || !pool.is_valid())
	{
		bench::ReportError("failed to create the recording resources", error);
		return 1;
	}

	const rhi::BufferDesc scratchDesc{
		.size	   = kScratchBufferBytes,
		.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eIndex,
		.debugName = "bench.scratch",
	};
	work.scratch = dev.create_buffer(scratchDesc, error);

	const std::array bindings{ rhi::DescriptorBinding{
		.binding = 0,
		.type	 = rhi::DescriptorType::eStorageBuffer,
		.count	 = 1,
		.stages	 = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
	} };
	const rhi::DescriptorSetLayoutHandle setLayout =
		dev.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "bench.setLayout" }, error);

	const std::array setLayouts{ setLayout };
	const std::array pushRanges{ rhi::PushConstantRange{
		.stages = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
		.offset = 0,
		.size	= kPushConstantBytes,
	} };
	work.layout =
		dev.create_pipeline_layout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushRanges, .debugName = "bench.pipelineLayout" }, error);

	rhi::DescriptorArena arena = dev.create_descriptor_arena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= 1,
			.maxDescriptors = 1,
			.debugName		= "bench.arena",
		},
		error);
	if (!work.scratch.is_valid() || !setLayout.is_valid() || !work.layout.is_valid() || !arena.is_valid())
	{
		bench::ReportError("failed to create the binding resources", error);
		return 1;
	}

	work.set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .debugName = "bench.set" }, error);
	if (!work.set.is_valid())
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
	if (!dev.update_descriptors(writes, error))
	{
		bench::ReportError("failed to write the descriptor set", error);
		return 1;
	}

	const rhi::DeviceCaps & caps = dev.get_caps();
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

		work.pipeline = dev.create_graphics_pipeline(pipelineDesc, error);
		if (!work.pipeline.is_valid())
		{
			bench::ReportError("failed to create the pipeline the draws are recorded against", error);
			return 1;
		}
	}

	if (!bench::native::Prepare(dev, work))
	{
		std::println("no native arm for {}, so there is nothing to compare against", dev.get_graphics_api_name());
		return 0;
	}

	const std::array toAttachment{ rhi::TextureBarrier{
		.texture = work.target,
		.before	 = { .use = rhi::ResourceUse::eDiscard },
		.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
	} };
	work.holdBarrier.at(0) = rhi::TextureBarrier{
		.texture = work.target,
		.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
	};

	const std::array colors{ rhi::RenderingAttachment{
		.view		= view,
		.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		.load		= rhi::LoadOp::eClear,
		.store		= rhi::StoreOp::eStore,
		.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
	} };
	const rhi::BeginRenderingDesc rendering{
		.colors = colors,
		.width	= kTargetExtent,
		.height = kTargetExtent,
	};

	benchmark::AddCustomContext("backend", std::string(dev.get_graphics_api_name()));
	benchmark::AddCustomContext("validation", std::string(bench::ValidationName(options.validation)));
	benchmark::AddCustomContext("spread tolerance", std::to_string(static_cast<int>(options.maxSpreadPercent)) + " percent");

	std::uint64_t sink		= 0;
	std::uint64_t submitted = 0;

	const auto onePass = [&](const Kind kind,
							 const std::size_t commands,
							 std::uint64_t & wallNanoseconds,
							 std::uint64_t & rhiNanoseconds,
							 std::uint64_t & nativeNanoseconds) -> bool
	{
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

		if (!pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = submitted }, error))
		{
			bench::ReportError("failed to reset the command pool", error);
			return false;
		}

		rhi::CommandList list = pool.Allocate("bench.list", error);
		if (!list.is_valid() || !list.Begin(error))
		{
			bench::ReportError("failed to begin recording", error);
			return false;
		}

		if (!list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error))
		{
			bench::ReportError("failed to move the target to its attachment state", error);
			return false;
		}

		if (bench::NeedsRenderingScope(kind) && !list.begin_rendering(rendering, error))
		{
			bench::ReportError("failed to open the rendering scope", error);
			return false;
		}

		if ((kind == Kind::eDraw || kind == Kind::eDrawIndexed) &&
			(!list.set_graphics_pipeline(work.pipeline, error) || !list.set_index_buffer(work.scratch, 0, false, error)))
		{
			bench::ReportError("failed to bind what a draw needs", error);
			return false;
		}

		rhiNanoseconds = RecordRhi(kind, list, work, commands, sink);

		if (!bench::native::Record(kind, list, work, commands, nativeNanoseconds))
		{
			return false;
		}

		if (bench::NeedsRenderingScope(kind) && !list.end_rendering(error))
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

		if (!queue.submit(submit, error) || !queue.Wait(timeline, submitted, kPassTimeoutNanoseconds, error))
		{
			bench::ReportError("failed to drain the recorded pass, which a timeout here means the GPU did not finish it inside thirty seconds", error);
			return false;
		}

		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
		wallNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		return true;
	};

	const auto probeShape = [&](const Kind kind)
	{
		return [&, kind](const std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds)
		{
			std::uint64_t nativeNanoseconds = 0;
			return onePass(kind, commands, wallNanoseconds, timedNanoseconds, nativeNanoseconds);
		};
	};

	if (!bench::WarmUp(options.warmupMilliseconds, options, probeShape(Kind::eSetViewport)))
	{
		return 1;
	}

	std::size_t comparable = 0;
	for (const Kind kind : bench::kKinds)
	{
		comparable += static_cast<std::size_t>(bench::native::Gap(kind).empty());
	}

	bool passFailed = false;

	for (const Kind kind : bench::kKinds)
	{
		const std::string name = std::string("native_delta/") + std::string(bench::KindName(kind));

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
				if (passFailed)
				{
					state.SkipWithError("an earlier pass failed, see the diagnostic above");
					return;
				}

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

				if (!bench::native::RecordsNothing(kind))
				{
					state.counters[kDeltaShareCounter] = benchmark::Counter((rhiNs - nativeNs) / nativeNs * 100.0);
				}
			});

		registered->UseManualTime()->Unit(benchmark::kNanosecond);

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

	constexpr rhi::DestroyDesc idle{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle };
	if (work.pipeline.is_valid())
	{
		dev.destroy(work.pipeline, idle, error);
	}
	dev.destroy(work.layout, idle, error);
	dev.destroy(setLayout, idle, error);
	dev.destroy(work.scratch, idle, error);
	dev.destroy(view, idle, error);
	dev.destroy(work.target, idle, error);
	dev.destroy(timeline, idle, error);

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

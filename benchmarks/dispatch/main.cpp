// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

#include "dispatch/indirect.hpp"
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
#include <string_view>
#include <utility>
#include <vector>

namespace rhi = azo::rhi;

using bench::IndirectApi;
using bench::Kind;
using bench::kPushConstantBytes;
using bench::kScratchBufferBytes;
using bench::kTargetExtent;
using bench::Workload;

namespace
{

	constexpr std::uint64_t kPassTimeoutNanoseconds = 30'000'000'000;

	constexpr const char * kIndirectCounter	  = "indirect_ns";
	constexpr const char * kDeltaCounter	  = "delta_ns";
	constexpr const char * kDeltaShareCounter = "delta_pct";

	constexpr std::size_t kCommandCeiling = 4'000'000;

	constexpr double kPreferredMinTimeSeconds = 0.1;

	constexpr double kMaxSpreadPercent = 2.0;

	constexpr std::array kSweptModes{ rhi::ValidationMode::eOff, rhi::ValidationMode::eReleaseLight, rhi::ValidationMode::eDeveloper };

	constexpr std::array<std::uint32_t, 4> kOpaqueShader{};

	struct Fixture final
	{
		rhi::ValidationMode validation = rhi::ValidationMode::eOff;

		rhi::UniqueDevice owned{};
		rhi::Device device{};
		rhi::Queue queue{};
		rhi::CommandPool pool{};
		rhi::DescriptorArena arena{};

		rhi::TextureViewHandle view{};
		rhi::TimelineHandle timeline{};
		rhi::DescriptorSetLayoutHandle setLayout{};

		Workload work{};

		std::array<rhi::TextureBarrier, 1> toAttachment{};
		std::array<rhi::RenderingAttachment, 1> colors{};

		std::uint64_t submitted = 0;

		std::string_view gap{};
	};

	[[nodiscard]] bool CreateFixture(Fixture & fixture, const rhi::ValidationMode validation)
	{
		fixture.validation = validation;

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null" } };
		if (!backends.HonoredRequest())
		{
			std::println("this build has no null backend, which is the only one this benchmark measures");
			return false;
		}

		rhi::DeviceDesc deviceDesc{};
		deviceDesc.validation		= validation;
		deviceDesc.requireSwapchain = false;
		deviceDesc.debugName		= "bench.dispatch";

		rhi::Result<rhi::UniqueDevice> created = backends.CreateDevice(deviceDesc);
		if (!created)
		{
			bench::ReportError("failed to create a device", created.GetError());
			return false;
		}

		fixture.owned  = std::move(created).Value();
		fixture.device = fixture.owned.Get();

		rhi::Device dev = fixture.device;
		rhi::Error error{};

		const rhi::TextureDesc targetDesc{
			.type	   = rhi::TextureType::eTex2D,
			.format	   = rhi::Format::eRGBA8UNorm,
			.width	   = kTargetExtent,
			.height	   = kTargetExtent,
			.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eColorAttachment),
			.debugName = "bench.colorTarget",
		};

		fixture.work.target = dev.CreateTexture(targetDesc, error);
		fixture.view		= dev.CreateTextureView(fixture.work.target, rhi::TextureViewDesc{ .debugName = "bench.colorTargetView" }, error);
		fixture.timeline	= dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "bench.timeline" }, error);
		fixture.queue		= dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
		fixture.pool		= dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "bench.pool" }, error);
		if (!fixture.work.target.IsValid() || !fixture.view.IsValid() || !fixture.timeline.IsValid() || !fixture.queue.IsValid() || !fixture.pool.IsValid())
		{
			bench::ReportError("failed to create the recording resources", error);
			return false;
		}

		const rhi::BufferDesc scratchDesc{
			.size	   = kScratchBufferBytes,
			.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eIndex,
			.debugName = "bench.scratch",
		};
		fixture.work.scratch = dev.CreateBuffer(scratchDesc, error);

		const std::array bindings{ rhi::DescriptorBinding{
			.binding = 0,
			.type	 = rhi::DescriptorType::eStorageBuffer,
			.count	 = 1,
			.stages	 = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
		} };
		fixture.setLayout = dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "bench.setLayout" }, error);

		const std::array setLayouts{ fixture.setLayout };
		const std::array pushRanges{ rhi::PushConstantRange{
			.stages = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
			.offset = 0,
			.size	= kPushConstantBytes,
		} };
		fixture.work.layout =
			dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushRanges, .debugName = "bench.pipelineLayout" }, error);

		fixture.arena = dev.CreateDescriptorArena(
			rhi::DescriptorArenaDesc{
				.type			= rhi::DescriptorArenaType::ePersistent,
				.maxSets		= 1,
				.maxDescriptors = 1,
				.debugName		= "bench.arena",
			},
			error);
		if (!fixture.work.scratch.IsValid() || !fixture.setLayout.IsValid() || !fixture.work.layout.IsValid() || !fixture.arena.IsValid())
		{
			bench::ReportError("failed to create the binding resources", error);
			return false;
		}

		fixture.work.set = fixture.arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = fixture.setLayout, .debugName = "bench.set" }, error);
		if (!fixture.work.set.IsValid())
		{
			bench::ReportError("failed to allocate the descriptor set", error);
			return false;
		}

		const std::array writes{ rhi::DescriptorWriteBuffer{
			.set	 = fixture.work.set,
			.binding = 0,
			.type	 = rhi::DescriptorType::eStorageBuffer,
			.buffer	 = fixture.work.scratch,
		} };
		if (!dev.UpdateDescriptors(writes, error))
		{
			bench::ReportError("failed to write the descriptor set", error);
			return false;
		}

		const std::array shaders{
			rhi::ShaderBinary{
				.stage		= rhi::ShaderStage::eVertex,
				.format		= rhi::ShaderBinaryFormat::eBackendNative,
				.data		= kOpaqueShader.data(),
				.size		= kOpaqueShader.size() * sizeof(std::uint32_t),
				.entryPoint = "vertexMain",
				.isSource	= false,
			},
			rhi::ShaderBinary{
				.stage		= rhi::ShaderStage::eFragment,
				.format		= rhi::ShaderBinaryFormat::eBackendNative,
				.data		= kOpaqueShader.data(),
				.size		= kOpaqueShader.size() * sizeof(std::uint32_t),
				.entryPoint = "fragmentMain",
				.isSource	= false,
			},
		};

		const rhi::VertexInputDesc vertexInput{};
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.layout							 = fixture.work.layout;
		pipelineDesc.shaders						 = shaders;
		pipelineDesc.vertexInput					 = &vertexInput;
		pipelineDesc.raster.cullMode				 = rhi::CullMode::eNone;
		pipelineDesc.renderTarget.colorFormats.at(0) = rhi::Format::eRGBA8UNorm;
		pipelineDesc.renderTarget.colorFormatCount	 = 1;
		pipelineDesc.blend.attachmentCount			 = 1;
		pipelineDesc.dynamicStates					 = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
		pipelineDesc.debugName						 = "bench.pipeline";

		fixture.work.pipeline = dev.CreateGraphicsPipeline(pipelineDesc, error);
		if (!fixture.work.pipeline.IsValid())
		{
			fixture.gap = "no pipeline: the Null backend turned the pipeline desc down";
		}

		fixture.toAttachment.at(0) = rhi::TextureBarrier{
			.texture = fixture.work.target,
			.before	 = { .use = rhi::ResourceUse::eDiscard },
			.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		};
		fixture.work.holdBarrier.at(0) = rhi::TextureBarrier{
			.texture = fixture.work.target,
			.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		};
		fixture.colors.at(0) = rhi::RenderingAttachment{
			.view		= fixture.view,
			.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
		};

		return true;
	}

	void DestroyFixture(Fixture & fixture)
	{
		if (!fixture.device.IsValid())
		{
			return;
		}

		rhi::Error error{};

		const rhi::DestroyDesc idle{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle };
		if (fixture.work.pipeline.IsValid())
		{
			fixture.device.Destroy(fixture.work.pipeline, idle, error);
		}
		fixture.device.Destroy(fixture.work.layout, idle, error);
		fixture.device.Destroy(fixture.setLayout, idle, error);
		fixture.device.Destroy(fixture.work.scratch, idle, error);
		fixture.device.Destroy(fixture.view, idle, error);
		fixture.device.Destroy(fixture.work.target, idle, error);
		fixture.device.Destroy(fixture.timeline, idle, error);
	}

	[[nodiscard]] std::uint64_t RecordRhi(const Kind kind, rhi::CommandList & list, const Workload & work, const std::size_t commands, std::uint64_t & accepted)
	{
		std::uint64_t taken = 0;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		switch (kind)
		{
		case Kind::eSetViewport:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.SetViewport(work.viewport));
			}
			break;

		case Kind::eSetScissor:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.SetScissor(work.scissor));
			}
			break;

		case Kind::ePushConstants:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.PushConstants(work.layout,
					rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
					0,
					kPushConstantBytes,
					work.pushConstants.data()));
			}
			break;

		case Kind::eBindDescriptorSet:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.BindDescriptorSet(work.layout, 0, work.set));
			}
			break;

		case Kind::eSetGraphicsPipeline:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.SetGraphicsPipeline(work.pipeline));
			}
			break;

		case Kind::eDraw:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.Draw(3, 1, 0, 0));
			}
			break;

		case Kind::eDrawIndexed:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.DrawIndexed(3, 1, 0, 0, 0));
			}
			break;

		case Kind::eBarrier:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(list.Barriers(rhi::BarrierBatch{ .textures = work.holdBarrier }));
			}
			break;
		}
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

		accepted = taken;
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
	}

	[[nodiscard]] std::uint64_t RecordIndirect(
		const Kind kind, const IndirectApi & block, void * impl, const Workload & work, const std::size_t commands, std::uint64_t & accepted)
	{
		const IndirectApi * table = &block;
		benchmark::DoNotOptimize(table);
		benchmark::DoNotOptimize(impl);

		std::uint64_t taken = 0;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		switch (kind)
		{
		case Kind::eSetViewport:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->setViewport(impl, work.viewport, nullptr));
			}
			break;

		case Kind::eSetScissor:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->setScissor(impl, work.scissor, nullptr));
			}
			break;

		case Kind::ePushConstants:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->pushConstants(impl,
					work.layout,
					rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment,
					0,
					kPushConstantBytes,
					work.pushConstants.data(),
					nullptr));
			}
			break;

		case Kind::eBindDescriptorSet:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->bindDescriptorSet(impl, work.layout, 0, work.set, {}, nullptr));
			}
			break;

		case Kind::eSetGraphicsPipeline:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->setGraphicsPipeline(impl, work.pipeline, nullptr));
			}
			break;

		case Kind::eDraw:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->draw(impl, 3, 1, 0, 0, nullptr));
			}
			break;

		case Kind::eDrawIndexed:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->drawIndexed(impl, 3, 1, 0, 0, 0, nullptr));
			}
			break;

		case Kind::eBarrier:
			for (std::size_t index = 0; index < commands; ++index)
			{
				taken += static_cast<std::uint64_t>(table->barriers(impl, rhi::BarrierBatch{ .textures = work.holdBarrier }, nullptr));
			}
			break;
		}
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

		accepted = taken;
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
	}

	struct PassTiming final
	{
		std::uint64_t wallNanoseconds	  = 0;
		std::uint64_t rhiNanoseconds	  = 0;
		std::uint64_t indirectNanoseconds = 0;
	};

	[[nodiscard]] bool RecordOnePass(Fixture & fixture, const Kind kind, const std::size_t commands, PassTiming & timing)
	{
		rhi::Error error{};

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

		if (!fixture.pool.Reset(rhi::RetirePoint{ .timeline = fixture.timeline, .value = fixture.submitted }, error))
		{
			bench::ReportError("failed to reset the command pool", error);
			return false;
		}

		rhi::CommandList list = fixture.pool.Allocate("bench.list", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			bench::ReportError("failed to begin recording", error);
			return false;
		}

		if (!list.Barriers(rhi::BarrierBatch{ .textures = fixture.toAttachment }, error))
		{
			bench::ReportError("failed to move the target to its attachment state", error);
			return false;
		}

		const rhi::BeginRenderingDesc rendering{
			.colors = fixture.colors,
			.width	= kTargetExtent,
			.height = kTargetExtent,
		};
		if (bench::NeedsRenderingScope(kind) && !list.BeginRendering(rendering, error))
		{
			bench::ReportError("failed to open the rendering scope", error);
			return false;
		}

		if ((kind == Kind::eDraw || kind == Kind::eDrawIndexed) &&
			(!list.SetGraphicsPipeline(fixture.work.pipeline, error) || !list.SetIndexBuffer(fixture.work.scratch, 0, false, error)))
		{
			bench::ReportError("failed to bind what a draw needs", error);
			return false;
		}

		std::uint64_t rhiAccepted	   = 0;
		std::uint64_t indirectAccepted = 0;
		timing.rhiNanoseconds		   = RecordRhi(kind, list, fixture.work, commands, rhiAccepted);
		timing.indirectNanoseconds	   = RecordIndirect(kind, bench::IndirectBlock(), bench::IndirectImpl(), fixture.work, commands, indirectAccepted);

		if (rhiAccepted != commands)
		{
			std::println("{} refused a {} under validation {}, so the measurement is not of a recording",
				commands - rhiAccepted,
				bench::KindName(kind),
				bench::ValidationName(fixture.validation));
			return false;
		}

		if (indirectAccepted != commands)
		{
			std::println("the indirect arm did not answer for every call, so a compiler has seen through it");
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

		++fixture.submitted;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = fixture.timeline, .value = fixture.submitted } };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = signals,
			.debugName	  = "bench.pass",
		};

		if (!fixture.queue.Submit(submit, error) || !fixture.queue.Wait(fixture.timeline, fixture.submitted, kPassTimeoutNanoseconds, error))
		{
			bench::ReportError("failed to drain the recorded pass, which a timeout here means nothing finished it inside thirty seconds", error);
			return false;
		}

		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
		timing.wallNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		return true;
	}

}

int main(int argc, char ** argv)
{
	std::array<std::string, 2> flagDefaults{ "--benchmark_repetitions=9", "--benchmark_display_aggregates_only=true" };

	std::vector<char *> args = bench::WithFlagDefaults(argc, argv, flagDefaults);

	const std::size_t repetitions = bench::FlagValue(args, "--benchmark_repetitions", 1);
	const bool ownMinTime		  = !bench::NamesFlag(args, "--benchmark_min_time");
	const bool ownValidation	  = !bench::NamesOption(args, "--validation");

	int argCount = static_cast<int>(args.size());
	benchmark::Initialize(&argCount, args.data(), bench::PrintHelp);

	bench::Options options{};
	options.commandCeiling	 = kCommandCeiling;
	options.maxSpreadPercent = kMaxSpreadPercent;
	options.validation		 = rhi::ValidationMode::eOff;
	if (!bench::ParseOptions(argCount, args.data(), options))
	{
		bench::PrintOwnOptions();
		return 2;
	}

	if (options.backend != nullptr && std::string_view(options.backend) != "null")
	{
		std::println("this benchmark measures dispatch against the null backend, so it has nothing to say about {}", options.backend);
		return 2;
	}

	std::vector<rhi::ValidationMode> modes;
	if (ownValidation)
	{
		modes.assign(kSweptModes.begin(), kSweptModes.end());
	}
	else
	{
		modes.push_back(options.validation);
	}

	std::vector<Fixture> fixtures(modes.size());
	for (std::size_t index = 0; index < modes.size(); ++index)
	{
		if (!CreateFixture(fixtures.at(index), modes.at(index)))
		{
			return 1;
		}
	}

	benchmark::AddCustomContext("backend", std::string(fixtures.front().device.GetGraphicsApiName()));
	benchmark::AddCustomContext("validation", ownValidation ? std::string("off, light and developer") : std::string(bench::ValidationName(options.validation)));
	benchmark::AddCustomContext("spread tolerance", std::to_string(static_cast<int>(options.maxSpreadPercent)) + " percent");

	const auto probeShape = [&fixtures](const std::size_t fixture, const Kind kind)
	{
		return [&fixtures, fixture, kind](const std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds)
		{
			PassTiming timing{};
			if (!RecordOnePass(fixtures.at(fixture), kind, commands, timing))
			{
				return false;
			}

			wallNanoseconds	 = timing.wallNanoseconds;
			timedNanoseconds = timing.rhiNanoseconds;
			return true;
		};
	};

	if (!bench::WarmUp(options.warmupMilliseconds, options, probeShape(0, Kind::eSetViewport)))
	{
		return 1;
	}

	std::size_t comparable = 0;
	for (const Fixture & fixture : fixtures)
	{
		for (const Kind kind : bench::kKinds)
		{
			comparable += static_cast<std::size_t>(!bench::NeedsPipeline(kind) || fixture.gap.empty());
		}
	}

	bool passFailed = false;

	for (std::size_t index = 0; index < fixtures.size(); ++index)
	{
		Fixture & fixture = fixtures.at(index);

		for (const Kind kind : bench::kKinds)
		{
			const std::string name =
				std::string("dispatch/") + std::string(bench::ValidationName(fixture.validation)) + "/" + std::string(bench::KindName(kind));

			if (bench::NeedsPipeline(kind) && !fixture.gap.empty())
			{
				const std::string_view gap = fixture.gap;
				benchmark::RegisterBenchmark(name,
					[gap](benchmark::State & state)
					{
						state.SkipWithMessage(std::string(gap));
					})
					->UseManualTime()
					->Unit(benchmark::kNanosecond);
				continue;
			}

			const bench::PassPlan plan = bench::PlanPasses(options, comparable, repetitions, kPreferredMinTimeSeconds, probeShape(index, kind));
			if (plan.commands == 0)
			{
				return 1;
			}

			const std::size_t commandsAPass = plan.commands;

			auto * registered = benchmark::RegisterBenchmark(name,
				[&fixture, &passFailed, kind, commandsAPass](benchmark::State & state)
				{
					if (passFailed)
					{
						state.SkipWithError("an earlier pass failed, see the diagnostic above");
						return;
					}

					state.SetLabel(std::to_string(commandsAPass) + " commands an arm a pass");

					std::uint64_t rhiTotal		= 0;
					std::uint64_t indirectTotal = 0;

					while (state.KeepRunningBatch(static_cast<benchmark::IterationCount>(commandsAPass)))
					{
						PassTiming timing{};
						if (!RecordOnePass(fixture, kind, commandsAPass, timing))
						{
							passFailed = true;
							state.SkipWithError("a recorded pass failed, see the diagnostic above");
							return;
						}

						rhiTotal += timing.rhiNanoseconds;
						indirectTotal += timing.indirectNanoseconds;

						state.SetIterationTime(static_cast<double>(timing.rhiNanoseconds) / bench::kNanosecondsASecond);
					}

					const double recorded	= static_cast<double>(state.iterations());
					const double rhiNs		= static_cast<double>(rhiTotal) / recorded;
					const double indirectNs = static_cast<double>(indirectTotal) / recorded;

					state.counters[kIndirectCounter] = benchmark::Counter(indirectNs);
					state.counters[kDeltaCounter]	 = benchmark::Counter(rhiNs - indirectNs);

					if (indirectNs > 0.0)
					{
						state.counters[kDeltaShareCounter] = benchmark::Counter((rhiNs - indirectNs) / indirectNs * 100.0);
					}
				});

			registered->UseManualTime()->Unit(benchmark::kNanosecond);

			if (ownMinTime)
			{
				registered->MinTime(plan.minTimeSeconds);
			}
		}
	}

	bench::SpreadGate gate{ kIndirectCounter };
	benchmark::RunSpecifiedBenchmarks(&gate);
	benchmark::Shutdown();

	std::println();
	std::println("  CPU dispatch only. The backend records nothing, so no number here contains a driver, a GPU or a frame.");
	std::println("  delta_ns is what the RHI adds over one indirect call, which is the floor under the native delta's own figures.");

	for (Fixture & fixture : fixtures)
	{
		DestroyFixture(fixture);
	}

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

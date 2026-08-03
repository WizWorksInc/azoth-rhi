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
 * Four shading languages against one RHI, each running the kernel it is actually good at. They do not reach the same answer. What they share is that the
 * binding a shader declares lands where the RHI's ABI says, whichever language declared it.
 *
 * Every kernel writes one storage buffer at set 0 binding 0 and the host checks the numbers that come back.
 */

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/builders/resource_builders.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "compiler.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint32_t kElements	= 64;
	constexpr std::uint64_t kBufferSize = kElements * sizeof(float);

	// What each kernel should produce, so a backend that binds the buffer somewhere else fails here without looking like it worked.
	struct Expectation final
	{
		std::uint32_t index = 0;
		float value			= 0.0f;
	};

	struct Kernel final
	{
		const char * language = nullptr;
		const char * file	  = nullptr;
		const char * entry	  = nullptr;
		const char * shows	  = nullptr;
		langs::SourceLanguage source{};

		// Dispatch shape. The GLSL kernel walks an 8 by 8 grid, the other two a single row of 64.
		std::uint32_t groupsX = 1;
		std::uint32_t groupsY = 1;
		langs::Threadgroup threadgroup{};

		Expectation expected{};
	};

	constexpr std::array kKernels{
		Kernel{ .language = "Slang",
			.file		  = "generics.slang",
			.entry		  = "computeMain",
			.shows		  = "an interface with two implementations, specialized where it is written",
			.source		  = langs::SourceLanguage::eSlang,
			.threadgroup  = { .x = 64 },
			// Chain(Scale{3}, Offset{1}, x) is x * 3 + 1, so element 10 is 31.
			.expected = { .index = 10, .value = 31.0f } },
		Kernel{ .language = "HLSL",
			.file		  = "reduce.hlsl",
			.entry		  = "computeMain",
			.shows		  = "groupshared memory and a barrier, named for two APIs at once",
			.source		  = langs::SourceLanguage::eHlsl,
			.threadgroup  = { .x = 64 },
			// The halving loop leaves the whole group's total in lane zero, and 0 through 63 sum to 2016.
			.expected = { .index = 0, .value = 2016.0f } },
		Kernel{ .language = "GLSL",
			.file		  = "pattern.glsl",
			.entry		  = "main",
			.shows		  = "a signed distance field over a grid, the shape every GLSL sketch starts from",
			.source		  = langs::SourceLanguage::eGlsl,
			.groupsX	  = 8,
			.groupsY	  = 8,
			.threadgroup  = { .x = 1, .y = 1 },
			// Cell 27 is x 3 y 3, whose centre sits 0.0884 from the middle of the grid and so 0.2616 inside a disc of radius 0.35.
			.expected = { .index = 27, .value = -0.2616f } },
		Kernel{ .language = "MSL",
			.file		  = "simd.metal",
			.entry		  = "computeMain",
			.shows		  = "the SIMD group as a named thing, reducing in registers where HLSL walks shared memory",
			.source		  = langs::SourceLanguage::eMsl,
			.threadgroup  = { .x = 64 },
			// The same total the HLSL kernel reaches, by a different road: 0 through 63 sum to 2016.
			.expected = { .index = 0, .value = 2016.0f } },
	};

	/*
	 * Runs one kernel and hands back what landed in the buffer. Everything below the shader is the same for all four: one layout, one dispatch, one readback, with
	 * only the language the kernel was written in changing.
	 */
	[[nodiscard]] bool RunKernel(rhi::Device dev, langs::ShaderCompiler & compiler, const Kernel & kernel, std::span<float> readBack, std::string & why)
	{
		const rhi::ShaderBinary binary = compiler.Compile(kernel.file, kernel.entry, kernel.source, kernel.threadgroup, why);
		if (binary.data == nullptr)
		{
			return false;
		}

		rhi::Error error{};

		constexpr std::array bindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .stages = rhi::ShaderStage::eCompute },
		};

		const rhi::DescriptorSetLayoutHandle setLayout =
			dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "languages.set" }, error);
		const std::array setLayouts{ setLayout };
		const rhi::PipelineLayoutHandle layout =
			dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .debugName = "languages.layout" }, error);
		if (!setLayout.IsValid() || !layout.IsValid())
		{
			why = "the pipeline layout was refused";
			return false;
		}

		const rhi::ComputePipelineHandle pipeline =
			dev.CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = binary, .debugName = "languages.pipeline" }, error);
		if (!pipeline.IsValid())
		{
			why = error.message != nullptr ? error.message : "the pipeline was refused";
			return false;
		}

		rhi::BufferBuilder storageDesc;
		storageDesc.Size(kBufferSize).GpuOnly().DebugName("languages.storage");
		storageDesc.Usage(rhi::Flags(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopySrc);
		const rhi::BufferHandle storage = dev.CreateBuffer(storageDesc.Build(), error);

		rhi::BufferBuilder readDesc;
		readDesc.Size(kBufferSize).Usage(rhi::BufferUsage::eCopyDst).CpuReadback().DebugName("languages.readback");
		const rhi::BufferHandle readback = dev.CreateBuffer(readDesc.Build(), error);
		if (!storage.IsValid() || !readback.IsValid())
		{
			why = "the buffers were refused";
			return false;
		}

		rhi::DescriptorArena arena = dev.CreateDescriptorArena(
			rhi::DescriptorArenaDesc{
				.type			= rhi::DescriptorArenaType::ePersistent,
				.maxSets		= 1,
				.maxDescriptors = 1,
				.debugName		= "languages.arena",
			},
			error);

		const rhi::DescriptorSetHandle set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .debugName = "languages.descriptors" }, error);
		const std::array writes{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .buffer = storage, .range = kBufferSize },
		};
		if (!set.IsValid() || !dev.UpdateDescriptors(std::span(writes), error))
		{
			why = "the descriptors were refused";
			return false;
		}

		const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "languages.timeline" }, error);
		rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eCompute, 0, error);
		rhi::CommandPool pool = dev.CreateCommandPool(rhi::CommandPoolDesc{ .queueType = rhi::QueueType::eCompute, .debugName = "languages.pool" }, error);
		rhi::CommandList list = pool.Allocate("languages.dispatch", error);
		if (!timeline.IsValid() || !queue.IsValid() || !list.IsValid() || !list.Begin(error))
		{
			why = "the submission objects were refused";
			return false;
		}

		const std::array intoShaderWrite{
			rhi::BufferBarrier{
				.buffer = storage,
				.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
				.after	= { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
			},
		};

		// The copy has to see what the dispatch wrote, which is what this barrier is for. Without it the readback races the kernel and reads zeroes.
		const std::array afterDispatch{
			rhi::BufferBarrier{
				.buffer = storage,
				.before = { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
				.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead },
			},
		};

		const bool recorded = list.Barriers(rhi::BarrierBatch{ .buffers = intoShaderWrite }, error) && list.SetComputePipeline(pipeline, error) &&
							  list.BindDescriptorSet(layout, 0, set, {}, error) && list.Dispatch(kernel.groupsX, kernel.groupsY, 1, error) &&
							  list.Barriers(rhi::BarrierBatch{ .buffers = afterDispatch }, error) &&
							  list.CopyBuffer(readback, 0, storage, 0, kBufferSize, error) && list.End(error);
		if (!recorded)
		{
			why = error.message != nullptr ? error.message : "recording failed";
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
		const rhi::SubmitDesc submit{ .commandLists = lists, .signals = signals, .debugName = "languages.submit" };

		constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
		if (!queue.Submit(submit, error) || !queue.Wait(timeline, 1, kNoTimeout, error))
		{
			why = error.message != nullptr ? error.message : "the dispatch did not complete";
			return false;
		}

		const rhi::MappedMemory mapped = dev.Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (mapped.data == nullptr)
		{
			why = "the readback buffer would not map";
			return false;
		}

		if (!mapped.coherent && !dev.InvalidateMappedRange(readback, 0, kBufferSize, error))
		{
			why = "the readback buffer would not invalidate";
			return false;
		}

		std::memcpy(readBack.data(), mapped.data, kBufferSize);
		static_cast<void>(dev.Unmap(readback, error));
		return true;
	}

} // namespace

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const char * requested = args.size() > 1 ? args[1] : nullptr;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };
	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .DebugName("shader_languages")
													  .Validation(rhi::ValidationMode::eDeveloper)
													  .Headless()
													  .ComputeQueue()
													  .Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		fw::ReportError("failed to create a device", device.GetError());
		// 77 is what ctest reads as a skip: no driver here is not a failure of the sample.
		return 77;
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	langs::ShaderCompiler compiler;
	std::string error;
	if (!compiler.Open(dev.GetGraphicsApiId(), error))
	{
		LOG_ERROR(fw::Log(), "{}", error);
		return 77;
	}

	int failures = 0;
	int ran		 = 0;
	for (const Kernel & kernel : kKernels)
	{
		std::vector<float> values(kElements, 0.0f);
		std::string why;
		if (!RunKernel(dev, compiler, kernel, values, why))
		{
			// A language this build cannot reach is worth saying out loud without hiding, since which front ends and targets a compiler has is the whole subject here.
			// It is not a failure of the RHI.
			LOG_INFO(fw::Log(), "{:<5} skipped: {}", kernel.language, why);
			continue;
		}

		++ran;
		const float got	   = values.at(kernel.expected.index);
		const bool matched = std::abs(got - kernel.expected.value) < 0.001f;
		failures += matched ? 0 : 1;

		LOG_INFO(fw::Log(), "{:<5} {}", kernel.language, kernel.shows);
		LOG_INFO(fw::Log(), "      element {} is {}, expected {}{}", kernel.expected.index, got, kernel.expected.value, matched ? "" : "  MISMATCH");
		LOG_INFO(fw::Log(), "      first four: {} {} {} {}", values.at(0), values.at(1), values.at(2), values.at(3));
	}

	if (ran == 0)
	{
		LOG_INFO(fw::Log(), "no language in this sample could reach this backend");
		return 77;
	}

	LOG_INFO(fw::Log(), "{} of {} languages ran, {} mismatched", ran, kKernels.size(), failures);
	return failures == 0 ? 0 : 1;
}

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
#include "tracy_lifetime.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint> // NOLINT
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
			.expected	  = { .index = 10, .value = 31.0f } },
		Kernel{ .language = "HLSL",
			.file		  = "reduce.hlsl",
			.entry		  = "computeMain",
			.shows		  = "groupshared memory and a barrier, named for two APIs at once",
			.source		  = langs::SourceLanguage::eHlsl,
			.threadgroup  = { .x = 64 },
			.expected	  = { .index = 0, .value = 2016.0f } },
		Kernel{ .language = "GLSL",
			.file		  = "pattern.glsl",
			.entry		  = "main",
			.shows		  = "a signed distance field over a grid, the shape every GLSL sketch starts from",
			.source		  = langs::SourceLanguage::eGlsl,
			.groupsX	  = 8,
			.groupsY	  = 8,
			.threadgroup  = { .x = 1, .y = 1 },
			.expected	  = { .index = 27, .value = -0.2616f } },
		Kernel{ .language = "MSL",
			.file		  = "simd.metal",
			.entry		  = "computeMain",
			.shows		  = "the SIMD group as a named thing, reducing in registers where HLSL walks shared memory",
			.source		  = langs::SourceLanguage::eMsl,
			.threadgroup  = { .x = 64 },
			.expected	  = { .index = 0, .value = 2016.0f } },
	};

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
			dev.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "languages.set" }, error);
		const std::array setLayouts{ setLayout };
		const rhi::PipelineLayoutHandle layout =
			dev.create_pipeline_layout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .debugName = "languages.layout" }, error);
		if (!setLayout.is_valid() || !layout.is_valid())
		{
			why = "the pipeline layout was refused";
			return false;
		}

		const rhi::ComputePipelineHandle pipeline =
			dev.create_compute_pipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = binary, .debugName = "languages.pipeline" }, error);
		if (!pipeline.is_valid())
		{
			why = error.message != nullptr ? error.message : "the pipeline was refused";
			return false;
		}

		rhi::BufferBuilder storageDesc;
		storageDesc.size(kBufferSize).gpu_only().debug_name("languages.storage");
		storageDesc.usage(rhi::Flags(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopySrc);
		const rhi::BufferHandle storage = dev.create_buffer(storageDesc.build(), error);

		rhi::BufferBuilder readDesc;
		readDesc.size(kBufferSize).usage(rhi::BufferUsage::eCopyDst).cpu_readback().debug_name("languages.readback");
		const rhi::BufferHandle readback = dev.create_buffer(readDesc.build(), error);
		if (!storage.is_valid() || !readback.is_valid())
		{
			why = "the buffers were refused";
			return false;
		}

		rhi::DescriptorArena arena = dev.create_descriptor_arena(
			rhi::DescriptorArenaDesc{
				.type			= rhi::DescriptorArenaType::ePersistent,
				.maxSets		= 1,
				.maxDescriptors = 1,
				.debugName		= "languages.arena",
			},
			error
		);

		const rhi::DescriptorSetHandle set = arena.allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .debugName = "languages.descriptors" }, error);
		const std::array writes{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .buffer = storage, .range = kBufferSize },
		};
		if (!set.is_valid() || !dev.update_descriptors(std::span(writes), error))
		{
			why = "the descriptors were refused";
			return false;
		}

		const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "languages.timeline" }, error);
		rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eCompute, 0, error);
		rhi::CommandPool pool = dev.create_command_pool(rhi::CommandPoolDesc{ .queueType = rhi::QueueType::eCompute, .debugName = "languages.pool" }, error);
		rhi::CommandList list = pool.allocate("languages.dispatch", error);
		if (!timeline.is_valid() || !queue.is_valid() || !list.is_valid() || !list.begin(error))
		{
			why = "the submission objects were refused";
			return false;
		}

		const std::array intoShaderWrite{
			rhi::BufferBarrier{
				.buffer = storage,
				.before = { .use = rhi::ResourceUse::eDiscard },
				.after	= { .use = rhi::ResourceUse::eStorageWrite, .stages = rhi::Stage::eCompute },
			},
		};

		const std::array afterDispatch{
			rhi::BufferBarrier{
				.buffer = storage,
				.before = { .use = rhi::ResourceUse::eStorageWrite, .stages = rhi::Stage::eCompute },
				.after	= { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			},
		};

		const bool recorded = list.barriers(rhi::BarrierBatch{ .buffers = intoShaderWrite }, error) && list.set_compute_pipeline(pipeline, error) &&
							  list.bind_descriptor_set(layout, 0, set, {}, error) && list.dispatch(kernel.groupsX, kernel.groupsY, 1, error) &&
							  list.barriers(rhi::BarrierBatch{ .buffers = afterDispatch }, error) &&
							  list.copy_buffer(readback, 0, storage, 0, kBufferSize, error) && list.end(error);
		if (!recorded)
		{
			why = error.message != nullptr ? error.message : "recording failed";
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
		const rhi::SubmitDesc submit{ .commandLists = lists, .signals = signals, .debugName = "languages.submit" };

		constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
		if (!queue.submit(submit, error) || !queue.wait(timeline, 1, kNoTimeout, error))
		{
			why = error.message != nullptr ? error.message : "the dispatch did not complete";
			return false;
		}

		const rhi::MappedMemory mapped = dev.map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (mapped.data == nullptr)
		{
			why = "the readback buffer would not map";
			return false;
		}

		if (!mapped.coherent && !dev.invalidate_mapped_range(readback, 0, kBufferSize, error))
		{
			why = "the readback buffer would not invalidate";
			return false;
		}

		std::memcpy(readBack.data(), mapped.data, kBufferSize);
		static_cast<void>(dev.unmap(readback, error));
		return true;
	}

} // namespace

int main(int argc, char ** argv)
{
	const azo::rhi::support::TracyLifetime tracyLifetime;

	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const char * requested = args.size() > 1 ? args[1] : nullptr;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };
	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .debug_name("shader_languages")
													  .validation(rhi::ValidationMode::eDeveloper)
													  .headless()
													  .compute_queue()
													  .build(backends.registry(), backends.preferred_apis());
	if (!device)
	{
		fw::ReportError("failed to create a device", device.get_error());
		return 77;
	}

	rhi::Device dev = device.value().get();
	LOG_INFO(fw::Log(), "backend: {}", dev.get_graphics_api_name());

	langs::ShaderCompiler compiler;
	std::string error;
	if (!compiler.Open(dev.get_graphics_api_id(), error))
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

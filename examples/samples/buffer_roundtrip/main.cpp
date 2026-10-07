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
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

}

namespace
{

	constexpr std::uint32_t kElementCount = 64;
	constexpr std::uint64_t kBufferBytes  = kElementCount * sizeof(std::uint32_t);

	std::vector<std::uint32_t> MakePattern()
	{
		std::vector<std::uint32_t> values(kElementCount);
		for (std::uint32_t index = 0; index < kElementCount; ++index)
		{
			values[index] = (index << 16u) | (kElementCount - index);
		}

		return values;
	}

	enum class MapOutcome : std::uint8_t
	{
		eDone,
		eNoHostMemory,
		eFailed,
	};

	MapOutcome WritePattern(rhi::Device dev, const rhi::BufferHandle buffer, const std::span<const std::uint32_t> values)
	{
		rhi::Error error{};
		const rhi::MappedMemory mapped = dev.map(buffer, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			if (error.code == rhi::ErrorCode::eUnsupportedFeature)
			{
				return MapOutcome::eNoHostMemory;
			}

			fw::ReportError("failed to map the upload buffer", error);
			return MapOutcome::eFailed;
		}

		std::memcpy(mapped.data, values.data(), values.size_bytes());

		if (!mapped.coherent && !dev.flush_mapped_range(buffer, 0, values.size_bytes(), error))
		{
			fw::ReportError("failed to flush the upload buffer", error);
			return MapOutcome::eFailed;
		}

		if (!dev.unmap(buffer, error))
		{
			fw::ReportError("failed to unmap the upload buffer", error);
			return MapOutcome::eFailed;
		}

		return MapOutcome::eDone;
	}

	bool ReadBackMatches(rhi::Device dev, const rhi::BufferHandle buffer, const std::span<const std::uint32_t> expected)
	{
		rhi::Error error{};
		const rhi::MappedMemory mapped = dev.map(buffer, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (mapped.data == nullptr)
		{
			fw::ReportError("failed to map the readback buffer", error);
			return false;
		}

		if (!mapped.coherent && !dev.invalidate_mapped_range(buffer, 0, expected.size_bytes(), error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return false;
		}

		std::vector<std::uint32_t> observed(expected.size());
		std::memcpy(observed.data(), mapped.data, expected.size_bytes());
		static_cast<void>(dev.unmap(buffer, error));

		LOG_INFO(fw::Log(), "wrote     0x{:x} 0x{:x} 0x{:x} ...", expected[0], expected[1], expected[2]);
		LOG_INFO(fw::Log(), "read back 0x{:x} 0x{:x} 0x{:x} ...", observed.at(0), observed.at(1), observed.at(2));

		return std::ranges::equal(observed, expected);
	}

}

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.honored_request())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().debug_name("buffer_roundtrip").headless().graphics_queue().build(backends.registry(), backends.preferred_apis());
	if (!device)
	{
		return fw::ReportNoDevice(device.get_error());
	}

	rhi::Device dev = device.value().get();
	LOG_INFO(fw::Log(), "backend: {}", dev.get_graphics_api_name());

	rhi::Error error{};

	rhi::BufferBuilder uploadDesc;
	uploadDesc.size(kBufferBytes).usage(rhi::BufferUsage::eCopySrc).cpu_upload().debug_name("example.upload");
	const rhi::BufferHandle upload = dev.create_buffer(uploadDesc.build(), error);

	rhi::BufferBuilder storageDesc;
	storageDesc.size(kBufferBytes).gpu_only().debug_name("example.storage");
	storageDesc.usage(rhi::Flags(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopyDst | rhi::BufferUsage::eCopySrc);
	const rhi::BufferHandle storage = dev.create_buffer(storageDesc.build(), error);

	rhi::BufferBuilder readbackDesc;
	readbackDesc.size(kBufferBytes).usage(rhi::BufferUsage::eCopyDst).cpu_readback().debug_name("example.readback");
	const rhi::BufferHandle readback = dev.create_buffer(readbackDesc.build(), error);

	if (!upload.is_valid() || !storage.is_valid() || !readback.is_valid())
	{
		fw::ReportError("failed to create the buffers", error);
		return 1;
	}

	const std::vector<std::uint32_t> pattern = MakePattern();
	const MapOutcome uploaded				 = WritePattern(dev, upload, pattern);
	if (uploaded == MapOutcome::eFailed)
	{
		return 1;
	}
	if (uploaded == MapOutcome::eNoHostMemory)
	{
		LOG_INFO(fw::Log(), "note: this backend exposes no mappable memory, so nothing is verified");
	}

	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "example.timeline" }, error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "example.pool" }, error);
	if (!timeline.is_valid() || !queue.is_valid() || !pool.is_valid())
	{
		fw::ReportError("failed to create the submission objects", error);
		return 1;
	}

	rhi::CommandList list = pool.allocate("example.copies", error);
	if (!list.is_valid() || !list.begin(error))
	{
		fw::ReportError("failed to begin recording", error);
		return 1;
	}

	const std::array intoStorage{
		rhi::BufferBarrier{
			.buffer = storage,
			.before = { .use = rhi::ResourceUse::eDiscard },
			.after	= { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
		},
	};

	const std::array outOfStorage{
		rhi::BufferBarrier{
			.buffer = storage,
			.before = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			.after	= { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
		},
	};

	const bool recorded = list.barriers(rhi::BarrierBatch{ .buffers = intoStorage }, error) && list.copy_buffer(storage, 0, upload, 0, kBufferBytes, error) &&
						  list.barriers(rhi::BarrierBatch{ .buffers = outOfStorage }, error) && list.copy_buffer(readback, 0, storage, 0, kBufferBytes, error) &&
						  list.end(error);
	if (!recorded)
	{
		fw::ReportError("failed to record the copies", error);
		return 1;
	}

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	const rhi::SubmitDesc submit{
		.commandLists = lists,
		.signals	  = signals,
		.debugName	  = "example.copySubmit",
	};

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
	if (!queue.submit(submit, error) || !queue.wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("failed to submit the copies", error);
		return 1;
	}

	int status = 0;
	if (uploaded == MapOutcome::eDone)
	{
		const bool matched = ReadBackMatches(dev, readback, pattern);
		LOG_INFO(fw::Log(), "{}", matched ? "the round trip matched" : "the round trip did not match");
		status = matched ? 0 : 1;
	}

	const rhi::DestroyDesc retired{
		.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
		.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
	};

	dev.destroy(readback, retired, error);
	dev.destroy(storage, retired, error);
	dev.destroy(upload, retired, error);
	dev.collect_garbage(timeline, 1, error);
	dev.destroy(timeline, {}, error);

	return status;
}

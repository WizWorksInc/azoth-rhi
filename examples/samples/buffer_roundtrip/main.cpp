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

} // namespace

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
		const rhi::MappedMemory mapped = dev.Map(buffer, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
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

		// Coherent memory reaches the GPU on its own. Everything else needs the write pushed out.
		if (!mapped.coherent && !dev.FlushMappedRange(buffer, 0, values.size_bytes(), error))
		{
			fw::ReportError("failed to flush the upload buffer", error);
			return MapOutcome::eFailed;
		}

		if (!dev.Unmap(buffer, error))
		{
			fw::ReportError("failed to unmap the upload buffer", error);
			return MapOutcome::eFailed;
		}

		return MapOutcome::eDone;
	}

	// Reads the buffer back and says whether it holds what went up, printing both either way.
	bool ReadBackMatches(rhi::Device dev, const rhi::BufferHandle buffer, const std::span<const std::uint32_t> expected)
	{
		rhi::Error error{};
		const rhi::MappedMemory mapped = dev.Map(buffer, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (mapped.data == nullptr)
		{
			fw::ReportError("failed to map the readback buffer", error);
			return false;
		}

		// The mirror of the flush above: the CPU's view of this range may predate the GPU's writes.
		if (!mapped.coherent && !dev.InvalidateMappedRange(buffer, 0, expected.size_bytes(), error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return false;
		}

		std::vector<std::uint32_t> observed(expected.size());
		std::memcpy(observed.data(), mapped.data, expected.size_bytes());
		static_cast<void>(dev.Unmap(buffer, error));

		LOG_INFO(fw::Log(), "wrote     0x{:x} 0x{:x} 0x{:x} ...", expected[0], expected[1], expected[2]);
		LOG_INFO(fw::Log(), "read back 0x{:x} 0x{:x} 0x{:x} ...", observed.at(0), observed.at(1), observed.at(2));

		return std::ranges::equal(observed, expected);
	}

} // namespace

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	// Every backend this build has, registered, with the requested one first. What that set is was settled when the library was compiled so this sample never
	// has to ask.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("buffer_roundtrip").Headless().GraphicsQueue().Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	rhi::Error error{};

	rhi::BufferBuilder uploadDesc;
	uploadDesc.Size(kBufferBytes).Usage(rhi::BufferUsage::eCopySrc).CpuUpload().DebugName("example.upload");
	const rhi::BufferHandle upload = dev.CreateBuffer(uploadDesc.Build(), error);

	rhi::BufferBuilder storageDesc;
	storageDesc.Size(kBufferBytes).GpuOnly().DebugName("example.storage");
	storageDesc.Usage(rhi::Flags(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopyDst | rhi::BufferUsage::eCopySrc);
	const rhi::BufferHandle storage = dev.CreateBuffer(storageDesc.Build(), error);

	rhi::BufferBuilder readbackDesc;
	readbackDesc.Size(kBufferBytes).Usage(rhi::BufferUsage::eCopyDst).CpuReadback().DebugName("example.readback");
	const rhi::BufferHandle readback = dev.CreateBuffer(readbackDesc.Build(), error);

	if (!upload.IsValid() || !storage.IsValid() || !readback.IsValid())
	{
		fw::ReportError("failed to create the buffers", error);
		return 1;
	}

	// Host visible memory is the one part of this a backend can genuinely not have. Where there is none the copies below still record and submit also there is
	// just nothing to look at afterwards.
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

	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "example.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "example.pool" }, error);
	if (!timeline.IsValid() || !queue.IsValid() || !pool.IsValid())
	{
		fw::ReportError("failed to create the submission objects", error);
		return 1;
	}

	rhi::CommandList list = pool.Allocate("example.copies", error);
	if (!list.IsValid() || !list.Begin(error))
	{
		fw::ReportError("failed to begin recording", error);
		return 1;
	}

	// The second copy has to see the first so the storage buffer moves from copy write to copy read between them. Without that barrier the two copies are
	// unordered against each other.
	const std::array intoStorage{
		rhi::BufferBarrier{
			.buffer = storage,
			.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
			.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite },
		},
	};

	const std::array outOfStorage{
		rhi::BufferBarrier{
			.buffer = storage,
			.before = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite },
			.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead },
		},
	};

	const bool recorded = list.Barriers(rhi::BarrierBatch{ .buffers = intoStorage }, error) && list.CopyBuffer(storage, 0, upload, 0, kBufferBytes, error) &&
						  list.Barriers(rhi::BarrierBatch{ .buffers = outOfStorage }, error) && list.CopyBuffer(readback, 0, storage, 0, kBufferBytes, error) &&
						  list.End(error);
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
	if (!queue.Submit(submit, error) || !queue.Wait(timeline, 1, kNoTimeout, error))
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

	// Retire everything against the value the submission signaled and then collect once it is reached.
	const rhi::DestroyDesc retired{
		.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
		.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
	};

	dev.Destroy(readback, retired, error);
	dev.Destroy(storage, retired, error);
	dev.Destroy(upload, retired, error);
	dev.CollectGarbage(timeline, 1, error);
	dev.Destroy(timeline, {}, error);

	return status;
}

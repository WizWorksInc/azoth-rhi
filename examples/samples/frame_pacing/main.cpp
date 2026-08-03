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
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// The backend name this sample takes as its first argument or null when it was not given one.

	const char * Yes(const bool value)
	{
		return value ? "yes" : "no";
	}

} // namespace

namespace
{

	constexpr std::size_t kFramesInFlight = 2;
	constexpr std::uint64_t kFrameCount	  = 8;
	constexpr std::uint64_t kScratchBytes = 4096;
	constexpr std::uint64_t kNoTimeout	  = std::numeric_limits<std::uint64_t>::max();

	// One slot of the ring: the pool a frame records from and the timeline value that retires it.
	struct FrameSlot final
	{
		rhi::CommandPool pool;
		std::uint64_t submitted = 0;
	};

} // namespace

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("frame_pacing").Headless().GraphicsQueue().Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}, timeline sync: {}", dev.GetGraphicsApiName(), Yes(dev.GetCaps().supportsTimelineSync));

	rhi::Error error{};
	rhi::Queue queue = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	if (!queue.IsValid())
	{
		fw::ReportError("failed to get the graphics queue", error);
		return 1;
	}

	// One timeline for the whole loop. Frame N signals value N so a completed value of N means every resource frame N touched is free again.
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "example.frameTimeline" }, error);
	if (!timeline.IsValid())
	{
		fw::ReportError("failed to create the frame timeline", error);
		return 1;
	}

	std::array<FrameSlot, kFramesInFlight> slots;
	for (FrameSlot & slot : slots)
	{
		slot.pool = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "example.framePool" }, error);
		if (!slot.pool.IsValid())
		{
			fw::ReportError("failed to create a frame command pool", error);
			return 1;
		}
	}

	const rhi::BufferDesc scratchDesc{
		.size = kScratchBytes,
		// eStorage as well as eCopyDst: clearing a buffer is a UAV clear on Direct3D 12, so the buffer has to be able to carry one.
		.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eCopyDst) | rhi::BufferUsage::eStorage,
		.memory	   = rhi::MemoryUsage::eGpuOnly,
		.debugName = "example.frameScratch",
	};

	for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
	{
		FrameSlot & slot = slots[(frame - 1) % kFramesInFlight];

		// The wait that bounds the loop. With two slots, frame 3 cannot start until frame 1 is done. This keeps the CPU from running arbitrarily far ahead of
		// the GPU.
		if (slot.submitted != 0 && !queue.Wait(timeline, slot.submitted, kNoTimeout, error))
		{
			fw::ReportError("failed to wait for a frame to retire", error);
			return 1;
		}

		std::uint64_t completed = 0;
		static_cast<void>(queue.GetCompletedValue(timeline, completed, error));

		// The reset goes after that wait, since a pool cannot be reset while the GPU is still executing lists allocated from it.
		if (slot.submitted != 0 && !slot.pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = slot.submitted }, error))
		{
			fw::ReportError("failed to reset a frame command pool", error);
			return 1;
		}

		const rhi::BufferHandle scratch = dev.CreateBuffer(scratchDesc, error);
		rhi::CommandList list			= slot.pool.Allocate("example.frameList", error);
		if (!scratch.IsValid() || !list.IsValid() || !list.Begin(error))
		{
			fw::ReportError("failed to start recording a frame", error);
			return 1;
		}

		list.BeginDebugLabel("example.frame");
		if (!list.ClearBuffer(scratch, 0, kScratchBytes, static_cast<std::uint32_t>(frame), error))
		{
			fw::ReportError("failed to record the frame's work", error);
			return 1;
		}
		list.EndDebugLabel();

		if (!list.End(error))
		{
			fw::ReportError("failed to close the frame's list", error);
			return 1;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = frame } };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = signals,
			.debugName	  = "example.frameSubmit",
		};

		if (!queue.Submit(submit, error))
		{
			fw::ReportError("failed to submit a frame", error);
			return 1;
		}

		slot.submitted = frame;

		// The per-frame resource goes away against the value this frame signaled, not now, since the GPU has not touched it yet.
		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = frame },
		};
		dev.Destroy(scratch, retired, error);
		dev.CollectGarbage(timeline, completed, error);

		LOG_INFO(fw::Log(), "frame {} on slot {}, retired through {}", frame, ((frame - 1) % kFramesInFlight), completed);
	}

	if (!queue.WaitIdle(error))
	{
		fw::ReportError("failed to drain the queue", error);
		return 1;
	}

	dev.CollectGarbage(timeline, kFrameCount, error);
	dev.Destroy(timeline, {}, error);

	LOG_INFO(fw::Log(), "{} frames through {} slots", kFrameCount, kFramesInFlight);
	return 0;
}

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
#include "azoth/rhi/core/version.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/profiler.hpp"
#include "azoth/rhi/host/tracy_profiler.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <tracy/Tracy.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <thread>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::size_t kFramesInFlight = 2;
	constexpr std::uint64_t kScratchBytes = 4096;
	constexpr std::uint64_t kNoTimeout	  = std::numeric_limits<std::uint64_t>::max();

	// Paced, not run flat out. Clearing a buffer six hundred times finishes in about the time Tracy takes to draw one frame, and a timeline where every frame
	// landed in the same millisecond has nothing in it to read.
	constexpr std::uint64_t kFrameCount			  = 600;
	constexpr std::chrono::milliseconds kInterval = std::chrono::milliseconds(16);

	// One slot of the ring: the pool a frame records from and the timeline value that retires it.
	struct FrameSlot final
	{
		rhi::CommandPool pool;
		std::uint64_t submitted = 0;
	};

	// The second sink, so the fan-out below is doing something visible. It counts what it is handed and stops there, which is enough to show that installing
	// Tracy's sink did not cost the application its own.
	class ZoneCounter final : public rhi::Profiler
	{
	public:
		void BeginZone(const rhi::ZoneLocation & /*location*/) override
		{
			m_zones.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] std::uint64_t Zones() const
		{
			return m_zones.load(std::memory_order_relaxed);
		}

	private:
		// Calls arrive from whichever thread is recording, so the counter is atomic. A sink that is not thread safe is a data race waiting for a second thread.
		std::atomic<std::uint64_t> m_zones{ 0 };
	};

	// The RHI owns none of these so they have to outlive every device that might report into them, which is what puts all four here and not in main. The profiler
	// slot holds one sink, so wanting the RHI's events in Tracy and somewhere else as well is a fan-out and not a choice.
	rhi::TracyProfiler g_tracy;
	ZoneCounter g_counter;
	std::array<rhi::Profiler *, 2> g_sinks{ &g_tracy, &g_counter };
	rhi::BroadcastProfiler g_broadcast{ g_sinks };

#ifdef TRACY_ON_DEMAND

	// An on-demand client records nothing until a server connects, so a run that starts first and finishes first hands over an empty capture. Waiting is what
	// keeps the reader from having to race the sample to its own profiler.
	bool WaitForServer()
	{
		constexpr std::chrono::milliseconds kPollInterval = std::chrono::milliseconds(100);
		constexpr int kPolls							  = 300;

		LOG_INFO(fw::Log(), "waiting up to {} seconds for a Tracy server", (kPolls * kPollInterval).count() / 1000);

		for (int poll = 0; poll < kPolls && !TracyIsConnected; ++poll)
		{
			std::this_thread::sleep_for(kPollInterval);
		}

		return TracyIsConnected;
	}

#endif // TRACY_ON_DEMAND

} // namespace

int main(int argc, char ** argv)
{
	// Installed before the device exists so bring-up is instrumented along with the frames, which is where the expensive part of a backend usually is.
	rhi::SetProfiler(&g_broadcast);

	constexpr rhi::BuildInfo build = rhi::GetBuildInfo();
	if (!build.profilingEnabled)
	{
		// With AZOTH_RHI_ENABLE_PROFILING off every instrumentation point in the library compiles to nothing, so the capture holds this sample's own zones and none
		// of the RHI's. Said here, not left to look like a Tracy problem.
		LOG_INFO(fw::Log(), "note: this build has profiling compiled out, so the RHI will report nothing");
	}

#ifdef TRACY_ON_DEMAND
	if (!WaitForServer())
	{
		LOG_INFO(fw::Log(), "note: no server connected, so this run records nothing. Start Tracy and run it again");
	}
#endif

	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("tracy_profiler").Headless().GraphicsQueue().Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	rhi::Error error{};
	rhi::Queue queue = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	if (!queue.IsValid())
	{
		fw::ReportError("failed to get the graphics queue", error);
		return 1;
	}

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
		.size	   = kScratchBytes,
		.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eCopyDst) | rhi::BufferUsage::eStorage,
		.memory	   = rhi::MemoryUsage::eGpuOnly,
		.debugName = "example.frameScratch",
	};

	for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
	{
		// At the top of the loop and not the bottom, which puts it between the zone below and the frame mark that closed the last one. The frame zone is then
		// measuring the frame's work instead of the wait that paced it.
		std::this_thread::sleep_for(kInterval);

		// The sample's own zone, opened with Tracy's macro, not through the sink. Everything the RHI reports between here and the closing brace nests inside it,
		// which is what handing the library the application's own client buys.
		ZoneScopedN("frame");

		FrameSlot & slot = slots[(frame - 1) % kFramesInFlight];

		if (slot.submitted != 0 && !queue.Wait(timeline, slot.submitted, kNoTimeout, error))
		{
			fw::ReportError("failed to wait for a frame to retire", error);
			return 1;
		}

		std::uint64_t completed = 0;
		static_cast<void>(queue.GetCompletedValue(timeline, completed, error));

		if (slot.submitted != 0 && !slot.pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = slot.submitted }, error))
		{
			fw::ReportError("failed to reset a frame command pool", error);
			return 1;
		}

		const rhi::BufferHandle scratch = dev.CreateBuffer(scratchDesc, error);
		if (!scratch.IsValid())
		{
			fw::ReportError("failed to create the frame's scratch buffer", error);
			return 1;
		}

		rhi::CommandList list = slot.pool.Allocate("example.frameList", error);

		{
			ZoneScopedN("record");

			if (!list.IsValid() || !list.Begin(error))
			{
				fw::ReportError("failed to start recording a frame", error);
				return 1;
			}

			if (!list.ClearBuffer(scratch, 0, kScratchBytes, static_cast<std::uint32_t>(frame), error) || !list.End(error))
			{
				fw::ReportError("failed to record the frame's work", error);
				return 1;
			}
		}

		{
			ZoneScopedN("submit");

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
		}

		slot.submitted = frame;

		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = frame },
		};
		dev.Destroy(scratch, retired, error);
		dev.CollectGarbage(timeline, completed, error);

		// What turns a run of zones into frames Tracy can slice on, and the reason its frame time graph has anything in it.
		FrameMark;
	}

	if (!queue.WaitIdle(error))
	{
		fw::ReportError("failed to drain the queue", error);
		return 1;
	}

	dev.CollectGarbage(timeline, kFrameCount, error);
	dev.Destroy(timeline, {}, error);

	LOG_INFO(fw::Log(), "{} frames, {} RHI zones through both sinks", kFrameCount, g_counter.Zones());

	// Cleared before the sinks go away at exit, since the library holds the pointer and not a reference to something it owns.
	rhi::SetProfiler(nullptr);
	return 0;
}

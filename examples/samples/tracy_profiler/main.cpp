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

	constexpr std::uint64_t kFrameCount			  = 600;
	constexpr std::chrono::milliseconds kInterval = std::chrono::milliseconds(16);

	struct FrameSlot final
	{
		rhi::CommandPool pool;
		std::uint64_t submitted = 0;
	};

	class ZoneCounter final : public rhi::Profiler
	{
	public:
		void begin_zone(const rhi::ZoneLocation &) override
		{
			m_zones.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] std::uint64_t Zones() const
		{
			return m_zones.load(std::memory_order_relaxed);
		}

	private:
		std::atomic<std::uint64_t> m_zones{ 0 };
	};

	rhi::TracyProfiler g_tracy;
	ZoneCounter g_counter;
	std::array<rhi::Profiler *, 2> g_sinks{ &g_tracy, &g_counter };
	rhi::BroadcastProfiler g_broadcast{ g_sinks };

#ifdef TRACY_ON_DEMAND

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

#endif

}

int main(int argc, char ** argv)
{
	rhi::set_profiler(&g_broadcast);

	constexpr rhi::BuildInfo build = rhi::get_build_info();
	if (!build.profilingEnabled)
	{
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
	if (requested != nullptr && !backends.honored_request())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().debug_name("tracy_profiler").headless().graphics_queue().build(backends.registry(), backends.preferred_apis());
	if (!device)
	{
		return fw::ReportNoDevice(device.get_error());
	}

	rhi::Device dev = device.value().get();
	LOG_INFO(fw::Log(), "backend: {}", dev.get_graphics_api_name());

	rhi::Error error{};
	rhi::Queue queue = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	if (!queue.is_valid())
	{
		fw::ReportError("failed to get the graphics queue", error);
		return 1;
	}

	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "example.frameTimeline" }, error);
	if (!timeline.is_valid())
	{
		fw::ReportError("failed to create the frame timeline", error);
		return 1;
	}

	std::array<FrameSlot, kFramesInFlight> slots;
	for (FrameSlot & slot : slots)
	{
		slot.pool = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "example.framePool" }, error);
		if (!slot.pool.is_valid())
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
		std::this_thread::sleep_for(kInterval);

		ZoneScopedN("frame");

		FrameSlot & slot = slots[(frame - 1) % kFramesInFlight];

		if (slot.submitted != 0 && !queue.wait(timeline, slot.submitted, kNoTimeout, error))
		{
			fw::ReportError("failed to wait for a frame to retire", error);
			return 1;
		}

		std::uint64_t completed = 0;
		static_cast<void>(queue.get_completed_value(timeline, completed, error));

		if (slot.submitted != 0 && !slot.pool.reset(rhi::RetirePoint{ .timeline = timeline, .value = slot.submitted }, error))
		{
			fw::ReportError("failed to reset a frame command pool", error);
			return 1;
		}

		const rhi::BufferHandle scratch = dev.create_buffer(scratchDesc, error);
		if (!scratch.is_valid())
		{
			fw::ReportError("failed to create the frame's scratch buffer", error);
			return 1;
		}

		rhi::CommandList list = slot.pool.allocate("example.frameList", error);

		{
			ZoneScopedN("record");

			if (!list.is_valid() || !list.begin(error))
			{
				fw::ReportError("failed to start recording a frame", error);
				return 1;
			}

			if (!list.clear_buffer(scratch, 0, kScratchBytes, static_cast<std::uint32_t>(frame), error) || !list.end(error))
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

			if (!queue.submit(submit, error))
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
		dev.destroy(scratch, retired, error);
		dev.collect_garbage(timeline, completed, error);

		FrameMark;
	}

	if (!queue.wait_idle(error))
	{
		fw::ReportError("failed to drain the queue", error);
		return 1;
	}

	dev.collect_garbage(timeline, kFrameCount, error);
	dev.destroy(timeline, {}, error);

	LOG_INFO(fw::Log(), "{} frames, {} RHI zones through both sinks", kFrameCount, g_counter.Zones());

	rhi::set_profiler(nullptr);
	return 0;
}

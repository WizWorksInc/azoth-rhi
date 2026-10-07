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
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/ownership/unique.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"
#include "tracy_lifetime.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace rhi = azo::rhi;

namespace
{
	constexpr std::uint64_t kNoTimeout	= std::numeric_limits<std::uint64_t>::max();
	constexpr std::uint64_t kBufferSize = 4096;

	[[nodiscard]] bool DeviceKnows(rhi::Device dev, const rhi::BufferHandle buffer)
	{
		rhi::BufferInfo info{};
		return dev.get_buffer_info(buffer, info);
	}

	[[nodiscard]] rhi::UniqueBuffer MakeBuffer(rhi::Device dev, const char * name)
	{
		rhi::Error error{};
		const rhi::BufferHandle handle = dev.create_buffer(
			rhi::BufferDesc{
				.size	   = kBufferSize,
				.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eCopySrc) | rhi::BufferUsage::eCopyDst,
				.memory	   = rhi::MemoryUsage::eGpuOnly,
				.debugName = name,
			},
			error
		);

		if (!handle.is_valid())
		{
			fw::ReportError("failed to create a buffer", error);
			return {};
		}

		return rhi::UniqueBuffer{ dev, handle };
	}

	[[nodiscard]] bool ShowOwnershipMoves(rhi::Device dev)
	{
		LOG_INFO(fw::Log(), "-- ownership moves --");

		std::vector<rhi::UniqueBuffer> pool;
		for (int index = 0; index < 4; ++index)
		{
			pool.push_back(MakeBuffer(dev, "unique.pool"));
			if (!pool.back())
			{
				return false;
			}
		}

		const rhi::BufferHandle first = pool.front().get();
		LOG_INFO(fw::Log(), "a pool of {} buffers survived reallocation, the first is still known: {}", pool.size(), DeviceKnows(dev, first));

		rhi::UniqueBuffer taken = std::move(pool.front());
		LOG_INFO(fw::Log(), "after moving one out, the slot holds a handle: {}, the taker holds one: {}", pool.front().is_valid(), taken.is_valid());

		const rhi::BufferHandle replaced = pool.back().get();
		pool.back()						 = MakeBuffer(dev, "unique.replacement");
		LOG_INFO(fw::Log(), "assigning over a slot destroyed what it held, still known: {}", DeviceKnows(dev, replaced));

		return !DeviceKnows(dev, replaced) && taken.is_valid() && !pool.front().is_valid();
	}

	[[nodiscard]] bool ShowReleaseAndReset(rhi::Device dev)
	{
		LOG_INFO(fw::Log(), "-- release and reset --");

		rhi::UniqueBuffer owned = MakeBuffer(dev, "unique.released");
		if (!owned)
		{
			return false;
		}

		const rhi::BufferHandle released = owned.release();
		LOG_INFO(fw::Log(), "released: the owner is empty ({}) and the handle is still live ({})", !owned.is_valid(), DeviceKnows(dev, released));

		rhi::Error error{};
		const bool destroyed = dev.destroy(released, {}, error);
		LOG_INFO(fw::Log(), "destroyed by hand: {}, still known: {}", destroyed, DeviceKnows(dev, released));

		rhi::UniqueBuffer early = MakeBuffer(dev, "unique.reset");
		if (!early)
		{
			return false;
		}

		const rhi::BufferHandle earlyHandle = early.get();

		if (!early.reset(error))
		{
			fw::ReportError("failed to destroy a buffer early", error);
			return false;
		}

		LOG_INFO(fw::Log(), "reset early: still known: {}, resetting again is harmless: {}", DeviceKnows(dev, earlyHandle), (early.reset(), true));

		return !DeviceKnows(dev, released) && !DeviceKnows(dev, earlyHandle);
	}

	[[nodiscard]] bool ShowDeferredDestruction(rhi::Device dev, rhi::Queue & queue)
	{
		LOG_INFO(fw::Log(), "-- deferred destruction --");

		rhi::Error error{};

		const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "unique.timeline" }, error);
		rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "unique.pool" }, error);
		if (!timeline.is_valid() || !pool.is_valid())
		{
			fw::ReportError("failed to set up the submission", error);
			return false;
		}

		rhi::UniqueTimeline timelineOwner{ dev, timeline };
		rhi::UniqueBuffer source = MakeBuffer(dev, "unique.deferred.src");
		rhi::UniqueBuffer target = MakeBuffer(dev, "unique.deferred.dst");
		if (!source || !target)
		{
			return false;
		}

		rhi::CommandList list = pool.allocate("unique.copy", error);
		const bool recorded	  = list.is_valid() && list.begin(error) && list.copy_buffer(*target, 0, *source, 0, kBufferSize, error) && list.end(error);
		if (!recorded)
		{
			fw::ReportError("failed to record the copy", error);
			return false;
		}

		constexpr std::uint64_t kSignalValue = 1;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = kSignalValue } };

		if (!queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "unique.submit" }, error))
		{
			fw::ReportError("failed to submit the copy", error);
			return false;
		}

		const rhi::DestroyDesc afterCopy{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = { .timeline = timeline, .value = kSignalValue },
		};

		source.set_destroy_desc(afterCopy);
		target.set_destroy_desc(afterCopy);

		const rhi::BufferHandle sourceHandle = source.get();
		source.reset();
		target.reset();

		LOG_INFO(fw::Log(), "let go of both buffers while the copy was still running, the handle is already unknown: {}", !DeviceKnows(dev, sourceHandle));

		if (!queue.wait(timeline, kSignalValue, kNoTimeout, error))
		{
			fw::ReportError("failed to wait for the copy", error);
			return false;
		}

		LOG_INFO(fw::Log(), "the copy completed and the native release ran behind it");

		static_cast<void>(pool.reset(rhi::RetirePoint{ .timeline = timeline, .value = kSignalValue }, error));
		timelineOwner.set_destroy_desc(rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle });

		return !DeviceKnows(dev, sourceHandle);
	}
}

int main(int argc, char ** argv)
{
	const azo::rhi::support::TracyLifetime tracyLifetime;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = fw::RequestedBackend(argc, argv) } };

	rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().debug_name("unique_handles").headless().graphics_queue().build(backends.registry(), backends.preferred_apis());
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
		fw::ReportError("failed to get a queue", error);
		return 1;
	}

	if (!ShowOwnershipMoves(dev) || !ShowReleaseAndReset(dev) || !ShowDeferredDestruction(dev, queue))
	{
		LOG_ERROR(fw::Log(), "an ownership check did not hold");
		return 1;
	}

	LOG_INFO(fw::Log(), "every handle was accounted for");

	return 0;
}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
		return dev.GetBufferInfo(buffer, info);
	}

	[[nodiscard]] rhi::UniqueBuffer MakeBuffer(rhi::Device dev, const char * name)
	{
		rhi::Error error{};
		const rhi::BufferHandle handle = dev.CreateBuffer(
			rhi::BufferDesc{
				.size	   = kBufferSize,
				.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eCopySrc) | rhi::BufferUsage::eCopyDst,
				.memory	   = rhi::MemoryUsage::eGpuOnly,
				.debugName = name,
			},
			error);

		if (!handle.IsValid())
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

		const rhi::BufferHandle first = pool.front().Get();
		LOG_INFO(fw::Log(), "a pool of {} buffers survived reallocation, the first is still known: {}", pool.size(), DeviceKnows(dev, first));

		rhi::UniqueBuffer taken = std::move(pool.front());
		LOG_INFO(fw::Log(), "after moving one out, the slot holds a handle: {}, the taker holds one: {}", pool.front().IsValid(), taken.IsValid());

		const rhi::BufferHandle replaced = pool.back().Get();
		pool.back()						 = MakeBuffer(dev, "unique.replacement");
		LOG_INFO(fw::Log(), "assigning over a slot destroyed what it held, still known: {}", DeviceKnows(dev, replaced));

		return !DeviceKnows(dev, replaced) && taken.IsValid() && !pool.front().IsValid();
	}

	[[nodiscard]] bool ShowReleaseAndReset(rhi::Device dev)
	{
		LOG_INFO(fw::Log(), "-- release and reset --");

		rhi::UniqueBuffer owned = MakeBuffer(dev, "unique.released");
		if (!owned)
		{
			return false;
		}

		const rhi::BufferHandle released = owned.Release();
		LOG_INFO(fw::Log(), "released: the owner is empty ({}) and the handle is still live ({})", !owned.IsValid(), DeviceKnows(dev, released));

		rhi::Error error{};
		const bool destroyed = dev.Destroy(released, {}, error);
		LOG_INFO(fw::Log(), "destroyed by hand: {}, still known: {}", destroyed, DeviceKnows(dev, released));

		rhi::UniqueBuffer early = MakeBuffer(dev, "unique.reset");
		if (!early)
		{
			return false;
		}

		const rhi::BufferHandle earlyHandle = early.Get();

		if (!early.Reset(error))
		{
			fw::ReportError("failed to destroy a buffer early", error);
			return false;
		}

		LOG_INFO(fw::Log(), "reset early: still known: {}, resetting again is harmless: {}", DeviceKnows(dev, earlyHandle), (early.Reset(), true));

		return !DeviceKnows(dev, released) && !DeviceKnows(dev, earlyHandle);
	}

	[[nodiscard]] bool ShowDeferredDestruction(rhi::Device dev, rhi::Queue & queue)
	{
		LOG_INFO(fw::Log(), "-- deferred destruction --");

		rhi::Error error{};

		const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "unique.timeline" }, error);
		rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "unique.pool" }, error);
		if (!timeline.IsValid() || !pool.IsValid())
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

		rhi::CommandList list = pool.Allocate("unique.copy", error);
		const bool recorded	  = list.IsValid() && list.Begin(error) && list.CopyBuffer(*target, 0, *source, 0, kBufferSize, error) && list.End(error);
		if (!recorded)
		{
			fw::ReportError("failed to record the copy", error);
			return false;
		}

		constexpr std::uint64_t kSignalValue = 1;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = kSignalValue } };

		if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "unique.submit" }, error))
		{
			fw::ReportError("failed to submit the copy", error);
			return false;
		}

		const rhi::DestroyDesc afterCopy{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = { .timeline = timeline, .value = kSignalValue },
		};

		source.SetDestroyDesc(afterCopy);
		target.SetDestroyDesc(afterCopy);

		const rhi::BufferHandle sourceHandle = source.Get();
		source.Reset();
		target.Reset();

		LOG_INFO(fw::Log(), "let go of both buffers while the copy was still running, the handle is already unknown: {}", !DeviceKnows(dev, sourceHandle));

		if (!queue.Wait(timeline, kSignalValue, kNoTimeout, error))
		{
			fw::ReportError("failed to wait for the copy", error);
			return false;
		}

		LOG_INFO(fw::Log(), "the copy completed and the native release ran behind it");

		static_cast<void>(pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = kSignalValue }, error));
		timelineOwner.SetDestroyDesc(rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle });

		return !DeviceKnows(dev, sourceHandle);
	}
}

int main(int argc, char ** argv)
{
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = fw::RequestedBackend(argc, argv) } };

	rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("unique_handles").Headless().GraphicsQueue().Build(backends.Registry(), backends.PreferredApis());
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

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/allocation_tracker.hpp"

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace azo::rhi::detail
{

	std::uint64_t AllocationTracker::KeyOf(const ResourceType type, const RawHandle handle) noexcept
	{
		return (static_cast<std::uint64_t>(type) << 56u) ^ (static_cast<std::uint64_t>(handle.generation) << 32u) ^ handle.index;
	}

	bool AllocationTracker::record(const ResourceType type, const RawHandle handle, const MemorySpan & span) noexcept
	{
		return try_insert_or_assign(azo::rhi::detail::at(m_records, static_cast<std::size_t>(type)).live, KeyOf(type, handle), span);
	}

	bool AllocationTracker::retire(const ResourceType type, const RawHandle handle, const DestroyDesc & desc, MemorySpan & out) noexcept
	{
		DeviceRecords & records = azo::rhi::detail::at(m_records, static_cast<std::size_t>(type));

		const auto tracked = records.live.find(KeyOf(type, handle));
		if (tracked == records.live.end())
		{
			return false;
		}

		const MemorySpan span = tracked->second;

		if (desc.policy == DestroyPolicy::eRequireAlreadyIdle)
		{
			records.live.erase(tracked);
			out = span;
			return true;
		}

		if (!try_push_back(
				records.pending,
				Pending{
					.span	   = span,
					.safeAfter = desc.safeAfter,
				}
			))
		{
			return false;
		}

		records.live.erase(tracked);
		return false;
	}

	void AllocationTracker::take_releasable(
		const ResourceType type,
		const TimelineHandle timeline,
		const std::uint64_t completedValue,
		HostVector<MemorySpan> & out
	) noexcept
	{
		DeviceRecords & records = azo::rhi::detail::at(m_records, static_cast<std::size_t>(type));

		HostVector<Pending> kept;
		if (!try_reserve(kept, records.pending.size()))
		{
			return;
		}

		for (const Pending & entry : records.pending)
		{
			const bool untimed	 = !entry.safeAfter.timeline.is_valid();
			const bool thisOne	 = entry.safeAfter.timeline == timeline;
			const bool completed = untimed || (thisOne && completedValue >= entry.safeAfter.value);

			if (completed && try_push_back(out, entry.span))
			{
				continue;
			}

			kept.push_back(entry);
		}

		records.pending = std::move(kept);
	}

	void AllocationTracker::take_all(const ResourceType type, HostVector<MemorySpan> & out) noexcept
	{
		DeviceRecords & records = azo::rhi::detail::at(m_records, static_cast<std::size_t>(type));

		std::size_t taken = 0;
		for (const Pending & entry : records.pending)
		{
			if (!try_push_back(out, entry.span))
			{
				break;
			}

			++taken;
		}

		records.pending.erase(records.pending.begin(), records.pending.begin() + static_cast<std::ptrdiff_t>(taken));
	}

	void AllocationTracker::forget() noexcept
	{
		for (DeviceRecords & records : m_records)
		{
			records.live.clear();
			records.pending.clear();
		}
	}

	std::size_t AllocationTracker::live_count() const noexcept
	{
		std::size_t live = 0;
		for (const DeviceRecords & records : m_records)
		{
			live += records.live.size();
		}

		return live;
	}

}

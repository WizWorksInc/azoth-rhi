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

#include "azoth/rhi/backend/allocation_tracker.hpp"

#include <mutex>
#include <utility>

namespace azo::rhi::detail
{

	std::uint64_t AllocationTracker::KeyOf(const ResourceType type, const RawHandle handle) noexcept
	{
		// Generation is part of the key so a recycled index cannot resolve to the span the previous occupant held.
		return (static_cast<std::uint64_t>(type) << 56u) ^ (static_cast<std::uint64_t>(handle.generation) << 32u) ^ handle.index;
	}

	void AllocationTracker::Record(const ResourceType type, const RawHandle handle, const MemorySpan & span)
	{
		m_records[static_cast<std::size_t>(type)].live.insert_or_assign(KeyOf(type, handle), span);
	}

	bool AllocationTracker::Retire(const ResourceType type, const RawHandle handle, const DestroyDesc & desc, MemorySpan & out)
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		const auto tracked = records.live.find(KeyOf(type, handle));
		if (tracked == records.live.end())
		{
			return false;
		}

		const MemorySpan span = tracked->second;
		records.live.erase(tracked);

		// The caller promised the GPU is already done so the range goes back now. Waiting for a collect that may never come would strand it.
		if (desc.policy == DestroyPolicy::eRequireAlreadyIdle)
		{
			out = span;
			return true;
		}

		records.pending.push_back(Pending{
			.span	   = span,
			.safeAfter = desc.safeAfter,
		});
		return false;
	}

	void AllocationTracker::TakeReleasable(
		const ResourceType type, const TimelineHandle timeline, const std::uint64_t completedValue, HostVector<MemorySpan> & out)
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		HostVector<Pending> kept;
		kept.reserve(records.pending.size());
		for (const Pending & entry : records.pending)
		{
			// A span with no timeline was retired without one being named so any collect releases it. One naming a different timeline than the caller is
			// collecting on is left for the collect that does name it.
			const bool untimed	 = !entry.safeAfter.timeline.IsValid();
			const bool thisOne	 = entry.safeAfter.timeline == timeline;
			const bool completed = untimed || (thisOne && completedValue >= entry.safeAfter.value);
			if (completed)
			{
				out.push_back(entry.span);
			}
			else
			{
				kept.push_back(entry);
			}
		}

		records.pending = std::move(kept);
	}

	void AllocationTracker::TakeAll(const ResourceType type, HostVector<MemorySpan> & out)
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		for (const Pending & entry : records.pending)
		{
			out.push_back(entry.span);
		}

		records.pending.clear();
	}

	void AllocationTracker::Forget()
	{
		for (DeviceRecords & records : m_records)
		{
			records.live.clear();
			records.pending.clear();
		}
	}

	std::size_t AllocationTracker::LiveCount() const
	{
		std::size_t live = 0;
		for (const DeviceRecords & records : m_records)
		{
			live += records.live.size();
		}

		return live;
	}

} // namespace azo::rhi::detail

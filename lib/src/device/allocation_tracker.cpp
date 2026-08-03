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

	/*
	 * Every mutating entry here allocates, and every caller is noexcept: the creates run inside Device::CreateBufferRouted and the collects inside noexcept
	 * lambdas. The host allocator seam is allowed to refuse, and refusing raises through HostAllocatorAdapter, so a raw insert or push here turns a host memory
	 * cap into a terminate. The Try forms carry that back as a bool instead.
	 */
	bool AllocationTracker::Record(const ResourceType type, const RawHandle handle, const MemorySpan & span) noexcept
	{
		return TryInsertOrAssign(m_records[static_cast<std::size_t>(type)].live, KeyOf(type, handle), span);
	}

	bool AllocationTracker::Retire(const ResourceType type, const RawHandle handle, const DestroyDesc & desc, MemorySpan & out) noexcept
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		const auto tracked = records.live.find(KeyOf(type, handle));
		if (tracked == records.live.end())
		{
			return false;
		}

		const MemorySpan span = tracked->second;

		// The caller promised the GPU is already done so the range goes back now. Every other policy leaves it pending, including a destroy that named no retire
		// point, which the next collect of any kind then releases.
		if (desc.policy == DestroyPolicy::eRequireAlreadyIdle)
		{
			records.live.erase(tracked);
			out = span;
			return true;
		}

		// Queued before the live record goes, so a queue that cannot grow leaves the span tracked rather than dropped between the two lists with nothing owning it.
		if (!TryPushBack(records.pending,
				Pending{
					.span	   = span,
					.safeAfter = desc.safeAfter,
				}))
		{
			return false;
		}

		records.live.erase(tracked);
		return false;
	}

	void AllocationTracker::TakeReleasable(
		const ResourceType type, const TimelineHandle timeline, const std::uint64_t completedValue, HostVector<MemorySpan> & out) noexcept
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		// Sized for the whole list up front so no push below can fail partway and strand a span in neither list. A reserve that cannot be met leaves this pass
		// entirely undone, which a later collect repeats.
		HostVector<Pending> kept;
		if (!TryReserve(kept, records.pending.size()))
		{
			return;
		}

		for (const Pending & entry : records.pending)
		{
			// A span with no timeline was retired without one being named so any collect releases it. One naming a different timeline than the caller is
			// collecting on is left for the collect that does name it.
			const bool untimed	 = !entry.safeAfter.timeline.IsValid();
			const bool thisOne	 = entry.safeAfter.timeline == timeline;
			const bool completed = untimed || (thisOne && completedValue >= entry.safeAfter.value);

			// A span the output cannot take stays pending, so it goes back on a later collect instead of being lost here.
			if (completed && TryPushBack(out, entry.span))
			{
				continue;
			}

			kept.push_back(entry);
		}

		records.pending = std::move(kept);
	}

	void AllocationTracker::TakeAll(const ResourceType type, HostVector<MemorySpan> & out) noexcept
	{
		DeviceRecords & records = m_records[static_cast<std::size_t>(type)];

		// Only what the output actually took is dropped, so a push that cannot grow leaves the rest pending for the next collect rather than losing it.
		std::size_t taken = 0;
		for (const Pending & entry : records.pending)
		{
			if (!TryPushBack(out, entry.span))
			{
				break;
			}

			++taken;
		}

		records.pending.erase(records.pending.begin(), records.pending.begin() + static_cast<std::ptrdiff_t>(taken));
	}

	void AllocationTracker::Forget() noexcept
	{
		for (DeviceRecords & records : m_records)
		{
			records.live.clear();
			records.pending.clear();
		}
	}

	std::size_t AllocationTracker::LiveCount() const noexcept
	{
		std::size_t live = 0;
		for (const DeviceRecords & records : m_records)
		{
			live += records.live.size();
		}

		return live;
	}

} // namespace azo::rhi::detail

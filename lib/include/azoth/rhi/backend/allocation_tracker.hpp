// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace azo::rhi::detail
{

	class AllocationTracker final
	{
	public:
		[[nodiscard]] bool Record(ResourceType type, RawHandle handle, const MemorySpan & span) noexcept;

		[[nodiscard]] bool Retire(ResourceType type, RawHandle handle, const DestroyDesc & desc, MemorySpan & out) noexcept;

		void TakeReleasable(ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, HostVector<MemorySpan> & out) noexcept;

		void TakeAll(ResourceType type, HostVector<MemorySpan> & out) noexcept;

		void Forget() noexcept;

		[[nodiscard]] std::size_t LiveCount() const noexcept;

	private:
		struct Pending final
		{
			MemorySpan span{};
			RetirePoint safeAfter{};
		};

		struct DeviceRecords final
		{
			HostMap<std::uint64_t, MemorySpan> live;
			HostVector<Pending> pending;
		};

		[[nodiscard]] static std::uint64_t KeyOf(ResourceType type, RawHandle handle) noexcept;

		std::array<DeviceRecords, kResourceTypeCount> m_records;
	};

}

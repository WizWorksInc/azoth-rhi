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

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint> // NOLINT

namespace azo::rhi::detail
{

	class AllocationTracker final
	{
	public:
		[[nodiscard]] bool record(ResourceType type, RawHandle handle, const MemorySpan & span) noexcept;

		[[nodiscard]] bool retire(ResourceType type, RawHandle handle, const DestroyDesc & desc, MemorySpan & out) noexcept;

		void take_releasable(ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, HostVector<MemorySpan> & out) noexcept;

		void take_all(ResourceType type, HostVector<MemorySpan> & out) noexcept;

		void forget() noexcept;

		[[nodiscard]] std::size_t live_count() const noexcept;

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

} // namespace azo::rhi::detail

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

/**
 * \file
 * \brief Tracks allocator-owned memory spans until resource destruction can finish.
 */

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi::detail
{

	/**
	 * \brief Tracks allocator spans backing backend resources until each span can be returned safely.
	 *
	 * Deferred destruction keeps backend resources alive after Destroy returns, so allocator spans stay recorded until collection reaches their retire point.
	 * \attention The caller must serialize access with the matching resource-kind guard. AllocationTracker does not take its own lock.
	 */
	class AllocationTracker final
	{
	public:
		/**
		 * \brief Records the allocator span backing a live resource handle.
		 *
		 * \param handle Type-erased backend resource handle used with type to build the live-record key.
		 */
		void Record(ResourceType type, RawHandle handle, const MemorySpan & span);

		/**
		 * \brief Removes a live span and either returns it immediately or defers it until the destroy retire point completes.
		 *
		 * \param desc Destruction policy. A default retire point makes the span immediately releasable.
		 * \param out Written only when the span can be released before this function returns.
		 */
		[[nodiscard]] bool Retire(ResourceType type, RawHandle handle, const DestroyDesc & desc, MemorySpan & out);

		/**
		 * \brief Moves pending spans whose retire point is covered by the completed timeline value into out.
		 *
		 * \param timeline Timeline used to test pending retire points for this collection pass.
		 * \param completedValue Completed value observed on timeline.
		 * \param out Receives releasable spans. This function does not free memory.
		 */
		void TakeReleasable(ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, HostVector<MemorySpan> & out);

		/**
		 * \brief Moves every live and pending span for one resource type into out.
		 *
		 * \attention The caller becomes responsible for releasing every returned span.
		 */
		void TakeAll(ResourceType type, HostVector<MemorySpan> & out);

		/**
		 * \brief Drops all records without returning spans to the allocator.
		 *
		 * \attention Call only when the owning backend heaps are being released as a whole.
		 */
		void Forget();

		[[nodiscard]] std::size_t LiveCount() const;

	private:
		/**
		 * \brief Allocator span whose backend resource is waiting for a retire point before release.
		 */
		struct Pending final
		{
			MemorySpan span{};
			RetirePoint safeAfter{};
		};

		/**
		 * \brief Live and pending allocator spans for one resource type.
		 */
		struct DeviceRecords final
		{
			HostMap<std::uint64_t, MemorySpan> live;
			HostVector<Pending> pending;
		};

		/**
		 * \brief Builds the per-resource record key from a type-erased handle.
		 *
		 * \param type Resource partition. It prevents two resource kinds with the same raw handle value from sharing a key.
		 */
		[[nodiscard]] static std::uint64_t KeyOf(ResourceType type, RawHandle handle) noexcept;

		std::array<DeviceRecords, kResourceTypeCount> m_records;
	};

} // namespace azo::rhi::detail

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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <atomic>
#include <cstdint> // NOLINT

namespace azo::rhi::detail
{
	namespace
	{
		std::atomic<HostAllocator *> g_HostAllocator{ nullptr };		   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<DeviceMemoryAllocator *> g_DeviceAllocator{ nullptr }; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<Profiler *> g_Profiler{ nullptr };					   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<std::uint64_t> g_ReentrancyViolations{ 0 };			   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		thread_local int g_TGuardsHeld = 0;								   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
	} // namespace

	std::atomic<HostAllocator *> & host_allocator_slot() noexcept
	{
		return g_HostAllocator;
	}

	std::atomic<DeviceMemoryAllocator *> & device_allocator_slot() noexcept
	{
		return g_DeviceAllocator;
	}

	std::atomic<Profiler *> & profiler_slot() noexcept
	{
		return g_Profiler;
	}

	int & guards_held() noexcept
	{
		return g_TGuardsHeld;
	}

	std::atomic<std::uint64_t> & reentrancy_violation_count() noexcept
	{
		return g_ReentrancyViolations;
	}
} // namespace azo::rhi::detail

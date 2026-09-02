// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <atomic>
#include <cstdint>

namespace azo::rhi::detail
{
	namespace
	{
		std::atomic<HostAllocator *> g_hostAllocator{ nullptr };		   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<DeviceMemoryAllocator *> g_deviceAllocator{ nullptr }; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<Profiler *> g_profiler{ nullptr };					   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<std::uint64_t> g_reentrancyViolations{ 0 };			   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		thread_local int t_guardsHeld = 0;								   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
	}

	std::atomic<HostAllocator *> & HostAllocatorSlot() noexcept
	{
		return g_hostAllocator;
	}

	std::atomic<DeviceMemoryAllocator *> & DeviceAllocatorSlot() noexcept
	{
		return g_deviceAllocator;
	}

	std::atomic<Profiler *> & ProfilerSlot() noexcept
	{
		return g_profiler;
	}

	int & GuardsHeld() noexcept
	{
		return t_guardsHeld;
	}

	std::atomic<std::uint64_t> & ReentrancyViolationCount() noexcept
	{
		return g_reentrancyViolations;
	}
}

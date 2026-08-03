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

/*
 * The process-wide seams a host installs into, gathered here because they share one requirement and not one subject.
 *
 * Each was an inline variable in its own public header. That is one object per binary as soon as the library is built shared: the host stores into the copy its
 * own translation units got, and the seam reports that nothing was installed.
 *
 * Defining them in the library and reaching them through an exported call leaves one object for every side.
 */

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <atomic>
#include <cstdint>

namespace azo::rhi::detail
{
	namespace
	{
		// Namespace scope, not function-local, so reaching one costs no guard variable on the first call. Every one is constant initialized.
		std::atomic<HostAllocator *> g_hostAllocator{ nullptr };		   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<DeviceMemoryAllocator *> g_deviceAllocator{ nullptr }; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<Profiler *> g_profiler{ nullptr };					   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		std::atomic<std::uint64_t> g_reentrancyViolations{ 0 };			   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
		thread_local int t_guardsHeld = 0;								   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
	} // namespace

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
} // namespace azo::rhi::detail

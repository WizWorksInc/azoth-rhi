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

#include "azoth/rhi/host/tracy_profiler.hpp"

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/device/threading.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <tracy/TracyC.h>

#include <tracy/Tracy.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>

namespace azo::rhi
{
	namespace
	{
		thread_local detail::HostVector<TracyCZoneCtx> g_TlZoneStack;

		std::size_t length(CString text) noexcept
		{
			return text != nullptr ? std::strlen(text) : 0;
		}

#ifdef TRACY_FIBERS

		SpinLock g_FiberNameLock;
		detail::HostMap<std::uint64_t, detail::HostString> g_FiberNames;

		CString intern_fiber_name(const FiberId fiber, const CString name)
		{
			const std::scoped_lock lock(g_FiberNameLock);

			const auto existing = g_FiberNames.find(fiber.value);
			if (existing != g_FiberNames.end())
			{
				return existing->second.c_str();
			}

			detail::HostString stored;
			if (name != nullptr)
			{
				stored = name;
			}
			else
			{
				stored = "azoth.rhi.fiber.";

				std::array<char, 24> digits{};
				const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), fiber.value);
				stored.append(digits.data(), static_cast<std::size_t>(written.ptr - digits.data()));
			}

			return g_FiberNames.emplace(fiber.value, std::move(stored)).first->second.c_str();
		}

#endif

	}

	void TracyProfiler::BeginZone(const ZoneLocation & location)
	{
		const std::uint64_t srcloc =
			___tracy_alloc_srcloc_name(location.line, location.file, length(location.file), nullptr, 0, location.name, length(location.name), location.color);

		g_TlZoneStack.push_back(___tracy_emit_zone_begin_alloc(srcloc, 1));
	}

	void TracyProfiler::EndZone()
	{
		if (g_TlZoneStack.empty())
		{
			return;
		}

		___tracy_emit_zone_end(g_TlZoneStack.back());
		g_TlZoneStack.pop_back();
	}

	void TracyProfiler::Plot(CString name, std::int64_t value)
	{
		if (name == nullptr)
		{
			return;
		}

		TracyPlot(name, value);
	}

	void TracyProfiler::GpuAllocate(const void * address, std::uint64_t size, CString pool)
	{
		if (address == nullptr || pool == nullptr)
		{
			return;
		}

		TracyAllocN(address, static_cast<std::size_t>(size), pool);
	}

	void TracyProfiler::GpuFree(const void * address, CString pool)
	{
		if (address == nullptr || pool == nullptr)
		{
			return;
		}

		TracyFreeN(address, pool);
	}

	void TracyProfiler::EnterFiber([[maybe_unused]] const FiberId fiber, [[maybe_unused]] const CString name)
	{
#ifdef TRACY_FIBERS
		TracyFiberEnter(intern_fiber_name(fiber, name));
#endif
	}

	void TracyProfiler::LeaveFiber([[maybe_unused]] const FiberId fiber)
	{
#ifdef TRACY_FIBERS
		TracyFiberLeave;
#endif
	}

}

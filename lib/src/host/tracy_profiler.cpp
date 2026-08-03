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

#include <tracy/TracyC.h>

#include <tracy/Tracy.hpp>

#include <array>
#include <charconv>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace azo::rhi
{
	namespace
	{
		// Tracy's C++ zone API is RAII, but the Profiler seam is begin and end so this goes through the C API instead and keeps the open contexts on a per-thread
		// stack. Zones nest strictly within a thread so a stack is all the bookkeeping needed.
		thread_local detail::HostVector<TracyCZoneCtx> tlZoneStack;

		std::size_t Length(CString text) noexcept
		{
			return text != nullptr ? std::strlen(text) : 0;
		}

#ifdef TRACY_FIBERS

		/*
		 * Tracy names a fiber with a string and keeps the pointer it was handed so a name has to outlive every zone that could still be attributed to it. They are
		 * interned here for the life of the process, one entry a fiber.
		 *
		 * A scheduler draws fiber identities from a pool it reuses so this settles at the size of that pool without growing with switches.
		 */
		SpinLock g_fiberNameLock;
		detail::HostMap<std::uint64_t, detail::HostString> g_fiberNames;

		/*
		 * The name Tracy sees for a fiber, decided the first time that identity turns up and never changed after. Tracy is still holding the pointer and reassigning
		 * the string would move the bytes out from under it.
		 *
		 * The host names its fibers from its own scheduler and the RHI passes null from inside a wait, so whichever arrives first decides. An unnamed fiber gets a
		 * synthetic name off its identity, which still keeps two apart.
		 */
		CString InternFiberName(const FiberId fiber, const CString name)
		{
			const std::scoped_lock lock(g_fiberNameLock);

			const auto existing = g_fiberNames.find(fiber.value);
			if (existing != g_fiberNames.end())
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

				// Twenty digits covers every std::uint64_t so this cannot come up short.
				std::array<char, 24> digits{};
				const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), fiber.value);
				stored.append(digits.data(), static_cast<std::size_t>(written.ptr - digits.data()));
			}

			return g_fiberNames.emplace(fiber.value, std::move(stored)).first->second.c_str();
		}

#endif // TRACY_FIBERS

	} // namespace

	void TracyProfiler::BeginZone(const ZoneLocation & location)
	{
		// Tracy interns the source location itself so allocating one per entry is the intended use of this entry point and not something to cache around.
		const std::uint64_t srcloc =
			___tracy_alloc_srcloc_name(location.line, location.file, Length(location.file), nullptr, 0, location.name, Length(location.name), location.color);

		tlZoneStack.push_back(___tracy_emit_zone_begin_alloc(srcloc, 1));
	}

	void TracyProfiler::EndZone()
	{
		// An unbalanced end would mean the RHI closed a zone it never opened. Drop it without popping an empty stack, since corrupting Tracy's nesting is worse than
		// a missing zone.
		if (tlZoneStack.empty())
		{
			return;
		}

		___tracy_emit_zone_end(tlZoneStack.back());
		tlZoneStack.pop_back();
	}

	void TracyProfiler::Plot(CString name, std::int64_t value)
	{
		if (name == nullptr)
		{
			return;
		}

		// Names come from string literals at the call sites so the pointer outlives the send.
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

	/*
	 * Tracy's own fiber support, which is what makes a zone follow the task instead of the worker. It compiles to nothing without TRACY_FIBERS and without it
	 * there is no name to intern either so the whole body goes with it.
	 */
	void TracyProfiler::EnterFiber([[maybe_unused]] const FiberId fiber, [[maybe_unused]] const CString name)
	{
#ifdef TRACY_FIBERS
		TracyFiberEnter(InternFiberName(fiber, name));
#endif
	}

	// Tracy leaves whatever fiber this worker is running so the identity is not needed to say which.
	void TracyProfiler::LeaveFiber([[maybe_unused]] const FiberId fiber)
	{
#ifdef TRACY_FIBERS
		TracyFiberLeave;
#endif
	}

} // namespace azo::rhi

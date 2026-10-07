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

#include "tracy_lifetime.hpp"

#include "azoth/rhi/host/profiler.hpp"

#ifdef TRACY_ENABLE
	#include "azoth/rhi/host/tracy_profiler.hpp"

	#include <tracy/Tracy.hpp>
#endif

#include <iostream>
#include <thread>

namespace
{
	bool RunProfiledWork()
	{
		const azo::rhi::support::TracyLifetime tracyLifetime;
#ifdef TRACY_ENABLE
		if (!TracyIsStarted)
		{
			return false;
		}
		{
			const azo::rhi::support::TracyLifetime borrowedLifetime;
		}
		if (!TracyIsStarted)
		{
			return false;
		}
		azo::rhi::TracyProfiler sink;
		const azo::rhi::ScopedProfiler installedProfiler{ &sink };
		const azo::rhi::ZoneLocation location{ .name = "lifetime" };
		const azo::rhi::detail::ScopedZone zone{ location };
		ZoneScopedN("lifetime");
		std::jthread worker(
			[]
			{
				ZoneScopedN("worker");
				FrameMark;
			}
		);
#endif
		return true;
	}
} // namespace

int main()
{
#ifdef TRACY_ENABLE
	const bool wasStarted = TracyIsStarted;
#endif
#ifdef AZOTH_RHI_TEST_MANUAL_TRACY
	if (TracyIsStarted)
	{
		std::cerr << "Tracy started before main\n";
		return 1;
	}
#endif
	if (!RunProfiledWork())
	{
		std::cerr << "Tracy stopped while executable resources were alive\n";
		return 1;
	}
	if (azo::rhi::get_profiler() != nullptr)
	{
		std::cerr << "Profiler sink remains installed after shutdown\n";
		return 1;
	}
#ifdef TRACY_ENABLE
	if (TracyIsStarted != wasStarted)
	{
		std::cerr << "Tracy lifetime was not restored\n";
		return 1;
	}
#endif
	return 0;
}

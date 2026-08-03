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
 * \brief Tracy profiler sink for RHI instrumentation.
 */

#include "azoth/rhi/host/profiler.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

#ifndef TRACY_ENABLE
	#error "The Tracy sink was not built. Add a Tracy client built with TRACY_ENABLE to your build and it is picked up from that target."
#endif

namespace azo::rhi
{

	/**
	 * \brief Routes RHI profiling events into the host application's Tracy client.
	 *
	 * Built against the host's Tracy client because only one should exist in a process. CPU zones, plots and GPU allocation events are forwarded. GPU timing is
	 * not, because Tracy GPU contexts need frame-loop-owned calibration points.
	 *
	 * \attention Fiber events require a Tracy client built with TRACY_FIBERS. Without it they do nothing and zones spanning cooperative waits may be attributed
	 * to the worker thread that resumes them.
	 */
	class TracyProfiler final : public Profiler
	{
	public:
		void BeginZone(const ZoneLocation & location) override;
		void EndZone() override;

		void Plot(CString name, std::int64_t value) override;

		void GpuAllocate(const void * address, std::uint64_t size, CString pool) override;
		void GpuFree(const void * address, CString pool) override;

		void EnterFiber(FiberId fiber, CString name) override;
		void LeaveFiber(FiberId fiber) override;
	};

} // namespace azo::rhi

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <cstdint>

#ifndef TRACY_ENABLE
	#error "The Tracy sink was not built. Add a Tracy client built with TRACY_ENABLE to your build and it is picked up from that target."
#endif

namespace azo::rhi
{

	class AZO_RHI_API TracyProfiler final : public Profiler
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

}

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
		void begin_zone(const ZoneLocation & location) override;
		void end_zone() override;

		void plot(CString name, std::int64_t value) override;

		void gpu_allocate(const void * address, std::uint64_t size, CString pool) override;
		void gpu_free(const void * address, CString pool) override;

		void enter_fiber(FiberId fiber, CString name) override;
		void leave_fiber(FiberId fiber) override;
	};

}

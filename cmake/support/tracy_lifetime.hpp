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

#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>
#endif

namespace azo::rhi::support
{
	// Construct on the main thread before instrumented resources and worker threads.
	class TracyLifetime final
	{
	public:
		TracyLifetime()
		{
#if defined(TRACY_ENABLE) && defined(TRACY_DELAYED_INIT) && defined(TRACY_MANUAL_LIFETIME)
			m_ownsProfiler = !tracy::IsProfilerStarted();
			if (m_ownsProfiler)
			{
				tracy::StartupProfiler();
			}
#endif
		}

		~TracyLifetime()
		{
#if defined(TRACY_ENABLE) && defined(TRACY_DELAYED_INIT) && defined(TRACY_MANUAL_LIFETIME)
			if (m_ownsProfiler)
			{
				tracy::ShutdownProfiler();
			}
#endif
		}

		TracyLifetime(const TracyLifetime &)			 = delete;
		TracyLifetime & operator=(const TracyLifetime &) = delete;
		TracyLifetime(TracyLifetime &&)					 = delete;
		TracyLifetime & operator=(TracyLifetime &&)		 = delete;

	private:
#if defined(TRACY_ENABLE) && defined(TRACY_DELAYED_INIT) && defined(TRACY_MANUAL_LIFETIME)
		bool m_ownsProfiler = false;
#endif
	};
} // namespace azo::rhi::support

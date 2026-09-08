// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <chrono>
#include <ratio>

namespace fw::util
{
	class Timer final
	{
	public:
		using Seconds	   = std::ratio<1>;
		using Milliseconds = std::milli;
		using Microseconds = std::micro;
		using Nanoseconds  = std::nano;

		using Clock				= std::chrono::steady_clock;
		using DefaultResolution = Seconds;

		Timer() : m_startTime(Clock::now()), m_lapTime(m_startTime), m_previousTick(m_startTime) {}

		void Start() noexcept
		{
			if (!m_running)
			{
				m_running	= true;
				m_startTime = Clock::now();
				m_lapTime	= m_startTime;
			}
		}

		void Lap() noexcept
		{
			m_lapping = true;
			m_lapTime = Clock::now();
		}

		template <typename ResolutionT = DefaultResolution>
		double Stop() noexcept
		{
			if (!m_running)
			{
				return 0.0;
			}

			m_running = false;
			m_lapping = false;

			const std::chrono::duration<double, ResolutionT> ran = Clock::now() - m_startTime;

			m_startTime = Clock::now();
			m_lapTime	= m_startTime;

			return ran.count();
		}

		template <typename ResolutionT = DefaultResolution>
		[[nodiscard]] double Elapsed() const noexcept
		{
			if (!m_running)
			{
				return 0.0;
			}

			const Clock::time_point from = m_lapping ? m_lapTime : m_startTime;

			return std::chrono::duration<double, ResolutionT>(Clock::now() - from).count();
		}

		template <typename ResolutionT = DefaultResolution>
		[[nodiscard]] double Tick() noexcept
		{
			const Clock::time_point now						   = Clock::now();
			const std::chrono::duration<double, ResolutionT> d = now - m_previousTick;
			m_previousTick									   = now;

			return d.count();
		}

		[[nodiscard]] bool IsRunning() const noexcept
		{
			return m_running;
		}

	private:
		bool m_running = false;
		bool m_lapping = false;

		Clock::time_point m_startTime;
		Clock::time_point m_lapTime;
		Clock::time_point m_previousTick;
	};
}

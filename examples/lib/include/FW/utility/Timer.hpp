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

#include <chrono>
#include <ratio>

namespace fw::util
{
	/**
	 * \brief Wall-clock durations between points in a frame, in whichever unit the caller asks for.
	 *
	 * Three questions, three calls. Tick answers how long since the last time it was asked, which is the frame delta an update takes. Lap answers how long
	 * a section took and starts the next one. Elapsed answers how long since Start without disturbing either.
	 *
	 * A steady clock and not the system one, so an NTP correction mid-frame cannot hand back a negative delta.
	 */
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

		/**
		 * \brief Starts the timer. Elapsed now measures from here.
		 */
		void Start() noexcept
		{
			if (!m_running)
			{
				m_running	= true;
				m_startTime = Clock::now();
				m_lapTime	= m_startTime;
			}
		}

		/**
		 * \brief Ends the current lap and begins another. Elapsed now measures from here and not from Start.
		 */
		void Lap() noexcept
		{
			m_lapping = true;
			m_lapTime = Clock::now();
		}

		/**
		 * \brief Stops the timer.
		 *
		 * \return How long it ran, or zero when it was not running.
		 */
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

		/**
		 * \brief How long since Start, or since the last Lap once one has been taken.
		 *
		 * \return Zero while the timer is stopped.
		 */
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

		/**
		 * \brief How long since this was last called, which is the delta a frame is stepped by.
		 *
		 * Answers regardless of whether the timer is running, since a frame loop wants a delta from the first frame onward.
		 */
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
} // namespace fw::util

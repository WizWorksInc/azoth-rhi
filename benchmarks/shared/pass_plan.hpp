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

#include "shared/options.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace bench
{

	// Where the ramp starts and the smallest pass it settles on unless the caller asked for a smaller ceiling than this. Two thousand ticks of a forty nanosecond
	// clock is a measurement even at the cheapest command shape so a machine that cannot afford more than this still gets measured.
	inline constexpr std::size_t kSmallestPass = 16'384;

	inline constexpr double kNanosecondsASecond = 1e9;

	/**
	 * \brief How big a pass is and what a repetition of them is worth, both measured on the machine.
	 */
	struct PassPlan final
	{
		// Commands one pass records, at most Options::commandCeiling. Zero says the machine could not be measured, which is a pass that failed.
		std::size_t commands = 0;

		// What Google Benchmark is told a repetition is worth, measured in the time it is timing. That is the recording loop alone, with the pass around it left
		// out.
		double minTimeSeconds = 0.0;
	};

	/**
	 * \brief Records one pass of the given size and answers the whole wall time and the timed part of it, both in nanoseconds. False where the pass failed.
	 */
	using ProbePass = std::function<bool(std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds)>;

	/**
	 * \brief Sizes a pass and a repetition of them against what the machine turns out to cost.
	 *
	 * Google Benchmark sizes a run from the recording loop alone, with the submit and the GPU work outside it, so a pass is ramped from kSmallestPass against
	 * the pass budget and the count comes out of the run budget.
	 *
	 * \param benchmarks how many the run registers, the budget being shared between them.
	 * \param repetitions what --benchmark_repetitions settled on, each paying that time again.
	 */
	[[nodiscard]] PassPlan PlanPasses(
		const Options & options, std::size_t benchmarks, std::size_t repetitions, double preferredMinTimeSeconds, const ProbePass & probe);

	/**
	 * \brief Records passes until the wall clock says the machine has ramped, a first sample otherwise measuring the ramp.
	 *
	 * By wall clock and not by pass count because what needs warming is the clock the core runs at: a laptop starting at its low-power frequency comes in a
	 * third slower on the first passes. The pass grows by the planner's own doubling, so the warm-up runs the size about to be measured.
	 *
	 * \return false where a pass failed.
	 */
	[[nodiscard]] bool WarmUp(double milliseconds, const Options & options, const ProbePass & pass);

} // namespace bench

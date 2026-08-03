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

#include "shared/pass_plan.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bench
{

	namespace
	{

		// Below this a minimum time reads as none at all to Google Benchmark, which then falls back to its own flag.
		constexpr double kSmallestMinTime = 1e-6;

		/*
		 * Down to the nearest one, two or five, which is what keeps a name the same run to run.
		 *
		 * Google Benchmark writes a minimum time a benchmark named for itself into that benchmark's name and a name is what its own tooling matches a run against
		 * an earlier one by. A time measured off the machine would otherwise move a little every run and take the name with it.
		 */
		[[nodiscard]] double SnapDown(const double seconds)
		{
			const double decade = std::pow(10.0, std::floor(std::log10(seconds)));
			const double digits = seconds / decade;

			if (digits >= 5.0)
			{
				return 5.0 * decade;
			}
			if (digits >= 2.0)
			{
				return 2.0 * decade;
			}

			return decade;
		}

	} // namespace

	PassPlan PlanPasses(
		const Options & options, const std::size_t benchmarks, const std::size_t repetitions, const double preferredMinTimeSeconds, const ProbePass & probe)
	{
		PassPlan plan{};

		/*
		 * What one slot of the run budget is worth, which is one benchmark's turn at one repetition.
		 *
		 * The pass budget is held to it as well, since a repetition records at least one whole pass: a pass allowed to cost more than a slot is a run that goes
		 * over the budget however few of them it then takes.
		 */
		const double slots		 = static_cast<double>(std::max<std::size_t>(benchmarks, 1) * std::max<std::size_t>(repetitions, 1));
		const double budgetASlot = options.runBudgetSeconds / slots;

		const double passBudgetSeconds = std::min(options.passBudgetMilliseconds / 1000.0, budgetASlot);
		const std::size_t ceiling	   = std::max<std::size_t>(options.commandCeiling, 1);

		double acceptedSeconds = 0.0;

		// The share of a pass that is the recording loop, which is the part Google Benchmark's own timing sees and the part a minimum time is spent in.
		double timedShare = 1.0;

		/*
		 * Doubling, since one probe is not enough to extrapolate from. What a pass costs stops being linear in what it records once a driver starts spilling and a
		 * benchmark that extrapolated through that point would size the pass from a slope it never measured.
		 */
		for (std::size_t commands = std::min(kSmallestPass, ceiling);;)
		{
			std::uint64_t wallNanoseconds  = 0;
			std::uint64_t timedNanoseconds = 0;
			if (!probe(commands, wallNanoseconds, timedNanoseconds))
			{
				return PassPlan{};
			}

			const double passSeconds = static_cast<double>(wallNanoseconds) / kNanosecondsASecond;

			// The first size is taken whatever it cost, there being nothing smaller to fall back to. A machine that cannot afford even this is measured at it
			// anyway.
			if (passSeconds > passBudgetSeconds && plan.commands != 0)
			{
				break;
			}

			plan.commands	= commands;
			acceptedSeconds = passSeconds;
			if (wallNanoseconds > 0)
			{
				timedShare = static_cast<double>(timedNanoseconds) / static_cast<double>(wallNanoseconds);
			}

			if (commands >= ceiling || passSeconds > passBudgetSeconds)
			{
				break;
			}

			commands = std::min(commands * 2, ceiling);
		}

		/*
		 * What the run budget leaves this benchmark, spent in the currency Google Benchmark measures a repetition in.
		 *
		 * The floor is one pass, since a repetition records at least one and no budget can buy less than that. A run whose shapes each cost more than their share
		 * therefore goes over the budget instead of reporting nothing. How far over is the pass budget times the benchmarks that could not fit.
		 */
		const double passesAllowed	= acceptedSeconds > 0.0 ? std::max(1.0, budgetASlot / acceptedSeconds) : 1.0;
		const double affordableTime = passesAllowed * acceptedSeconds * timedShare;

		plan.minTimeSeconds = SnapDown(std::max(std::min(preferredMinTimeSeconds, affordableTime), kSmallestMinTime));
		return plan;
	}

	bool WarmUp(const double milliseconds, const Options & options, const ProbePass & pass)
	{
		const double passBudgetSeconds = options.passBudgetMilliseconds / 1000.0;
		const std::size_t ceiling	   = std::max<std::size_t>(options.commandCeiling, 1);

		std::size_t commands = std::min(kSmallestPass, ceiling);

		const std::chrono::steady_clock::time_point until =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>(milliseconds));

		while (std::chrono::steady_clock::now() < until)
		{
			std::uint64_t wallNanoseconds  = 0;
			std::uint64_t timedNanoseconds = 0;
			if (!pass(commands, wallNanoseconds, timedNanoseconds))
			{
				return false;
			}

			if (static_cast<double>(wallNanoseconds) / kNanosecondsASecond < passBudgetSeconds)
			{
				commands = std::min(commands * 2, ceiling);
			}
		}

		return true;
	}

} // namespace bench

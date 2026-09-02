// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		constexpr double kSmallestMinTime = 1e-6;

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

	}

	PassPlan PlanPasses(
		const Options & options, const std::size_t benchmarks, const std::size_t repetitions, const double preferredMinTimeSeconds, const ProbePass & probe)
	{
		PassPlan plan{};

		const double slots		 = static_cast<double>(std::max<std::size_t>(benchmarks, 1) * std::max<std::size_t>(repetitions, 1));
		const double budgetASlot = options.runBudgetSeconds / slots;

		const double passBudgetSeconds = std::min(options.passBudgetMilliseconds / 1000.0, budgetASlot);
		const std::size_t ceiling	   = std::max<std::size_t>(options.commandCeiling, 1);

		double acceptedSeconds = 0.0;

		double timedShare = 1.0;

		for (std::size_t commands = std::min(kSmallestPass, ceiling);;)
		{
			std::uint64_t wallNanoseconds  = 0;
			std::uint64_t timedNanoseconds = 0;
			if (!probe(commands, wallNanoseconds, timedNanoseconds))
			{
				return PassPlan{};
			}

			const double passSeconds = static_cast<double>(wallNanoseconds) / kNanosecondsASecond;

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

}

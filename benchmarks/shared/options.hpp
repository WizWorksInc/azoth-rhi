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

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bench
{

	/**
	 * \brief What a benchmark here takes on top of what Google Benchmark takes.
	 *
	 * The two figures that differ between the benchmarks are left to the program, which fills them in before the command line is read over them.
	 */
	struct Options final
	{
		const char * backend = nullptr;

		/*
		 * The most commands one pass may record.
		 *
		 * A ceiling the planner works down from. What a pass can afford is measured on the machine it is running on and the answer is never above this: see
		 * PlanPasses.
		 */
		std::size_t commandCeiling = 0;

		/*
		 * The wall time one pass may cost, which is the budget the ceiling above is cut down to fit.
		 *
		 * The whole pass, including the part of it that is not timed. A pass submits what it recorded and waits for it. On a real backend that is the GPU running
		 * every command the pass recorded. A pass of two million draws is therefore minutes of work that no timer here would report.
		 */
		double passBudgetMilliseconds = 120.0;

		// The wall time the whole run is planned against, which decides how big a pass is and how many of them a repetition gets. A run that would come in under
		// it is left alone and one whose every shape costs more than its share comes in over it: see PlanPasses.
		double runBudgetSeconds = 45.0;

		/*
		 * How long to record before the first figure is kept.
		 *
		 * Ours instead of --benchmark_min_warmup_time because Google Benchmark warms each benchmark separately and what needs warming is the clock the core is
		 * running at: a laptop that starts at its low-power frequency needs the time once and the shapes that follow keep it spent. Warming eight of them
		 * separately is minutes of wall time for a ramp that had already finished.
		 */
		double warmupMilliseconds = 3000.0;

		// The spread the run is judged on, as a whole percent of the median.
		double maxSpreadPercent = 0.0;

		azo::rhi::ValidationMode validation = azo::rhi::ValidationMode::eOff;
	};

	[[nodiscard]] std::string_view ValidationName(azo::rhi::ValidationMode mode);

	void ReportError(std::string_view what, const azo::rhi::Error & error);

	// What every benchmark here takes, printed under what Google Benchmark prints for itself.
	void PrintOwnOptions();

	// The two together. This is what benchmark::Initialize is handed so --help answers for the whole command line instead of half of it.
	void PrintHelp();

	// What Google Benchmark left behind, which is this program's own options and anything misspelled.
	[[nodiscard]] bool ParseOptions(int argc, char ** argv, Options & options);

	/**
	 * \brief The command line Google Benchmark is initialised with: the defaults first, then what the caller wrote.
	 *
	 * Google Benchmark takes the last spelling of a flag it is given so a default put in front of the caller's line is a default the caller still overrides.
	 * --benchmark_dry_run drops all of them for a run that only proves the benchmark still works.
	 *
	 * The returned vector points into defaults and into argv so both have to outlive it.
	 */
	[[nodiscard]] std::vector<char *> WithFlagDefaults(int argc, char ** argv, std::span<std::string> defaults);

	// The value the command line settled on for a Google Benchmark flag that takes a whole number or fallback where it names none. Ask before Initialize, which
	// takes the flags it recognises back out of the line as it parses them.
	[[nodiscard]] std::size_t FlagValue(std::span<char * const> args, std::string_view flag, std::size_t fallback);

	// Whether the line names a flag at all, whatever it set it to. What a benchmark decides for itself, a caller who spelled the flag out has said they want to
	// decide instead.
	[[nodiscard]] bool NamesFlag(std::span<char * const> args, std::string_view flag);

	// The same question for one of this program's own options, which take their value as the next argument where a Google Benchmark flag takes it after an equals
	// sign. A benchmark that sweeps a setting by default reads this to find out whether the caller has picked one instead.
	[[nodiscard]] bool NamesOption(std::span<char * const> args, std::string_view option);

} // namespace bench

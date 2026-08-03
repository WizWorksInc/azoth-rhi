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

#include "shared/options.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rhi = azo::rhi;

namespace bench
{

	namespace
	{

		[[nodiscard]] bool ParseSize(const std::string_view text, std::size_t & out)
		{
			const char * first = text.data();
			const char * last  = first + text.size();

			std::size_t parsed					= 0;
			const std::from_chars_result result = std::from_chars(first, last, parsed);
			if (result.ec != std::errc{} || result.ptr != last)
			{
				return false;
			}

			out = parsed;
			return true;
		}

		// Whole numbers only, since that is how every figure here is written on the command line and it keeps this out of the locale's way.
		[[nodiscard]] bool ParseWholeNumber(const std::string_view text, double & out)
		{
			std::size_t parsed = 0;
			if (!ParseSize(text, parsed))
			{
				return false;
			}

			out = static_cast<double>(parsed);
			return true;
		}

		[[nodiscard]] bool ParseValidation(const std::string_view text, rhi::ValidationMode & out)
		{
			if (text == "off")
			{
				out = rhi::ValidationMode::eOff;
				return true;
			}
			if (text == "light")
			{
				out = rhi::ValidationMode::eReleaseLight;
				return true;
			}
			if (text == "developer")
			{
				out = rhi::ValidationMode::eDeveloper;
				return true;
			}

			return false;
		}

	} // namespace

	std::string_view ValidationName(const rhi::ValidationMode mode)
	{
		switch (mode)
		{
		case rhi::ValidationMode::eOff:			 return "off";
		case rhi::ValidationMode::eReleaseLight: return "light";
		case rhi::ValidationMode::eDeveloper:	 return "developer";
		case rhi::ValidationMode::eCapture:		 return "capture";
		}

		return "unknown";
	}

	void ReportError(const std::string_view what, const rhi::Error & error)
	{
		std::println("{}: {} (error code {})", what, error.message != nullptr ? error.message : "no diagnostic", static_cast<unsigned>(error.code));
	}

	void PrintOwnOptions()
	{
		std::println("azoth-rhi options:");
		std::println("          [backend]");
		std::println("          [--commands N]           the most commands one pass may record");
		std::println("          [--pass-budget MS]       the wall time one pass may cost, which is what cuts the count above down");
		std::println("          [--run-budget SECONDS]   the wall time the whole run may cost");
		std::println("          [--warmup MS]            how long to record before the first figure is kept");
		std::println("          [--max-spread WHOLE_PERCENT]");
		std::println("          [--validation off|light|developer]");
	}

	void PrintHelp()
	{
		benchmark::PrintDefaultHelp();
		PrintOwnOptions();
	}

	bool ParseOptions(const int argc, char ** argv, Options & options)
	{
		const std::span<char * const> args(argv, static_cast<std::size_t>(argc));

		for (std::size_t index = 1; index < args.size(); ++index)
		{
			const std::string_view argument = args[index];

			// The one positional so the benchmarks run the same way the examples do.
			if (!argument.starts_with("--"))
			{
				options.backend = args[index];
				continue;
			}

			if (index + 1 >= args.size())
			{
				std::println("{} wants a value", argument);
				return false;
			}

			const std::string_view value = args[index + 1];
			++index;

			bool accepted = false;
			if (argument == "--commands")
			{
				accepted = ParseSize(value, options.commandCeiling);
			}
			else if (argument == "--pass-budget")
			{
				accepted = ParseWholeNumber(value, options.passBudgetMilliseconds);
			}
			else if (argument == "--warmup")
			{
				accepted = ParseWholeNumber(value, options.warmupMilliseconds);
			}
			else if (argument == "--run-budget")
			{
				accepted = ParseWholeNumber(value, options.runBudgetSeconds);
			}
			else if (argument == "--max-spread")
			{
				accepted = ParseWholeNumber(value, options.maxSpreadPercent);
			}
			else if (argument == "--validation")
			{
				accepted = ParseValidation(value, options.validation);
			}
			else
			{
				std::println("unknown option {}", argument);
				return false;
			}

			if (!accepted)
			{
				std::println("{} does not take {}", argument, value);
				return false;
			}
		}

		return true;
	}

	std::vector<char *> WithFlagDefaults(const int argc, char ** argv, const std::span<std::string> defaults)
	{
		std::vector<char *> args;
		args.reserve(static_cast<std::size_t>(argc) + defaults.size());

		args.push_back(argv[0]);
		for (std::string & flag : defaults)
		{
			args.push_back(flag.data());
		}
		args.insert(args.end(), argv + 1, argv + argc);

		return args;
	}

	namespace
	{

		[[nodiscard]] bool IsFlag(const std::string_view argument, const std::string_view flag)
		{
			return argument.starts_with(flag) && argument.size() > flag.size() && argument[flag.size()] == '=';
		}

	} // namespace

	std::size_t FlagValue(const std::span<char * const> args, const std::string_view flag, const std::size_t fallback)
	{
		std::size_t found = fallback;

		// The last one wins, which is the rule Google Benchmark itself parses by.
		for (const char * const argument : args)
		{
			const std::string_view text = argument;
			if (!IsFlag(text, flag))
			{
				continue;
			}

			std::size_t parsed = 0;
			if (ParseSize(text.substr(flag.size() + 1), parsed))
			{
				found = parsed;
			}
		}

		return found;
	}

	bool NamesFlag(const std::span<char * const> args, const std::string_view flag)
	{
		return std::ranges::any_of(args,
			[flag](const char * const argument)
			{
				return IsFlag(argument, flag);
			});
	}

	bool NamesOption(const std::span<char * const> args, const std::string_view option)
	{
		return std::ranges::any_of(args,
			[option](const char * const argument)
			{
				return std::string_view(argument) == option;
			});
	}

} // namespace bench

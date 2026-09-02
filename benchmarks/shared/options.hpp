// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	struct Options final
	{
		const char * backend = nullptr;

		std::size_t commandCeiling = 0;

		double passBudgetMilliseconds = 120.0;

		double runBudgetSeconds = 45.0;

		double warmupMilliseconds = 3000.0;

		double maxSpreadPercent = 0.0;

		azo::rhi::ValidationMode validation = azo::rhi::ValidationMode::eOff;
	};

	[[nodiscard]] std::string_view ValidationName(azo::rhi::ValidationMode mode);

	void ReportError(std::string_view what, const azo::rhi::Error & error);

	void PrintOwnOptions();

	void PrintHelp();

	[[nodiscard]] bool ParseOptions(int argc, char ** argv, Options & options);

	[[nodiscard]] std::vector<char *> WithFlagDefaults(int argc, char ** argv, std::span<std::string> defaults);

	[[nodiscard]] std::size_t FlagValue(std::span<char * const> args, std::string_view flag, std::size_t fallback);

	[[nodiscard]] bool NamesFlag(std::span<char * const> args, std::string_view flag);

	[[nodiscard]] bool NamesOption(std::span<char * const> args, std::string_view option);

}

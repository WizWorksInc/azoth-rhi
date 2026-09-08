// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "shared/options.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace bench
{

	inline constexpr std::size_t kSmallestPass = 16'384;

	inline constexpr double kNanosecondsASecond = 1e9;

	struct PassPlan final
	{
		std::size_t commands = 0;

		double minTimeSeconds = 0.0;
	};

	using ProbePass = std::function<bool(std::size_t commands, std::uint64_t & wallNanoseconds, std::uint64_t & timedNanoseconds)>;

	[[nodiscard]] PassPlan PlanPasses(
		const Options & options, std::size_t benchmarks, std::size_t repetitions, double preferredMinTimeSeconds, const ProbePass & probe);

	[[nodiscard]] bool WarmUp(double milliseconds, const Options & options, const ProbePass & pass);

}

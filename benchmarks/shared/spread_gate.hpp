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

#include <benchmark/benchmark.h>

#include <string_view>
#include <vector>

namespace bench
{

	/**
	 * \brief The console report plus the tolerance the run is judged on.
	 *
	 * Google Benchmark reports the spread over the repetitions and rules on none of it, so a figure a machine cannot resolve still reads like a measurement. A
	 * run that spreads wider than the tolerance says so and leaves through a non-zero exit. The statistic is the coefficient of variation, which takes more
	 * than one repetition to have at all.
	 */
	class SpreadGate final : public benchmark::ConsoleReporter
	{
	public:
		/**
		 * \brief Watches the reported time and, where a benchmark reports a second measurement beside it, that counter as well.
		 *
		 * \param pairedCounter the counter carrying the second measurement or empty where there is none. The native delta times two arms and can only report one
		 * of them as the time. An arm that alone came back noisy is still a delta nothing can be concluded from.
		 */
		explicit SpreadGate(std::string_view pairedCounter = {});

		void ReportRuns(const std::vector<Run> & reports) override;

		[[nodiscard]] double WorstSpreadPercent() const
		{
			return m_worstSpreadPercent;
		}

	private:
		std::string_view m_pairedCounter;
		double m_worstSpreadPercent = 0.0;
	};

} // namespace bench

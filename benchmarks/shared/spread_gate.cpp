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

#include "shared/spread_gate.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace bench
{

	SpreadGate::SpreadGate(const std::string_view pairedCounter) : m_pairedCounter(pairedCounter) {}

	void SpreadGate::ReportRuns(const std::vector<Run> & reports)
	{
		for (const Run & report : reports)
		{
			if (report.run_type != Run::RT_Aggregate || report.aggregate_name != "cv")
			{
				continue;
			}

			// A coefficient of variation is reported as the fraction of the mean it is, both in the time and in every counter beside it.
			m_worstSpreadPercent = std::max(m_worstSpreadPercent, report.real_accumulated_time * 100.0);

			if (m_pairedCounter.empty())
			{
				continue;
			}

			for (const auto & [name, counter] : report.counters)
			{
				if (name == m_pairedCounter)
				{
					m_worstSpreadPercent = std::max(m_worstSpreadPercent, counter.value * 100.0);
				}
			}
		}

		ConsoleReporter::ReportRuns(reports);
	}

} // namespace bench

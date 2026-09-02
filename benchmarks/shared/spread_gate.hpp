// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <benchmark/benchmark.h>

#include <string_view>
#include <vector>

namespace bench
{

	class SpreadGate final : public benchmark::ConsoleReporter
	{
	public:
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

}

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

#include "azoth/rhi/core/version.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <string>

namespace
{

	void PrintConfiguration()
	{
		namespace rhi  = azo::rhi;
		namespace test = azo::rhi::test;

		constexpr rhi::BuildInfo build = rhi::GetBuildInfo();

		std::cout << "AzothRHI " << build.versionMajor << '.' << build.versionMinor << '.' << build.versionPatch << " test suite\n";

		std::cout << "  validation mode: " << AZOTH_RHI_TEST_CONFIGURATION_NAME << '\n';
		std::cout << "  profiling: " << (build.profilingEnabled ? "compiled in" : "compiled out") << (build.tracyEnabled ? ", Tracy sink built" : "") << '\n';

		std::cout << "  backends under test:";
		for (const test::Backend & backend : test::SelectedBackends())
		{
			std::cout << ' ' << backend.shortName;
		}
		if (test::SelectedBackends().empty())
		{
			std::cout << " none";
		}
		std::cout << '\n';

		if (const std::uint32_t scale = test::StressScale(); scale != 1)
		{
			std::cout << "  stress scale: " << scale << '\n';
		}

		std::cout << std::flush;
	}

} // namespace

int main(int argc, char ** argv)
{
	::testing::InitGoogleMock(&argc, argv);

	if (const std::string problem = azo::rhi::test::ValidateBackendSelection(); !problem.empty())
	{
		std::cerr << problem << '\n';
		return 1;
	}

	PrintConfiguration();

	return RUN_ALL_TESTS();
}

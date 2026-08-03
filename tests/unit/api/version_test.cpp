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
#include "azoth/rhi/device/selection.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	TEST(BuildInfo, ConstantFoldsSoCheckingABackendCostsNothing)
	{
		constexpr rhi::BuildInfo info = rhi::GetBuildInfo();
		static_assert(info.versionMajor == rhi::kVersionMajor);
		static_assert(std::is_trivially_copyable_v<rhi::BuildInfo>);

		SUCCEED();
	}

	TEST(BuildInfo, ReportsTheVersionTheHeadersDeclare)
	{
		constexpr rhi::BuildInfo info = rhi::GetBuildInfo();

		EXPECT_EQ(info.versionMajor, rhi::kVersionMajor);
		EXPECT_EQ(info.versionMinor, rhi::kVersionMinor);
		EXPECT_EQ(info.versionPatch, rhi::kVersionPatch);
	}

	TEST(BuildInfo, VersionStringAgreesWithTheVersionNumbers)
	{
		const std::string expected = std::to_string(rhi::kVersionMajor) + '.' + std::to_string(rhi::kVersionMinor) + '.' + std::to_string(rhi::kVersionPatch);

		ASSERT_NE(rhi::kVersionString, nullptr);
		EXPECT_EQ(std::string_view{ rhi::kVersionString }, expected);
	}

	TEST(BuildInfo, SaysNothingAboutWhichBackendsThisBuildHas)
	{
		constexpr rhi::BuildInfo info = rhi::GetBuildInfo();
		static_assert(info.versionMajor == rhi::kVersionMajor);

		EXPECT_FALSE(rhi::AvailableBackends().empty()) << "the Null backend is always compiled in, so this is never empty";
		EXPECT_NE(rhi::FindAvailableBackend(rhi::NullApi::id), nullptr);
	}

	TEST(BuildInfo, ReportsWhetherInstrumentationIsLive)
	{
		constexpr rhi::BuildInfo info = rhi::GetBuildInfo();

#ifdef AZOTH_RHI_ENABLE_PROFILING
		EXPECT_TRUE(info.profilingEnabled);
#else
		EXPECT_FALSE(info.profilingEnabled) << "profiling is reported live but the macros compiled to nothing";
#endif

#ifdef TRACY_ENABLE
		EXPECT_TRUE(info.tracyEnabled);
#else
		EXPECT_FALSE(info.tracyEnabled);
#endif
	}

	TEST(BuildInfo, DefaultConstructsToNothingPresent)
	{
		constexpr rhi::BuildInfo blank{};

		static_assert(!blank.profilingEnabled);
		static_assert(!blank.tracyEnabled);

		static_assert(blank.versionMajor == rhi::kVersionMajor);

		SUCCEED();
	}

} // namespace

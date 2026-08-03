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

#include "azoth/rhi/core/build_config.hpp"
#include "azoth/rhi/rhi.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	TEST(BuildConfig, TheClipSpaceSettlesOnceAndHoldsThatChoice)
	{
		EXPECT_EQ(rhi::GetClipSpace(), rhi::ClipSpaceConvention::eYUp) << "a process that never settles the convention runs on eYUp throughout";

		EXPECT_TRUE(rhi::SetClipSpace(rhi::ClipSpaceConvention::eYDown));
		EXPECT_EQ(rhi::GetClipSpace(), rhi::ClipSpaceConvention::eYDown);

		EXPECT_FALSE(rhi::SetClipSpace(rhi::ClipSpaceConvention::eYUp));
		EXPECT_EQ(rhi::GetClipSpace(), rhi::ClipSpaceConvention::eYDown);

		EXPECT_FALSE(rhi::SetClipSpace(rhi::ClipSpaceConvention::eYDown));
		EXPECT_EQ(rhi::GetClipSpace(), rhi::ClipSpaceConvention::eYDown);
	}

	TEST(BuildConfig, TheUmbrellaCarriesTheClipSpaceConvention)
	{
		static_assert(std::is_enum_v<rhi::ClipSpaceConvention>);
		static_assert(std::is_same_v<decltype(rhi::GetClipSpace()), rhi::ClipSpaceConvention>);

		SUCCEED();
	}

} // namespace

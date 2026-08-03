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

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace rhi = azo::rhi;

namespace
{

	TEST(Result, HoldsAValueWhenConstructedFromOne)
	{
		constexpr rhi::Result<int> result = 42;

		static_assert(result.HasValue());
		static_assert(static_cast<bool>(result));
		static_assert(result.Value() == 42);

		EXPECT_TRUE(result.HasValue());
	}

	TEST(Result, HoldsAnErrorWhenConstructedFromOne)
	{
		constexpr rhi::Result<int> result = rhi::Error{
			.code	 = rhi::ErrorCode::eOutOfMemory,
			.message = "out of memory",
		};

		static_assert(!result.HasValue());
		static_assert(!static_cast<bool>(result));
		static_assert(result.GetError().code == rhi::ErrorCode::eOutOfMemory);

		EXPECT_STREQ(result.GetError().message, "out of memory");
	}

	TEST(Result, ConvertsToBoolOnlyExplicitly)
	{
		static_assert(!std::convertible_to<rhi::Result<int>, bool>);
		static_assert(std::constructible_from<bool, rhi::Result<int>>);

		const rhi::Result<int> ok = 1;
		if (ok)
		{
			SUCCEED();
			return;
		}
		ADD_FAILURE() << "a Result holding a value was falsy in a condition";
	}

	TEST(Result, ConstructsImplicitlyFromAValueOrAnError)
	{
		// Both directions are implicit on purpose, so a fallible function can return a value or an rhi::Error without naming rhi::Result at every exit.
		static_assert(std::convertible_to<int, rhi::Result<int>>);
		static_assert(std::convertible_to<rhi::Error, rhi::Result<int>>);

		const auto fallible = [](const bool succeed) -> rhi::Result<int>
		{
			if (succeed)
			{
				return 7;
			}
			return rhi::Error{
				.code	 = rhi::ErrorCode::eInvalidArgument,
				.message = "no",
			};
		};

		EXPECT_TRUE(fallible(true).HasValue());
		EXPECT_EQ(fallible(false).GetError().code, rhi::ErrorCode::eInvalidArgument);
	}

	TEST(Result, RvalueValueMovesRatherThanCopies)
	{
		rhi::Result<std::unique_ptr<int>> result = std::make_unique<int>(9);
		ASSERT_TRUE(result.HasValue());

		const std::unique_ptr<int> taken = std::move(result).Value();
		ASSERT_NE(taken, nullptr);
		EXPECT_EQ(*taken, 9);
		// NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the state after the move is exactly what this asserts.
		EXPECT_EQ(result.Value(), nullptr) << "the value was copied out instead of moved";
	}

	TEST(Result, ValueIsReachableThroughBothConstAndMutableReferences)
	{
		rhi::Result<std::string> result = std::string{ "payload" };

		result.Value() += "!";
		EXPECT_EQ(result.Value(), "payload!");

		const rhi::Result<std::string> & readOnly = result;
		EXPECT_EQ(readOnly.Value(), "payload!");

		static_assert(std::same_as<decltype(std::declval<rhi::Result<std::string> &>().Value()), std::string &>);
		static_assert(std::same_as<decltype(std::declval<const rhi::Result<std::string> &>().Value()), const std::string &>);
		static_assert(std::same_as<decltype(std::declval<rhi::Result<std::string> &&>().Value()), std::string &&>);
	}

	TEST(Result, CarriesAMoveOnlyValueWithoutRequiringACopy)
	{
		static_assert(!std::copy_constructible<rhi::UniqueDevice>);
		static_assert(std::move_constructible<rhi::Result<std::unique_ptr<int>>>);

		SUCCEED();
	}

	TEST(ResultVoid, DefaultsToSuccess)
	{
		constexpr rhi::Result<void> result;

		static_assert(result.HasValue());
		static_assert(static_cast<bool>(result));

		EXPECT_TRUE(result.HasValue());
	}

	TEST(ResultVoid, HoldsAnErrorWhenConstructedFromOne)
	{
		constexpr rhi::Result<void> result = rhi::Error{
			.code	 = rhi::ErrorCode::eInvalidState,
			.message = "already registered",
		};

		static_assert(!result.HasValue());
		static_assert(result.GetError().code == rhi::ErrorCode::eInvalidState);

		EXPECT_FALSE(result.HasValue());
	}

	TEST(ResultVoid, ReturnsCleanlyFromBothPaths)
	{
		const auto fallible = [](const bool succeed) -> rhi::Result<void>
		{
			if (succeed)
			{
				return {};
			}
			return rhi::Error{
				.code	 = rhi::ErrorCode::eUnsupportedFeature,
				.message = "no",
			};
		};

		EXPECT_TRUE(fallible(true));
		EXPECT_FALSE(fallible(false));
		EXPECT_EQ(fallible(false).GetError().code, rhi::ErrorCode::eUnsupportedFeature);
	}

	TEST(ErrorValue, DefaultsToOkWithNoMessage)
	{
		constexpr rhi::Error error{};

		static_assert(error.code == rhi::ErrorCode::eOk);
		static_assert(error.message == nullptr);

		SUCCEED();
	}

	TEST(ErrorValue, IsATrivialAggregateSoItCostsNothingToPassAround)
	{
		static_assert(std::is_aggregate_v<rhi::Error>);
		static_assert(std::is_trivially_copyable_v<rhi::Error>);

		SUCCEED();
	}

} // namespace

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/query.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class TimestampStageTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(TimestampStageTest);

	constexpr std::uint32_t kPoolQueries = 4;

	static_assert(rhi::QueueCanNameStage(rhi::QueueType::eGraphics, rhi::Stage::eColorOutput));
	static_assert(!rhi::QueueCanNameStage(rhi::QueueType::eCompute, rhi::Stage::eColorOutput));
	static_assert(!rhi::QueueCanNameStage(rhi::QueueType::eCopy, rhi::Stage::eCompute));
	static_assert(rhi::QueueCanNameStage(rhi::QueueType::eCopy, rhi::Stage::eAllCommands));

	TEST_P(TimestampStageTest, RefusesAGraphicsStageOnACopyList)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		{
			test::Recording recording(Dev(), rhi::QueueType::eCopy);
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Error stageError{};
			EXPECT_FALSE(recording.List().WriteTimestamp(pool, 0, rhi::Stage::eColorOutput, stageError))
				<< "a copy list wrote a timestamp at a stage only a graphics queue runs";
			EXPECT_TRUE(test::ErrorIsPopulated(stageError));

			EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 0, rhi::Stage::eCopy, error), error));

			EXPECT_TRUE(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

	TEST_P(TimestampStageTest, AcceptsTheSameGraphicsStageOnAGraphicsList)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 0, rhi::Stage::eColorOutput, error), error));
			EXPECT_TRUE(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

	TEST_P(TimestampStageTest, RefusesAGraphicsStageOnAComputeList)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		{
			test::Recording recording(Dev(), rhi::QueueType::eCompute);
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Error stageError{};
			EXPECT_FALSE(recording.List().WriteTimestamp(pool, 0, rhi::Stage::eVertexWork, stageError))
				<< "a compute list wrote a timestamp at a stage only a graphics queue runs";
			EXPECT_TRUE(test::ErrorIsPopulated(stageError));

			EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 0, rhi::Stage::eCompute, error), error));

			EXPECT_TRUE(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

}

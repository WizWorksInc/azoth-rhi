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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class RetireTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(RetireTest);

	constexpr std::uint64_t kRounds	 = 500;
	constexpr std::uint64_t kForever = std::numeric_limits<std::uint64_t>::max();

	TEST_P(RetireTest, ResetsAPoolTheMomentAWaitOnItsOwnSignalReturns)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		for (std::uint64_t round = 1; round <= kRounds; ++round)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error));
			ASSERT_TRUE(test::Ok(list.Begin(error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> lists{ &list };
			const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = round } };
			ASSERT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error));

			ASSERT_TRUE(test::Ok(queue.Wait(timeline, round, kForever, error), error));
			ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = round }, error), error)) << "round " << round;
		}

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(RetireTest, ResetsAPoolTheMomentAWaitOnALaterSubmitsSignalReturns)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		for (std::uint64_t round = 1; round <= kRounds; ++round)
		{
			rhi::CommandList unsignaled = pool.Allocate("azoth.rhi.test.unsignaled", error);
			ASSERT_TRUE(test::Ok(unsignaled.IsValid(), error));
			ASSERT_TRUE(test::Ok(unsignaled.Begin(error), error));
			ASSERT_TRUE(test::Ok(unsignaled.End(error), error));

			rhi::CommandList signaled = pool.Allocate("azoth.rhi.test.signaled", error);
			ASSERT_TRUE(test::Ok(signaled.IsValid(), error));
			ASSERT_TRUE(test::Ok(signaled.Begin(error), error));
			ASSERT_TRUE(test::Ok(signaled.End(error), error));

			std::array<const rhi::CommandList *, 1> first{ &unsignaled };
			ASSERT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = first }, error), error));

			std::array<const rhi::CommandList *, 1> second{ &signaled };
			const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = round } };
			ASSERT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = second, .signals = signals }, error), error));

			ASSERT_TRUE(test::Ok(queue.Wait(timeline, round, kForever, error), error));
			ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = round }, error), error)) << "round " << round;
		}

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(RetireTest, ResubmitsAListTheMomentAWaitOnItsOwnSignalReturns)
	{
		if (!Dev().GetCaps().supportsCommandListResubmit)
		{
			GTEST_SKIP() << "this backend does not submit a list twice";
		}

		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		for (std::uint64_t round = 1; round <= kRounds; ++round)
		{
			const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = round } };
			ASSERT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error)) << "round " << round;
			ASSERT_TRUE(test::Ok(queue.Wait(timeline, round, kForever, error), error));
		}

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

}

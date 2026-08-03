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

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class TimelineTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(TimelineTest);

	TEST_P(TimelineTest, CreatesAndDestroysATimeline)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);

		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, CreatesATimelineSeededAtANonZeroValue)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(64), error);

		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, ReadsBackACompletedValue)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		std::uint64_t completed = 0;
		EXPECT_TRUE(test::Ok(queue.GetCompletedValue(timeline, completed, error), error));

		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, TheThreeCompletedValueOverloadsAgree)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		std::uint64_t sentinel = 0;
		ASSERT_TRUE(queue.GetCompletedValue(timeline, sentinel));

		std::uint64_t withError = 0;
		ASSERT_TRUE(test::Ok(queue.GetCompletedValue(timeline, withError, error), error));

		const rhi::Result<std::uint64_t> asResult = queue.GetCompletedValueWithResult(timeline);
		ASSERT_TRUE(test::Ok(asResult));

		EXPECT_EQ(sentinel, withError);
		EXPECT_EQ(withError, asResult.Value());

		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, ACompletedValueNeverMovesBackwards)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(8), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		std::uint64_t previous = 0;
		ASSERT_TRUE(test::Ok(queue.GetCompletedValue(timeline, previous, error), error));

		for (int poll = 0; poll < 16; ++poll)
		{
			std::uint64_t current = 0;
			ASSERT_TRUE(test::Ok(queue.GetCompletedValue(timeline, current, error), error));
			EXPECT_GE(current, previous) << "a timeline's completed value went backwards";
			previous = current;
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, ClearsTheOutputWhenACompletedValueQueryFails)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		std::uint64_t completed = 0xDEADBEEF;
		rhi::Error queryError{};

		if (queue.GetCompletedValue(
				rhi::TimelineHandle{
					.index		= 6000,
					.generation = 1,
				},
				completed,
				queryError))
		{
			GTEST_SKIP() << "this backend does not reject an unknown timeline handle";
		}

		EXPECT_EQ(completed, 0u) << "a failed query left the caller's output holding its previous value";
	}

	TEST_P(TimelineTest, SignalsAndWaitsThroughTheQueue)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		if (!queue.Signal(timeline, 1, error))
		{
			static_cast<void>(Dev().Destroy(timeline, {}, error));
			GTEST_SKIP() << "this backend does not signal timelines from the host: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(queue.Wait(timeline, 1, test::kWaitTimeoutNanoseconds, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

	TEST_P(TimelineTest, CreatesAndDestroysABinarySemaphore)
	{
		rhi::Error error{};
		const rhi::BinarySemaphoreHandle semaphore = Dev().CreateBinarySemaphore(rhi::BinarySemaphoreDesc{ .debugName = "azoth.rhi.test.semaphore" }, error);

		ASSERT_TRUE(test::Ok(semaphore.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(semaphore, {}, error), error));
	}

	TEST_P(TimelineTest, KeepsTimelineAndSemaphoreHandleDomainsApart)
	{
		static_assert(!std::is_convertible_v<rhi::TimelineHandle, rhi::BinarySemaphoreHandle>);
		static_assert(!std::is_convertible_v<rhi::BinarySemaphoreHandle, rhi::TimelineHandle>);

		SUCCEED();
	}

	TEST_P(TimelineTest, WaitIdleReturnsOnAQueueWithNothingSubmitted)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
	}

	TEST_P(TimelineTest, CollectsGarbageAgainstATimelineValue)
	{
		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		const rhi::DestroyDesc deferred{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
		};
		ASSERT_TRUE(test::Ok(Dev().Destroy(buffer, deferred, error), error));

		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(timeline, 1, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
	}

} // namespace

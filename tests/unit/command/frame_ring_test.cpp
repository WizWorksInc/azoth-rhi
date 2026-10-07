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

#include "azoth/rhi/commands/frame_ring.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class FrameRingTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] rhi::Queue GraphicsQueue(rhi::Error & error)
		{
			return Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		}
	};

	AZO_RHI_BACKEND_SUITE(FrameRingTest);

	TEST_P(FrameRingTest, CreatesARingWithAPoolPerFrameInFlight)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing ring = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 3, .debugName = "test.ring" }, error);

		EXPECT_TRUE(test::Ok(ring.is_valid(), error));
		EXPECT_EQ(ring.frames_in_flight(), 3u);
		EXPECT_EQ(ring.frame_index(), 0u);
		EXPECT_TRUE(ring.Timeline().IsValid());
	}

	TEST_P(FrameRingTest, RefusesADepthOutsideWhatItCanHold)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		for (const std::uint32_t depth : { 0u, rhi::kMaxFramesInFlight + 1 })
		{
			rhi::Error created{};
			const rhi::FrameRing ring = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = depth }, created);

			EXPECT_FALSE(ring.is_valid()) << "accepted a depth of " << depth;
			EXPECT_NE(created.code, rhi::ErrorCode::eOk);
		}
	}

	TEST_P(FrameRingTest, BeginOnAnUncreatedRingFailsRatherThanDividingByZero)
	{
		rhi::FrameRing ring;
		ASSERT_FALSE(ring.is_valid());

		rhi::Error error{};
		const rhi::CommandList list = ring.Begin(error);

		EXPECT_FALSE(list.is_valid());
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, MovingARingLeavesTheSourceInvalidRatherThanADuplicate)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing source = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 2 }, error);
		ASSERT_TRUE(test::Ok(source.is_valid(), error));

		const rhi::FrameRing moved = std::move(source);

		EXPECT_TRUE(moved.is_valid());
		EXPECT_EQ(moved.frames_in_flight(), 2u);

		// NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the state after a move is exactly what this asserts.
		EXPECT_FALSE(source.is_valid());

		rhi::Error begun{};
		// NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
		const rhi::CommandList list = source.Begin(begun);
		EXPECT_FALSE(list.is_valid());
		EXPECT_NE(begun.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, WalksFramesThroughItsSlotsInOrder)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		constexpr std::uint32_t kDepth = 2;
		rhi::FrameRing ring			   = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = kDepth }, error);
		ASSERT_TRUE(test::Ok(ring.is_valid(), error));

		for (std::uint64_t frame = 1; frame <= 5; ++frame)
		{
			rhi::CommandList list = ring.Begin(error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error)) << "frame " << frame;

			EXPECT_EQ(ring.frame_index(), frame);
			EXPECT_EQ(ring.slot_index(), frame % kDepth);
			EXPECT_EQ(ring.Signal().value, frame);
			EXPECT_EQ(ring.retire().value, frame);
			EXPECT_TRUE(ring.Signal().timeline == ring.Timeline());

			ASSERT_TRUE(test::Ok(list.Begin(error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> lists{ &list };
			const std::array signals{ ring.Signal() };
			ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error));
		}

		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
	}

	TEST_P(FrameRingTest, DestroysItsTimelineWhenItGoesOutOfScope)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::TimelineHandle timeline{};
		{
			const rhi::FrameRing ring = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 2 }, error);
			ASSERT_TRUE(test::Ok(ring.is_valid(), error));
			timeline = ring.Timeline();
		}

		if (test::kValidatesHandles)
		{
			rhi::Error destroyed{};
			EXPECT_FALSE(Dev().destroy(timeline, {}, destroyed)) << "the ring left its timeline for the device to reclaim";
		}
	}

	TEST_P(FrameRingTest, ABoundedBeginGivesUpOnAFrameThatWasNeverSubmitted)
	{
		if (IsNullBackend())
		{
			GTEST_SKIP() << "the null backend retires timeline values without a queue actually running them";
		}

		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing ring = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 1 }, error);
		ASSERT_TRUE(test::Ok(ring.is_valid(), error));

		const rhi::CommandList dropped = ring.Begin(error);
		ASSERT_TRUE(test::Ok(dropped.is_valid(), error));

		constexpr std::uint64_t kTenMilliseconds = 10'000'000;

		rhi::Error waited{};
		const rhi::CommandList list = ring.Begin(kTenMilliseconds, waited);

		EXPECT_FALSE(list.is_valid());
		EXPECT_NE(waited.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, HandsBackAListThatHasNotBeenBegunYet)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing ring = rhi::FrameRing::create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 1 }, error);
		ASSERT_TRUE(test::Ok(ring.is_valid(), error));

		rhi::CommandList list = ring.Begin(error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		EXPECT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

}

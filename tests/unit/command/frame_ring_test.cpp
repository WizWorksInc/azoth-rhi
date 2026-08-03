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
			return Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		}
	};

	AZO_RHI_BACKEND_SUITE(FrameRingTest);

	TEST_P(FrameRingTest, CreatesARingWithAPoolPerFrameInFlight)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing ring = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 3, .debugName = "test.ring" }, error);

		EXPECT_TRUE(test::Ok(ring.IsValid(), error));
		EXPECT_EQ(ring.FramesInFlight(), 3u);
		EXPECT_EQ(ring.FrameIndex(), 0u);
		EXPECT_TRUE(ring.Timeline().IsValid());
	}

	TEST_P(FrameRingTest, RefusesADepthOutsideWhatItCanHold)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		for (const std::uint32_t depth : { 0u, rhi::kMaxFramesInFlight + 1 })
		{
			rhi::Error created{};
			const rhi::FrameRing ring = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = depth }, created);

			EXPECT_FALSE(ring.IsValid()) << "accepted a depth of " << depth;
			EXPECT_NE(created.code, rhi::ErrorCode::eOk);
		}
	}

	TEST_P(FrameRingTest, BeginOnAnUncreatedRingFailsRatherThanDividingByZero)
	{
		rhi::FrameRing ring;
		ASSERT_FALSE(ring.IsValid());

		rhi::Error error{};
		const rhi::CommandList list = ring.Begin(error);

		EXPECT_FALSE(list.IsValid());
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, MovingARingLeavesTheSourceInvalidRatherThanADuplicate)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing source = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 2 }, error);
		ASSERT_TRUE(test::Ok(source.IsValid(), error));

		const rhi::FrameRing moved = std::move(source);

		EXPECT_TRUE(moved.IsValid());
		EXPECT_EQ(moved.FramesInFlight(), 2u);

		// NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the state after a move is exactly what this asserts.
		EXPECT_FALSE(source.IsValid());

		rhi::Error begun{};
		// NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
		const rhi::CommandList list = source.Begin(begun);
		EXPECT_FALSE(list.IsValid());
		EXPECT_NE(begun.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, WalksFramesThroughItsSlotsInOrder)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		constexpr std::uint32_t kDepth = 2;
		rhi::FrameRing ring			   = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = kDepth }, error);
		ASSERT_TRUE(test::Ok(ring.IsValid(), error));

		for (std::uint64_t frame = 1; frame <= 5; ++frame)
		{
			rhi::CommandList list = ring.Begin(error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error)) << "frame " << frame;

			EXPECT_EQ(ring.FrameIndex(), frame);
			EXPECT_EQ(ring.SlotIndex(), frame % kDepth);
			EXPECT_EQ(ring.Signal().value, frame);
			EXPECT_EQ(ring.Retire().value, frame);
			EXPECT_TRUE(ring.Signal().timeline == ring.Timeline());

			ASSERT_TRUE(test::Ok(list.Begin(error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> lists{ &list };
			const std::array signals{ ring.Signal() };
			ASSERT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error));
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

		rhi::FrameRing ring = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 1 }, error);
		ASSERT_TRUE(test::Ok(ring.IsValid(), error));

		const rhi::CommandList dropped = ring.Begin(error);
		ASSERT_TRUE(test::Ok(dropped.IsValid(), error));

		constexpr std::uint64_t kTenMilliseconds = 10'000'000;

		rhi::Error waited{};
		const rhi::CommandList list = ring.Begin(kTenMilliseconds, waited);

		EXPECT_FALSE(list.IsValid());
		EXPECT_NE(waited.code, rhi::ErrorCode::eOk);
	}

	TEST_P(FrameRingTest, HandsBackAListThatHasNotBeenBegunYet)
	{
		rhi::Error error{};
		rhi::Queue queue = GraphicsQueue(error);

		rhi::FrameRing ring = rhi::FrameRing::Create(Dev(), queue, rhi::FrameRingDesc{ .framesInFlight = 1 }, error);
		ASSERT_TRUE(test::Ok(ring.IsValid(), error));

		rhi::CommandList list = ring.Begin(error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		EXPECT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

} // namespace

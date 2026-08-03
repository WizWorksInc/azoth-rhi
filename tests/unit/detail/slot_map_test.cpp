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

#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/handle.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	struct PayloadTag final
	{
	};

	using TestMap	 = rhi::SlotMap<PayloadTag, int>;
	using TestHandle = rhi::Handle<PayloadTag>;

	TEST(SlotMap, StoreHandsBackALiveHandle)
	{
		TestMap map;
		const TestHandle handle = map.Store(42);

		ASSERT_TRUE(handle.IsValid());
		ASSERT_NE(map.Resolve(handle, true), nullptr);
		EXPECT_EQ(*map.Resolve(handle, true), 42);
		EXPECT_EQ(map.LiveCount(), 1u);
	}

	TEST(SlotMap, HandsOutDistinctIndicesWhileNothingIsRetired)
	{
		TestMap map;
		const TestHandle first	= map.Store(1);
		const TestHandle second = map.Store(2);

		EXPECT_NE(first.index, second.index);
		EXPECT_EQ(*map.Resolve(first, true), 1);
		EXPECT_EQ(*map.Resolve(second, true), 2);
		EXPECT_EQ(map.LiveCount(), 2u);
	}

	TEST(SlotMap, RetireDropsTheHandleAndTheLiveCount)
	{
		TestMap map;
		const TestHandle handle = map.Store(7);

		EXPECT_TRUE(map.Retire(handle, true));
		EXPECT_EQ(map.Resolve(handle, true), nullptr);
		EXPECT_EQ(map.LiveCount(), 0u);
	}

	TEST(SlotMap, ReusesTheSlotWithABumpedGenerationSoTheOldHandleStaysDead)
	{
		TestMap map;
		const TestHandle first = map.Store(1);
		ASSERT_TRUE(map.Retire(first, true));

		const TestHandle second = map.Store(2);
		EXPECT_EQ(second.index, first.index) << "the retired slot was not reused";
		EXPECT_NE(second.generation, first.generation) << "the reused slot kept its old generation";

		EXPECT_EQ(map.Resolve(first, true), nullptr);
		ASSERT_NE(map.Resolve(second, true), nullptr);
		EXPECT_EQ(*map.Resolve(second, true), 2);
	}

	TEST(SlotMap, RejectsADoubleRetireSoNothingIsFreedTwice)
	{
		TestMap map;
		const TestHandle handle = map.Store(1);

		EXPECT_TRUE(map.Retire(handle, true));
		EXPECT_FALSE(map.Retire(handle, true)) << "a second retire of the same handle was accepted";

		const TestHandle reused = map.Store(2);
		const TestHandle fresh	= map.Store(3);
		EXPECT_NE(reused.index, fresh.index);
	}

	TEST(SlotMap, BoundsAreCheckedEvenWithValidationOff)
	{
		// validate gates the generation and liveness checks. The index bound is not negotiable because an out-of-range read would be undefined behavior
		// and not a rejected call.
		TestMap map;
		static_cast<void>(map.Store(1));

		constexpr TestHandle outOfRange{
			.index		= 1000,
			.generation = 1,
		};
		EXPECT_EQ(map.Resolve(outOfRange, false), nullptr);
		EXPECT_EQ(map.Resolve(outOfRange, true), nullptr);
		EXPECT_FALSE(map.Retire(outOfRange, false));
		EXPECT_FALSE(map.Retire(outOfRange, true));
	}

	TEST(SlotMap, SkippingValidationResolvesAStaleHandleToTheCurrentOccupant)
	{
		// The documented cost of a release build: with validation off the stale check is not performed so a retired handle resolves to whatever now lives
		// in the slot. Pinning it here means a change to that trade-off is a deliberate edit and not a surprise.
		TestMap map;
		const TestHandle first = map.Store(1);
		ASSERT_TRUE(map.Retire(first, true));
		const TestHandle second = map.Store(2);
		ASSERT_EQ(second.index, first.index);

		EXPECT_EQ(map.Resolve(first, true), nullptr);
		ASSERT_NE(map.Resolve(first, false), nullptr);
		EXPECT_EQ(*map.Resolve(first, false), 2);
	}

	TEST(SlotMap, TheDeviceTagKeepsTwoMapsFromAliasing)
	{
		TestMap owner(1u);
		TestMap other(2u);

		const TestHandle fromOwner = owner.Store(1);
		static_cast<void>(other.Store(2));

		EXPECT_NE(rhi::detail::TagOfIndex(fromOwner.index), 0u);
		EXPECT_EQ(other.Resolve(fromOwner, true), nullptr) << "a foreign handle resolved inside another map";
		EXPECT_FALSE(other.Retire(fromOwner, true)) << "a foreign handle was retired by another map";
	}

	TEST(SlotMap, AForeignHandleIsRejectedEvenWithValidationOff)
	{
		TestMap owner(1u);
		TestMap other(2u);

		const TestHandle fromOwner = owner.Store(1);
		static_cast<void>(other.Store(2));

		EXPECT_EQ(other.Resolve(fromOwner, false), nullptr) << "a foreign handle resolved with validation off";
		EXPECT_FALSE(other.Retire(fromOwner, false)) << "a foreign handle was retired with validation off";
	}

	TEST(SlotMap, RecyclingOneSlotLeavesTheDeviceTagAlone)
	{
		TestMap map(1u);
		const TestHandle first			 = map.Store(0);
		const std::uint32_t expectedTag	 = rhi::detail::TagOfIndex(first.index);
		const std::uint32_t expectedSlot = rhi::detail::SlotOfIndex(first.index);
		ASSERT_TRUE(map.Retire(first, true));

		TestHandle handle{};
		for (int i = 0; i < 100000; ++i)
		{
			handle = map.Store(i);
			ASSERT_TRUE(map.Retire(handle, true));
		}

		EXPECT_EQ(rhi::detail::TagOfIndex(handle.index), expectedTag) << "the generation counter carried into the device tag";
		EXPECT_EQ(rhi::detail::SlotOfIndex(handle.index), expectedSlot);
	}

	TEST(SlotMap, ConstResolveFollowsTheSameRules)
	{
		TestMap map;
		const TestHandle handle	 = map.Store(5);
		const TestMap & readOnly = map;

		ASSERT_NE(readOnly.Resolve(handle, true), nullptr);
		EXPECT_EQ(*readOnly.Resolve(handle, true), 5);

		ASSERT_TRUE(map.Retire(handle, true));
		EXPECT_EQ(readOnly.Resolve(handle, true), nullptr);
	}

	TEST(SlotMap, ForEachLiveVisitsOnlyLiveSlots)
	{
		TestMap map;
		const TestHandle first = map.Store(1);
		static_cast<void>(map.Store(2));
		const TestHandle third = map.Store(3);

		ASSERT_TRUE(map.Retire(first, true));
		ASSERT_TRUE(map.Retire(third, true));

		std::vector<int> visited;
		map.ForEachLive(
			[&visited](const int & payload)
			{
				visited.push_back(payload);
			});

		EXPECT_EQ(visited, std::vector<int>{ 2 });
	}

	TEST(SlotMap, ResetEmptiesTheMapAndRestartsIndices)
	{
		TestMap map;
		static_cast<void>(map.Store(1));
		static_cast<void>(map.Store(2));

		map.Reset();
		EXPECT_EQ(map.LiveCount(), 0u);

		const TestHandle afterReset = map.Store(3);
		EXPECT_EQ(afterReset.index, 0u);
		EXPECT_EQ(map.LiveCount(), 1u);
	}

	TEST(SlotMap, MovesPayloadsRatherThanCopyingThem)
	{
		rhi::SlotMap<PayloadTag, std::string> map;

		std::string payload = "a string long enough to have heap storage of its own";
		const char * before = payload.data();

		const rhi::Handle<PayloadTag> handle = map.Store(std::move(payload));
		ASSERT_NE(map.Resolve(handle, true), nullptr);
		EXPECT_EQ(map.Resolve(handle, true)->data(), before) << "the payload was copied into the slot";
	}

	TEST(SlotMap, SurvivesAlternatingStoreAndRetireWithoutLeakingSlots)
	{
		TestMap map;
		std::vector<std::uint32_t> generations;

		for (int round = 0; round < 64; ++round)
		{
			const TestHandle handle = map.Store(round);
			EXPECT_EQ(handle.index, 0u) << "a freed slot was not reused on round " << round;
			generations.push_back(handle.generation);
			ASSERT_TRUE(map.Retire(handle, true));
		}

		EXPECT_EQ(map.LiveCount(), 0u);

		for (std::size_t index = 1; index < generations.size(); ++index)
		{
			EXPECT_GT(generations[index], generations[index - 1]) << "the generation did not advance on reuse";
		}
	}

	TEST(SlotMap, AResolveSurvivesGrowthOnAnotherThread)
	{
		TestMap map;

		const TestHandle first = map.Store(1234);
		ASSERT_TRUE(first.IsValid());

		std::atomic<bool> stop{ false };
		std::atomic<std::size_t> reads{ 0 };
		std::atomic<bool> mismatched{ false };

		std::thread reader(
			[&]
			{
				while (!stop.load(std::memory_order_relaxed))
				{
					const int * payload = map.Resolve(first, true);
					if (payload == nullptr || *payload != 1234)
					{
						mismatched.store(true, std::memory_order_relaxed);
						return;
					}

					reads.fetch_add(1, std::memory_order_relaxed);
				}
			});

		constexpr std::size_t kAtLeastRead = 64;
		constexpr int kAtLeastStored	   = 4096;
		constexpr int kStoreCeiling		   = 1 << 20;

		std::vector<TestHandle> grown;
		grown.reserve(kAtLeastStored);

		int index = 0;
		while (index < kStoreCeiling && (index < kAtLeastStored || reads.load(std::memory_order_relaxed) < kAtLeastRead))
		{
			grown.push_back(map.Store(index));
			++index;
		}

		stop.store(true, std::memory_order_relaxed);
		reader.join();

		EXPECT_FALSE(mismatched.load(std::memory_order_relaxed)) << "a resolve taken before growth stopped naming what it named";
		EXPECT_GE(reads.load(std::memory_order_relaxed), kAtLeastRead) << "the reader never got a turn, so this proved nothing";

		for (int stored = 0; stored < index; ++stored)
		{
			const int * payload = map.Resolve(grown[static_cast<std::size_t>(stored)], true);
			ASSERT_NE(payload, nullptr) << "slot " << stored;
			EXPECT_EQ(*payload, stored);
		}
	}

	TEST(SlotMap, SlotsKeepTheirAddressAcrossGrowth)
	{
		TestMap map;

		const TestHandle early	 = map.Store(7);
		const int * beforeGrowth = map.Resolve(early, true);
		ASSERT_NE(beforeGrowth, nullptr);

		for (int index = 0; index < 8192; ++index)
		{
			static_cast<void>(map.Store(index));
		}

		EXPECT_EQ(map.Resolve(early, true), beforeGrowth) << "a slot moved, which is the whole thing chunked storage rules out";
	}

} // namespace

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
#include <cstdint> // NOLINT
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
		const TestHandle handle = map.store(42);

		ASSERT_TRUE(handle.is_valid());
		ASSERT_NE(map.resolve(handle, true), nullptr);
		EXPECT_EQ(*map.resolve(handle, true), 42);
		EXPECT_EQ(map.live_count(), 1u);
	}

	TEST(SlotMap, HandsOutDistinctIndicesWhileNothingIsRetired)
	{
		TestMap map;
		const TestHandle first	= map.store(1);
		const TestHandle second = map.store(2);

		EXPECT_NE(first.index, second.index);
		EXPECT_EQ(*map.resolve(first, true), 1);
		EXPECT_EQ(*map.resolve(second, true), 2);
		EXPECT_EQ(map.live_count(), 2u);
	}

	TEST(SlotMap, RetireDropsTheHandleAndTheLiveCount)
	{
		TestMap map;
		const TestHandle handle = map.store(7);

		EXPECT_TRUE(map.retire(handle, true));
		EXPECT_EQ(map.resolve(handle, true), nullptr);
		EXPECT_EQ(map.live_count(), 0u);
	}

	TEST(SlotMap, ReusesTheSlotWithABumpedGenerationSoTheOldHandleStaysDead)
	{
		TestMap map;
		const TestHandle first = map.store(1);
		ASSERT_TRUE(map.retire(first, true));

		const TestHandle second = map.store(2);
		EXPECT_EQ(second.index, first.index) << "the retired slot was not reused";
		EXPECT_NE(second.generation, first.generation) << "the reused slot kept its old generation";

		EXPECT_EQ(map.resolve(first, true), nullptr);
		ASSERT_NE(map.resolve(second, true), nullptr);
		EXPECT_EQ(*map.resolve(second, true), 2);
	}

	TEST(SlotMap, RejectsADoubleRetireSoNothingIsFreedTwice)
	{
		TestMap map;
		const TestHandle handle = map.store(1);

		EXPECT_TRUE(map.retire(handle, true));
		EXPECT_FALSE(map.retire(handle, true)) << "a second retire of the same handle was accepted";

		const TestHandle reused = map.store(2);
		const TestHandle fresh	= map.store(3);
		EXPECT_NE(reused.index, fresh.index);
	}

	TEST(SlotMap, BoundsAreCheckedEvenWithValidationOff)
	{
		TestMap map;
		static_cast<void>(map.store(1));

		constexpr TestHandle outOfRange{
			.index		= 1000,
			.generation = 1,
		};
		EXPECT_EQ(map.resolve(outOfRange, false), nullptr);
		EXPECT_EQ(map.resolve(outOfRange, true), nullptr);
		EXPECT_FALSE(map.retire(outOfRange, false));
		EXPECT_FALSE(map.retire(outOfRange, true));
	}

	TEST(SlotMap, SkippingValidationResolvesAStaleHandleToTheCurrentOccupant)
	{
		TestMap map;
		const TestHandle first = map.store(1);
		ASSERT_TRUE(map.retire(first, true));
		const TestHandle second = map.store(2);
		ASSERT_EQ(second.index, first.index);

		EXPECT_EQ(map.resolve(first, true), nullptr);
		ASSERT_NE(map.resolve(first, false), nullptr);
		EXPECT_EQ(*map.resolve(first, false), 2);
	}

	TEST(SlotMap, TheDeviceTagKeepsTwoMapsFromAliasing)
	{
		TestMap owner(1u);
		TestMap other(2u);

		const TestHandle fromOwner = owner.store(1);
		static_cast<void>(other.store(2));

		EXPECT_NE(rhi::detail::tag_of_index(fromOwner.index), 0u);
		EXPECT_EQ(other.resolve(fromOwner, true), nullptr) << "a foreign handle resolved inside another map";
		EXPECT_FALSE(other.retire(fromOwner, true)) << "a foreign handle was retired by another map";
	}

	TEST(SlotMap, AForeignHandleIsRejectedEvenWithValidationOff)
	{
		TestMap owner(1u);
		TestMap other(2u);

		const TestHandle fromOwner = owner.store(1);
		static_cast<void>(other.store(2));

		EXPECT_EQ(other.resolve(fromOwner, false), nullptr) << "a foreign handle resolved with validation off";
		EXPECT_FALSE(other.retire(fromOwner, false)) << "a foreign handle was retired with validation off";
	}

	TEST(SlotMap, RecyclingOneSlotLeavesTheDeviceTagAlone)
	{
		TestMap map(1u);
		const TestHandle first			 = map.store(0);
		const std::uint32_t expectedTag	 = rhi::detail::tag_of_index(first.index);
		const std::uint32_t expectedSlot = rhi::detail::slot_of_index(first.index);
		ASSERT_TRUE(map.retire(first, true));

		TestHandle handle{};
		for (int i = 0; i < 100000; ++i)
		{
			handle = map.store(i);
			ASSERT_TRUE(map.retire(handle, true));
		}

		EXPECT_EQ(rhi::detail::tag_of_index(handle.index), expectedTag) << "the generation counter carried into the device tag";
		EXPECT_EQ(rhi::detail::slot_of_index(handle.index), expectedSlot);
	}

	TEST(SlotMap, ConstResolveFollowsTheSameRules)
	{
		TestMap map;
		const TestHandle handle	 = map.store(5);
		const TestMap & readOnly = map;

		ASSERT_NE(readOnly.resolve(handle, true), nullptr);
		EXPECT_EQ(*readOnly.resolve(handle, true), 5);

		ASSERT_TRUE(map.retire(handle, true));
		EXPECT_EQ(readOnly.resolve(handle, true), nullptr);
	}

	TEST(SlotMap, ForEachLiveVisitsOnlyLiveSlots)
	{
		TestMap map;
		const TestHandle first = map.store(1);
		static_cast<void>(map.store(2));
		const TestHandle third = map.store(3);

		ASSERT_TRUE(map.retire(first, true));
		ASSERT_TRUE(map.retire(third, true));

		std::vector<int> visited;
		map.for_each_live(
			[&visited](const int & payload)
			{
				visited.push_back(payload);
			}
		);

		EXPECT_EQ(visited, std::vector<int>{ 2 });
	}

	TEST(SlotMap, ResetEmptiesTheMapAndRestartsIndices)
	{
		TestMap map;
		static_cast<void>(map.store(1));
		static_cast<void>(map.store(2));

		map.reset();
		EXPECT_EQ(map.live_count(), 0u);

		const TestHandle afterReset = map.store(3);
		EXPECT_EQ(afterReset.index, 0u);
		EXPECT_EQ(map.live_count(), 1u);
	}

	TEST(SlotMap, MovesPayloadsRatherThanCopyingThem)
	{
		rhi::SlotMap<PayloadTag, std::string> map;

		std::string payload = "a string long enough to have heap storage of its own";
		const char * before = payload.data();

		const rhi::Handle<PayloadTag> handle = map.store(std::move(payload));
		ASSERT_NE(map.resolve(handle, true), nullptr);
		EXPECT_EQ(map.resolve(handle, true)->data(), before) << "the payload was copied into the slot";
	}

	TEST(SlotMap, SurvivesAlternatingStoreAndRetireWithoutLeakingSlots)
	{
		TestMap map;
		std::vector<std::uint32_t> generations;

		for (int round = 0; round < 64; ++round)
		{
			const TestHandle handle = map.store(round);
			EXPECT_EQ(handle.index, 0u) << "a freed slot was not reused on round " << round;
			generations.push_back(handle.generation);
			ASSERT_TRUE(map.retire(handle, true));
		}

		EXPECT_EQ(map.live_count(), 0u);

		for (std::size_t index = 1; index < generations.size(); ++index)
		{
			EXPECT_GT(generations[index], generations[index - 1]) << "the generation did not advance on reuse";
		}
	}

	TEST(SlotMap, AResolveSurvivesGrowthOnAnotherThread)
	{
		TestMap map;

		const TestHandle first = map.store(1234);
		ASSERT_TRUE(first.is_valid());

		constexpr std::size_t kAtLeastRead = 64;
		constexpr int kAtLeastStored	   = 4096;

		std::vector<TestHandle> grown;
		grown.reserve(static_cast<std::size_t>(kAtLeastStored));

		std::atomic<bool> stop{ false };
		std::atomic<std::size_t> reads{ 0 };
		std::atomic<int> published{ 0 };
		std::atomic<bool> mismatched{ false };

		std::thread reader(
			[&]
			{
				while (!stop.load(std::memory_order_relaxed))
				{
					const int * payload = map.resolve(first, true);
					if (payload == nullptr || *payload != 1234)
					{
						mismatched.store(true, std::memory_order_relaxed);
						return;
					}

					if (const int reach = published.load(std::memory_order_acquire); reach > 0)
					{
						const int newest		 = reach - 1;
						const int * grownPayload = map.resolve(grown[static_cast<std::size_t>(newest)], true);
						if (grownPayload == nullptr || *grownPayload != newest)
						{
							mismatched.store(true, std::memory_order_relaxed);
							return;
						}
					}

					reads.fetch_add(1, std::memory_order_relaxed);
				}
			}
		);

		for (int index = 0; index < kAtLeastStored; ++index)
		{
			grown.push_back(map.store(index));
			published.store(index + 1, std::memory_order_release);
		}

		while (reads.load(std::memory_order_relaxed) < kAtLeastRead && !mismatched.load(std::memory_order_relaxed))
		{
			std::this_thread::yield();
		}

		stop.store(true, std::memory_order_relaxed);
		reader.join();

		EXPECT_FALSE(mismatched.load(std::memory_order_relaxed)) << "a resolve taken before growth stopped naming what it named";
		EXPECT_GE(reads.load(std::memory_order_relaxed), kAtLeastRead) << "the reader never got a turn, so this proved nothing";

		for (int stored = 0; stored < kAtLeastStored; ++stored)
		{
			const int * payload = map.resolve(grown[static_cast<std::size_t>(stored)], true);
			ASSERT_NE(payload, nullptr) << "slot " << stored;
			EXPECT_EQ(*payload, stored);
		}
	}

	TEST(SlotMap, SlotsKeepTheirAddressAcrossGrowth)
	{
		TestMap map;

		const TestHandle early	 = map.store(7);
		const int * beforeGrowth = map.resolve(early, true);
		ASSERT_NE(beforeGrowth, nullptr);

		for (int index = 0; index < 8192; ++index)
		{
			static_cast<void>(map.store(index));
		}

		EXPECT_EQ(map.resolve(early, true), beforeGrowth) << "a slot moved, which is the whole thing chunked storage rules out";
	}

} // namespace

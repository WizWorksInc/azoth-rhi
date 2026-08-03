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

#include "azoth/rhi/backend/support/object_pool.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace detail = azo::rhi::detail;

namespace
{

	struct Tracked final
	{
		explicit Tracked(const int value = 0) noexcept : payload(value)
		{
			++liveCount;
			++constructedCount;
		}

		Tracked(const Tracked & other) noexcept : payload(other.payload)
		{
			++liveCount;
			++constructedCount;
		}

		Tracked & operator=(const Tracked &) = delete;

		Tracked(Tracked &&)				= delete;
		Tracked & operator=(Tracked &&) = delete;

		~Tracked()
		{
			--liveCount;
			++destroyedCount;
		}

		int payload = 0;

		static inline int liveCount		   = 0;
		static inline int constructedCount = 0;
		static inline int destroyedCount   = 0;

		static void ResetCounters() noexcept
		{
			liveCount		 = 0;
			constructedCount = 0;
			destroyedCount	 = 0;
		}
	};

	class ObjectPoolTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			Tracked::ResetCounters();
		}

		void TearDown() override
		{
			EXPECT_EQ(Tracked::liveCount, 0) << "the pool left objects alive";
		}
	};

	TEST_F(ObjectPoolTest, NewConstructsInPlaceAndCounts)
	{
		detail::TypedObjectPool<Tracked> pool(4);

		Tracked * object = pool.New(11);
		ASSERT_NE(object, nullptr);
		EXPECT_EQ(object->payload, 11);
		EXPECT_EQ(pool.LiveCount(), 1u);
		EXPECT_EQ(Tracked::constructedCount, 1);
	}

	TEST_F(ObjectPoolTest, AddressesSurviveGrowingPastTheFirstPage)
	{
		detail::TypedObjectPool<Tracked> pool(2);

		std::vector<Tracked *> objects;
		for (int index = 0; index < 9; ++index)
		{
			Tracked * object = pool.New(index);
			ASSERT_NE(object, nullptr) << "allocation " << index << " failed with no page limit set";
			objects.push_back(object);
		}

		for (std::size_t index = 0; index < objects.size(); ++index)
		{
			EXPECT_EQ(objects[index]->payload, static_cast<int>(index)) << "the object at index " << index << " moved or was overwritten";
		}
	}

	TEST_F(ObjectPoolTest, HonoursThePageLimitByRefusingRatherThanGrowing)
	{
		detail::TypedObjectPool<Tracked> pool(2, 2);

		for (int index = 0; index < 4; ++index)
		{
			EXPECT_NE(pool.New(index), nullptr) << "allocation " << index << " failed inside the page budget";
		}

		EXPECT_EQ(pool.New(4), nullptr) << "the pool grew past its page limit";
		EXPECT_EQ(pool.LiveCount(), 4u);
	}

	TEST_F(ObjectPoolTest, ZeroBlocksPerPageIsTreatedAsOne)
	{
		detail::TypedObjectPool<Tracked> pool(0);

		EXPECT_NE(pool.New(1), nullptr);
		EXPECT_NE(pool.New(2), nullptr);
		EXPECT_EQ(pool.LiveCount(), 2u);
	}

	TEST_F(ObjectPoolTest, ResetDestroysEverythingAndReleasesThePages)
	{
		detail::TypedObjectPool<Tracked> pool(2);
		for (int index = 0; index < 5; ++index)
		{
			ASSERT_NE(pool.New(index), nullptr);
		}
		ASSERT_EQ(Tracked::liveCount, 5);

		pool.Reset();

		EXPECT_EQ(Tracked::liveCount, 0);
		EXPECT_EQ(Tracked::destroyedCount, 5);
		EXPECT_EQ(pool.LiveCount(), 0u);

		EXPECT_NE(pool.New(99), nullptr);
		EXPECT_EQ(pool.LiveCount(), 1u);
	}

	TEST_F(ObjectPoolTest, DestructorDestroysWhatResetWasNeverCalledOn)
	{
		{
			detail::TypedObjectPool<Tracked> pool(3);
			for (int index = 0; index < 7; ++index)
			{
				ASSERT_NE(pool.New(index), nullptr);
			}
			EXPECT_EQ(Tracked::liveCount, 7);
		}

		EXPECT_EQ(Tracked::liveCount, 0);
		EXPECT_EQ(Tracked::destroyedCount, 7);
	}

	TEST_F(ObjectPoolTest, MoveConstructionTakesTheStorageWithoutDestroyingIt)
	{
		detail::TypedObjectPool<Tracked> source(2);
		Tracked * object = source.New(5);
		ASSERT_NE(object, nullptr);
		ASSERT_NE(source.New(6), nullptr);

		detail::TypedObjectPool<Tracked> moved(std::move(source));

		EXPECT_EQ(Tracked::destroyedCount, 0) << "moving the pool destroyed its objects";
		EXPECT_EQ(moved.LiveCount(), 2u);
		EXPECT_EQ(object->payload, 5) << "the object moved when the pool did";
	}

	TEST_F(ObjectPoolTest, MoveAssignmentDestroysWhatTheTargetHeld)
	{
		detail::TypedObjectPool<Tracked> target(2);
		ASSERT_NE(target.New(1), nullptr);
		ASSERT_NE(target.New(2), nullptr);

		detail::TypedObjectPool<Tracked> source(2);
		Tracked * survivor = source.New(3);
		ASSERT_NE(survivor, nullptr);

		target = std::move(source);

		EXPECT_EQ(Tracked::destroyedCount, 2) << "the objects the target already held were not released";
		EXPECT_EQ(target.LiveCount(), 1u);
		EXPECT_EQ(survivor->payload, 3);
	}

	TEST_F(ObjectPoolTest, KeepsTheDebugNameItWasGiven)
	{
		const detail::TypedObjectPool<Tracked> pool(4, 0, "rhi.test.pool");
		ASSERT_NE(pool.DebugName(), nullptr);
		EXPECT_STREQ(pool.DebugName(), "rhi.test.pool");
	}

	TEST(ObjectPoolAlignment, RespectsOverAlignedTypes)
	{
		struct alignas(64) CacheLine final
		{
			std::uint64_t value = 0;
		};

		detail::TypedObjectPool<CacheLine> pool(3);
		for (int index = 0; index < 7; ++index)
		{
			CacheLine * object = pool.New();
			ASSERT_NE(object, nullptr);
			EXPECT_EQ(reinterpret_cast<std::uintptr_t>(object) % 64u, 0u) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
				<< "block " << index << " is not on its required alignment";
		}
	}

} // namespace

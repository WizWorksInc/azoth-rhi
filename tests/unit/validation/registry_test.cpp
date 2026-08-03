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

#include "azoth/rhi/validation/registry.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	using rhi::validation::HandleRegistry;
	using rhi::validation::RegisteredHandle;
	using rhi::validation::ResourceRecord;

	[[nodiscard]] std::uint32_t Index(const std::uint32_t tag, const std::uint32_t slot) noexcept
	{
		return rhi::detail::ComposeIndex(tag, slot);
	}

	[[nodiscard]] RegisteredHandle Buffer(const std::uint32_t tag, const std::uint32_t slot, const std::uint32_t generation = 1) noexcept
	{
		return RegisteredHandle{
			.type		= rhi::ResourceType::eBuffer,
			.index		= Index(tag, slot),
			.generation = generation,
		};
	}

	TEST(HandleRegistry, KnowsNothingBeforeAnythingIsRecorded)
	{
		const HandleRegistry registry;

		EXPECT_FALSE(registry.IsLive(Buffer(1, 0)));
		EXPECT_EQ(registry.LiveCount(), 0u);
	}

	TEST(HandleRegistry, RecognizesAHandleItWasToldAbout)
	{
		HandleRegistry registry;
		const RegisteredHandle buffer = Buffer(1, 7);

		ASSERT_TRUE(registry.Record(buffer));
		EXPECT_TRUE(registry.IsLive(buffer));
		EXPECT_EQ(registry.LiveCount(rhi::ResourceType::eBuffer), 1u);
		EXPECT_EQ(registry.LiveCount(), 1u);
	}

	TEST(HandleRegistry, AHandleHeldPastItsDestroyNoLongerResolves)
	{
		HandleRegistry registry;

		const RegisteredHandle first = Buffer(1, 3, 1);
		ASSERT_TRUE(registry.Record(first));
		ASSERT_TRUE(registry.Retire(first));

		EXPECT_FALSE(registry.IsLive(first)) << "a retired handle still resolved";

		const RegisteredHandle reused = Buffer(1, 3, 2);
		ASSERT_TRUE(registry.Record(reused));

		EXPECT_TRUE(registry.IsLive(reused));
		EXPECT_FALSE(registry.IsLive(first)) << "the old handle came back to life when its slot was reused";
	}

	TEST(HandleRegistry, ASecondDestroyOfTheSameHandleIsRefused)
	{
		HandleRegistry registry;
		const RegisteredHandle buffer = Buffer(1, 0);

		ASSERT_TRUE(registry.Record(buffer));
		EXPECT_TRUE(registry.Retire(buffer));
		EXPECT_FALSE(registry.Retire(buffer)) << "a double destroy was accepted, which is the case this exists to catch";
		EXPECT_EQ(registry.LiveCount(), 0u);
	}

	TEST(HandleRegistry, AHandleFromAnotherDeviceIsNotThisOnes)
	{
		HandleRegistry registry;

		ASSERT_TRUE(registry.Record(Buffer(1, 5)));
		EXPECT_TRUE(registry.IsLive(Buffer(1, 5)));
		EXPECT_FALSE(registry.IsLive(Buffer(2, 5))) << "a handle from another device resolved against this one";
		EXPECT_FALSE(registry.Retire(Buffer(2, 5))) << "another device's handle was accepted for destroy";

		EXPECT_TRUE(registry.IsLive(Buffer(1, 5)));
	}

	TEST(HandleRegistry, TwoDevicesSharingARowStayApart)
	{
		HandleRegistry registry;

		ASSERT_TRUE(registry.Record(Buffer(1, 9)));
		ASSERT_TRUE(registry.Record(Buffer(2, 9)));

		EXPECT_FALSE(registry.IsLive(Buffer(1, 9))) << "the second record took the row, so the first is no longer what it names";
		EXPECT_TRUE(registry.IsLive(Buffer(2, 9)));
	}

	TEST(HandleRegistry, TheInvalidHandleIsNeverRecorded)
	{
		HandleRegistry registry;
		EXPECT_FALSE(registry.Record(RegisteredHandle{ .type = rhi::ResourceType::eBuffer }));
		EXPECT_EQ(registry.LiveCount(), 0u);
	}

	TEST(HandleRegistry, KindsAreKeptApart)
	{
		HandleRegistry registry;

		const RegisteredHandle buffer{ .type = rhi::ResourceType::eBuffer, .index = Index(1, 4), .generation = 1 };
		const RegisteredHandle texture{ .type = rhi::ResourceType::eTexture, .index = Index(1, 4), .generation = 1 };

		ASSERT_TRUE(registry.Record(buffer));
		EXPECT_FALSE(registry.IsLive(texture)) << "a buffer answered for a texture of the same index";

		ASSERT_TRUE(registry.Record(texture));
		ASSERT_TRUE(registry.Retire(buffer));
		EXPECT_TRUE(registry.IsLive(texture)) << "retiring a buffer retired the texture beside it";
	}

	TEST(HandleRegistry, HoldsWhatIsKnownAboutALiveResource)
	{
		HandleRegistry registry;
		const RegisteredHandle buffer = Buffer(1, 2);

		ASSERT_TRUE(registry.Record(buffer));

		ResourceRecord * record = registry.Lookup(buffer);
		ASSERT_NE(record, nullptr);

		record->access.store(0x24, std::memory_order_relaxed);
		record->owned.store(true, std::memory_order_relaxed);
		record->owner.store(2, std::memory_order_relaxed);

		const ResourceRecord * again = registry.Lookup(buffer);
		ASSERT_EQ(again, record) << "the same handle resolved to a different record";
		EXPECT_EQ(again->access.load(std::memory_order_relaxed), 0x24u);
		EXPECT_TRUE(again->owned.load(std::memory_order_relaxed));
	}

	TEST(HandleRegistry, ARecycledSlotStartsWithNothingKnownAboutIt)
	{
		HandleRegistry registry;

		const RegisteredHandle first = Buffer(1, 6, 1);
		ASSERT_TRUE(registry.Record(first));
		registry.Lookup(first)->access.store(0x99, std::memory_order_relaxed);
		registry.Lookup(first)->owned.store(true, std::memory_order_relaxed);
		ASSERT_TRUE(registry.Retire(first));

		const RegisteredHandle reused = Buffer(1, 6, 2);
		ASSERT_TRUE(registry.Record(reused));

		const ResourceRecord * record = registry.Lookup(reused);
		ASSERT_NE(record, nullptr);
		EXPECT_EQ(record->access.load(std::memory_order_relaxed), 0u) << "a new resource inherited its predecessor's state";
		EXPECT_FALSE(record->owned.load(std::memory_order_relaxed));
	}

	TEST(HandleRegistry, GrowsAcrossChunksWithoutMovingWhatItAlreadyHandedOut)
	{
		constexpr std::uint32_t kFarPastTheFirstChunk = 5000;

		HandleRegistry registry;

		const RegisteredHandle early = Buffer(1, 0);
		ASSERT_TRUE(registry.Record(early));

		ResourceRecord * held = registry.Lookup(early);
		ASSERT_NE(held, nullptr);
		held->access.store(0x1234, std::memory_order_relaxed);

		for (std::uint32_t slot = 1; slot <= kFarPastTheFirstChunk; ++slot)
		{
			ASSERT_TRUE(registry.Record(Buffer(1, slot))) << "the registry refused to grow at slot " << slot;
		}

		EXPECT_EQ(held->access.load(std::memory_order_relaxed), 0x1234u) << "growth moved a record a caller was already holding";
		EXPECT_EQ(registry.Lookup(early), held) << "the same handle resolved somewhere else after growth";
		EXPECT_EQ(registry.LiveCount(), kFarPastTheFirstChunk + 1u);
	}

	TEST(HandleRegistry, gate_WaitFreeRegistryRead)
	{
		constexpr std::uint32_t kAtLeastRecorded = 4000;
		constexpr std::size_t kAtLeastRead		 = 64;

		HandleRegistry registry;

		const RegisteredHandle held = Buffer(1, 0);
		ASSERT_TRUE(registry.Record(held));
		registry.Lookup(held)->access.store(0xABCD, std::memory_order_relaxed);

		std::atomic<bool> writing{ true };
		std::atomic<std::size_t> reads{ 0 };
		std::atomic<std::size_t> wrongAnswers{ 0 };

		std::thread reader(
			[&]
			{
				while (writing.load(std::memory_order_relaxed))
				{
					const ResourceRecord * record = registry.Lookup(held);
					if (record == nullptr || record->access.load(std::memory_order_relaxed) != 0xABCDu)
					{
						wrongAnswers.fetch_add(1, std::memory_order_relaxed);
					}
					reads.fetch_add(1, std::memory_order_relaxed);
				}
			});

		constexpr std::uint32_t kSlotCeiling = 1u << 20;

		std::uint32_t slot = 1;
		while (slot < kSlotCeiling && (slot <= kAtLeastRecorded || reads.load(std::memory_order_relaxed) < kAtLeastRead))
		{
			ASSERT_TRUE(registry.Record(Buffer(1, slot)));
			++slot;
		}

		writing.store(false, std::memory_order_relaxed);
		reader.join();

		EXPECT_GE(reads.load(std::memory_order_relaxed), kAtLeastRead) << "the reader never ran alongside the writer, so this proved nothing";
		EXPECT_EQ(wrongAnswers.load(std::memory_order_relaxed), 0u) << "a read got the wrong answer while the registry was growing";
	}

	TEST(HandleRegistry, CountsWhatIsStillLivePerKind)
	{
		HandleRegistry registry;

		for (std::uint32_t slot = 0; slot < 8; ++slot)
		{
			ASSERT_TRUE(registry.Record(Buffer(1, slot)));
			ASSERT_TRUE(registry.Record(RegisteredHandle{ .type = rhi::ResourceType::eTexture, .index = Index(1, slot), .generation = 1 }));
		}

		EXPECT_EQ(registry.LiveCount(rhi::ResourceType::eBuffer), 8u);
		EXPECT_EQ(registry.LiveCount(rhi::ResourceType::eTexture), 8u);
		EXPECT_EQ(registry.LiveCount(), 16u);

		for (std::uint32_t slot = 0; slot < 8; ++slot)
		{
			ASSERT_TRUE(registry.Retire(Buffer(1, slot)));
		}

		EXPECT_EQ(registry.LiveCount(rhi::ResourceType::eBuffer), 0u);
		EXPECT_EQ(registry.LiveCount(), 8u) << "retiring the buffers took the textures with them";
	}

} // namespace

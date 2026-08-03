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

#include "conformance/block_contract.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace azo::rhi::test::oracle
{
	namespace
	{

		constexpr int kConcurrentQueryThreads = 8;

		constexpr int kConcurrentQueriesPerThread = 64;

		[[nodiscard]] const BackendObject * Published(void * impl)
		{
			return detail::ObjectOf(impl);
		}

		template <typename Block>
		[[nodiscard]] const void * QueryOnce(void * impl)
		{
			const BackendObject * object = Published(impl);
			if (object == nullptr || object->queryInterface == nullptr)
			{
				return nullptr;
			}
			return object->queryInterface(impl, InterfaceTraits<Block>::kId, InterfaceTraits<Block>::kVersion);
		}

		template <typename Block>
		void ExpectAnswersTheSameWayTwice(void * impl, const char * what)
		{
			const BackendObject * object = Published(impl);
			ASSERT_NE(object, nullptr) << what << " has no interface entry point in its first word";
			ASSERT_NE(object->queryInterface, nullptr) << what << " left its interface entry point null";

			constexpr InterfaceId id		 = InterfaceTraits<Block>::kId;
			constexpr std::uint32_t kVersion = InterfaceTraits<Block>::kVersion;

			const void * first	= object->queryInterface(impl, id, kVersion);
			const void * second = object->queryInterface(impl, id, kVersion);
			ASSERT_NE(first, nullptr) << what << " publishes no block the RHI can drive it through";
			EXPECT_EQ(first, second) << what << " answered with a different block the second time, which the cached pointer would never see";

			EXPECT_EQ(first, detail::QueryBlock<Block>(impl)) << what << " resolves to a different block through the RHI than through its own entry";

			const auto * block = static_cast<const Block *>(first);
			EXPECT_EQ(block->header.byteSize, sizeof(Block)) << what << " declared a size other than the layout it published";
			EXPECT_GE(block->header.version, 1u) << what << " published a block with no version";

			EXPECT_EQ(object->queryInterface(impl, MakeInterfaceId("azoth.rhi.block.nobodyMintedThis"), 1), nullptr)
				<< what << " answered for a block it does not implement";
			EXPECT_EQ(object->queryInterface(impl, id, kVersion + 1000), nullptr) << what << " handed back an older block than the caller asked for";
		}

		template <typename Block>
		void ExpectAnswersTheSameWayConcurrently(void * impl, const char * what)
		{
			const void * expected = QueryOnce<Block>(impl);
			ASSERT_NE(expected, nullptr) << what << " publishes no block to ask for concurrently";

			std::atomic<bool> start{ false };
			std::vector<const void *> seen(kConcurrentQueryThreads, nullptr);
			std::vector<std::thread> threads;
			threads.reserve(kConcurrentQueryThreads);

			for (int index = 0; index < kConcurrentQueryThreads; ++index)
			{
				threads.emplace_back(
					[&, index]
					{
						while (!start.load(std::memory_order_acquire))
						{
							std::this_thread::yield();
						}

						const void * mine = QueryOnce<Block>(impl);
						for (int repeat = 1; repeat < kConcurrentQueriesPerThread; ++repeat)
						{
							if (QueryOnce<Block>(impl) != mine)
							{
								mine = nullptr;
								break;
							}
						}
						seen[static_cast<std::size_t>(index)] = mine;
					});
			}

			start.store(true, std::memory_order_release);
			for (std::thread & thread : threads)
			{
				thread.join();
			}

			for (int index = 0; index < kConcurrentQueryThreads; ++index)
			{
				EXPECT_EQ(seen[static_cast<std::size_t>(index)], expected)
					<< what << " answered thread " << index << " with a block other than the one it answers on its own, so the pointer a per thread facade "
					<< "cached depends on which thread built it";
			}
		}

		void ExpectCallableForTheWholeLife(Device device)
		{
			Error error{};

			CommandPool pool = device.CreateCommandPool(samples::CommandPool(QueueType::eGraphics), error);
			ASSERT_TRUE(Ok(pool.IsValid(), error));

			void * poolImpl			= detail::FacadeBuilder::ImplOf(pool);
			const void * atCreation = QueryOnce<CommandPoolApi>(poolImpl);
			ASSERT_NE(atCreation, nullptr) << "a command pool published no block when it was new";

			CommandList list = pool.Allocate("azoth.rhi.conformance.blockLifetime", error);
			ASSERT_TRUE(Ok(list.IsValid(), error));
			EXPECT_TRUE(Ok(list.Begin(error), error));
			EXPECT_TRUE(Ok(list.End(error), error));

			EXPECT_EQ(QueryOnce<CommandPoolApi>(poolImpl), atCreation) << "a command pool answered differently once it had been used, and a facade built "
																		  "after that work would hold a pointer the first one never saw";
		}

	} // namespace

	void CheckRequiredBlocks(Device device)
	{
		void * deviceImpl = detail::FacadeBuilder::ImplOf(device);
		ASSERT_NE(deviceImpl, nullptr) << "a valid device resolved to no implementation";

		EXPECT_NE(QueryOnce<CoreDeviceApi>(deviceImpl), nullptr) << "the device publishes no CoreDeviceApi, which is the one block it cannot be driven "
																	"without";

		Error error{};

		Queue queue = device.GetQueue(QueueType::eGraphics, 0, error);
		ASSERT_TRUE(Ok(queue.IsValid(), error));
		EXPECT_NE(QueryOnce<QueueApi>(detail::FacadeBuilder::ImplOf(queue)), nullptr) << "a queue publishes no QueueApi, so nothing can be submitted to it";

		CommandPool pool = device.CreateCommandPool(samples::CommandPool(QueueType::eGraphics), error);
		ASSERT_TRUE(Ok(pool.IsValid(), error));
		EXPECT_NE(QueryOnce<CommandPoolApi>(detail::FacadeBuilder::ImplOf(pool)), nullptr) << "a command pool publishes no CommandPoolApi, so it can hand "
																							  "out nothing";

		CommandList list = pool.Allocate("azoth.rhi.conformance.requiredBlocks", error);
		ASSERT_TRUE(Ok(list.IsValid(), error));
		EXPECT_NE(QueryOnce<RenderCommandApi>(detail::FacadeBuilder::ImplOf(list)), nullptr) << "a command list publishes no RenderCommandApi, so nothing "
																								"can be recorded into it";

		DescriptorArena arena = device.CreateDescriptorArena(samples::DescriptorArena(), error);
		ASSERT_TRUE(Ok(arena.IsValid(), error));
		EXPECT_NE(QueryOnce<DescriptorArenaApi>(detail::FacadeBuilder::ImplOf(arena)), nullptr) << "a descriptor arena publishes no DescriptorArenaApi, so "
																								   "no set can be allocated from it";
	}

	void CheckCapsFollowBlocks(Device device)
	{
		void * deviceImpl		= detail::FacadeBuilder::ImplOf(device);
		const DeviceCaps & caps = device.GetCaps();

		const bool present		 = QueryOnce<PresentApi>(deviceImpl) != nullptr;
		const bool placedMemory	 = QueryOnce<PlacedMemoryApi>(deviceImpl) != nullptr;
		const bool pipelineCache = QueryOnce<PipelineCacheApi>(deviceImpl) != nullptr;
		const bool residency	 = QueryOnce<ResidencyApi>(deviceImpl) != nullptr;
		const bool rayTracing	 = QueryOnce<RayTracingApi>(deviceImpl) != nullptr;
		const bool query		 = QueryOnce<QueryApi>(deviceImpl) != nullptr;

		EXPECT_EQ(caps.supportsSurfaces, present) << "supportsSurfaces disagrees with whether the device published PresentApi";
		EXPECT_EQ(caps.supportsPlacedResources, placedMemory) << "supportsPlacedResources disagrees with whether the device published PlacedMemoryApi";
		EXPECT_EQ(caps.supportsResourceAdoption, QueryOnce<AdoptionApi>(deviceImpl) != nullptr)
			<< "supportsResourceAdoption disagrees with whether the device published AdoptionApi";
		EXPECT_EQ(caps.supportsPipelineCache, pipelineCache) << "supportsPipelineCache disagrees with whether the device published PipelineCacheApi";
		EXPECT_EQ(caps.supportsMemoryBudget, residency) << "supportsMemoryBudget disagrees with whether the device published ResidencyApi";

		if (!caps.supportsScaledBlit)
		{
			for (std::size_t raw = 0; raw < kFormatCount; ++raw)
			{
				const FormatSupport support = device.GetFormatSupport(static_cast<Format>(raw));
				EXPECT_FALSE(support.blitSrc || support.blitDst) << "a format reports blit support on a device reporting no scaled blit";
			}
		}

		if (caps.supportsRayTracing)
		{
			EXPECT_TRUE(rayTracing) << "supportsRayTracing is set on a device that published no RayTracingApi";
		}
		if (caps.supportsTimestampQueries)
		{
			EXPECT_TRUE(query) << "supportsTimestampQueries is set on a device that published no QueryApi";
		}

		EXPECT_FALSE(caps.supportsTimestampWritesInScope && !caps.supportsTimestampQueries)
			<< "the device allows timestamp writes inside a scope while reporting no timestamp queries";

		EXPECT_FALSE(caps.supportsSurfaces && !present) << "the device claims surfaces with no block to present through";
	}

	void CheckBlockStability(Device device)
	{
		ExpectAnswersTheSameWayTwice<CoreDeviceApi>(detail::FacadeBuilder::ImplOf(device), "the device");

		Error error{};

		Queue queue = device.GetQueue(QueueType::eGraphics, 0, error);
		ASSERT_TRUE(Ok(queue.IsValid(), error));
		ExpectAnswersTheSameWayTwice<QueueApi>(detail::FacadeBuilder::ImplOf(queue), "a queue");

		CommandPool pool = device.CreateCommandPool(samples::CommandPool(QueueType::eGraphics), error);
		ASSERT_TRUE(Ok(pool.IsValid(), error));
		ExpectAnswersTheSameWayTwice<CommandPoolApi>(detail::FacadeBuilder::ImplOf(pool), "a command pool");

		CommandList list = pool.Allocate("azoth.rhi.conformance.blockStability", error);
		ASSERT_TRUE(Ok(list.IsValid(), error));
		ExpectAnswersTheSameWayTwice<RenderCommandApi>(detail::FacadeBuilder::ImplOf(list), "a command list");

		DescriptorArena arena = device.CreateDescriptorArena(samples::DescriptorArena(), error);
		ASSERT_TRUE(Ok(arena.IsValid(), error));
		ExpectAnswersTheSameWayTwice<DescriptorArenaApi>(detail::FacadeBuilder::ImplOf(arena), "a descriptor arena");

		ExpectCallableForTheWholeLife(device);
	}

	void CheckConcurrentBlockQuery(Device device)
	{
		ExpectAnswersTheSameWayConcurrently<CoreDeviceApi>(detail::FacadeBuilder::ImplOf(device), "the device");

		Error error{};

		Queue queue = device.GetQueue(QueueType::eGraphics, 0, error);
		ASSERT_TRUE(Ok(queue.IsValid(), error));
		ExpectAnswersTheSameWayConcurrently<QueueApi>(detail::FacadeBuilder::ImplOf(queue), "a queue");

		CommandPool pool = device.CreateCommandPool(samples::CommandPool(QueueType::eGraphics), error);
		ASSERT_TRUE(Ok(pool.IsValid(), error));
		ExpectAnswersTheSameWayConcurrently<CommandPoolApi>(detail::FacadeBuilder::ImplOf(pool), "a command pool");

		CommandList list = pool.Allocate("azoth.rhi.conformance.concurrentQuery", error);
		ASSERT_TRUE(Ok(list.IsValid(), error));
		ExpectAnswersTheSameWayConcurrently<RenderCommandApi>(detail::FacadeBuilder::ImplOf(list), "a command list");
	}

} // namespace azo::rhi::test::oracle

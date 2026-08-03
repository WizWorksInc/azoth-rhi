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

#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"
#include "harness/spies.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class RecycleTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] rhi::DeviceDesc MakeDeviceDesc() const override
		{
			return test::DefaultDeviceDesc();
		}
	};

	AZO_RHI_BACKEND_SUITE(RecycleTest);

	TEST_P(RecycleTest, NeverReissuesALiveHandleAcrossManyCreateAndDestroyRounds)
	{
		const std::uint32_t rounds = test::ScaledIterations(2000);

		rhi::Error error{};
		std::unordered_set<std::uint64_t> issued;
		issued.reserve(rounds);

		for (std::uint32_t round = 0; round < rounds; ++round)
		{
			const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(buffer.IsValid(), error)) << "round " << round << " could not allocate";

			const auto key = (static_cast<std::uint64_t>(buffer.index) << 32u) | buffer.generation;
			ASSERT_TRUE(issued.insert(key).second) << "round " << round << " reissued a handle that had already been handed out";

			ASSERT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error)) << "round " << round << " could not release";
		}

		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(error), error));
	}

	TEST_P(RecycleTest, HoldsManyResourcesLiveAtOnceAndReleasesThemAll)
	{
		const std::uint32_t count = test::ScaledIterations(4000);

		rhi::Error error{};
		std::vector<rhi::BufferHandle> buffers;
		buffers.reserve(count);

		for (std::uint32_t index = 0; index < count; ++index)
		{
			const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(64), error);
			ASSERT_TRUE(test::Ok(buffer.IsValid(), error)) << "allocation " << index << " of " << count << " failed";
			buffers.push_back(buffer);
		}

		for (std::size_t index = buffers.size(); index > 0; --index)
		{
			ASSERT_TRUE(test::Ok(Dev().Destroy(buffers[index - 1], {}, error), error)) << "release " << (index - 1) << " failed";
		}

		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(error), error));
	}

	TEST_P(RecycleTest, RecyclesTexturesAndViewsTogetherWithoutLosingOrder)
	{
		const std::uint32_t rounds = test::ScaledIterations(500);

		rhi::Error error{};
		for (std::uint32_t round = 0; round < rounds; ++round)
		{
			const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(16), error);
			ASSERT_TRUE(test::Ok(texture.IsValid(), error)) << "round " << round;

			const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, test::samples::FullTextureView(), error);
			ASSERT_TRUE(test::Ok(view.IsValid(), error)) << "round " << round;

			ASSERT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error)) << "round " << round;
			ASSERT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error)) << "round " << round;
		}

		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(error), error));
	}

	TEST_P(RecycleTest, RecordsAndResetsAPoolManyTimesOver)
	{
		const std::uint32_t frames = test::ScaledIterations(500);

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		for (std::uint32_t frame = 0; frame < frames; ++frame)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.recycleList", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error)) << "frame " << frame;
			ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "frame " << frame;
			ASSERT_TRUE(test::Ok(list.End(error), error)) << "frame " << frame;
			ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error)) << "frame " << frame;
		}
	}

	/*
	 * The same loop, counted. A pool takes its lists back at Reset and hands the same ones out again, so once the first pass has built what the loop needs,
	 * every pass after it holds exactly that much.
	 *
	 * Counted from the host allocator and not from anything a layer reports about itself, since this is the shape a leak hides in: an allocate that answers
	 * with a new object every time still passes every check above.
	 */
	TEST_P(RecycleTest, HoldsHostMemoryFlatAcrossAFrameLoop)
	{
		const std::uint32_t frames = test::ScaledIterations(200);

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		const auto runFrames = [&](const std::uint32_t count)
		{
			for (std::uint32_t frame = 0; frame < count; ++frame)
			{
				rhi::CommandList list = pool.Allocate("azoth.rhi.test.flatList", error);
				ASSERT_TRUE(test::Ok(list.IsValid(), error)) << "frame " << frame;
				ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "frame " << frame;
				ASSERT_TRUE(test::Ok(list.End(error), error)) << "frame " << frame;
				ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error)) << "frame " << frame;
			}
		};

		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		// Blocks this allocator handed out, so storage the device took before it was installed is not counted and cannot make the second pass read low.
		runFrames(frames);
		const std::size_t settled = allocator.LiveBlocks();

		runFrames(frames);
		EXPECT_EQ(allocator.LiveBlocks(), settled) << "the frame loop kept host storage it never gave back, over " << frames << " frames";
	}

	TEST_P(RecycleTest, CreatesResourcesFromSeveralThreadsAtOnce)
	{
		constexpr int kThreads		  = 8;
		const std::uint32_t perThread = test::ScaledIterations(250);

		std::vector<std::vector<rhi::BufferHandle>> perThreadHandles(kThreads);
		std::atomic<int> ready{ 0 };
		std::atomic<bool> anyFailure{ false };

		std::vector<std::thread> workers;
		workers.reserve(kThreads);
		for (int worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&, worker]
				{
					perThreadHandles[worker].reserve(perThread);

					ready.fetch_add(1, std::memory_order_release);
					while (ready.load(std::memory_order_acquire) < kThreads)
					{
						std::this_thread::yield();
					}

					rhi::Error error{};
					for (std::uint32_t index = 0; index < perThread; ++index)
					{
						const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(64), error);
						if (!buffer.IsValid())
						{
							anyFailure.store(true, std::memory_order_relaxed);
							return;
						}
						perThreadHandles[worker].push_back(buffer);
					}
				});
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		ASSERT_FALSE(anyFailure.load()) << "a concurrent creation failed";

		std::unordered_set<std::uint64_t> seen;
		seen.reserve(static_cast<std::size_t>(kThreads) * perThread);
		for (const std::vector<rhi::BufferHandle> & handles : perThreadHandles)
		{
			for (const rhi::BufferHandle handle : handles)
			{
				const auto key = (static_cast<std::uint64_t>(handle.index) << 32u) | handle.generation;
				ASSERT_TRUE(seen.insert(key).second) << "two threads were handed the same buffer handle";
			}
		}

		rhi::Error error{};
		for (const std::vector<rhi::BufferHandle> & handles : perThreadHandles)
		{
			for (const rhi::BufferHandle handle : handles)
			{
				ASSERT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
			}
		}
		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(error), error));
	}

	TEST_P(RecycleTest, RecordsOnSeveralThreadsEachOwningItsOwnPool)
	{
		constexpr int kThreads	   = 4;
		const std::uint32_t frames = test::ScaledIterations(100);

		rhi::Error setupError{};
		std::vector<rhi::CommandPool> pools;
		pools.reserve(kThreads);
		for (int worker = 0; worker < kThreads; ++worker)
		{
			rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), setupError);
			ASSERT_TRUE(test::Ok(pool.IsValid(), setupError)) << "could not create the pool for worker " << worker;
			pools.push_back(pool);
		}

		std::atomic<int> ready{ 0 };
		std::atomic<int> failures{ 0 };
		std::vector<std::thread> workers;
		workers.reserve(kThreads);

		for (int worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&, worker]
				{
					rhi::CommandPool & pool = pools[static_cast<std::size_t>(worker)];

					ready.fetch_add(1, std::memory_order_release);
					while (ready.load(std::memory_order_acquire) < kThreads)
					{
						std::this_thread::yield();
					}

					rhi::Error error{};
					for (std::uint32_t frame = 0; frame < frames; ++frame)
					{
						rhi::CommandList list = pool.Allocate("azoth.rhi.test.threadList", error);
						if (!list.IsValid() || !list.Begin(error) || !list.End(error) || !pool.Reset(rhi::RetirePoint{}, error))
						{
							failures.fetch_add(1, std::memory_order_relaxed);
							return;
						}
					}
				});
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		EXPECT_EQ(failures.load(), 0) << "concurrent recording on separate pools failed";
	}

	TEST_P(RecycleTest, StaysValidationCleanAcrossTheWholeRun)
	{
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "the recycle run produced native validation errors");
	}

} // namespace

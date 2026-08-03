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

#include "azoth/rhi/backend/support/spin_lock.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	TEST(SpinLock, ModelsTheLockableShapeTheStandardLibraryNeeds)
	{
		static_assert(requires(rhi::SpinLock lock) {
			lock.lock();
			{ lock.try_lock() } -> std::same_as<bool>;
			lock.unlock();
		});

		SUCCEED();
	}

	TEST(SpinLock, AcquiresAndReleases)
	{
		rhi::SpinLock lock;

		ASSERT_TRUE(lock.try_lock());
		lock.unlock();

		lock.lock();
		lock.unlock();

		EXPECT_TRUE(lock.try_lock());
		lock.unlock();
	}

	TEST(SpinLock, TryLockFailsWhileHeld)
	{
		rhi::SpinLock lock;
		lock.lock();

		EXPECT_FALSE(lock.try_lock()) << "try_lock succeeded on a held lock, which would hand two owners the same critical section";

		lock.unlock();
		EXPECT_TRUE(lock.try_lock());
		lock.unlock();
	}

	TEST(SpinLock, WorksUnderScopedLock)
	{
		rhi::SpinLock lock;
		{
			const std::scoped_lock guard(lock);
			EXPECT_FALSE(lock.try_lock());
		}
		EXPECT_TRUE(lock.try_lock());
		lock.unlock();
	}

	TEST(SpinLock, SerializesConcurrentIncrements)
	{
		// A plain counter and not an atomic one on purpose: the read-modify-write is only safe because the lock made it so and a broken lock shows up
		// as a lost update.
		constexpr int kThreads	 = 8;
		constexpr int kPerThread = 4096;

		rhi::SpinLock lock;
		std::uint64_t counter = 0;
		std::atomic<int> ready{ 0 };

		std::vector<std::thread> workers;
		workers.reserve(kThreads);
		for (int worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&]
				{
					ready.fetch_add(1, std::memory_order_release);
					while (ready.load(std::memory_order_acquire) < kThreads)
					{
						rhi::CpuSpinHint();
					}

					for (int index = 0; index < kPerThread; ++index)
					{
						const std::scoped_lock guard(lock);
						++counter;
					}
				});
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		EXPECT_EQ(counter, static_cast<std::uint64_t>(kThreads) * kPerThread);
	}

	TEST(SpinLock, ReleaseIsVisibleToTheNextAcquirer)
	{
		rhi::SpinLock lock;
		int guarded = 0;
		std::atomic<bool> written{ false };

		lock.lock();
		std::thread reader(
			[&]
			{
				while (!written.load(std::memory_order_acquire))
				{
					rhi::CpuSpinHint();
				}
				const std::scoped_lock guard(lock);
				EXPECT_EQ(guarded, 1234);
			});

		guarded = 1234;
		written.store(true, std::memory_order_release);
		lock.unlock();

		reader.join();
	}

	TEST(CpuSpinHint, IsCallableAndHasNoObservableEffect)
	{
		for (int index = 0; index < 16; ++index)
		{
			rhi::CpuSpinHint();
		}
		SUCCEED();
	}

} // namespace

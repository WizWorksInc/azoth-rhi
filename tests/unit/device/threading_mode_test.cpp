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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/threading.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/spies.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <semaphore>
#include <string_view>
#include <thread>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

using test::Scheduler;

namespace
{

	class ThreadingModeTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ThreadingModeTest);

	struct ThreadAffineScheduler final
	{
		std::binary_semaphore gate{ 1 };
		std::atomic<std::thread::id> owner;
		std::atomic<std::size_t> affinityViolations{ 0 };

		[[nodiscard]] rhi::SyncOps Ops() noexcept
		{
			return rhi::SyncOps{
				.context = this,
				.create =
					[](void * context) noexcept
				{
					return context;
				},
				.destroy = [](void *, void *) noexcept {},
				.acquire =
					[](void * context, void *) noexcept
				{
					auto * self = static_cast<ThreadAffineScheduler *>(context);
					self->gate.acquire();
					self->owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
				},
				.tryAcquire =
					[](void * context, void *) noexcept
				{
					auto * self = static_cast<ThreadAffineScheduler *>(context);
					if (!self->gate.try_acquire())
					{
						return false;
					}

					self->owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
					return true;
				},
				.release =
					[](void * context, void *) noexcept
				{
					auto * self = static_cast<ThreadAffineScheduler *>(context);
					if (self->owner.load(std::memory_order_relaxed) != std::this_thread::get_id())
					{
						self->affinityViolations.fetch_add(1, std::memory_order_relaxed);
					}

					self->gate.release();
				},
				.currentFiber =
					[](void *) noexcept
				{
					return rhi::FiberId{ 0 };
				},
			};
		}
	};

	TEST(ThreadingMode, ADeviceIsDrivenByThreadsUnlessItSaysOtherwise)
	{
		EXPECT_EQ(rhi::DeviceDesc{}.threading, rhi::ThreadingMode::eThreads);
		EXPECT_FALSE(rhi::DeviceDesc{}.sync.IsComplete()) << "a device that asked for nothing should carry no operations";
	}

	TEST(ThreadingMode, TheBuilderSetsTheModeAndTheOperationsTogether)
	{
		Scheduler scheduler;

		const rhi::Result<rhi::UniqueDevice> cooperative = rhi::DeviceBuilder{}.RequireSwapchain(false).Cooperative(scheduler.Ops()).Build<rhi::NullApi>();
		EXPECT_TRUE(test::Ok(cooperative)) << "Cooperative did not set the mode alongside the operations";

		const rhi::Result<rhi::UniqueDevice> halfSet =
			rhi::DeviceBuilder{}.RequireSwapchain(false).Threading(rhi::ThreadingMode::eCooperative).Build<rhi::NullApi>();
		EXPECT_FALSE(halfSet.HasValue()) << "the mode alone was taken for a cooperative device";
		EXPECT_EQ(halfSet.GetError().code, rhi::ErrorCode::eInvalidArgument);

		const rhi::Result<rhi::UniqueDevice> single =
			rhi::DeviceBuilder{}.RequireSwapchain(false).Threading(rhi::ThreadingMode::eSingleThreaded).Build<rhi::NullApi>();
		EXPECT_TRUE(test::Ok(single));
	}

	TEST(SyncOps, APartlyFilledSetIsNotAMode)
	{
		Scheduler scheduler;

		rhi::SyncOps ops = scheduler.Ops();
		EXPECT_TRUE(ops.IsComplete());

		ops.release = nullptr;
		EXPECT_FALSE(ops.IsComplete()) << "a set with no release passed as complete, and release is the one that has to cross a fiber migration";

		EXPECT_FALSE(rhi::SyncOps{}.IsComplete());

		rhi::SyncOps withoutProfiling = scheduler.Ops();
		withoutProfiling.currentFiber = nullptr;
		EXPECT_TRUE(withoutProfiling.IsComplete());
	}

	TEST_P(ThreadingModeTest, ACooperativeDeviceWithoutOperationsIsRefusedByName)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_FALSE(device.IsValid()) << "a cooperative device came up with no operations to guard with";
		EXPECT_EQ(device.GetError().code, rhi::ErrorCode::eInvalidArgument);
		EXPECT_TRUE(test::ErrorIsPopulated(device.GetError()));
	}

	TEST_P(ThreadingModeTest, EveryModeStillCreatesADevice)
	{
		Scheduler scheduler;

		for (const rhi::ThreadingMode mode : { rhi::ThreadingMode::eSingleThreaded, rhi::ThreadingMode::eThreads, rhi::ThreadingMode::eCooperative })
		{
			rhi::DeviceDesc desc = test::DefaultDeviceDesc();
			desc.threading		 = mode;
			if (mode == rhi::ThreadingMode::eCooperative)
			{
				desc.sync = scheduler.Ops();
			}

			const test::DeviceHarness device{ CurrentBackend(), desc };
			EXPECT_TRUE(test::Ok(device.IsValid(), device.GetError())) << "mode " << static_cast<int>(mode);
		}
	}

	TEST_P(ThreadingModeTest, gate_TwoSchedulersOneProcess)
	{
		Scheduler first;
		Scheduler second;

		rhi::DeviceDesc firstDesc = test::DefaultDeviceDesc();
		firstDesc.threading		  = rhi::ThreadingMode::eCooperative;
		firstDesc.sync			  = first.Ops();

		rhi::DeviceDesc secondDesc = test::DefaultDeviceDesc();
		secondDesc.threading	   = rhi::ThreadingMode::eCooperative;
		secondDesc.sync			   = second.Ops();

		const test::DeviceHarness firstDevice{ CurrentBackend(), firstDesc };
		const test::DeviceHarness secondDevice{ CurrentBackend(), secondDesc };

		ASSERT_TRUE(test::Ok(firstDevice.IsValid(), firstDevice.GetError()));
		ASSERT_TRUE(test::Ok(secondDevice.IsValid(), secondDevice.GetError()));

		EXPECT_NE(firstDesc.sync.context, secondDesc.sync.context);
		EXPECT_EQ(firstDesc.sync.context, &first);
		EXPECT_EQ(secondDesc.sync.context, &second);
	}

	TEST_P(ThreadingModeTest, gate_CooperativeSuite)
	{
		Scheduler scheduler;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();

		{
			const test::DeviceHarness device{ CurrentBackend(), desc };
			ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

			EXPECT_GT(scheduler.created.load(std::memory_order_relaxed), 0u) << "the device never asked the host for a lock";

			const std::size_t before = scheduler.acquired.load(std::memory_order_relaxed);

			rhi::Error error{};
			const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
			EXPECT_GT(scheduler.acquired.load(std::memory_order_relaxed), before) << "a create went through without taking the host's lock";

			EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		}

		EXPECT_EQ(scheduler.destroyed.load(std::memory_order_relaxed), scheduler.created.load(std::memory_order_relaxed))
			<< "the device kept locks the host handed it";
	}

	TEST_P(ThreadingModeTest, ACollectTakesEveryGuardInTurnRatherThanAllOfThemAtOnce)
	{
		Scheduler scheduler;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::TimelineHandle timeline = device.Get().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer,
								 rhi::DestroyDesc{
									 .policy	= rhi::DestroyPolicy::eDeferUntilSafe,
									 .safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 0 },
								 },
								 error),
			error));

		const std::size_t before = scheduler.acquired.load(std::memory_order_relaxed);
		EXPECT_TRUE(test::Ok(device.Get().CollectGarbage(error), error));

		EXPECT_GT(scheduler.acquired.load(std::memory_order_relaxed), before) << "a collect drained a queue without taking the guard that queue is under";
		EXPECT_EQ(scheduler.held.load(std::memory_order_relaxed), 0u) << "the collect gave a lock back fewer times than it took one";
		EXPECT_EQ(scheduler.peakHeld.load(std::memory_order_relaxed), 1u) << "a guard was taken while another was still held";

		EXPECT_TRUE(test::Ok(device.Get().Destroy(timeline, {}, error), error));
	}

#ifdef AZOTH_RHI_ENABLE_PROFILING

	TEST_P(ThreadingModeTest, gate_FiberZoneAttribution)
	{
		constexpr std::uint64_t kFiber = 0x5eed;

		Scheduler scheduler;
		scheduler.fiber.store(kFiber, std::memory_order_relaxed);

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		test::RecordingProfiler sink;
		const test::ScopedProfiler scope(&sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		ASSERT_FALSE(sink.fiberEvents.empty()) << "a cooperative wait told the profiler nothing, so a zone across it lands on whichever worker resumed";
		ASSERT_EQ(sink.fiberEvents.size() % 2, 0u) << "a fiber was left without being entered again, or entered without being left";

		for (std::size_t i = 0; i + 1 < sink.fiberEvents.size(); i += 2)
		{
			EXPECT_FALSE(sink.fiberEvents[i].entered) << "the fiber was entered before the wait, not left";
			EXPECT_TRUE(sink.fiberEvents[i + 1].entered) << "the wait returned without the fiber being entered again";
			EXPECT_EQ(sink.fiberEvents[i].fiber, rhi::FiberId{ kFiber });
			EXPECT_EQ(sink.fiberEvents[i + 1].fiber, rhi::FiberId{ kFiber });

			EXPECT_FALSE(sink.fiberEvents[i + 1].named) << "the RHI invented a name for a fiber it only knows the identity of";
		}

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

	TEST_P(ThreadingModeTest, ADeviceReportsToItsOwnProfilerRatherThanTheProcessOne)
	{
		constexpr std::uint64_t kFiber = 0xd00d;

		Scheduler scheduler;
		scheduler.fiber.store(kFiber, std::memory_order_relaxed);

		test::RecordingProfiler mine;
		test::RecordingProfiler processWide;
		const rhi::ScopedProfiler installed(&processWide);

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();
		desc.profiler		 = &mine;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_FALSE(mine.fiberEvents.empty()) << "a suspension went somewhere other than the sink this device was given";
		EXPECT_TRUE(processWide.fiberEvents.empty()) << "a device with its own sink still reported to the process one";

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

	TEST_P(ThreadingModeTest, AHostThatNamesNoFiberIsAskedForNone)
	{
		Scheduler scheduler;

		rhi::DeviceDesc desc   = test::DefaultDeviceDesc();
		desc.threading		   = rhi::ThreadingMode::eCooperative;
		desc.sync			   = scheduler.Ops();
		desc.sync.currentFiber = nullptr;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		test::RecordingProfiler sink;
		const test::ScopedProfiler scope(&sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_TRUE(sink.fiberEvents.empty()) << "a host that reports no fiber was told about one anyway";

		EXPECT_GT(sink.beginZoneCalls, 0);

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

#else

	TEST_P(ThreadingModeTest, CompiledOutProfilingRaisesNoFiberTransitions)
	{
		Scheduler scheduler;
		scheduler.fiber.store(0x5eed, std::memory_order_relaxed);

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		test::RecordingProfiler sink;
		const test::ScopedProfiler scope(&sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_TRUE(sink.fiberEvents.empty()) << "a fiber transition was raised with AZOTH_RHI_ENABLE_PROFILING off";

		EXPECT_GT(scheduler.acquired.load(std::memory_order_relaxed), 0u);

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

#endif

	TEST_P(ThreadingModeTest, gate_SingleThreadedTakesNoLock)
	{
		Scheduler scheduler;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eSingleThreaded;
		desc.sync			 = scheduler.Ops();

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_EQ(scheduler.created.load(std::memory_order_relaxed), 0u) << "a single threaded device built locks it will never take";
		EXPECT_EQ(scheduler.acquired.load(std::memory_order_relaxed), 0u) << "a single threaded device took a lock";

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

	TEST(ThreadingMode, gate_FiberMigrationSafety)
	{
		{
			Scheduler migrating;
			rhi::SyncOps ops		 = migrating.Ops();
			rhi::Profiler * profiler = nullptr;

			rhi::DeviceLock guard;
			ASSERT_TRUE(guard.Bind(rhi::ThreadingMode::eCooperative, &ops, &profiler));

			guard.lock();
			const std::thread::id acquiredOn = std::this_thread::get_id();

			std::thread::id releasedOn{};
			std::thread mover(
				[&]
				{
					releasedOn = std::this_thread::get_id();
					guard.unlock();
				});
			mover.join();

			EXPECT_NE(releasedOn, acquiredOn) << "the release did not actually happen on another thread, so nothing migrated";
			EXPECT_EQ(migrating.held.load(std::memory_order_relaxed), 0u) << "the guard did not come back after a cross-thread release";

			std::atomic<bool> retaken{ false };
			std::thread next(
				[&]
				{
					guard.lock();
					retaken.store(true, std::memory_order_relaxed);
					guard.unlock();
				});

			const auto givenUp = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!retaken.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < givenUp)
			{
				std::this_thread::yield();
			}

			EXPECT_TRUE(retaken.load(std::memory_order_relaxed)) << "the guard could not be taken again after a release from another thread";
			next.join();
			guard.Release();
		}

		{
			ThreadAffineScheduler affine;
			rhi::SyncOps ops		 = affine.Ops();
			rhi::Profiler * profiler = nullptr;

			rhi::DeviceLock guard;
			ASSERT_TRUE(guard.Bind(rhi::ThreadingMode::eCooperative, &ops, &profiler));

			guard.lock();
			std::thread mover(
				[&]
				{
					guard.unlock();
				});
			mover.join();

			EXPECT_EQ(affine.affinityViolations.load(std::memory_order_relaxed), 1u)
				<< "a host whose release only works on the acquiring thread went unnoticed, which is the mistake this rules out";

			guard.Release();
		}
	}

	enum class Answered : std::uint8_t
	{
		eByANamedGate,

		eHere,

		eCallerObligation,
	};

	struct ThreadingRule final
	{
		std::string_view surface;
		Answered answered;
		std::string_view by;
	};

	constexpr std::array kThreadingModel{
		ThreadingRule{ "Command recording", Answered::eHere, "recording takes no host lock in any mode" },
		ThreadingRule{ "CommandList", Answered::eHere, "one list per thread, recorded concurrently" },
		ThreadingRule{ "CommandPool", Answered::eHere, "one pool per thread, allocated from concurrently" },
		ThreadingRule{ "Queue::Submit", Answered::eCallerObligation, "concurrent submits need external serialization" },
		ThreadingRule{ "Swapchain::Resize", Answered::eCallerObligation, "invalidates acquired back buffers and views" },
		ThreadingRule{ "DeviceMemoryAllocator", Answered::eHere, "called concurrently by creates from several threads" },
		ThreadingRule{ "Profiler", Answered::eHere, "zones opened from several threads at once" },
		ThreadingRule{ "Device creates and destroys", Answered::eByANamedGate, "gate_CreateGranularityPreserved" },
		ThreadingRule{ "DescriptorArena", Answered::eByANamedGate, "gate_ConcurrentArenaAllocationIsGuardedByTheRhi" },
		ThreadingRule{ "Instance and device lifetime", Answered::eByANamedGate, "gate_ConcurrentLifetimeIsGuardedByTheRhi" },
		ThreadingRule{ "Backend entry points", Answered::eByANamedGate, "gate_backend_lock_contract" },
		ThreadingRule{ "SpinLock", Answered::eByANamedGate, "gate_FiberMigrationSafety" },
		ThreadingRule{ "SyncOps replacing SetLockYield", Answered::eByANamedGate, "gate_TwoSchedulersOneProcess" },
		ThreadingRule{ "queryInterface", Answered::eByANamedGate, "gate_ConcurrentBlockQuery" },
		ThreadingRule{ "Validation registry", Answered::eByANamedGate, "gate_WaitFreeRegistryRead" },
		ThreadingRule{ "Profiler zones under fibers", Answered::eByANamedGate, "gate_FiberZoneAttribution" },
		ThreadingRule{ "AllocationTracker", Answered::eByANamedGate, "gate_NoGlobalLockWhenSingleThreaded" },
		ThreadingRule{ "Backend catalog", Answered::eByANamedGate, "gate_ConcurrentRegistration" },
		ThreadingRule{ "Module live object count", Answered::eByANamedGate, "gate_ModuleUnloadRefusedWhileLive" },
		ThreadingRule{ "eDeviceLost", Answered::eCallerObligation, "releasing the device is the caller's, and is not synchronized" },
	};

	static_assert(kThreadingModel.size() == 20);

	TEST_P(ThreadingModeTest, gate_ThreadingModelPreserved)
	{
		test::RecordingProfiler profiler;
		const rhi::ScopedProfiler installed(&profiler);

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eThreads;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		constexpr int kThreads = 4;

		{
			std::atomic<int> recorded{ 0 };
			std::vector<std::thread> workers;
			workers.reserve(kThreads);

			for (int worker = 0; worker < kThreads; ++worker)
			{
				workers.emplace_back(
					[&]
					{
						rhi::Error error{};
						rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
						if (!pool.IsValid())
						{
							return;
						}

						rhi::CommandList list = pool.Allocate("azoth.rhi.test.threadingModel", error);
						if (!list.IsValid() || !list.Begin(error))
						{
							return;
						}

						for (int command = 0; command < 64; ++command)
						{
							static_cast<void>(list.SetViewport(rhi::Viewport{ .width = 64.0f, .height = 64.0f }, error));
						}

						if (list.End(error))
						{
							recorded.fetch_add(1, std::memory_order_relaxed);
						}
					});
			}

			for (std::thread & worker : workers)
			{
				worker.join();
			}

			EXPECT_EQ(recorded.load(), kThreads) << "recording from several threads into their own lists did not come back clean";
		}

		{
			test::RecordingDeviceAllocator allocator;

			rhi::DeviceDesc allocating = test::DefaultDeviceDesc();
			allocating.threading	   = rhi::ThreadingMode::eThreads;
			allocating.allocator	   = &allocator;

			const test::DeviceHarness placing{ CurrentBackend(), allocating };
			if (placing.IsValid())
			{
				std::atomic<int> created{ 0 };
				std::vector<std::thread> workers;
				workers.reserve(kThreads);

				for (int worker = 0; worker < kThreads; ++worker)
				{
					workers.emplace_back(
						[&]
						{
							rhi::Error error{};
							const rhi::BufferHandle buffer = placing.Get().CreateBuffer(test::samples::StorageBuffer(), error);
							if (buffer.IsValid())
							{
								created.fetch_add(1, std::memory_order_relaxed);
								static_cast<void>(placing.Get().Destroy(buffer, {}, error));
							}
						});
				}

				for (std::thread & worker : workers)
				{
					worker.join();
				}

				EXPECT_EQ(created.load(), kThreads) << "concurrent creates through one allocator lost one";

				rhi::Error error{};
				EXPECT_TRUE(test::Ok(placing.Get().CollectGarbage(error), error));
				EXPECT_EQ(allocator.OutstandingSpans(), 0u) << "a span went back to the allocator twice or not at all";
			}
		}

#ifdef AZOTH_RHI_ENABLE_PROFILING
		{
			const std::size_t before = profiler.beginZoneCalls;

			std::vector<std::thread> workers;
			workers.reserve(kThreads);

			for (int worker = 0; worker < kThreads; ++worker)
			{
				workers.emplace_back(
					[&]
					{
						rhi::Error error{};
						for (int index = 0; index < 32; ++index)
						{
							const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
							if (buffer.IsValid())
							{
								static_cast<void>(device.Get().Destroy(buffer, {}, error));
							}
						}
					});
			}

			for (std::thread & worker : workers)
			{
				worker.join();
			}

			EXPECT_GT(profiler.beginZoneCalls, before) << "the library did no profiled work, so nothing reached the sink concurrently";
			EXPECT_EQ(profiler.beginZoneCalls, profiler.endZoneCalls) << "zones opened and closed from several threads did not balance";
		}
#endif

		std::size_t here = 0;
		for (const ThreadingRule & rule : kThreadingModel)
		{
			switch (rule.answered)
			{
			case Answered::eHere:			  ++here; break;
			case Answered::eByANamedGate:	  EXPECT_FALSE(rule.by.empty()) << rule.surface << " names no gate"; break;
			case Answered::eCallerObligation: EXPECT_FALSE(rule.by.empty()) << rule.surface << " says nothing about what the caller owes"; break;
			}
		}

		EXPECT_EQ(here, 5u) << "the rules this gate answers itself changed without the cases above changing with them";
	}

	TEST_P(ThreadingModeTest, gate_CreateGranularityPreserved)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eThreads;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		std::atomic<bool> textureDone{ false };
		std::atomic<bool> keepBufferBusy{ true };
		std::atomic<std::size_t> buffers{ 0 };

		std::atomic<bool> creatorStopped{ false };

		std::thread creator(
			[&]
			{
				rhi::Error error{};
				while (keepBufferBusy.load(std::memory_order_relaxed))
				{
					const rhi::BufferHandle handle = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
					if (!handle.IsValid())
					{
						break;
					}

					buffers.fetch_add(1, std::memory_order_relaxed);
					static_cast<void>(device.Get().Destroy(handle, {}, error));
				}

				creatorStopped.store(true, std::memory_order_relaxed);
			});

		constexpr std::size_t kAtLeastBuffers = 8;
		constexpr int kAtLeastTextures		  = 64;
		constexpr auto kProgressDeadline	  = std::chrono::seconds(10);

		const auto givenUp = std::chrono::steady_clock::now() + kProgressDeadline;
		while (buffers.load(std::memory_order_relaxed) < kAtLeastBuffers && !creatorStopped.load(std::memory_order_relaxed) &&
			   std::chrono::steady_clock::now() < givenUp)
		{
			std::this_thread::yield();
		}

		const std::size_t beforeTextures = buffers.load(std::memory_order_relaxed);
		ASSERT_GE(beforeTextures, kAtLeastBuffers) << "the buffer thread created " << beforeTextures << " buffers in " << kProgressDeadline.count()
												   << " seconds, so either a device wide lock is holding it off or it never ran. Either way nothing below "
													  "would prove anything about granularity.";

		rhi::Error error{};
		const auto texturesGivenUp = std::chrono::steady_clock::now() + kProgressDeadline;
		std::size_t afterTextures  = beforeTextures;

		for (int attempt = 0; attempt < kAtLeastTextures || afterTextures == beforeTextures; ++attempt)
		{
			const rhi::TextureHandle texture = device.Get().CreateTexture(test::samples::SampledTexture2D(), error);
			if (!texture.IsValid())
			{
				break;
			}

			static_cast<void>(device.Get().Destroy(texture, {}, error));
			textureDone.store(true, std::memory_order_relaxed);

			afterTextures = buffers.load(std::memory_order_relaxed);
			if (creatorStopped.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() >= texturesGivenUp)
			{
				break;
			}
		}

		keepBufferBusy.store(false, std::memory_order_relaxed);
		creator.join();

		EXPECT_TRUE(textureDone.load(std::memory_order_relaxed)) << "texture creates never got through while buffers were being created";

		EXPECT_GT(afterTextures, beforeTextures) << "no buffer was created during the texture run, so the two never actually ran at the same time";
	}

} // namespace

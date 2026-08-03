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
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/spies.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class AllocatorSeamTest : public ::testing::Test
	{
	protected:
		void TearDown() override
		{
			ASSERT_EQ(rhi::GetHostAllocator(), nullptr) << "a case leaked its host allocator into the next one";
			ASSERT_EQ(rhi::GetDeviceMemoryAllocator(), nullptr) << "a case leaked its device allocator into the next one";
		}
	};

	TEST_F(AllocatorSeamTest, NothingIsInstalledByDefault)
	{
		EXPECT_EQ(rhi::GetHostAllocator(), nullptr);
		EXPECT_EQ(rhi::GetDeviceMemoryAllocator(), nullptr);
	}

	TEST_F(AllocatorSeamTest, InstallingAHostAllocatorIsVisibleAndReversible)
	{
		test::CountingHostAllocator allocator;
		{
			const test::ScopedHostAllocator scope(&allocator);
			EXPECT_EQ(rhi::GetHostAllocator(), &allocator);
		}
		EXPECT_EQ(rhi::GetHostAllocator(), nullptr) << "the previous allocator was not put back";
	}

	TEST_F(AllocatorSeamTest, InstallingNestsSoATooLifetimeCanOverrideAndRestore)
	{
		test::CountingHostAllocator outer;
		test::CountingHostAllocator inner;

		const test::ScopedHostAllocator outerScope(&outer);
		{
			const test::ScopedHostAllocator innerScope(&inner);
			EXPECT_EQ(rhi::GetHostAllocator(), &inner);
		}
		EXPECT_EQ(rhi::GetHostAllocator(), &outer) << "the inner scope restored to null and not to what it replaced";
	}

	TEST_F(AllocatorSeamTest, HostAllocateRoutesThroughTheInstalledAllocator)
	{
		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		void * memory = rhi::HostAllocate(128, alignof(std::max_align_t));
		// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the only path that skips the free is a run this assertion already failed.
		ASSERT_NE(memory, nullptr);
		EXPECT_EQ(allocator.allocateCalls.load(), 1u);
		EXPECT_EQ(allocator.LiveBlocks(), 1u);

		rhi::HostFree(memory, 128, alignof(std::max_align_t));
		EXPECT_EQ(allocator.freeCalls.load(), 1u);
		EXPECT_EQ(allocator.LiveBlocks(), 0u);
		EXPECT_EQ(allocator.liveBytes.load(), 0);
	}

	TEST_F(AllocatorSeamTest, FreeReceivesTheSizeAndAlignmentTheAllocationWasMadeWith)
	{
		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		for (const std::size_t alignment : { alignof(void *), std::size_t{ 16 }, std::size_t{ 64 } })
		{
			for (const std::size_t size : { std::size_t{ 8 }, std::size_t{ 129 }, std::size_t{ 4096 } })
			{
				void * memory = rhi::HostAllocate(size, alignment);
				// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the only path that skips the free is a run this assertion already failed.
				ASSERT_NE(memory, nullptr);
				rhi::HostFree(memory, size, alignment);
			}
		}

		EXPECT_EQ(allocator.mismatchedFrees, 0u) << "a free reported a different size or alignment from its allocation";
		EXPECT_EQ(allocator.freesOfUnknownPointers, 0u);
		EXPECT_EQ(allocator.LiveBlocks(), 0u);
	}

	TEST_F(AllocatorSeamTest, HostAllocateHonoursTheRequestedAlignment)
	{
		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		for (const std::size_t alignment : { std::size_t{ 16 }, std::size_t{ 64 }, std::size_t{ 256 } })
		{
			void * memory = rhi::HostAllocate(alignment * 3, alignment);
			// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the only path that skips the free is a run this assertion already failed.
			ASSERT_NE(memory, nullptr);
			EXPECT_EQ(reinterpret_cast<std::uintptr_t>(memory) % alignment, 0u) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
				<< "an allocation came back off its requested alignment of " << alignment;
			rhi::HostFree(memory, alignment * 3, alignment);
		}
	}

	TEST_F(AllocatorSeamTest, HostAllocateFallsBackToOperatorNewWhenNothingIsInstalled)
	{
		ASSERT_EQ(rhi::GetHostAllocator(), nullptr);

		void * memory = rhi::HostAllocate(64, alignof(std::max_align_t));
		// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the only path that skips the free is a run this assertion already failed.
		ASSERT_NE(memory, nullptr);
		rhi::HostFree(memory, 64, alignof(std::max_align_t));
	}

	TEST_F(AllocatorSeamTest, AnAllocatorThatRefusesReturnsNullRatherThanThrowing)
	{
		class RefusingAllocator final : public rhi::HostAllocator
		{
		public:
			void * Allocate(std::size_t, std::size_t) override
			{
				return nullptr;
			}

			void Free(void *, std::size_t, std::size_t) override {}
		};

		RefusingAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		EXPECT_EQ(rhi::HostAllocate(64, alignof(std::max_align_t)), nullptr);
	}

	TEST_F(AllocatorSeamTest, TheStandardLibraryAdapterIsStatelessAndAlwaysCompaesEqual)
	{
		static_assert(std::is_empty_v<rhi::HostAllocatorAdapter<int>>);

		const rhi::HostAllocatorAdapter<int> first;
		const rhi::HostAllocatorAdapter<int> second;
		EXPECT_TRUE(first == second);

		const rhi::HostAllocatorAdapter<double> converted{ first };
		EXPECT_TRUE(first == converted);
	}

	TEST_F(AllocatorSeamTest, TheStandardLibraryAdapterRoutesContainerStorageThroughTheSeam)
	{
		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		{
			std::vector<int, rhi::HostAllocatorAdapter<int>> values;
			values.reserve(256);
			for (int index = 0; index < 256; ++index)
			{
				values.push_back(index);
			}

			EXPECT_GT(allocator.allocateCalls.load(), 0u) << "container storage did not come from the installed allocator";
			EXPECT_GT(allocator.LiveBlocks(), 0u);
		}

		EXPECT_EQ(allocator.LiveBlocks(), 0u) << "the container's storage was not returned";
		EXPECT_EQ(allocator.mismatchedFrees, 0u);
	}

	TEST_F(AllocatorSeamTest, TheAdapterRefusesAnAllocationThatWouldOverflowItsSizeComputation)
	{
#ifdef AZOTH_RHI_NO_EXCEPTIONS
		// RefuseAllocation aborts and does not throw in this build, which a case cannot observe and go on running. The refusal itself is the same either
		// way, so what is skipped here is how it gets reported and not whether the overflow is caught.
		GTEST_SKIP() << "the adapter aborts and does not throw under AZOTH_RHI_NO_EXCEPTIONS";
#else
		rhi::HostAllocatorAdapter<double> adapter;
		EXPECT_THROW(static_cast<void>(adapter.allocate(std::numeric_limits<std::size_t>::max() / 2)), std::bad_alloc);
#endif
	}

	TEST_F(AllocatorSeamTest, InstallingADeviceAllocatorIsVisibleAndReversible)
	{
		test::RecordingDeviceAllocator allocator;
		{
			const test::ScopedDeviceAllocator scope(&allocator);
			EXPECT_EQ(rhi::GetDeviceMemoryAllocator(), &allocator);
		}
		EXPECT_EQ(rhi::GetDeviceMemoryAllocator(), nullptr);
	}

	TEST_F(AllocatorSeamTest, ADeviceAllocatorReportsNoStatisticsByDefault)
	{
		class MinimalAllocator final : public rhi::DeviceMemoryAllocator
		{
		public:
			bool Allocate(rhi::Device, const rhi::MemoryRequest &, rhi::MemorySpan &) override
			{
				return false;
			}

			void Free(rhi::Device, const rhi::MemorySpan &) override {}
		};

		const MinimalAllocator allocator;
		const rhi::MemoryStats stats = allocator.Stats();

		EXPECT_EQ(stats.heapCount, 0u);
		EXPECT_EQ(stats.liveAllocations, 0u);
	}

	TEST_F(AllocatorSeamTest, AMemorySpanIsValidOnlyOnceItNamesAHeap)
	{
		constexpr rhi::MemorySpan empty{};
		static_assert(!empty.IsValid());

		constexpr rhi::MemorySpan granted{
			.heap	  = rhi::HeapHandle{ .index = 2, .generation = 1 },
			.offset	  = 0,
			.size	  = 256,
			.userData = nullptr,
		};
		static_assert(granted.IsValid());

		SUCCEED();
	}

	TEST_F(AllocatorSeamTest, HeapTypeForUsageMapsEveryMemoryUsageOntoAHeapClass)
	{
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eCpuUpload) == rhi::HeapType::eCpuUpload);
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eCpuToGpu) == rhi::HeapType::eCpuUpload);
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eCpuReadback) == rhi::HeapType::eCpuReadback);

		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eGpuOnly) == rhi::HeapType::eGpuLocal);
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eGpuToCpu) == rhi::HeapType::eGpuLocal);
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eTransient) == rhi::HeapType::eGpuLocal);
		static_assert(rhi::HeapTypeForUsage(rhi::MemoryUsage::eReserved) == rhi::HeapType::eGpuLocal);

		SUCCEED();
	}

	TEST_F(AllocatorSeamTest, TheAllocatorInterfacesAreNeitherCopyableNorMovable)
	{
		static_assert(!std::is_copy_constructible_v<rhi::HostAllocator>);
		static_assert(!std::is_move_constructible_v<rhi::HostAllocator>);
		static_assert(!std::is_copy_constructible_v<rhi::DeviceMemoryAllocator>);
		static_assert(!std::is_move_constructible_v<rhi::DeviceMemoryAllocator>);

		SUCCEED();
	}

	class HostAllocatorRoutingTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(HostAllocatorRoutingTest);

	TEST_P(HostAllocatorRoutingTest, TheLibraryDrawsItsOwnStateFromTheInstalledAllocator)
	{
		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		const auto round = [this]() -> bool
		{
			std::array<rhi::BufferHandle, 16> buffers{};
			rhi::Error error{};

			for (rhi::BufferHandle & buffer : buffers)
			{
				buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
				if (!buffer.IsValid())
				{
					return false;
				}
			}

			for (const rhi::BufferHandle buffer : buffers)
			{
				if (!Dev().Destroy(buffer, {}, error))
				{
					return false;
				}
			}

			return true;
		};

		ASSERT_TRUE(round());
		EXPECT_GT(allocator.allocateCalls.load(), 0u) << "sixteen buffers were created and destroyed without one allocation reaching the installed "
														 "allocator, so the library is still going straight to operator new for its own state";
		EXPECT_GT(allocator.freeCalls.load(), 0u) << "allocations arrived but nothing came back, so the two halves of the seam are not wired to the same "
													 "state";

		const std::size_t settled = allocator.LiveBlocks();

		ASSERT_TRUE(round());

		EXPECT_EQ(allocator.LiveBlocks(), settled) << "a create and destroy round did not hand back everything it took";
		EXPECT_EQ(allocator.mismatchedFrees, 0u) << "a free reported a different size or alignment from the allocation that produced the pointer";
	}

	class DeviceAllocatorRoutingTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(DeviceAllocatorRoutingTest);

	TEST_P(DeviceAllocatorRoutingTest, gate_TwoAllocatorsOneProcess)
	{
		test::RecordingDeviceAllocator first;
		test::RecordingDeviceAllocator second;

		rhi::DeviceDesc firstDesc  = test::DefaultDeviceDesc();
		firstDesc.allocator		   = &first;
		rhi::DeviceDesc secondDesc = test::DefaultDeviceDesc();
		secondDesc.allocator	   = &second;

		const test::DeviceHarness one{ CurrentBackend(), firstDesc };
		const test::DeviceHarness two{ CurrentBackend(), secondDesc };
		if (!one.IsValid() || !two.IsValid())
		{
			GTEST_SKIP() << CurrentBackend().displayName << " cannot place resources, so it cannot honor an allocator";
		}

		rhi::Error error{};
		const rhi::BufferHandle fromOne = one.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(fromOne.IsValid(), error));

		EXPECT_EQ(first.OutstandingSpans(), 1u) << "a create did not reach the allocator its device was given";
		EXPECT_EQ(second.OutstandingSpans(), 0u) << "one device's create reached another device's allocator";

		const rhi::BufferHandle fromTwo = two.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(fromTwo.IsValid(), error));

		EXPECT_EQ(first.OutstandingSpans(), 1u);
		EXPECT_EQ(second.OutstandingSpans(), 1u) << "the second device's create did not reach its own allocator";

		EXPECT_TRUE(test::Ok(one.Get().Destroy(fromOne, {}, error), error));
		EXPECT_TRUE(test::Ok(two.Get().Destroy(fromTwo, {}, error), error));
		EXPECT_EQ(first.OutstandingSpans(), 1u) << "a deferred destroy released its span before the collect";

		EXPECT_TRUE(test::Ok(one.Get().CollectGarbage(error), error));
		EXPECT_EQ(first.OutstandingSpans(), 0u) << "a collect did not return the span its device's allocator granted";
		EXPECT_EQ(second.OutstandingSpans(), 1u) << "one device's collect returned another device's span";

		EXPECT_TRUE(test::Ok(two.Get().CollectGarbage(error), error));
		EXPECT_EQ(second.OutstandingSpans(), 0u);
	}

	TEST_P(DeviceAllocatorRoutingTest, gate_NoGlobalLockWhenSingleThreaded)
	{
		test::Scheduler scheduler;
		test::RecordingDeviceAllocator allocator;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eSingleThreaded;
		desc.sync			 = scheduler.Ops();
		desc.allocator		 = &allocator;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		if (!device.IsValid())
		{
			GTEST_SKIP() << CurrentBackend().displayName << " cannot place resources, so it cannot honor an allocator";
		}

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		EXPECT_EQ(allocator.OutstandingSpans(), 1u) << "the create did not route through the installed allocator";

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().CollectGarbage(error), error));

		EXPECT_EQ(scheduler.acquired.load(std::memory_order_relaxed), 0u) << "a single threaded device took a lock on the create path";
		EXPECT_EQ(scheduler.created.load(std::memory_order_relaxed), 0u) << "a single threaded device asked the host for a lock it cannot need";
	}

	TEST_P(DeviceAllocatorRoutingTest, gate_NoGuardAcrossAReentrantCall)
	{
		test::Scheduler scheduler;
		test::RecordingDeviceAllocator allocator;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.threading		 = rhi::ThreadingMode::eCooperative;
		desc.sync			 = scheduler.Ops();
		desc.allocator		 = &allocator;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		if (!device.IsValid())
		{
			GTEST_SKIP() << CurrentBackend().displayName << " cannot place resources, so it cannot honor an allocator";
		}

		scheduler.peakHeld.store(0, std::memory_order_relaxed);

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		EXPECT_EQ(scheduler.peakHeld.load(std::memory_order_relaxed), 1u) << "a buffer create held one guard while taking another";

		const rhi::TextureHandle texture = device.Get().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));
		EXPECT_EQ(scheduler.peakHeld.load(std::memory_order_relaxed), 1u) << "a texture create held one guard while taking another";

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().CollectGarbage(error), error));
		EXPECT_EQ(scheduler.peakHeld.load(std::memory_order_relaxed), 1u) << "a collect held one guard while taking another";
		EXPECT_EQ(allocator.OutstandingSpans(), 0u);
	}

	TEST_P(DeviceAllocatorRoutingTest, TheRhiNeverCallsTheAllocatorWhileHoldingAGuard)
	{
		test::RecordingDeviceAllocator allocator;

		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.allocator		 = &allocator;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		if (!device.IsValid())
		{
			GTEST_SKIP() << CurrentBackend().displayName << " cannot place resources, so it cannot honor an allocator";
		}

		const std::uint64_t before = rhi::detail::ReentrancyViolations();

		rhi::Error error{};

		const rhi::BufferHandle immediate = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(immediate.IsValid(), error));

		EXPECT_TRUE(test::Ok(device.Get().Destroy(immediate, rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle }, error), error));

		const rhi::BufferHandle deferred = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(deferred.IsValid(), error));
		EXPECT_TRUE(test::Ok(device.Get().Destroy(deferred, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().CollectGarbage(error), error));

		EXPECT_EQ(rhi::detail::ReentrancyViolations(), before) << "the RHI called the allocator while holding a device guard";
		EXPECT_EQ(allocator.OutstandingSpans(), 0u) << "a span the allocator granted was never handed back";
	}

} // namespace

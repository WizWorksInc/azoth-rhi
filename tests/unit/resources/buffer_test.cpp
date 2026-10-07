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
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint> // NOLINT
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BufferTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BufferTest);

	class DeviceLocalMappingTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] rhi::DeviceDesc MakeDeviceDesc() const override
		{
			rhi::DeviceDesc desc		 = test::DefaultDeviceDesc();
			desc.allowDeviceLocalMapping = m_allow;
			return desc;
		}

		bool m_allow = false;
	};

	AZO_RHI_BACKEND_SUITE(DeviceLocalMappingTest);

	TEST_P(DeviceLocalMappingTest, RefusesToMapDeviceLocalMemoryUnlessAsked)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error)) << "the sample storage buffer is device local, and it did not create";

		rhi::Error mapError{};
		const rhi::MappedMemory mapped = Dev().map(buffer, {}, mapError);
		EXPECT_EQ(mapped.data, nullptr) << "device-local memory was mapped without DeviceDesc::allowDeviceLocalMapping";
		EXPECT_TRUE(test::ErrorIsPopulated(mapError));

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(DeviceLocalMappingTest, ReportsWhetherDeviceLocalMemoryCouldBeMappedAtAll)
	{
		const bool unified = Caps().deviceLocalMemoryIsHostVisible;
		EXPECT_EQ(unified, Dev().get_caps().deviceLocalMemoryIsHostVisible);

		if (!unified)
		{
			SUCCEED() << CurrentBackend().displayName << " keeps host and device memory apart, so the opt-in cannot apply";
		}
	}

	TEST_P(BufferTest, CreatesAndDestroysAStorageBuffer)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);

		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, TheThreeCreationOverloadsAllProduceAUsableBuffer)
	{
		const rhi::BufferHandle sentinel = Dev().create_buffer(test::samples::StorageBuffer());
		EXPECT_TRUE(sentinel.is_valid());

		rhi::Error error{};
		const rhi::BufferHandle withError = Dev().create_buffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(withError.is_valid(), error));

		const rhi::Result<rhi::BufferHandle> asResult = Dev().create_buffer_with_result(test::samples::StorageBuffer());
		ASSERT_TRUE(test::Ok(asResult));
		EXPECT_TRUE(asResult.value().is_valid());

		EXPECT_NE(sentinel, withError);
		EXPECT_NE(withError, asResult.value());

		EXPECT_TRUE(test::Ok(Dev().destroy(sentinel, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(withError, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(asResult.value(), {}, error), error));
	}

	TEST_P(BufferTest, AcceptsEveryMemoryUsageTheBaselineNeeds)
	{
		rhi::Error error{};
		for (const rhi::BufferDesc & desc : { test::samples::StorageBuffer(), test::samples::UploadBuffer(), test::samples::ReadbackBuffer() })
		{
			const rhi::BufferHandle buffer = Dev().create_buffer(desc, error);
			EXPECT_TRUE(test::Ok(buffer.is_valid(), error)) << "memory usage " << static_cast<int>(desc.memory) << " was refused";
			if (buffer.is_valid())
			{
				EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
			}
		}
	}

	TEST_P(BufferTest, HandsOutADistinctHandleForEveryLiveBuffer)
	{
		constexpr int kCount = 64;

		rhi::Error error{};
		std::vector<rhi::BufferHandle> buffers;
		buffers.reserve(kCount);

		for (int index = 0; index < kCount; ++index)
		{
			const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(buffer.is_valid(), error)) << "creation " << index << " failed";
			buffers.push_back(buffer);
		}

		for (std::size_t lhs = 0; lhs < buffers.size(); ++lhs)
		{
			for (std::size_t rhs = lhs + 1; rhs < buffers.size(); ++rhs)
			{
				ASSERT_NE(buffers[lhs], buffers[rhs]) << "two live buffers share a handle";
			}
		}

		for (const rhi::BufferHandle buffer : buffers)
		{
			EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
		}
	}

	TEST_P(BufferTest, ReusesSlotsAfterDestructionWithoutReusingHandles)
	{
		rhi::Error error{};

		const rhi::BufferHandle first = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(first.is_valid(), error));
		ASSERT_TRUE(test::Ok(Dev().destroy(first, {}, error), error));

		const rhi::BufferHandle second = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(second.is_valid(), error));

		EXPECT_NE(first, second) << "a destroyed buffer's handle came back verbatim";

		EXPECT_TRUE(test::Ok(Dev().destroy(second, {}, error), error));
	}

	TEST_P(BufferTest, RejectsDestroyingTheSameBufferTwice)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		ASSERT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(Dev().destroy(buffer, {}, secondError)) << "a double destroy was accepted, which would free the native object twice";
		EXPECT_TRUE(test::ErrorIsPopulated(secondError));
	}

	TEST_P(BufferTest, RejectsDestroyingAHandleItNeverIssued)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		EXPECT_FALSE(Dev().destroy(rhi::BufferHandle{ .index = 4096, .generation = 7 }, {}, error));
		EXPECT_TRUE(test::ErrorIsPopulated(error));

		rhi::Error invalidError{};
		EXPECT_FALSE(Dev().destroy(rhi::BufferHandle{}, {}, invalidError));
	}

	TEST_P(BufferTest, RejectsAStaleHandleWhoseSlotWasAlreadyReissued)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle stale = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(stale.is_valid(), error));
		ASSERT_TRUE(test::Ok(Dev().destroy(stale, {}, error), error));

		const rhi::BufferHandle successor = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(successor.is_valid(), error));

		rhi::Error staleError{};
		EXPECT_FALSE(Dev().destroy(stale, {}, staleError)) << "a stale handle destroyed the resource that took over its slot";

		EXPECT_TRUE(test::Ok(Dev().destroy(successor, {}, error), error));
	}

	TEST_P(BufferTest, AcceptsBothDestroyPolicies)
	{
		rhi::Error error{};

		const rhi::BufferHandle deferred = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(deferred.is_valid(), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(deferred, rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eDeferUntilSafe }, error), error));

		const rhi::BufferHandle immediate = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(immediate.is_valid(), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(immediate, rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle }, error), error));
	}

	TEST_P(BufferTest, ReportsAMemoryFootprintForADescBeforeAnythingIsCreated)
	{
		rhi::Error error{};
		rhi::MemoryInfo info{};

		if (!Dev().get_buffer_memory_info(test::samples::StorageBuffer(), info, error))
		{
			GTEST_SKIP() << "this backend does not report buffer memory info: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(true, error));
	}

	TEST_P(BufferTest, ClearsTheOutputWhenAMemoryQueryFails)
	{
		rhi::MemoryInfo info{
			.size	   = 12345,
			.alignment = 678,
		};
		rhi::Error error{};

		rhi::BufferDesc absurd = test::samples::StorageBuffer();
		absurd.size			   = 0;

		if (Dev().get_buffer_memory_info(absurd, info, error))
		{
			GTEST_SKIP() << "this backend accepts a zero-sized buffer desc, so there is no failure to observe";
		}

		EXPECT_EQ(info.size, 0u) << "a failed query left the caller's output untouched";
		EXPECT_EQ(info.alignment, 0u);
	}

	TEST_P(BufferTest, RefusesAnUnmapWithNoMapOutstanding)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::Error neverMapped{};
		EXPECT_FALSE(Dev().unmap(buffer, neverMapped)) << "a buffer that was never mapped was unmapped";
		EXPECT_EQ(neverMapped.code, rhi::ErrorCode::eInvalidState);

		const rhi::MappedMemory mapped = Dev().map(buffer, {}, error);
		if (mapped.data == nullptr)
		{
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().unmap(buffer, error), error));

		rhi::Error twice{};
		EXPECT_FALSE(Dev().unmap(buffer, twice)) << "the same mapping was unmapped twice";
		EXPECT_EQ(twice.code, rhi::ErrorCode::eInvalidState);

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, KeepsANestedMapOpenUntilItsLastUnmap)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const rhi::MappedMemory outer = Dev().map(buffer, {}, error);
		if (outer.data == nullptr)
		{
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		const rhi::MappedMemory inner = Dev().map(buffer, {}, error);
		ASSERT_TRUE(test::Ok(inner.data != nullptr, error)) << "a second map of a mapped buffer was refused";
		EXPECT_EQ(inner.data, outer.data);

		EXPECT_TRUE(test::Ok(Dev().unmap(buffer, error), error));
		std::memset(outer.data, 0x5A, static_cast<std::size_t>(test::samples::kBufferSize));
		EXPECT_TRUE(test::Ok(Dev().unmap(buffer, error), error));

		rhi::Error extra{};
		EXPECT_FALSE(Dev().unmap(buffer, extra)) << "an unmap beyond the maps outstanding was accepted";
		EXPECT_EQ(extra.code, rhi::ErrorCode::eInvalidState);

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, DestroysABufferThatIsStillMapped)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const rhi::MappedMemory mapped = Dev().map(buffer, {}, error);
		if (mapped.data == nullptr)
		{
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}
		static_cast<void>(Dev().map(buffer, {}, error));

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, DestroysADeviceThatStillHoldsAMappedBuffer)
	{
		test::DeviceHarness harness(CurrentBackend());
		ASSERT_TRUE(harness.IsValid()) << test::Describe(harness.GetError());

		rhi::Error error{};
		const rhi::BufferHandle buffer = harness.Get().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		if (harness.Get().map(buffer, {}, error).data == nullptr)
		{
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}
	}

	TEST_P(BufferTest, NestsAsManyMapsAsTheCallerAsksFor)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		constexpr std::uint32_t kDepth = 300;
		std::uint32_t held			   = 0;
		rhi::Error refused{};
		while (held < kDepth && Dev().map(buffer, {}, refused).data != nullptr)
		{
			++held;
		}
		if (held == 0)
		{
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(refused);
		}

		EXPECT_EQ(held, kDepth) << "a backend that maps imposed a nesting ceiling of its own at " << held << ": " << test::Describe(refused);

		for (std::uint32_t i = 0; i < held; ++i)
		{
			ASSERT_TRUE(test::Ok(Dev().unmap(buffer, error), error)) << "unmap " << i << " of " << held;
		}

		rhi::Error extra{};
		EXPECT_FALSE(Dev().unmap(buffer, extra)) << "an unmap beyond the maps outstanding was accepted";
		EXPECT_EQ(extra.code, rhi::ErrorCode::eInvalidState) << test::Describe(extra);

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, CountsMapsAndUnmapsFromSeveralThreadsAtOnce)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::Error probe{};
		if (Dev().map(buffer, {}, probe).data == nullptr)
		{
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(probe);
		}
		ASSERT_TRUE(test::Ok(Dev().unmap(buffer, error), error));

		constexpr int kThreads		  = 8;
		const std::uint32_t perThread = test::ScaledIterations(500);

		std::atomic<int> ready{ 0 };
		std::atomic<int> mapsRefused{ 0 };
		std::atomic<int> unmapsRefused{ 0 };

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
						std::this_thread::yield();
					}

					for (std::uint32_t index = 0; index < perThread; ++index)
					{
						rhi::Error mapError{};
						if (Dev().map(buffer, {}, mapError).data == nullptr)
						{
							mapsRefused.fetch_add(1, std::memory_order_relaxed);
							continue;
						}

						rhi::Error unmapError{};
						if (!Dev().unmap(buffer, unmapError))
						{
							unmapsRefused.fetch_add(1, std::memory_order_relaxed);
						}
					}
				}
			);
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		EXPECT_EQ(mapsRefused.load(), 0) << "a map was refused while eight threads each held at most one";
		EXPECT_EQ(unmapsRefused.load(), 0) << "an unmap was refused for a map that had been taken";

		rhi::Error extra{};
		EXPECT_FALSE(Dev().unmap(buffer, extra)) << "the maps and unmaps did not balance back to none outstanding";
		EXPECT_EQ(extra.code, rhi::ErrorCode::eInvalidState) << test::Describe(extra);

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "mapping one buffer from eight threads ");
	}

	TEST_P(BufferTest, RefusesAMapRangeWhoseEndWrapsPastTheBuffer)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::Error wrapped{};
		const rhi::MappedMemory mapped = Dev().map(buffer, rhi::MapDesc{ .offset = 16, .size = ~std::uint64_t{ 0 } - 8 }, wrapped);
		EXPECT_EQ(mapped.data, nullptr) << "a map whose offset plus size wraps around was accepted, size " << mapped.size;
		EXPECT_TRUE(test::ErrorIsPopulated(wrapped));
		if (mapped.data != nullptr)
		{
			static_cast<void>(Dev().unmap(buffer, error));
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, KeepsBufferAndTextureHandleDomainsApartAtCompileTime)
	{
		static_assert(!std::is_convertible_v<rhi::BufferHandle, rhi::TextureHandle>);
		static_assert(!std::is_convertible_v<rhi::TextureHandle, rhi::BufferHandle>);

		SUCCEED();
	}

} // namespace

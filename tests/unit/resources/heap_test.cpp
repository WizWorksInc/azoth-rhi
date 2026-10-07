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
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class HeapTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(HeapTest);

	TEST_P(HeapTest, CreatesAndDestroysAHeap)
	{
		rhi::Error error{};
		const rhi::HeapHandle heap = Dev().create_heap(test::samples::GpuHeap(), error);

		ASSERT_TRUE(test::Ok(heap.is_valid(), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, CreatesAHeapOfEachClass)
	{
		rhi::Error error{};

		for (const rhi::HeapType type : { rhi::HeapType::eGpuLocal, rhi::HeapType::eCpuUpload, rhi::HeapType::eCpuReadback, rhi::HeapType::eTransient })
		{
			rhi::HeapDesc desc = test::samples::GpuHeap();
			desc.type		   = type;

			const rhi::HeapHandle heap = Dev().create_heap(desc, error);
			EXPECT_TRUE(test::Ok(heap.is_valid(), error)) << "heap class " << static_cast<int>(type) << " was refused";
			if (heap.is_valid())
			{
				EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
			}
		}
	}

	TEST_P(HeapTest, PlacesABufferIntoAHeapItCreated)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const rhi::HeapHandle heap = Dev().create_heap(test::samples::GpuHeap(), error);
		ASSERT_TRUE(test::Ok(heap.is_valid(), error));

		rhi::PlacedBufferDesc placed{};
		placed.buffer = test::samples::StorageBuffer();
		placed.heap	  = heap;
		placed.offset = 0;

		const rhi::BufferHandle buffer = Dev().create_placed_buffer(placed, error);
		if (!buffer.is_valid())
		{
			static_cast<void>(Dev().destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused a placed buffer: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, PlacesATextureIntoAHeapItCreated)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		rhi::HeapDesc heapDesc = test::samples::GpuHeap();
		heapDesc.allowTextures = true;

		const rhi::HeapHandle heap = Dev().create_heap(heapDesc, error);
		ASSERT_TRUE(test::Ok(heap.is_valid(), error));

		rhi::PlacedTextureDesc placed{};
		placed.texture = test::samples::SampledTexture2D();
		placed.heap	   = heap;
		placed.offset  = 0;

		const rhi::TextureHandle texture = Dev().create_placed_texture(placed, error);
		if (!texture.is_valid())
		{
			static_cast<void>(Dev().destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused a placed texture: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, RefusesToPlaceAResourceIntoAHeapThatDoesNotExist)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		rhi::PlacedBufferDesc placed{};
		placed.buffer = test::samples::StorageBuffer();
		placed.heap	  = rhi::HeapHandle{
			.index		= 8191,
			.generation = 5,
		};

		const rhi::BufferHandle buffer = Dev().create_placed_buffer(placed, error);
		EXPECT_FALSE(buffer.is_valid()) << "a buffer was placed into a heap the device never created";
	}

	TEST_P(HeapTest, ReportsAMemoryBudgetOrSaysItCannot)
	{
		rhi::Error error{};
		rhi::MemoryBudgetInfo budget{};

		const bool reported = Dev().query_memory_budget(rhi::HeapType::eGpuLocal, budget, error);
		if (!reported)
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			GTEST_SKIP() << "this backend does not report a memory budget: " << test::Describe(error);
		}

		EXPECT_EQ(budget.heap, rhi::HeapType::eGpuLocal) << "the budget came back describing a different heap class";
	}

	TEST_P(HeapTest, MapsAHostVisibleBufferOrSaysItCannot)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const rhi::MappedMemory mapped = Dev().map(
			buffer,
			rhi::MapDesc{
				.mode	= rhi::MapMode::eWrite,
				.offset = 0,
				.size	= test::samples::kBufferSize,
			},
			error
		);
		if (mapped.data == nullptr)
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		EXPECT_GE(mapped.size, test::samples::kBufferSize) << "the mapping covers less than was asked for";

		std::memset(mapped.data, 0xAB, static_cast<std::size_t>(test::samples::kBufferSize));

		if (!mapped.coherent)
		{
			EXPECT_TRUE(test::Ok(Dev().flush_mapped_range(buffer, 0, test::samples::kBufferSize, error), error));
		}

		EXPECT_TRUE(test::Ok(Dev().unmap(buffer, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(HeapTest, MapsTwoPlacedBuffersFromOneHeapAtOnce)
	{
		rhi::Error error{};
		rhi::HeapDesc heapDesc	   = test::samples::GpuHeap();
		heapDesc.type			   = rhi::HeapType::eCpuUpload;
		heapDesc.allowTextures	   = false;
		const rhi::HeapHandle heap = Dev().create_heap(heapDesc, error);
		if (!heap.is_valid())
		{
			GTEST_SKIP() << "this backend does not create an upload heap: " << test::Describe(error);
		}

		constexpr std::uint64_t kSecondOffset = 1u << 16u;
		rhi::PlacedBufferDesc placed{};
		placed.buffer				   = test::samples::UploadBuffer();
		placed.heap					   = heap;
		const rhi::BufferHandle first  = Dev().create_placed_buffer(placed, error);
		placed.offset				   = kSecondOffset;
		const rhi::BufferHandle second = Dev().create_placed_buffer(placed, error);
		ASSERT_TRUE(test::Ok(first.is_valid() && second.is_valid(), error));

		const rhi::MappedMemory one = Dev().map(first, {}, error);
		if (one.data == nullptr)
		{
			static_cast<void>(Dev().destroy(first, {}, error));
			static_cast<void>(Dev().destroy(second, {}, error));
			static_cast<void>(Dev().destroy(heap, {}, error));
			GTEST_SKIP() << "this backend does not map a placed buffer: " << test::Describe(error);
		}
		const rhi::MappedMemory two = Dev().map(second, {}, error);
		ASSERT_TRUE(test::Ok(two.data != nullptr, error)) << "a second placed buffer in a mapped heap would not map";
		EXPECT_NE(one.data, two.data);

		std::memset(one.data, 0x11, static_cast<std::size_t>(test::samples::kBufferSize));
		std::memset(two.data, 0x22, static_cast<std::size_t>(test::samples::kBufferSize));
		EXPECT_EQ(*static_cast<const std::uint8_t *>(one.data), 0x11) << "the two placed buffers mapped to overlapping memory";

		EXPECT_TRUE(test::Ok(Dev().unmap(first, error), error));
		EXPECT_EQ(*static_cast<const std::uint8_t *>(two.data), 0x22) << "unmapping one placed buffer took the other's mapping with it";
		EXPECT_TRUE(test::Ok(Dev().unmap(second, error), error));

		EXPECT_TRUE(test::Ok(Dev().destroy(first, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(second, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, MapsTwoPlacedBuffersOfOneHeapFromTwoThreadsAtOnce)
	{
		rhi::Error error{};
		rhi::HeapDesc heapDesc	   = test::samples::GpuHeap();
		heapDesc.type			   = rhi::HeapType::eCpuUpload;
		heapDesc.allowTextures	   = false;
		const rhi::HeapHandle heap = Dev().create_heap(heapDesc, error);
		if (!heap.is_valid())
		{
			GTEST_SKIP() << "this backend does not create an upload heap: " << test::Describe(error);
		}

		constexpr std::uint64_t kSecondOffset = 1u << 16u;
		rhi::PlacedBufferDesc placed{};
		placed.buffer				   = test::samples::UploadBuffer();
		placed.heap					   = heap;
		const rhi::BufferHandle first  = Dev().create_placed_buffer(placed, error);
		placed.offset				   = kSecondOffset;
		const rhi::BufferHandle second = Dev().create_placed_buffer(placed, error);
		if (!first.is_valid() || !second.is_valid())
		{
			static_cast<void>(Dev().destroy(first, {}, error));
			static_cast<void>(Dev().destroy(second, {}, error));
			static_cast<void>(Dev().destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused a placed buffer: " << test::Describe(error);
		}

		rhi::Error probe{};
		if (Dev().map(first, {}, probe).data == nullptr)
		{
			static_cast<void>(Dev().destroy(first, {}, error));
			static_cast<void>(Dev().destroy(second, {}, error));
			static_cast<void>(Dev().destroy(heap, {}, error));
			GTEST_SKIP() << "this backend does not map a placed buffer: " << test::Describe(probe);
		}
		ASSERT_TRUE(test::Ok(Dev().unmap(first, error), error));

		const std::uint32_t rounds = test::ScaledIterations(500);
		std::atomic<int> ready{ 0 };
		std::atomic<int> refused{ 0 };

		const auto hammer = [&](const rhi::BufferHandle buffer)
		{
			ready.fetch_add(1, std::memory_order_release);
			while (ready.load(std::memory_order_acquire) < 2)
			{
				std::this_thread::yield();
			}

			for (std::uint32_t round = 0; round < rounds; ++round)
			{
				rhi::Error mapError{};
				if (Dev().map(buffer, {}, mapError).data == nullptr)
				{
					refused.fetch_add(1, std::memory_order_relaxed);
					continue;
				}
				rhi::Error unmapError{};
				if (!Dev().unmap(buffer, unmapError))
				{
					refused.fetch_add(1, std::memory_order_relaxed);
				}
			}
		};

		std::thread onFirst(hammer, first);
		std::thread onSecond(hammer, second);
		onFirst.join();
		onSecond.join();

		EXPECT_EQ(refused.load(), 0) << "mapping two placed buffers of one heap at once was refused";

		rhi::Error firstExtra{};
		EXPECT_FALSE(Dev().unmap(first, firstExtra)) << "the first buffer did not balance back to none outstanding";
		EXPECT_EQ(firstExtra.code, rhi::ErrorCode::eInvalidState) << test::Describe(firstExtra);
		rhi::Error secondExtra{};
		EXPECT_FALSE(Dev().unmap(second, secondExtra)) << "the second buffer did not balance back to none outstanding";
		EXPECT_EQ(secondExtra.code, rhi::ErrorCode::eInvalidState) << test::Describe(secondExtra);

		const rhi::MappedMemory one = Dev().map(first, {}, error);
		const rhi::MappedMemory two = Dev().map(second, {}, error);
		ASSERT_TRUE(test::Ok(one.data != nullptr && two.data != nullptr, error)) << "a placed buffer would not map after the threads were done";
		std::memset(one.data, 0x11, static_cast<std::size_t>(test::samples::kBufferSize));
		std::memset(two.data, 0x22, static_cast<std::size_t>(test::samples::kBufferSize));
		EXPECT_EQ(*static_cast<const std::uint8_t *>(one.data), 0x11) << "the two placed buffers mapped to overlapping memory";
		EXPECT_TRUE(test::Ok(Dev().unmap(first, error), error));
		EXPECT_TRUE(test::Ok(Dev().unmap(second, error), error));

		EXPECT_TRUE(test::Ok(Dev().destroy(first, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(second, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(heap, {}, error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "mapping two placed buffers of one heap from two threads ");
	}

	TEST_P(HeapTest, ClearsTheMappingWhenMapFails)
	{
		rhi::Error error{};

		const rhi::MappedMemory mapped = Dev().map(rhi::BufferHandle{ .index = 7777, .generation = 2 }, rhi::MapDesc{}, error);

		EXPECT_EQ(mapped.data, nullptr);
		EXPECT_EQ(mapped.size, 0u) << "a failed map reported a size for a null pointer";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(HeapTest, SetsResidencyPrioritiesOrSaysItCannot)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const std::vector<rhi::ResidencyPriorityDesc> priorities{
			rhi::ResidencyPriorityDesc{ .buffer = buffer, .texture = {}, .priority = rhi::ResidencyPriority::eHigh },
		};

		if (!Dev().set_residency_priority(priorities, error))
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			static_cast<void>(Dev().destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not take residency priorities: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

}

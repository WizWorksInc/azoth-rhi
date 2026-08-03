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

#include <cstdint>
#include <cstring>
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
		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(), error);

		ASSERT_TRUE(test::Ok(heap.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, CreatesAHeapOfEachClass)
	{
		rhi::Error error{};

		for (const rhi::HeapType type : { rhi::HeapType::eGpuLocal, rhi::HeapType::eCpuUpload, rhi::HeapType::eCpuReadback, rhi::HeapType::eTransient })
		{
			rhi::HeapDesc desc = test::samples::GpuHeap();
			desc.type		   = type;

			const rhi::HeapHandle heap = Dev().CreateHeap(desc, error);
			EXPECT_TRUE(test::Ok(heap.IsValid(), error)) << "heap class " << static_cast<int>(type) << " was refused";
			if (heap.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
			}
		}
	}

	TEST_P(HeapTest, PlacesABufferIntoAHeapItCreated)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(), error);
		ASSERT_TRUE(test::Ok(heap.IsValid(), error));

		rhi::PlacedBufferDesc placed{};
		placed.buffer = test::samples::StorageBuffer();
		placed.heap	  = heap;
		placed.offset = 0;

		const rhi::BufferHandle buffer = Dev().CreatePlacedBuffer(placed, error);
		if (!buffer.IsValid())
		{
			static_cast<void>(Dev().Destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused a placed buffer: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
	}

	TEST_P(HeapTest, PlacesATextureIntoAHeapItCreated)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		rhi::HeapDesc heapDesc = test::samples::GpuHeap();
		heapDesc.allowTextures = true;

		const rhi::HeapHandle heap = Dev().CreateHeap(heapDesc, error);
		ASSERT_TRUE(test::Ok(heap.IsValid(), error));

		rhi::PlacedTextureDesc placed{};
		placed.texture = test::samples::SampledTexture2D();
		placed.heap	   = heap;
		placed.offset  = 0;

		const rhi::TextureHandle texture = Dev().CreatePlacedTexture(placed, error);
		if (!texture.IsValid())
		{
			static_cast<void>(Dev().Destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused a placed texture: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
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

		const rhi::BufferHandle buffer = Dev().CreatePlacedBuffer(placed, error);
		EXPECT_FALSE(buffer.IsValid()) << "a buffer was placed into a heap the device never created";
	}

	TEST_P(HeapTest, ReportsAMemoryBudgetOrSaysItCannot)
	{
		rhi::Error error{};
		rhi::MemoryBudgetInfo budget{};

		const bool reported = Dev().QueryMemoryBudget(rhi::HeapType::eGpuLocal, budget, error);
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
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		const rhi::MappedMemory mapped = Dev().Map(buffer,
			rhi::MapDesc{
				.mode	= rhi::MapMode::eWrite,
				.offset = 0,
				.size	= test::samples::kBufferSize,
			},
			error);
		if (mapped.data == nullptr)
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			static_cast<void>(Dev().Destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		EXPECT_GE(mapped.size, test::samples::kBufferSize) << "the mapping covers less than was asked for";

		std::memset(mapped.data, 0xAB, static_cast<std::size_t>(test::samples::kBufferSize));

		if (!mapped.coherent)
		{
			EXPECT_TRUE(test::Ok(Dev().FlushMappedRange(buffer, 0, test::samples::kBufferSize, error), error));
		}

		EXPECT_TRUE(test::Ok(Dev().Unmap(buffer, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(HeapTest, ClearsTheMappingWhenMapFails)
	{
		rhi::Error error{};

		const rhi::MappedMemory mapped = Dev().Map(rhi::BufferHandle{ .index = 7777, .generation = 2 }, rhi::MapDesc{}, error);

		EXPECT_EQ(mapped.data, nullptr);
		EXPECT_EQ(mapped.size, 0u) << "a failed map reported a size for a null pointer";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(HeapTest, SetsResidencyPrioritiesOrSaysItCannot)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		const std::vector<rhi::ResidencyPriorityDesc> priorities{
			rhi::ResidencyPriorityDesc{ .buffer = buffer, .texture = {}, .priority = rhi::ResidencyPriority::eHigh },
		};

		if (!Dev().SetResidencyPriority(priorities, error))
		{
			EXPECT_TRUE(test::ErrorIsPopulated(error));
			static_cast<void>(Dev().Destroy(buffer, {}, error));
			GTEST_SKIP() << "this backend does not take residency priorities: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

} // namespace

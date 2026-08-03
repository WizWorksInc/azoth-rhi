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

#include <array>
#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class SparseTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(SparseTest);

	// One page, which is 64 KiB on Direct3D 12 and the standard block size on Vulkan. The same number on both, so a bind is expressible without asking
	// the backend what its page is.
	constexpr std::uint64_t kPageBytes = std::uint64_t{ 64 } * 1024;

	[[nodiscard]] rhi::BufferDesc SparseBuffer()
	{
		rhi::BufferDesc desc{};
		desc.size				= kPageBytes * 4;
		desc.usage				= rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopyDst;
		desc.memory				= rhi::MemoryUsage::eGpuOnly;
		desc.allowSparseBinding = true;
		desc.debugName			= "azoth.rhi.test.sparseBuffer";
		return desc;
	}

	TEST_P(SparseTest, BindsAPageIntoASparseBufferAndTakesItBackOut)
	{
		AZO_RHI_REQUIRE_CAP(Caps().sparseTier >= rhi::SparseTier::eBuffers, "sparse buffers");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(SparseBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error)) << "a device reporting sparse buffers refused to create one";

		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(kPageBytes * 4), error);
		ASSERT_TRUE(test::Ok(heap.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const std::array<rhi::SparseBufferBind, 1> bind{
			rhi::SparseBufferBind{ .buffer = buffer, .resourceOffset = 0, .page = rhi::SparseMemoryPage{ .heap = heap, .heapOffset = 0, .size = kPageBytes } },
		};

		rhi::SparseBindDesc bound{};
		bound.buffers	= bind;
		bound.debugName = "azoth.rhi.test.bindPage";
		EXPECT_TRUE(test::Ok(queue.BindSparse(bound, error), error)) << "binding one page to a sparse buffer failed";

		const std::array<rhi::SparseBufferBind, 1> unbind{
			rhi::SparseBufferBind{ .buffer = buffer, .resourceOffset = 0, .page = rhi::SparseMemoryPage{ .size = kPageBytes } },
		};

		rhi::SparseBindDesc released{};
		released.buffers   = unbind;
		released.debugName = "azoth.rhi.test.unbindPage";
		EXPECT_TRUE(test::Ok(queue.BindSparse(released, error), error)) << "unbinding a page from a sparse buffer failed";

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
	}

	TEST_P(SparseTest, RefusesASparseBindAgainstABufferThatDidNotAskForOne)
	{
		AZO_RHI_REQUIRE_CAP(Caps().sparseTier >= rhi::SparseTier::eBuffers, "sparse buffers");

		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that remembers what a buffer was created as");

		rhi::Error error{};
		const rhi::BufferHandle ordinary = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(ordinary.IsValid(), error));

		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(kPageBytes * 4), error);
		ASSERT_TRUE(test::Ok(heap.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const std::array<rhi::SparseBufferBind, 1> bind{
			rhi::SparseBufferBind{
				.buffer = ordinary, .resourceOffset = 0, .page = rhi::SparseMemoryPage{ .heap = heap, .heapOffset = 0, .size = kPageBytes } },
		};

		rhi::SparseBindDesc bound{};
		bound.buffers = bind;

		rhi::Error bindError{};
		EXPECT_FALSE(queue.BindSparse(bound, bindError)) << "a page was bound into a buffer that owns its memory outright";
		EXPECT_TRUE(test::ErrorIsPopulated(bindError));

		EXPECT_TRUE(test::Ok(Dev().Destroy(ordinary, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
	}

	TEST_P(SparseTest, ADeviceWithoutSparseRefusesToCreateASparseBuffer)
	{
		AZO_RHI_REQUIRE_CAP(Caps().sparseTier == rhi::SparseTier::eNone, "a device without sparse support");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(SparseBuffer(), error);

		EXPECT_FALSE(buffer.IsValid()) << "a device reporting no sparse support created a sparse buffer anyway";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(SparseTest, BindsATileIntoASparseTexture)
	{
		AZO_RHI_REQUIRE_CAP(Caps().sparseTier >= rhi::SparseTier::eResidentTextures, "sparse textures");

		rhi::Error error{};
		rhi::TextureDesc desc	= test::samples::SampledTexture2D();
		desc.width				= 512;
		desc.height				= 512;
		desc.mipLevels			= 1;
		desc.allowSparseBinding = true;
		desc.debugName			= "azoth.rhi.test.sparseTexture";

		const rhi::TextureHandle texture = Dev().CreateTexture(desc, error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error)) << "a device reporting sparse textures refused to create one";

		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(kPageBytes * 16), error);
		ASSERT_TRUE(test::Ok(heap.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const std::array<rhi::SparseTextureBind, 1> bind{
			rhi::SparseTextureBind{ .texture = texture,
				.subresource				 = rhi::TextureSubresource{ .aspects = rhi::TextureAspect::eColor, .mip = 0, .layer = 0 },
				.offset						 = rhi::Offset3D{ .x = 0, .y = 0, .z = 0 },
				.extent						 = rhi::Extent3D{ .width = 128, .height = 128, .depth = 1 },
				.page						 = rhi::SparseMemoryPage{ .heap = heap, .heapOffset = 0, .size = kPageBytes } },
		};

		rhi::SparseBindDesc bound{};
		bound.textures	= bind;
		bound.debugName = "azoth.rhi.test.bindTile";
		EXPECT_TRUE(test::Ok(queue.BindSparse(bound, error), error)) << "binding one tile to a sparse texture failed";

		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(heap, {}, error), error));
	}

} // namespace

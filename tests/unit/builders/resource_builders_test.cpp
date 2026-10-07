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

#include "azoth/rhi/builders/resource_builders.hpp"

#include <gtest/gtest.h>

#include <cstdint> // NOLINT
#include <limits>
#include <string>

namespace rhi = azo::rhi;

namespace
{

	TEST(BufferBuilder, DefaultsToAnEmptyDescRatherThanAUsableOne)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}.build();

		EXPECT_EQ(desc.size, 0u);
		EXPECT_EQ(desc.stride, 0u);
		EXPECT_TRUE(desc.usage.empty());
		EXPECT_EQ(desc.memory, rhi::MemoryUsage::eGpuOnly);
		EXPECT_FALSE(desc.allowAliasing);
		EXPECT_FALSE(desc.allowSparseBinding);
		EXPECT_EQ(desc.debugName, nullptr);
	}

	TEST(BufferBuilder, ChainsEveryFieldOntoTheDesc)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}
										 .size(4096)
										 .stride(16)
										 .usage(rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eVertex) | rhi::BufferUsage::eCopyDst)
										 .cpu_upload()
										 .aliasing()
										 .sparse_binding()
										 .build();

		EXPECT_EQ(desc.size, 4096u);
		EXPECT_EQ(desc.stride, 16u);
		EXPECT_TRUE(desc.usage.contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(desc.usage.contains(rhi::BufferUsage::eCopyDst));
		EXPECT_EQ(desc.memory, rhi::MemoryUsage::eCpuUpload);
		EXPECT_TRUE(desc.allowAliasing);
		EXPECT_TRUE(desc.allowSparseBinding);
	}

	TEST(BufferBuilder, UsageReplacesWhileAddUsageAccumulates)
	{
		const rhi::BufferDesc replaced = rhi::BufferBuilder{}.usage(rhi::BufferUsage::eVertex).usage(rhi::BufferUsage::eIndex).build();
		EXPECT_FALSE(replaced.usage.contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(replaced.usage.contains(rhi::BufferUsage::eIndex));

		const rhi::BufferDesc accumulated = rhi::BufferBuilder{}.usage(rhi::BufferUsage::eVertex).add_usage(rhi::BufferUsage::eIndex).build();
		EXPECT_TRUE(accumulated.usage.contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(accumulated.usage.contains(rhi::BufferUsage::eIndex));
	}

	TEST(BufferBuilder, AddUsageIsIdempotent)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}.add_usage(rhi::BufferUsage::eStorage).add_usage(rhi::BufferUsage::eStorage).build();

		EXPECT_EQ(desc.usage, rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage));
	}

	TEST(BufferBuilder, MemoryShorthandsMatchTheEnumeratorsTheyName)
	{
		EXPECT_EQ(rhi::BufferBuilder{}.gpu_only().build().memory, rhi::MemoryUsage::eGpuOnly);
		EXPECT_EQ(rhi::BufferBuilder{}.cpu_upload().build().memory, rhi::MemoryUsage::eCpuUpload);
		EXPECT_EQ(rhi::BufferBuilder{}.cpu_readback().build().memory, rhi::MemoryUsage::eCpuReadback);

		EXPECT_EQ(rhi::BufferBuilder{}.memory(rhi::MemoryUsage::eTransient).build().memory, rhi::MemoryUsage::eTransient);
	}

	TEST(BufferBuilder, DebugNamePointsIntoTheBuilderAndSurvivesUntilItIsModified)
	{
		rhi::BufferBuilder builder;
		builder.size(64).debug_name("azoth.rhi.test.named");

		const rhi::BufferDesc first	 = builder.build();
		const rhi::BufferDesc second = builder.build();

		ASSERT_NE(first.debugName, nullptr);
		EXPECT_STREQ(first.debugName, "azoth.rhi.test.named");
		EXPECT_EQ(first.debugName, second.debugName) << "two builds of an untouched builder handed back different storage";
	}

	TEST(BufferBuilder, AnEmptyDebugNameStaysNullRatherThanBecomingAnEmptyString)
	{
		EXPECT_EQ(rhi::BufferBuilder{}.debug_name("").build().debugName, nullptr);
		EXPECT_NE(rhi::BufferBuilder{}.debug_name("x").build().debugName, nullptr);
	}

	TEST(BufferBuilder, DebugNameAcceptsAViewThatIsNotNullTerminated)
	{
		const std::string source = "prefix.name.suffix";
		rhi::BufferBuilder builder;
		const rhi::BufferDesc desc = builder.debug_name(std::string_view{ source }.substr(7, 4)).build();

		ASSERT_NE(desc.debugName, nullptr);
		EXPECT_STREQ(desc.debugName, "name");
	}

	TEST(TextureBuilder, DefaultsToAUsableTwoDimensionalShapeWithNoFormatOrUsage)
	{
		const rhi::TextureDesc desc = rhi::TextureBuilder{}.build();

		EXPECT_EQ(desc.type, rhi::TextureType::eTex2D);
		EXPECT_EQ(desc.format, rhi::Format::eUndefined);
		EXPECT_EQ(desc.width, 1u);
		EXPECT_EQ(desc.height, 1u);
		EXPECT_EQ(desc.depth, 1u);
		EXPECT_EQ(desc.mipLevels, 1u);
		EXPECT_EQ(desc.arrayLayers, 1u);
		EXPECT_EQ(desc.samples, rhi::SampleCount::e1);
		EXPECT_TRUE(desc.usage.empty());
	}

	TEST(TextureBuilder, ExtentDefaultsTheDimensionsATwoDimensionalTextureDoesNotUse)
	{
		const rhi::TextureDesc flat = rhi::TextureBuilder{}.extent(256).build();
		EXPECT_EQ(flat.width, 256u);
		EXPECT_EQ(flat.height, 1u);
		EXPECT_EQ(flat.depth, 1u);

		const rhi::TextureDesc volume = rhi::TextureBuilder{}.type(rhi::TextureType::eTex3D).extent(32, 16, 8).build();
		EXPECT_EQ(volume.width, 32u);
		EXPECT_EQ(volume.height, 16u);
		EXPECT_EQ(volume.depth, 8u);
	}

	TEST(TextureBuilder, CarriesTheShapeAndUsageThroughToTheDesc)
	{
		const rhi::TextureDesc desc = rhi::TextureBuilder{}
										  .type(rhi::TextureType::eTexCube)
										  .format(rhi::Format::eRGBA16Float)
										  .extent(128, 128)
										  .mips(8)
										  .layers(6)
										  .samples(rhi::SampleCount::e4)
										  .add_usage(rhi::TextureUsage::eSampled)
										  .add_usage(rhi::TextureUsage::eColorAttachment)
										  .memory(rhi::MemoryUsage::eGpuOnly)
										  .build();

		EXPECT_EQ(desc.type, rhi::TextureType::eTexCube);
		EXPECT_EQ(desc.format, rhi::Format::eRGBA16Float);
		EXPECT_EQ(desc.mipLevels, 8u);
		EXPECT_EQ(desc.arrayLayers, 6u);
		EXPECT_EQ(desc.samples, rhi::SampleCount::e4);
		EXPECT_TRUE(desc.usage.contains(rhi::TextureUsage::eSampled));
		EXPECT_TRUE(desc.usage.contains(rhi::TextureUsage::eColorAttachment));
	}

	TEST(MapBuilder, DefaultsToWritingTheWholeBuffer)
	{
		constexpr rhi::MapDesc desc = rhi::MapBuilder{}.build();

		static_assert(desc.mode == rhi::MapMode::eWrite);
		static_assert(desc.offset == 0);
		static_assert(desc.size == std::numeric_limits<std::uint64_t>::max());

		SUCCEED();
	}

	TEST(MapBuilder, ModeShorthandsAndTheRangeSettersAgreeWithTheDesc)
	{
		EXPECT_EQ(rhi::MapBuilder{}.read().build().mode, rhi::MapMode::eRead);
		EXPECT_EQ(rhi::MapBuilder{}.write().build().mode, rhi::MapMode::eWrite);
		EXPECT_EQ(rhi::MapBuilder{}.read_write().build().mode, rhi::MapMode::eReadWrite);
		EXPECT_EQ(rhi::MapBuilder{}.mode(rhi::MapMode::eRead).build().mode, rhi::MapMode::eRead);

		const rhi::MapDesc ranged = rhi::MapBuilder{}.offset(128).size(64).build();
		EXPECT_EQ(ranged.offset, 128u);
		EXPECT_EQ(ranged.size, 64u);

		EXPECT_EQ(rhi::MapBuilder{}.size(64).whole_buffer().build().size, std::numeric_limits<std::uint64_t>::max());
	}

	TEST(HeapBuilder, DefaultsToAGpuLocalHeapThatTakesBothResourceKinds)
	{
		const rhi::HeapDesc desc = rhi::HeapBuilder{}.build();

		EXPECT_EQ(desc.type, rhi::HeapType::eGpuLocal);
		EXPECT_EQ(desc.size, 0u);
		EXPECT_EQ(desc.alignment, 0u);
		EXPECT_TRUE(desc.allowBuffers);
		EXPECT_TRUE(desc.allowTextures);
		EXPECT_FALSE(desc.allowAliasing);
	}

	TEST(HeapBuilder, NarrowsWhatAHeapWillAccept)
	{
		const rhi::HeapDesc buffersOnly = rhi::HeapBuilder{}.size(1u << 20u).allow_textures(false).aliasing().build();

		EXPECT_EQ(buffersOnly.size, 1u << 20u);
		EXPECT_TRUE(buffersOnly.allowBuffers);
		EXPECT_FALSE(buffersOnly.allowTextures);
		EXPECT_TRUE(buffersOnly.allowAliasing);
	}

	TEST(PlacedBuilders, CarryTheResourceDescTheHeapAndTheOffset)
	{
		constexpr rhi::HeapHandle heap{
			.index		= 4,
			.generation = 1,
		};

		const rhi::BufferDesc buffer			 = rhi::BufferBuilder{}.size(512).add_usage(rhi::BufferUsage::eStorage).build();
		const rhi::PlacedBufferDesc placedBuffer = rhi::PlacedBufferBuilder{}.buffer(buffer).heap(heap).offset(256).build();

		EXPECT_EQ(placedBuffer.heap, heap);
		EXPECT_EQ(placedBuffer.offset, 256u);
		EXPECT_EQ(placedBuffer.buffer.size, 512u);

		const rhi::TextureDesc texture			   = rhi::TextureBuilder{}.format(rhi::Format::eRGBA8UNorm).extent(64, 64).build();
		const rhi::PlacedTextureDesc placedTexture = rhi::PlacedTextureBuilder{}.texture(texture).heap(heap).offset(1024).build();

		EXPECT_EQ(placedTexture.heap, heap);
		EXPECT_EQ(placedTexture.offset, 1024u);
		EXPECT_EQ(placedTexture.texture.width, 64u);
	}

	TEST(ResidencyPriorityBuilder, NamesOneResourceRatherThanBoth)
	{
		constexpr rhi::BufferHandle buffer{
			.index		= 1,
			.generation = 1,
		};
		constexpr rhi::TextureHandle texture{
			.index		= 2,
			.generation = 1,
		};

		const rhi::ResidencyPriorityDesc asBuffer = rhi::ResidencyPriorityBuilder{}.buffer(buffer).priority(rhi::ResidencyPriority::eHigh).build();
		EXPECT_EQ(asBuffer.buffer, buffer);
		EXPECT_FALSE(asBuffer.texture.is_valid());
		EXPECT_EQ(asBuffer.priority, rhi::ResidencyPriority::eHigh);

		const rhi::ResidencyPriorityDesc asTexture = rhi::ResidencyPriorityBuilder{}.buffer(buffer).texture(texture).build();
		EXPECT_EQ(asTexture.texture, texture);
		EXPECT_FALSE(asTexture.buffer.is_valid()) << "naming a texture left the earlier buffer set as well";
		EXPECT_EQ(asTexture.priority, rhi::ResidencyPriority::eNormal);
	}

} // namespace

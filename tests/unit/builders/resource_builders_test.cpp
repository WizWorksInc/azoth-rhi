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

#include <cstdint>
#include <limits>
#include <string>

namespace rhi = azo::rhi;

namespace
{

	TEST(BufferBuilder, DefaultsToAnEmptyDescRatherThanAUsableOne)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}.Build();

		EXPECT_EQ(desc.size, 0u);
		EXPECT_EQ(desc.stride, 0u);
		EXPECT_TRUE(desc.usage.Empty());
		EXPECT_EQ(desc.memory, rhi::MemoryUsage::eGpuOnly);
		EXPECT_FALSE(desc.allowAliasing);
		EXPECT_FALSE(desc.allowSparseBinding);
		EXPECT_FALSE(desc.persistentMap);
		EXPECT_EQ(desc.debugName, nullptr);
	}

	TEST(BufferBuilder, ChainsEveryFieldOntoTheDesc)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}
										 .Size(4096)
										 .Stride(16)
										 .Usage(rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eVertex) | rhi::BufferUsage::eCopyDst)
										 .CpuUpload()
										 .Aliasing()
										 .SparseBinding()
										 .PersistentMap()
										 .Build();

		EXPECT_EQ(desc.size, 4096u);
		EXPECT_EQ(desc.stride, 16u);
		EXPECT_TRUE(desc.usage.Contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(desc.usage.Contains(rhi::BufferUsage::eCopyDst));
		EXPECT_EQ(desc.memory, rhi::MemoryUsage::eCpuUpload);
		EXPECT_TRUE(desc.allowAliasing);
		EXPECT_TRUE(desc.allowSparseBinding);
		EXPECT_TRUE(desc.persistentMap);
	}

	TEST(BufferBuilder, UsageReplacesWhileAddUsageAccumulates)
	{
		const rhi::BufferDesc replaced = rhi::BufferBuilder{}.Usage(rhi::BufferUsage::eVertex).Usage(rhi::BufferUsage::eIndex).Build();
		EXPECT_FALSE(replaced.usage.Contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(replaced.usage.Contains(rhi::BufferUsage::eIndex));

		const rhi::BufferDesc accumulated = rhi::BufferBuilder{}.Usage(rhi::BufferUsage::eVertex).AddUsage(rhi::BufferUsage::eIndex).Build();
		EXPECT_TRUE(accumulated.usage.Contains(rhi::BufferUsage::eVertex));
		EXPECT_TRUE(accumulated.usage.Contains(rhi::BufferUsage::eIndex));
	}

	TEST(BufferBuilder, AddUsageIsIdempotent)
	{
		const rhi::BufferDesc desc = rhi::BufferBuilder{}.AddUsage(rhi::BufferUsage::eStorage).AddUsage(rhi::BufferUsage::eStorage).Build();

		EXPECT_EQ(desc.usage, rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage));
	}

	TEST(BufferBuilder, MemoryShorthandsMatchTheEnumeratorsTheyName)
	{
		EXPECT_EQ(rhi::BufferBuilder{}.GpuOnly().Build().memory, rhi::MemoryUsage::eGpuOnly);
		EXPECT_EQ(rhi::BufferBuilder{}.CpuUpload().Build().memory, rhi::MemoryUsage::eCpuUpload);
		EXPECT_EQ(rhi::BufferBuilder{}.CpuReadback().Build().memory, rhi::MemoryUsage::eCpuReadback);

		EXPECT_EQ(rhi::BufferBuilder{}.Memory(rhi::MemoryUsage::eTransient).Build().memory, rhi::MemoryUsage::eTransient);
	}

	TEST(BufferBuilder, DebugNamePointsIntoTheBuilderAndSurvivesUntilItIsModified)
	{
		rhi::BufferBuilder builder;
		builder.Size(64).DebugName("azoth.rhi.test.named");

		const rhi::BufferDesc first	 = builder.Build();
		const rhi::BufferDesc second = builder.Build();

		ASSERT_NE(first.debugName, nullptr);
		EXPECT_STREQ(first.debugName, "azoth.rhi.test.named");
		EXPECT_EQ(first.debugName, second.debugName) << "two builds of an untouched builder handed back different storage";
	}

	TEST(BufferBuilder, AnEmptyDebugNameStaysNullRatherThanBecomingAnEmptyString)
	{
		EXPECT_EQ(rhi::BufferBuilder{}.DebugName("").Build().debugName, nullptr);
		EXPECT_NE(rhi::BufferBuilder{}.DebugName("x").Build().debugName, nullptr);
	}

	TEST(BufferBuilder, DebugNameAcceptsAViewThatIsNotNullTerminated)
	{
		const std::string source = "prefix.name.suffix";
		rhi::BufferBuilder builder;
		const rhi::BufferDesc desc = builder.DebugName(std::string_view{ source }.substr(7, 4)).Build();

		ASSERT_NE(desc.debugName, nullptr);
		EXPECT_STREQ(desc.debugName, "name");
	}

	TEST(TextureBuilder, DefaultsToAUsableTwoDimensionalShapeWithNoFormatOrUsage)
	{
		const rhi::TextureDesc desc = rhi::TextureBuilder{}.Build();

		EXPECT_EQ(desc.type, rhi::TextureType::eTex2D);
		EXPECT_EQ(desc.format, rhi::Format::eUndefined);
		EXPECT_EQ(desc.width, 1u);
		EXPECT_EQ(desc.height, 1u);
		EXPECT_EQ(desc.depth, 1u);
		EXPECT_EQ(desc.mipLevels, 1u);
		EXPECT_EQ(desc.arrayLayers, 1u);
		EXPECT_EQ(desc.samples, rhi::SampleCount::e1);
		EXPECT_TRUE(desc.usage.Empty());
		EXPECT_EQ(desc.initialLayout, rhi::TextureLayout::eUndefined);
	}

	TEST(TextureBuilder, ExtentDefaultsTheDimensionsATwoDimensionalTextureDoesNotUse)
	{
		const rhi::TextureDesc flat = rhi::TextureBuilder{}.Extent(256).Build();
		EXPECT_EQ(flat.width, 256u);
		EXPECT_EQ(flat.height, 1u);
		EXPECT_EQ(flat.depth, 1u);

		const rhi::TextureDesc volume = rhi::TextureBuilder{}.Type(rhi::TextureType::eTex3D).Extent(32, 16, 8).Build();
		EXPECT_EQ(volume.width, 32u);
		EXPECT_EQ(volume.height, 16u);
		EXPECT_EQ(volume.depth, 8u);
	}

	TEST(TextureBuilder, CarriesTheShapeAndUsageThroughToTheDesc)
	{
		const rhi::TextureDesc desc = rhi::TextureBuilder{}
										  .Type(rhi::TextureType::eTexCube)
										  .Format(rhi::Format::eRGBA16Float)
										  .Extent(128, 128)
										  .Mips(8)
										  .Layers(6)
										  .Samples(rhi::SampleCount::e4)
										  .AddUsage(rhi::TextureUsage::eSampled)
										  .AddUsage(rhi::TextureUsage::eColorAttachment)
										  .InitialLayout(rhi::TextureLayout::eShaderReadOnly)
										  .Memory(rhi::MemoryUsage::eGpuOnly)
										  .Build();

		EXPECT_EQ(desc.type, rhi::TextureType::eTexCube);
		EXPECT_EQ(desc.format, rhi::Format::eRGBA16Float);
		EXPECT_EQ(desc.mipLevels, 8u);
		EXPECT_EQ(desc.arrayLayers, 6u);
		EXPECT_EQ(desc.samples, rhi::SampleCount::e4);
		EXPECT_TRUE(desc.usage.Contains(rhi::TextureUsage::eSampled));
		EXPECT_TRUE(desc.usage.Contains(rhi::TextureUsage::eColorAttachment));
		EXPECT_EQ(desc.initialLayout, rhi::TextureLayout::eShaderReadOnly);
	}

	TEST(MapBuilder, DefaultsToWritingTheWholeBuffer)
	{
		constexpr rhi::MapDesc desc = rhi::MapBuilder{}.Build();

		static_assert(desc.mode == rhi::MapMode::eWrite);
		static_assert(desc.offset == 0);
		static_assert(desc.size == std::numeric_limits<std::uint64_t>::max());

		SUCCEED();
	}

	TEST(MapBuilder, ModeShorthandsAndTheRangeSettersAgreeWithTheDesc)
	{
		EXPECT_EQ(rhi::MapBuilder{}.Read().Build().mode, rhi::MapMode::eRead);
		EXPECT_EQ(rhi::MapBuilder{}.Write().Build().mode, rhi::MapMode::eWrite);
		EXPECT_EQ(rhi::MapBuilder{}.ReadWrite().Build().mode, rhi::MapMode::eReadWrite);
		EXPECT_EQ(rhi::MapBuilder{}.Mode(rhi::MapMode::eRead).Build().mode, rhi::MapMode::eRead);

		const rhi::MapDesc ranged = rhi::MapBuilder{}.Offset(128).Size(64).Build();
		EXPECT_EQ(ranged.offset, 128u);
		EXPECT_EQ(ranged.size, 64u);

		EXPECT_EQ(rhi::MapBuilder{}.Size(64).WholeBuffer().Build().size, std::numeric_limits<std::uint64_t>::max());
	}

	TEST(HeapBuilder, DefaultsToAGpuLocalHeapThatTakesBothResourceKinds)
	{
		const rhi::HeapDesc desc = rhi::HeapBuilder{}.Build();

		EXPECT_EQ(desc.type, rhi::HeapType::eGpuLocal);
		EXPECT_EQ(desc.size, 0u);
		EXPECT_EQ(desc.alignment, 0u);
		EXPECT_TRUE(desc.allowBuffers);
		EXPECT_TRUE(desc.allowTextures);
		EXPECT_FALSE(desc.allowAliasing);
	}

	TEST(HeapBuilder, NarrowsWhatAHeapWillAccept)
	{
		const rhi::HeapDesc buffersOnly = rhi::HeapBuilder{}.Size(1u << 20u).AllowTextures(false).Aliasing().Build();

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

		const rhi::BufferDesc buffer			 = rhi::BufferBuilder{}.Size(512).AddUsage(rhi::BufferUsage::eStorage).Build();
		const rhi::PlacedBufferDesc placedBuffer = rhi::PlacedBufferBuilder{}.Buffer(buffer).Heap(heap).Offset(256).Build();

		EXPECT_EQ(placedBuffer.heap, heap);
		EXPECT_EQ(placedBuffer.offset, 256u);
		EXPECT_EQ(placedBuffer.buffer.size, 512u);

		const rhi::TextureDesc texture			   = rhi::TextureBuilder{}.Format(rhi::Format::eRGBA8UNorm).Extent(64, 64).Build();
		const rhi::PlacedTextureDesc placedTexture = rhi::PlacedTextureBuilder{}.Texture(texture).Heap(heap).Offset(1024).Build();

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

		const rhi::ResidencyPriorityDesc asBuffer = rhi::ResidencyPriorityBuilder{}.Buffer(buffer).Priority(rhi::ResidencyPriority::eHigh).Build();
		EXPECT_EQ(asBuffer.buffer, buffer);
		EXPECT_FALSE(asBuffer.texture.IsValid());
		EXPECT_EQ(asBuffer.priority, rhi::ResidencyPriority::eHigh);

		const rhi::ResidencyPriorityDesc asTexture = rhi::ResidencyPriorityBuilder{}.Buffer(buffer).Texture(texture).Build();
		EXPECT_EQ(asTexture.texture, texture);
		EXPECT_FALSE(asTexture.buffer.IsValid()) << "naming a texture left the earlier buffer set as well";
		EXPECT_EQ(asTexture.priority, rhi::ResidencyPriority::eNormal);
	}

} // namespace

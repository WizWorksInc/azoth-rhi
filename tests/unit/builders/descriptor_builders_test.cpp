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

#include "azoth/rhi/builders/descriptor_builders.hpp"

#include "harness/spans.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	TEST(DescriptorBindingBuilder, DefaultsToASingleUniformBufferVisibleEverywhere)
	{
		constexpr rhi::DescriptorBinding binding = rhi::DescriptorBindingBuilder{}.Build();

		static_assert(binding.binding == 0);
		static_assert(binding.type == rhi::DescriptorType::eUniformBuffer);
		static_assert(binding.count == 1);
		static_assert(binding.flags.Empty());

		EXPECT_TRUE(binding.stages.Contains(rhi::ShaderStage::eVertex));
		EXPECT_TRUE(binding.stages.Contains(rhi::ShaderStage::eFragment));
	}

	TEST(DescriptorBindingBuilder, CarriesSlotTypeCountAndVisibility)
	{
		const rhi::DescriptorBinding binding = rhi::DescriptorBindingBuilder{}
												   .Binding(3)
												   .Type(rhi::DescriptorType::eTextureSRV)
												   .Count(64)
												   .Stages(rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eFragment) | rhi::ShaderStage::eCompute)
												   .Build();

		EXPECT_EQ(binding.binding, 3u);
		EXPECT_EQ(binding.type, rhi::DescriptorType::eTextureSRV);
		EXPECT_EQ(binding.count, 64u);
		EXPECT_TRUE(binding.stages.Contains(rhi::ShaderStage::eFragment));
		EXPECT_FALSE(binding.stages.Contains(rhi::ShaderStage::eVertex));
	}

	TEST(DescriptorBindingBuilder, FlagsReplacesWhileAddFlagAccumulates)
	{
		const rhi::DescriptorBinding replaced =
			rhi::DescriptorBindingBuilder{}.Flags(rhi::DescriptorBindingFlag::eBindless).Flags(rhi::DescriptorBindingFlag::ePartiallyBound).Build();
		EXPECT_FALSE(replaced.flags.Contains(rhi::DescriptorBindingFlag::eBindless));
		EXPECT_TRUE(replaced.flags.Contains(rhi::DescriptorBindingFlag::ePartiallyBound));

		const rhi::DescriptorBinding accumulated = rhi::DescriptorBindingBuilder{}
													   .AddFlag(rhi::DescriptorBindingFlag::eBindless)
													   .AddFlag(rhi::DescriptorBindingFlag::ePartiallyBound)
													   .AddFlag(rhi::DescriptorBindingFlag::eUpdateAfterBind)
													   .Build();
		EXPECT_TRUE(accumulated.flags.Contains(rhi::DescriptorBindingFlag::eBindless));
		EXPECT_TRUE(accumulated.flags.Contains(rhi::DescriptorBindingFlag::ePartiallyBound));
		EXPECT_TRUE(accumulated.flags.Contains(rhi::DescriptorBindingFlag::eUpdateAfterBind));
	}

	TEST(DescriptorSetLayoutBuilder, StartsEmpty)
	{
		const rhi::DescriptorSetLayoutBuilder builder;
		const rhi::DescriptorSetLayoutDesc desc = builder.Build();

		EXPECT_TRUE(desc.bindings.empty());
		EXPECT_EQ(desc.debugName, nullptr);
	}

	TEST(DescriptorSetLayoutBuilder, AccumulatesBindingsInTheOrderTheyWereAdded)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		builder.Binding(rhi::DescriptorBindingBuilder{}.Binding(0).Type(rhi::DescriptorType::eUniformBuffer).Build())
			.Binding(rhi::DescriptorBindingBuilder{}.Binding(1).Type(rhi::DescriptorType::eTextureSRV).Build())
			.Binding(rhi::DescriptorBindingBuilder{}.Binding(2).Type(rhi::DescriptorType::eSampler).Build());

		const rhi::DescriptorSetLayoutDesc desc = builder.Build();

		ASSERT_EQ(desc.bindings.size(), 3u);
		EXPECT_EQ(test::At(desc.bindings, 0).type, rhi::DescriptorType::eUniformBuffer);
		EXPECT_EQ(test::At(desc.bindings, 1).type, rhi::DescriptorType::eTextureSRV);
		EXPECT_EQ(test::At(desc.bindings, 2).type, rhi::DescriptorType::eSampler);
	}

	TEST(DescriptorSetLayoutBuilder, TakesABatchOfBindingsAndCopiesThem)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		{
			const std::array bindings{ rhi::DescriptorBindingBuilder{}.Binding(0).Build(), rhi::DescriptorBindingBuilder{}.Binding(1).Build() };
			builder.Bindings(bindings);
		}

		const rhi::DescriptorSetLayoutDesc desc = builder.Build();
		ASSERT_EQ(desc.bindings.size(), 2u);
		EXPECT_EQ(test::At(desc.bindings, 0).binding, 0u);
		EXPECT_EQ(test::At(desc.bindings, 1).binding, 1u);
	}

	TEST(DescriptorSetLayoutBuilder, ClearBindingsEmptiesTheLayoutForReuse)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		builder.Binding(rhi::DescriptorBindingBuilder{}.Binding(0).Build()).ClearBindings().Binding(rhi::DescriptorBindingBuilder{}.Binding(9).Build());

		const rhi::DescriptorSetLayoutDesc desc = builder.Build();
		ASSERT_EQ(desc.bindings.size(), 1u);
		EXPECT_EQ(test::At(desc.bindings, 0).binding, 9u);
	}

	TEST(DescriptorSetLayoutBuilder, BindingSpanPointsIntoStorageTheBuilderOwns)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		for (std::uint32_t slot = 0; slot < 32; ++slot)
		{
			builder.Binding(rhi::DescriptorBindingBuilder{}.Binding(slot).Build());
		}

		const rhi::DescriptorSetLayoutDesc desc = builder.Build();
		ASSERT_EQ(desc.bindings.size(), 32u);
		for (std::uint32_t slot = 0; slot < 32; ++slot)
		{
			EXPECT_EQ(test::At(desc.bindings, slot).binding, slot);
		}
	}

	TEST(PipelineLayoutBuilder, StartsWithNoSetsAndNoPushConstants)
	{
		const rhi::PipelineLayoutBuilder builder;
		const rhi::PipelineLayoutDesc desc = builder.Build();

		EXPECT_TRUE(desc.sets.empty());
		EXPECT_TRUE(desc.pushConstants.empty());
	}

	TEST(PipelineLayoutBuilder, KeepsSetOrderBecauseTheIndexIsTheBindingSlot)
	{
		// A pipeline layout's set order is not cosmetic. BindDescriptorSet names a set by index so reordering here would silently rebind everything one
		// slot over.
		constexpr rhi::DescriptorSetLayoutHandle first{
			.index		= 1,
			.generation = 1,
		};
		constexpr rhi::DescriptorSetLayoutHandle second{
			.index		= 2,
			.generation = 1,
		};
		constexpr rhi::DescriptorSetLayoutHandle third{
			.index		= 3,
			.generation = 1,
		};

		rhi::PipelineLayoutBuilder builder;
		builder.Set(first).Set(second).Set(third);

		const rhi::PipelineLayoutDesc desc = builder.Build();
		ASSERT_EQ(desc.sets.size(), 3u);
		EXPECT_EQ(desc.sets[0], first);
		EXPECT_EQ(desc.sets[1], second);
		EXPECT_EQ(desc.sets[2], third);
	}

	TEST(PipelineLayoutBuilder, TakesPushConstantsEitherAsARangeOrAsItsParts)
	{
		rhi::PipelineLayoutBuilder builder;
		builder
			.PushConstant(rhi::PushConstantRange{
				.stages = rhi::ShaderStage::eVertex,
				.offset = 0,
				.size	= 16,
			})
			.PushConstant(rhi::ShaderStage::eFragment, 16, 32);

		const rhi::PipelineLayoutDesc desc = builder.Build();
		ASSERT_EQ(desc.pushConstants.size(), 2u);

		EXPECT_TRUE(test::At(desc.pushConstants, 0).stages.Contains(rhi::ShaderStage::eVertex));
		EXPECT_EQ(test::At(desc.pushConstants, 0).size, 16u);

		EXPECT_TRUE(test::At(desc.pushConstants, 1).stages.Contains(rhi::ShaderStage::eFragment));
		EXPECT_EQ(test::At(desc.pushConstants, 1).offset, 16u);
		EXPECT_EQ(test::At(desc.pushConstants, 1).size, 32u);
	}

	TEST(PipelineLayoutBuilder, ClearsSetsAndPushConstantsIndependently)
	{
		constexpr rhi::DescriptorSetLayoutHandle layout{
			.index		= 1,
			.generation = 1,
		};

		rhi::PipelineLayoutBuilder builder;
		builder.Set(layout).PushConstant(rhi::ShaderStage::eAll, 0, 4).ClearSets();

		const rhi::PipelineLayoutDesc afterClearSets = builder.Build();
		EXPECT_TRUE(afterClearSets.sets.empty());
		EXPECT_EQ(afterClearSets.pushConstants.size(), 1u) << "clearing the sets also dropped the push constants";

		builder.ClearPushConstants();
		EXPECT_TRUE(builder.Build().pushConstants.empty());
	}

	TEST(DescriptorArenaBuilder, DefaultsToAFrameTransientShaderVisibleArena)
	{
		const rhi::DescriptorArenaDesc desc = rhi::DescriptorArenaBuilder{}.Build();

		EXPECT_EQ(desc.type, rhi::DescriptorArenaType::eFrameTransient);
		EXPECT_TRUE(desc.shaderVisible);
		EXPECT_EQ(desc.maxSets, 0u);
		EXPECT_EQ(desc.maxDescriptors, 0u);
	}

	TEST(DescriptorArenaBuilder, TypeShorthandsMatchTheEnumeratorsTheyName)
	{
		EXPECT_EQ(rhi::DescriptorArenaBuilder{}.Persistent().Build().type, rhi::DescriptorArenaType::ePersistent);
		EXPECT_EQ(rhi::DescriptorArenaBuilder{}.Persistent().FrameTransient().Build().type, rhi::DescriptorArenaType::eFrameTransient);

		const rhi::DescriptorArenaDesc sized = rhi::DescriptorArenaBuilder{}.MaxSets(8).MaxDescriptors(256).ShaderVisible(false).Build();
		EXPECT_EQ(sized.maxSets, 8u);
		EXPECT_EQ(sized.maxDescriptors, 256u);
		EXPECT_FALSE(sized.shaderVisible);
	}

	TEST(DescriptorSetAllocBuilder, CarriesTheLayoutAndTheVariableCount)
	{
		constexpr rhi::DescriptorSetLayoutHandle layout{
			.index		= 5,
			.generation = 2,
		};

		const rhi::DescriptorSetAllocDesc desc = rhi::DescriptorSetAllocBuilder{}.Layout(layout).VariableDescriptorCount(1024).Build();

		EXPECT_EQ(desc.layout, layout);
		EXPECT_EQ(desc.variableDescriptorCount, 1024u);
	}

	TEST(DescriptorWriteBuilders, DefaultTheRangeToTheWholeBufferAndTheLayoutToShaderReadOnly)
	{
		constexpr rhi::DescriptorWriteBuffer buffer = rhi::DescriptorWriteBufferBuilder{}.Build();
		static_assert(buffer.range == std::numeric_limits<std::uint64_t>::max());
		static_assert(buffer.offset == 0);
		static_assert(buffer.type == rhi::DescriptorType::eUniformBuffer);

		constexpr rhi::DescriptorWriteTexture texture = rhi::DescriptorWriteTextureBuilder{}.Build();
		static_assert(texture.expectedLayout == rhi::TextureLayout::eShaderReadOnly);
		static_assert(texture.type == rhi::DescriptorType::eTextureSRV);

		SUCCEED();
	}

	TEST(DescriptorWriteBuilders, TakeTheBindingAndTheArrayIndexTogether)
	{
		constexpr rhi::DescriptorSetHandle set{
			.index		= 1,
			.generation = 1,
		};
		constexpr rhi::BufferHandle buffer{
			.index		= 2,
			.generation = 1,
		};

		const rhi::DescriptorWriteBuffer write =
			rhi::DescriptorWriteBufferBuilder{}.Set(set).Binding(4, 17).Type(rhi::DescriptorType::eStorageBuffer).Buffer(buffer).Range(64, 128).Build();

		EXPECT_EQ(write.set, set);
		EXPECT_EQ(write.binding, 4u);
		EXPECT_EQ(write.arrayIndex, 17u);
		EXPECT_EQ(write.type, rhi::DescriptorType::eStorageBuffer);
		EXPECT_EQ(write.buffer, buffer);
		EXPECT_EQ(write.offset, 64u);
		EXPECT_EQ(write.range, 128u);
	}

	TEST(DescriptorWriteBuilders, CarryAViewAndASamplerTogetherForCombinedBindings)
	{
		constexpr rhi::TextureViewHandle view{
			.index		= 3,
			.generation = 1,
		};
		constexpr rhi::SamplerHandle sampler{
			.index		= 4,
			.generation = 1,
		};

		const rhi::DescriptorWriteTexture write = rhi::DescriptorWriteTextureBuilder{}
													  .View(view)
													  .Sampler(sampler)
													  .ExpectedLayout(rhi::TextureLayout::eGeneral)
													  .Type(rhi::DescriptorType::eTextureUAV)
													  .Build();

		EXPECT_EQ(write.view, view);
		EXPECT_EQ(write.sampler, sampler);
		EXPECT_EQ(write.expectedLayout, rhi::TextureLayout::eGeneral);
		EXPECT_EQ(write.type, rhi::DescriptorType::eTextureUAV);

		const rhi::DescriptorWriteSampler samplerOnly = rhi::DescriptorWriteSamplerBuilder{}.Binding(2).Sampler(sampler).Build();
		EXPECT_EQ(samplerOnly.binding, 2u);
		EXPECT_EQ(samplerOnly.sampler, sampler);
	}

} // namespace

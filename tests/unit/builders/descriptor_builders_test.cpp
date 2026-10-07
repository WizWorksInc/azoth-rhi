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
#include <cstdint> // NOLINT

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	TEST(DescriptorBindingBuilder, DefaultsToASingleUniformBufferVisibleEverywhere)
	{
		constexpr rhi::DescriptorBinding binding = rhi::DescriptorBindingBuilder{}.build();

		static_assert(binding.binding == 0);
		static_assert(binding.type == rhi::DescriptorType::eUniformBuffer);
		static_assert(binding.count == 1);
		static_assert(binding.flags.empty());

		EXPECT_TRUE(binding.stages.contains(rhi::ShaderStage::eVertex));
		EXPECT_TRUE(binding.stages.contains(rhi::ShaderStage::eFragment));
	}

	TEST(DescriptorBindingBuilder, CarriesSlotTypeCountAndVisibility)
	{
		const rhi::DescriptorBinding binding = rhi::DescriptorBindingBuilder{}
												   .binding(3)
												   .type(rhi::DescriptorType::eTextureSRV)
												   .count(64)
												   .stages(rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eFragment) | rhi::ShaderStage::eCompute)
												   .build();

		EXPECT_EQ(binding.binding, 3u);
		EXPECT_EQ(binding.type, rhi::DescriptorType::eTextureSRV);
		EXPECT_EQ(binding.count, 64u);
		EXPECT_TRUE(binding.stages.contains(rhi::ShaderStage::eFragment));
		EXPECT_FALSE(binding.stages.contains(rhi::ShaderStage::eVertex));
	}

	TEST(DescriptorBindingBuilder, FlagsReplacesWhileAddFlagAccumulates)
	{
		const rhi::DescriptorBinding replaced =
			rhi::DescriptorBindingBuilder{}.flags(rhi::DescriptorBindingFlag::eBindless).flags(rhi::DescriptorBindingFlag::ePartiallyBound).build();
		EXPECT_FALSE(replaced.flags.contains(rhi::DescriptorBindingFlag::eBindless));
		EXPECT_TRUE(replaced.flags.contains(rhi::DescriptorBindingFlag::ePartiallyBound));

		const rhi::DescriptorBinding accumulated = rhi::DescriptorBindingBuilder{}
													   .add_flag(rhi::DescriptorBindingFlag::eBindless)
													   .add_flag(rhi::DescriptorBindingFlag::ePartiallyBound)
													   .add_flag(rhi::DescriptorBindingFlag::eUpdateAfterBind)
													   .build();
		EXPECT_TRUE(accumulated.flags.contains(rhi::DescriptorBindingFlag::eBindless));
		EXPECT_TRUE(accumulated.flags.contains(rhi::DescriptorBindingFlag::ePartiallyBound));
		EXPECT_TRUE(accumulated.flags.contains(rhi::DescriptorBindingFlag::eUpdateAfterBind));
	}

	TEST(DescriptorSetLayoutBuilder, StartsEmpty)
	{
		const rhi::DescriptorSetLayoutBuilder builder;
		const rhi::DescriptorSetLayoutDesc desc = builder.build();

		EXPECT_TRUE(desc.bindings.empty());
		EXPECT_EQ(desc.debugName, nullptr);
	}

	TEST(DescriptorSetLayoutBuilder, AccumulatesBindingsInTheOrderTheyWereAdded)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		builder.binding(rhi::DescriptorBindingBuilder{}.binding(0).type(rhi::DescriptorType::eUniformBuffer).build())
			.binding(rhi::DescriptorBindingBuilder{}.binding(1).type(rhi::DescriptorType::eTextureSRV).build())
			.binding(rhi::DescriptorBindingBuilder{}.binding(2).type(rhi::DescriptorType::eSampler).build());

		const rhi::DescriptorSetLayoutDesc desc = builder.build();

		ASSERT_EQ(desc.bindings.size(), 3u);
		EXPECT_EQ(test::At(desc.bindings, 0).type, rhi::DescriptorType::eUniformBuffer);
		EXPECT_EQ(test::At(desc.bindings, 1).type, rhi::DescriptorType::eTextureSRV);
		EXPECT_EQ(test::At(desc.bindings, 2).type, rhi::DescriptorType::eSampler);
	}

	TEST(DescriptorSetLayoutBuilder, TakesABatchOfBindingsAndCopiesThem)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		{
			const std::array bindings{ rhi::DescriptorBindingBuilder{}.binding(0).build(), rhi::DescriptorBindingBuilder{}.binding(1).build() };
			builder.bindings(bindings);
		}

		const rhi::DescriptorSetLayoutDesc desc = builder.build();
		ASSERT_EQ(desc.bindings.size(), 2u);
		EXPECT_EQ(test::At(desc.bindings, 0).binding, 0u);
		EXPECT_EQ(test::At(desc.bindings, 1).binding, 1u);
	}

	TEST(DescriptorSetLayoutBuilder, ClearBindingsEmptiesTheLayoutForReuse)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		builder.binding(rhi::DescriptorBindingBuilder{}.binding(0).build()).clear_bindings().binding(rhi::DescriptorBindingBuilder{}.binding(9).build());

		const rhi::DescriptorSetLayoutDesc desc = builder.build();
		ASSERT_EQ(desc.bindings.size(), 1u);
		EXPECT_EQ(test::At(desc.bindings, 0).binding, 9u);
	}

	TEST(DescriptorSetLayoutBuilder, BindingSpanPointsIntoStorageTheBuilderOwns)
	{
		rhi::DescriptorSetLayoutBuilder builder;
		for (std::uint32_t slot = 0; slot < 32; ++slot)
		{
			builder.binding(rhi::DescriptorBindingBuilder{}.binding(slot).build());
		}

		const rhi::DescriptorSetLayoutDesc desc = builder.build();
		ASSERT_EQ(desc.bindings.size(), 32u);
		for (std::uint32_t slot = 0; slot < 32; ++slot)
		{
			EXPECT_EQ(test::At(desc.bindings, slot).binding, slot);
		}
	}

	TEST(PipelineLayoutBuilder, StartsWithNoSetsAndNoPushConstants)
	{
		const rhi::PipelineLayoutBuilder builder;
		const rhi::PipelineLayoutDesc desc = builder.build();

		EXPECT_TRUE(desc.sets.empty());
		EXPECT_TRUE(desc.pushConstants.empty());
	}

	TEST(PipelineLayoutBuilder, KeepsSetOrderBecauseTheIndexIsTheBindingSlot)
	{
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
		builder.set(first).set(second).set(third);

		const rhi::PipelineLayoutDesc desc = builder.build();
		ASSERT_EQ(desc.sets.size(), 3u);
		EXPECT_EQ(desc.sets[0], first);
		EXPECT_EQ(desc.sets[1], second);
		EXPECT_EQ(desc.sets[2], third);
	}

	TEST(PipelineLayoutBuilder, TakesPushConstantsEitherAsARangeOrAsItsParts)
	{
		rhi::PipelineLayoutBuilder builder;
		builder
			.push_constant(
				rhi::PushConstantRange{
					.stages = rhi::ShaderStage::eVertex,
					.offset = 0,
					.size	= 16,
				}
			)
			.push_constant(rhi::ShaderStage::eFragment, 16, 32);

		const rhi::PipelineLayoutDesc desc = builder.build();
		ASSERT_EQ(desc.pushConstants.size(), 2u);

		EXPECT_TRUE(test::At(desc.pushConstants, 0).stages.contains(rhi::ShaderStage::eVertex));
		EXPECT_EQ(test::At(desc.pushConstants, 0).size, 16u);

		EXPECT_TRUE(test::At(desc.pushConstants, 1).stages.contains(rhi::ShaderStage::eFragment));
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
		builder.set(layout).push_constant(rhi::ShaderStage::eAll, 0, 4).clear_sets();

		const rhi::PipelineLayoutDesc afterClearSets = builder.build();
		EXPECT_TRUE(afterClearSets.sets.empty());
		EXPECT_EQ(afterClearSets.pushConstants.size(), 1u) << "clearing the sets also dropped the push constants";

		builder.clear_push_constants();
		EXPECT_TRUE(builder.build().pushConstants.empty());
	}

	TEST(DescriptorArenaBuilder, DefaultsToAFrameTransientShaderVisibleArena)
	{
		const rhi::DescriptorArenaDesc desc = rhi::DescriptorArenaBuilder{}.build();

		EXPECT_EQ(desc.type, rhi::DescriptorArenaType::eFrameTransient);
		EXPECT_TRUE(desc.shaderVisible);
		EXPECT_EQ(desc.maxSets, 0u);
		EXPECT_EQ(desc.maxDescriptors, 0u);
	}

	TEST(DescriptorArenaBuilder, TypeShorthandsMatchTheEnumeratorsTheyName)
	{
		EXPECT_EQ(rhi::DescriptorArenaBuilder{}.persistent().build().type, rhi::DescriptorArenaType::ePersistent);
		EXPECT_EQ(rhi::DescriptorArenaBuilder{}.persistent().frame_transient().build().type, rhi::DescriptorArenaType::eFrameTransient);

		const rhi::DescriptorArenaDesc sized = rhi::DescriptorArenaBuilder{}.max_sets(8).max_descriptors(256).shader_visible(false).build();
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

		const rhi::DescriptorSetAllocDesc desc = rhi::DescriptorSetAllocBuilder{}.layout(layout).variable_descriptor_count(1024).build();

		EXPECT_EQ(desc.layout, layout);
		EXPECT_EQ(desc.variableDescriptorCount, 1024u);
	}

	TEST(DescriptorWriteBuilders, DefaultTheRangeToTheWholeBufferAndTheLayoutToShaderReadOnly)
	{
		constexpr rhi::DescriptorWriteBuffer buffer = rhi::DescriptorWriteBufferBuilder{}.build();
		static_assert(buffer.range == std::numeric_limits<std::uint64_t>::max());
		static_assert(buffer.offset == 0);
		static_assert(buffer.type == rhi::DescriptorType::eUniformBuffer);

		constexpr rhi::DescriptorWriteTexture texture = rhi::DescriptorWriteTextureBuilder{}.build();
		static_assert(texture.expectedUse == rhi::ResourceUse::eSampledRead);
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
			rhi::DescriptorWriteBufferBuilder{}.set(set).binding(4, 17).type(rhi::DescriptorType::eStorageBuffer).buffer(buffer).range(64, 128).build();

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
													  .view(view)
													  .sampler(sampler)
													  .expected_use(rhi::ResourceUse::eStorageWrite)
													  .type(rhi::DescriptorType::eTextureUAV)
													  .build();

		EXPECT_EQ(write.view, view);
		EXPECT_EQ(write.sampler, sampler);
		EXPECT_EQ(write.expectedUse, rhi::ResourceUse::eStorageWrite);
		EXPECT_EQ(write.type, rhi::DescriptorType::eTextureUAV);

		const rhi::DescriptorWriteSampler samplerOnly = rhi::DescriptorWriteSamplerBuilder{}.binding(2).sampler(sampler).build();
		EXPECT_EQ(samplerOnly.binding, 2u);
		EXPECT_EQ(samplerOnly.sampler, sampler);
	}

} // namespace

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

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rhi = azo::rhi;

namespace
{

	struct Fixture final
	{
		std::array<rhi::PushConstantRange, 1> pushConstants{
			rhi::PushConstantRange{ .stages = rhi::ShaderStage::eVertex, .offset = 0, .size = 64 },
		};

		std::array<rhi::DescriptorBinding, 4> set0{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer },
			rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eTextureSRV, .count = 3 },
			rhi::DescriptorBinding{ .binding = 2, .type = rhi::DescriptorType::eSampler },
			rhi::DescriptorBinding{ .binding = 3, .type = rhi::DescriptorType::eCombinedImageSampler },
		};

		std::array<rhi::DescriptorBinding, 3> set1{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer },
			rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eUniformBuffer },
			rhi::DescriptorBinding{ .binding = 2, .type = rhi::DescriptorType::eTextureSRV, .count = 128, .flags = rhi::DescriptorBindingFlag::eBindless },
		};

		std::array<rhi::DescriptorSetLayoutDesc, 2> sets{
			rhi::DescriptorSetLayoutDesc{ .bindings = set0 },
			rhi::DescriptorSetLayoutDesc{ .bindings = set1 },
		};

		[[nodiscard]] rhi::ShaderAbiLayout Layout() const noexcept
		{
			return rhi::ShaderAbiLayout{ .sets = sets, .pushConstants = pushConstants };
		}
	};

	TEST(BindingAbi, VulkanAddressesABindingByThePairItWasDeclaredWith)
	{
		const Fixture fixture;

		const rhi::NativeBinding first = rhi::NativeBindingFor(rhi::VulkanApi::id, rhi::BindingTier::eBasic, fixture.Layout(), 1, 2);
		ASSERT_TRUE(first.exists);
		EXPECT_EQ(first.resource.space, 1u);
		EXPECT_EQ(first.resource.index, 2u);
		EXPECT_EQ(first.resource.klass, rhi::NativeSlotClass::eBinding);
		EXPECT_FALSE(first.hasSampler);
	}

	TEST(BindingAbi, PushConstantsSitInASpaceNoDescriptorSetUses)
	{
		const Fixture fixture;

		const rhi::NativeBinding uniform = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 0);
		ASSERT_TRUE(uniform.exists);
		EXPECT_EQ(uniform.resource.klass, rhi::NativeSlotClass::eConstantBuffer);
		EXPECT_EQ(uniform.resource.space, rhi::D3D12RegisterSpaceForSet(0));
		EXPECT_NE(uniform.resource.space, rhi::kPushConstantRegisterSpace) << "a set must not share the push constant space";
		EXPECT_EQ(uniform.resource.index, 0u) << "push constants no longer push this up";
	}

	TEST(BindingAbi, EachSetGetsItsOwnRegisterSpaceAndCountsFromZero)
	{
		const Fixture fixture;

		const rhi::NativeBinding second = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 1, 1);
		ASSERT_TRUE(second.exists);
		EXPECT_EQ(second.resource.klass, rhi::NativeSlotClass::eConstantBuffer);
		EXPECT_EQ(second.resource.space, rhi::D3D12RegisterSpaceForSet(1));
		EXPECT_EQ(second.resource.index, 0u) << "counting restarts inside each set";
	}

	TEST(BindingAbi, ABindingIsUnmovedByChangesToAnotherSet)
	{
		const Fixture fixture;
		const rhi::NativeBinding before = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 1, 1);

		std::array<rhi::DescriptorBinding, 5> widened{
			fixture.set0[0],
			fixture.set0[1],
			fixture.set0[2],
			fixture.set0[3],
			rhi::DescriptorBinding{ .binding = 4, .type = rhi::DescriptorType::eUniformBuffer },
		};
		std::array<rhi::DescriptorSetLayoutDesc, 2> sets{
			rhi::DescriptorSetLayoutDesc{ .bindings = widened },
			rhi::DescriptorSetLayoutDesc{ .bindings = fixture.set1 },
		};
		const rhi::ShaderAbiLayout grown{ .sets = sets, .pushConstants = fixture.pushConstants };

		const rhi::NativeBinding after = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, grown, 1, 1);
		ASSERT_TRUE(after.exists);
		EXPECT_EQ(after.resource, before.resource) << "a binding added to set 0 moved a binding in set 1";
	}

	TEST(BindingAbi, AnArrayAdvancesTheCounterByItsCount)
	{
		const Fixture fixture;

		const rhi::NativeBinding array = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 1);
		ASSERT_TRUE(array.exists);
		EXPECT_EQ(array.resource.index, 0u);

		const rhi::NativeBinding combined = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 3);
		ASSERT_TRUE(combined.exists);
		EXPECT_EQ(combined.resource.klass, rhi::NativeSlotClass::eShaderResource);
		EXPECT_EQ(combined.resource.index, 3u) << "the array before it took t0 through t2";
	}

	TEST(BindingAbi, ACombinedBindingTakesBothATextureAndASamplerSlot)
	{
		const Fixture fixture;

		const rhi::NativeBinding combined = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 3);
		ASSERT_TRUE(combined.exists);
		ASSERT_TRUE(combined.hasSampler);
		EXPECT_EQ(combined.sampler.klass, rhi::NativeSlotClass::eSampler);
		EXPECT_EQ(combined.sampler.index, 1u) << "set 0 binding 2 is a plain sampler and took s0";
	}

	TEST(BindingAbi, ABindlessBindingTakesASpaceOfItsOwnAtRegisterZero)
	{
		const Fixture fixture;

		const rhi::NativeBinding bindless = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eUnbounded, fixture.Layout(), 1, 2);
		ASSERT_TRUE(bindless.exists);
		EXPECT_GT(bindless.resource.space, rhi::D3D12RegisterSpaceForSet(1)) << "a bindless array takes a space above every set";
		EXPECT_EQ(bindless.resource.index, 0u);
		EXPECT_EQ(bindless.resource.klass, rhi::NativeSlotClass::eShaderResource);
	}

	TEST(BindingAbi, MetalNumbersItsArgumentTablesByBindingNumberBelowArgumentBuffers)
	{
		const Fixture fixture;

		const rhi::NativeBinding uniform = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eDynamicIndexing, fixture.Layout(), 0, 0);
		ASSERT_TRUE(uniform.exists);
		EXPECT_EQ(uniform.resource.klass, rhi::NativeSlotClass::eBuffer);
		EXPECT_EQ(uniform.resource.index, 0u);

		const rhi::NativeBinding texture = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eDynamicIndexing, fixture.Layout(), 0, 1);
		ASSERT_TRUE(texture.exists);
		EXPECT_EQ(texture.resource.klass, rhi::NativeSlotClass::eTexture);
		EXPECT_EQ(texture.resource.index, 1u) << "the table index is the binding number, not a per-table counter";
	}

	TEST(BindingAbi, MetalHasNoAnswerForASecondSetBelowArgumentBuffers)
	{
		const Fixture fixture;

		const rhi::NativeBinding second = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eDynamicIndexing, fixture.Layout(), 1, 0);
		EXPECT_FALSE(second.exists);
	}

	TEST(BindingAbi, MetalAddressesASetAsAnArgumentBufferWhereItHasThem)
	{
		const Fixture fixture;

		const rhi::NativeBinding uniform = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eUnbounded, fixture.Layout(), 0, 0);
		ASSERT_TRUE(uniform.exists);
		EXPECT_EQ(uniform.resource.klass, rhi::NativeSlotClass::eArgumentBufferMember);
		EXPECT_EQ(uniform.resource.space, rhi::MetalArgumentBufferIndexForSet(0));
		EXPECT_EQ(uniform.resource.index, 0u);

		const rhi::NativeBinding texture = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eUnbounded, fixture.Layout(), 0, 1);
		ASSERT_TRUE(texture.exists);
		EXPECT_EQ(texture.resource.space, rhi::MetalArgumentBufferIndexForSet(0)) << "every member of one set sits in that set's own buffer";
		EXPECT_EQ(texture.resource.index, 1u) << "the member's place in the struct, which is declaration order";

		const rhi::NativeBinding second = rhi::NativeBindingFor(rhi::MetalApi::id, rhi::BindingTier::eUnbounded, fixture.Layout(), 1, 0);
		ASSERT_TRUE(second.exists) << "a set beyond the first has an answer once the device has argument buffers";
		EXPECT_EQ(second.resource.space, rhi::MetalArgumentBufferIndexForSet(1));
		EXPECT_EQ(second.resource.index, 0u) << "members are numbered inside their own set, not across sets";
	}

	TEST(BindingAbi, ABindingTheLayoutDoesNotDeclareHasNoSlot)
	{
		const Fixture fixture;

		EXPECT_FALSE(rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 99).exists);
		EXPECT_FALSE(rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 7, 0).exists);
		EXPECT_FALSE(rhi::NativeBindingFor(rhi::NullApi::id, rhi::BindingTier::eBasic, fixture.Layout(), 0, 0).exists)
			<< "the Null backend has no native slots to speak for";
	}

	TEST(BindingAbi, AMapThatMatchesTheAbiIsAccepted)
	{
		const Fixture fixture;

		for (const rhi::GraphicsApiId api : { rhi::VulkanApi::id, rhi::D3D12Api::id, rhi::MetalApi::id })
		{
			// Metal addresses set zero only, so building the map from what the ABI answers is also what keeps this to the bindings each backend has.
			std::array<rhi::ShaderBindingEntry, 7> entries{};
			std::size_t count = 0;
			for (std::uint32_t set = 0; set < 2; ++set)
			{
				for (std::uint32_t binding = 0; binding < 4; ++binding)
				{
					const rhi::NativeBinding at = rhi::NativeBindingFor(api, rhi::BindingTier::eBasic, fixture.Layout(), set, binding);
					if (!at.exists)
					{
						continue;
					}

					entries.at(count) = rhi::ShaderBindingEntry{
						.set		= set,
						.binding	= binding,
						.resource	= at.resource,
						.sampler	= at.sampler,
						.hasSampler = at.hasSampler,
					};
					++count;
				}
			}

			ASSERT_GT(count, 0u);

			const rhi::ShaderBindingMap map{ .bindings = std::span(entries).first(count) };
			EXPECT_FALSE(rhi::CheckShaderBindingMap(api, rhi::BindingTier::eBasic, fixture.Layout(), map).found)
				<< "a map stating exactly what the ABI says was refused";
		}
	}

	TEST(BindingAbi, AnEmptyMapIsCheckedForItsRevisionAlone)
	{
		const Fixture fixture;

		const rhi::ShaderBindingMap conforming{ .abi = rhi::kShaderAbiVersion };
		EXPECT_FALSE(rhi::CheckShaderBindingMap(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), conforming).found);

		const rhi::ShaderBindingMap stale{ .abi = rhi::ShaderAbiVersion{ rhi::kShaderAbiVersion.value - 1 } };
		const rhi::ShaderBindingDisagreement bad = rhi::CheckShaderBindingMap(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), stale);
		EXPECT_TRUE(bad.found);
		EXPECT_TRUE(bad.wrongAbiVersion);
	}

	TEST(BindingAbi, AMapThatMovedASlotIsRefusedAndSaysWhere)
	{
		const Fixture fixture;

		const rhi::NativeBinding at = rhi::NativeBindingFor(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), 1, 1);
		ASSERT_TRUE(at.exists);

		rhi::NativeSlot moved = at.resource;
		moved.space += 1;

		const std::array<rhi::ShaderBindingEntry, 1> entries{
			rhi::ShaderBindingEntry{ .set = 1, .binding = 1, .resource = moved },
		};
		const rhi::ShaderBindingMap map{ .bindings = entries };

		const rhi::ShaderBindingDisagreement bad = rhi::CheckShaderBindingMap(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), map);
		ASSERT_TRUE(bad.found);
		EXPECT_FALSE(bad.wrongAbiVersion);
		EXPECT_FALSE(bad.unknownToLayout);
		EXPECT_EQ(bad.set, 1u);
		EXPECT_EQ(bad.binding, 1u);
		EXPECT_EQ(bad.declared, moved);
		EXPECT_EQ(bad.expected, at.resource) << "the refusal has to name where the binding actually lands, not only that it disagreed";
	}

	TEST(BindingAbi, AMapNamingABindingTheLayoutLacksIsRefusedAsThat)
	{
		const Fixture fixture;

		const std::array<rhi::ShaderBindingEntry, 1> entries{
			rhi::ShaderBindingEntry{ .set = 0, .binding = 9, .resource = rhi::NativeSlot{ .space = 1, .index = 0 } },
		};
		const rhi::ShaderBindingMap map{ .bindings = entries };

		const rhi::ShaderBindingDisagreement bad = rhi::CheckShaderBindingMap(rhi::D3D12Api::id, rhi::BindingTier::eBasic, fixture.Layout(), map);
		ASSERT_TRUE(bad.found);
		EXPECT_TRUE(bad.unknownToLayout);
		EXPECT_EQ(bad.binding, 9u);
	}

	TEST(BindingAbi, TheAbiRevisionIsStated)
	{
		static_assert(rhi::kShaderAbiVersion.value >= 2);
		EXPECT_EQ(rhi::kShaderAbiVersion, rhi::ShaderAbiVersion{ 2 });
	}

} // namespace

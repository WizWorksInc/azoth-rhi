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
#include "azoth/rhi/resources/descriptors.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class DescriptorTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(DescriptorTest);

	TEST_P(DescriptorTest, CreatesAndDestroysASetLayout)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle handle = Dev().CreateDescriptorSetLayout(layout.Desc(), error);

		ASSERT_TRUE(test::Ok(handle.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
	}

	TEST_P(DescriptorTest, CreatesALayoutWithNoBindingsAtAll)
	{
		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle handle =
			Dev().CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .debugName = "azoth.rhi.test.emptySet" }, error);

		ASSERT_TRUE(test::Ok(handle.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
	}

	TEST_P(DescriptorTest, CreatesALayoutCoveringTheDescriptorTypesTheBaselineUses)
	{
		const std::array bindings{ rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eUniformBuffer, .count = 1 },
			rhi::DescriptorBinding{
				.binding = 1,
				.type	 = rhi::DescriptorType::eStorageBuffer,
				.count	 = 1,
			},
			rhi::DescriptorBinding{
				.binding = 2,
				.type	 = rhi::DescriptorType::eTextureSRV,
				.count	 = 1,
			},
			rhi::DescriptorBinding{ .binding = 3, .type = rhi::DescriptorType::eSampler, .count = 1 } };

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle handle = Dev().CreateDescriptorSetLayout(
			rhi::DescriptorSetLayoutDesc{
				.bindings  = bindings,
				.debugName = "azoth.rhi.test.mixedSet",
			},
			error);

		ASSERT_TRUE(test::Ok(handle.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
	}

	TEST_P(DescriptorTest, CreatesAPipelineLayoutOverASetAndAPushConstantRange)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(setLayout);
		const rhi::PipelineLayoutHandle handle = Dev().CreatePipelineLayout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(handle.IsValid(), error));

		EXPECT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, CreatesAPipelineLayoutWithNothingInIt)
	{
		rhi::Error error{};
		const rhi::PipelineLayoutHandle handle = Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.emptyLayout" }, error);

		ASSERT_TRUE(test::Ok(handle.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(handle, {}, error), error));
	}

	TEST_P(DescriptorTest, CreatesADescriptorArena)
	{
		rhi::Error error{};
		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);

		EXPECT_TRUE(test::Ok(arena.IsValid(), error));
	}

	TEST_P(DescriptorTest, AllocatesSetsFromAnArena)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetAllocDesc alloc{
			.layout					 = setLayout,
			.variableDescriptorCount = 0,
			.debugName				 = "azoth.rhi.test.set",
		};

		const rhi::DescriptorSetHandle first = arena.Allocate(alloc, error);
		ASSERT_TRUE(test::Ok(first.IsValid(), error));

		const rhi::DescriptorSetHandle second = arena.Allocate(alloc, error);
		ASSERT_TRUE(test::Ok(second.IsValid(), error));

		EXPECT_NE(first, second) << "two sets from the same arena share a handle";

		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, ResetsAnArenaAndAllocatesFromItAgain)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetAllocDesc alloc{
			.layout					 = setLayout,
			.variableDescriptorCount = 0,
			.debugName				 = "azoth.rhi.test.set",
		};

		ASSERT_TRUE(arena.Allocate(alloc, error).IsValid());
		ASSERT_TRUE(test::Ok(arena.Reset(rhi::RetirePoint{}, error), error));

		const rhi::DescriptorSetHandle afterReset = arena.Allocate(alloc, error);
		EXPECT_TRUE(test::Ok(afterReset.IsValid(), error)) << "a reset arena stopped handing out sets";

		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, RejectsBindingASetThatItsArenaHasSinceReset)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(setLayout);
		const rhi::PipelineLayoutHandle layoutHandle = Dev().CreatePipelineLayout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(layoutHandle.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle stale = arena.Allocate(
			rhi::DescriptorSetAllocDesc{
				.layout					 = setLayout,
				.variableDescriptorCount = 0,
				.debugName				 = "azoth.rhi.test.staleSet",
			},
			error);
		ASSERT_TRUE(test::Ok(stale.IsValid(), error));

		ASSERT_TRUE(test::Ok(arena.Reset(rhi::RetirePoint{}, error), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error bindError{};
		EXPECT_FALSE(recording.List().BindDescriptorSet(layoutHandle, 0, stale, {}, bindError)) << "a set outlived the arena reset that invalidated it";
		EXPECT_TRUE(test::ErrorIsPopulated(bindError));

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(layoutHandle, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, BindsASetThatIsStillLive)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(setLayout);
		const rhi::PipelineLayoutHandle layoutHandle = Dev().CreatePipelineLayout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(layoutHandle.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle live = arena.Allocate(
			rhi::DescriptorSetAllocDesc{
				.layout					 = setLayout,
				.variableDescriptorCount = 0,
				.debugName				 = "azoth.rhi.test.liveSet",
			},
			error);
		ASSERT_TRUE(test::Ok(live.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(test::Ok(recording.List().BindDescriptorSet(layoutHandle, 0, live, {}, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(layoutHandle, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, ADynamicOffsetGoesToTheBindingItNamesRatherThanItsPlaceInTheList)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsDynamicBufferOffsets, "dynamic buffer offsets");

		rhi::Error error{};

		constexpr std::uint64_t kRange	  = 256;
		constexpr std::uint64_t kRoomy	  = 4096;
		constexpr std::uint64_t kSnug	  = kRange;
		constexpr std::uint64_t kFarOffset = 1024;

		const auto uniformOf = [](const std::uint64_t size) noexcept
		{
			return rhi::BufferDesc{
				.size	   = size,
				.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eUniform),
				.debugName = "azoth.rhi.test.dynamicUniform",
			};
		};

		const rhi::BufferHandle roomy = Dev().CreateBuffer(uniformOf(kRoomy), error);
		const rhi::BufferHandle snug  = Dev().CreateBuffer(uniformOf(kSnug), error);
		ASSERT_TRUE(test::Ok(roomy.IsValid() && snug.IsValid(), error));

		constexpr std::array bindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eDynamicUniformBuffer, .stages = rhi::ShaderStage::eCompute },
			rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eDynamicUniformBuffer, .stages = rhi::ShaderStage::eCompute },
		};

		const rhi::DescriptorSetLayoutHandle setLayout =
			Dev().CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.test.dynamicSet" }, error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const std::array setLayouts{ setLayout };
		const rhi::PipelineLayoutHandle pipelineLayout =
			Dev().CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .debugName = "azoth.rhi.test.dynamicLayout" }, error);
		ASSERT_TRUE(test::Ok(pipelineLayout.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(
			rhi::DescriptorArenaDesc{ .type = rhi::DescriptorArenaType::ePersistent, .maxSets = 1, .maxDescriptors = 2 }, error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout }, error);
		ASSERT_TRUE(test::Ok(set.IsValid(), error));

		const std::array writes{
			rhi::DescriptorWriteBuffer{
				.set = set, .binding = 0, .type = rhi::DescriptorType::eDynamicUniformBuffer, .buffer = roomy, .offset = 0, .range = kRange },
			rhi::DescriptorWriteBuffer{
				.set = set, .binding = 1, .type = rhi::DescriptorType::eDynamicUniformBuffer, .buffer = snug, .offset = 0, .range = kRange },
		};
		ASSERT_TRUE(test::Ok(Dev().UpdateDescriptors(std::span(writes), error), error));

		const std::array offsets{
			rhi::DynamicDescriptorOffset{ .binding = 1, .arrayIndex = 0, .offset = 0 },
			rhi::DynamicDescriptorOffset{ .binding = 0, .arrayIndex = 0, .offset = kFarOffset },
		};

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(test::Ok(recording.List().BindDescriptorSet(pipelineLayout, 0, set, std::span(offsets), error), error));
		EXPECT_TRUE(recording.End());

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a dynamic offset was applied to the wrong binding, which put a range outside its buffer");

		EXPECT_TRUE(test::Ok(Dev().Destroy(pipelineLayout, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(snug, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(roomy, {}, error), error));
	}

	TEST_P(DescriptorTest, WritesABufferDescriptorIntoASet)
	{
		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle set = arena.Allocate(
			rhi::DescriptorSetAllocDesc{
				.layout					 = setLayout,
				.variableDescriptorCount = 0,
				.debugName				 = "azoth.rhi.test.set",
			},
			error);
		ASSERT_TRUE(test::Ok(set.IsValid(), error));

		rhi::BufferDesc uniformDesc		= test::samples::StorageBuffer();
		uniformDesc.usage				= rhi::BufferUsage::eUniform;
		const rhi::BufferHandle uniform = Dev().CreateBuffer(uniformDesc, error);
		ASSERT_TRUE(test::Ok(uniform.IsValid(), error));

		const std::array writes{ rhi::DescriptorWriteBuffer{ .set = set,
			.binding											  = 0,
			.arrayIndex											  = 0,
			.type												  = rhi::DescriptorType::eUniformBuffer,
			.buffer												  = uniform,
			.offset												  = 0,
			.range												  = test::samples::kBufferSize } };

		EXPECT_TRUE(test::Ok(Dev().UpdateDescriptors(writes, error), error));

		EXPECT_TRUE(test::Ok(Dev().Destroy(uniform, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, WritesACombinedImageSamplerThroughOneBinding)
	{
		// One binding addressing a texture and its sampler together. Vulkan has a descriptor type for it. Direct3D 12 keeps samplers in a separate heap, so
		// one write must fill a slot in both.
		const std::array<rhi::DescriptorBinding, 1> bindings{ rhi::DescriptorBinding{
			.binding = 0,
			.type	 = rhi::DescriptorType::eCombinedImageSampler,
			.count	 = 1,
			.stages	 = rhi::ShaderStage::eFragment,
		} };

		rhi::Error error{};
		const rhi::DescriptorSetLayoutHandle setLayout =
			Dev().CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.test.combinedLayout" }, error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(setLayout);
		const rhi::PipelineLayoutHandle pipeline = Dev().CreatePipelineLayout(pipelineLayout.Desc(), error);
		EXPECT_TRUE(test::Ok(pipeline.IsValid(), error)) << "a pipeline layout over a combined binding was refused";

		rhi::DescriptorArena arena = Dev().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle set =
			arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = setLayout, .variableDescriptorCount = 0, .debugName = "azoth.rhi.test.combinedSet" }, error);
		ASSERT_TRUE(test::Ok(set.IsValid(), error));

		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		const rhi::TextureViewHandle view = Dev().CreateTextureView(texture, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		const rhi::SamplerHandle sampler = Dev().CreateSampler(test::samples::LinearSampler(), error);
		ASSERT_TRUE(test::Ok(sampler.IsValid(), error));

		const std::array writes{ rhi::DescriptorWriteTexture{
			.set			= set,
			.binding		= 0,
			.arrayIndex		= 0,
			.type			= rhi::DescriptorType::eCombinedImageSampler,
			.view			= view,
			.sampler		= sampler,
			.expectedLayout = rhi::TextureLayout::eShaderReadOnly,
		} };

		EXPECT_TRUE(test::Ok(Dev().UpdateDescriptors(writes, error), error)) << "a combined image sampler write was refused";
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a combined image sampler write tripped native validation");

		EXPECT_TRUE(test::Ok(Dev().Destroy(sampler, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		if (pipeline.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(pipeline, {}, error), error));
		}
		EXPECT_TRUE(test::Ok(Dev().Destroy(setLayout, {}, error), error));
	}

	TEST_P(DescriptorTest, AcceptsAnEmptyBatchOfDescriptorWrites)
	{
		rhi::Error error{};
		EXPECT_TRUE(test::Ok(Dev().UpdateDescriptors(std::span<const rhi::DescriptorWriteBuffer>{}, error), error));
		EXPECT_TRUE(test::Ok(Dev().UpdateDescriptors(std::span<const rhi::DescriptorWriteTexture>{}, error), error));
		EXPECT_TRUE(test::Ok(Dev().UpdateDescriptors(std::span<const rhi::DescriptorWriteSampler>{}, error), error));
	}

	TEST_P(DescriptorTest, ADefaultConstructedArenaIsInert)
	{
		rhi::DescriptorArena arena;
		EXPECT_FALSE(arena.IsValid());
	}

} // namespace

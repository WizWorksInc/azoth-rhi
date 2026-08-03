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

#include "azoth/rhi/ownership/raii.hpp"
#include "azoth/rhi/ownership/unique.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <utility>

namespace rhi  = azo::rhi;
namespace raii = azo::rhi::raii;
namespace test = azo::rhi::test;

namespace
{

	class RaiiTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] raii::Device Adopt()
		{
			const std::array<rhi::GraphicsApiId, 1> only{ CurrentBackend().id };
			rhi::Result<rhi::UniqueDevice> owner = rhi::CreateDevice(Harness().Registry(), only, MakeDeviceDesc());
			EXPECT_TRUE(test::Ok(owner));
			return raii::Device{ std::move(owner.Value()) };
		}
	};

	AZO_RHI_BACKEND_SUITE(RaiiTest);

	TEST(Raii, gate_TierTwoCompleteness)
	{
		static_assert(raii::detail::DeviceDestroyable<rhi::BufferHandle> && raii::detail::kOwns<raii::Buffer>);
		static_assert(raii::detail::DeviceDestroyable<rhi::TextureHandle> && raii::detail::kOwns<raii::Texture>);
		static_assert(raii::detail::DeviceDestroyable<rhi::TextureViewHandle> && raii::detail::kOwns<raii::TextureView>);
		static_assert(raii::detail::DeviceDestroyable<rhi::SamplerHandle> && raii::detail::kOwns<raii::Sampler>);
		static_assert(raii::detail::DeviceDestroyable<rhi::HeapHandle> && raii::detail::kOwns<raii::Heap>);
		static_assert(raii::detail::DeviceDestroyable<rhi::DescriptorSetLayoutHandle> && raii::detail::kOwns<raii::DescriptorSetLayout>);
		static_assert(raii::detail::DeviceDestroyable<rhi::PipelineLayoutHandle> && raii::detail::kOwns<raii::PipelineLayout>);
		static_assert(raii::detail::DeviceDestroyable<rhi::GraphicsPipelineHandle> && raii::detail::kOwns<raii::GraphicsPipeline>);
		static_assert(raii::detail::DeviceDestroyable<rhi::ComputePipelineHandle> && raii::detail::kOwns<raii::ComputePipeline>);
		static_assert(raii::detail::DeviceDestroyable<rhi::RayTracingPipelineHandle> && raii::detail::kOwns<raii::RayTracingPipeline>);
		static_assert(raii::detail::DeviceDestroyable<rhi::PipelineCacheHandle> && raii::detail::kOwns<raii::PipelineCache>);
		static_assert(raii::detail::DeviceDestroyable<rhi::AccelerationStructureHandle> && raii::detail::kOwns<raii::AccelerationStructure>);
		static_assert(raii::detail::DeviceDestroyable<rhi::QueryPoolHandle> && raii::detail::kOwns<raii::QueryPool>);
		static_assert(raii::detail::DeviceDestroyable<rhi::TimelineHandle> && raii::detail::kOwns<raii::Timeline>);
		static_assert(raii::detail::DeviceDestroyable<rhi::BinarySemaphoreHandle> && raii::detail::kOwns<raii::BinarySemaphore>);

		static_assert(raii::detail::kBorrows<raii::CommandList>);
		static_assert(raii::detail::kBorrows<raii::DescriptorSet>);

		static_assert(raii::detail::kBorrows<raii::Queue>);
		static_assert(raii::detail::kBorrows<raii::CommandPool>);
		static_assert(raii::detail::kBorrows<raii::DescriptorArena>);
		static_assert(raii::detail::kBorrows<raii::Swapchain>);

		static_assert(raii::detail::DeviceDestroyable<rhi::DescriptorSetHandle> && raii::detail::kBorrows<raii::DescriptorSet>,
			"a descriptor set has a Device::Destroy and is still borrowed. If that changed, the reason has to change with it");

		static_assert(!raii::detail::DeviceDestroyable<rhi::DescriptorArenaHandle>);

		SUCCEED();
	}

	TEST(Raii, ANullDeviceIsHarmlessToDestroy)
	{
		raii::Device device;
		EXPECT_FALSE(device.IsValid());

		raii::Device moved = std::move(device);
		EXPECT_FALSE(moved.IsValid());

		static_assert(!std::is_copy_constructible_v<raii::Device>);
		static_assert(std::is_move_constructible_v<raii::Device>);
	}

	TEST_P(RaiiTest, gate_TierParity)
	{
		raii::Device device = Adopt();
		ASSERT_TRUE(device.IsValid());

		const rhi::TextureHandle never{};

		rhi::Error flat{};
		const rhi::TextureViewHandle fromFlat = device.Get().CreateTextureView(never, rhi::TextureViewDesc{}, flat);
		ASSERT_FALSE(fromFlat.IsValid()) << "the flat API accepted a texture it never handed out, so there is no failure to compare";

		const rhi::Result<rhi::TextureViewHandle> fromTierOne = device.Get().CreateTextureViewWithResult(never, rhi::TextureViewDesc{});
		ASSERT_FALSE(fromTierOne.HasValue());

		const rhi::Result<raii::TextureView> fromTierTwo = device.CreateTextureView(never, rhi::TextureViewDesc{});
		ASSERT_FALSE(fromTierTwo.HasValue());

		EXPECT_EQ(fromTierOne.GetError().code, flat.code) << "the Result form and the out-Error form disagree about why this failed";
		EXPECT_EQ(fromTierTwo.GetError().code, flat.code) << "tier two reports a different code than the flat API it forwards to";
		EXPECT_TRUE(test::ErrorIsPopulated(fromTierTwo.GetError())) << "tier two lost the diagnostic on its way out";
	}

	TEST_P(RaiiTest, VendsWhatItCreatesAndDestroysItAtScopeExit)
	{
		raii::Device device = Adopt();
		ASSERT_TRUE(device.IsValid());

		rhi::BufferHandle raw{};
		{
			rhi::Result<raii::Buffer> buffer = device.CreateBuffer(test::samples::StorageBuffer());
			ASSERT_TRUE(test::Ok(buffer));
			ASSERT_TRUE(buffer.Value().IsValid());
			raw = buffer.Value().Get();
		}

		if (test::kValidatesHandles)
		{
			rhi::Error error{};
			EXPECT_FALSE(device.Get().Destroy(raw, {}, error)) << "the scope ended without destroying the buffer tier two vended";
		}
	}

	TEST_P(RaiiTest, gate_DestructionOrder)
	{
		rhi::BufferHandle raw{};

		{
			raii::Device device = Adopt();
			ASSERT_TRUE(device.IsValid());

			rhi::Result<raii::Buffer> buffer = device.CreateBuffer(test::samples::StorageBuffer());
			ASSERT_TRUE(test::Ok(buffer));
			raw = buffer.Value().Get();

			rhi::Error error{};
			EXPECT_TRUE(test::Ok(buffer.Value().Reset(error), error));
			EXPECT_FALSE(buffer.Value().IsValid());
		}

		EXPECT_TRUE(raw.IsValid()) << "the handle value itself is unaffected by any of this, which is why the hazard is silent";
	}

	TEST_P(RaiiTest, gate_ResetOutlivesItsBorrowings)
	{
		if constexpr (!test::kValidatesHandles)
		{
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;
		}

		raii::Device device = Adopt();
		ASSERT_TRUE(device.IsValid());

		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::Result<rhi::DescriptorSetLayoutHandle> setLayout = device.Get().CreateDescriptorSetLayoutWithResult(layout.Desc());
		ASSERT_TRUE(test::Ok(setLayout));

		rhi::Result<raii::DescriptorArena> arena = device.CreateDescriptorArena(test::samples::DescriptorArena());
		ASSERT_TRUE(test::Ok(arena));

		const raii::DescriptorSet borrowed = arena.Value().Allocate(
			rhi::DescriptorSetAllocDesc{
				.layout					 = setLayout.Value(),
				.variableDescriptorCount = 0,
				.debugName				 = "azoth.rhi.test.borrowed",
			},
			error);
		ASSERT_TRUE(test::Ok(borrowed.IsValid(), error));

		ASSERT_TRUE(test::Ok(arena.Value().Reset(rhi::RetirePoint{}, error), error));

		error = {};
		EXPECT_FALSE(device.Get().Destroy(borrowed, {}, error)) << "a descriptor set survived the reset of the arena that owns it";
		EXPECT_TRUE(test::ErrorIsPopulated(error)) << "the refusal came back with no diagnostic";

		static_cast<void>(device.Get().Destroy(setLayout.Value(), {}, error));
	}

} // namespace

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
			rhi::Result<rhi::UniqueDevice> owner = rhi::create_device(Harness().Registry(), only, MakeDeviceDesc());
			EXPECT_TRUE(test::Ok(owner));
			return raii::Device{ std::move(owner.value()) };
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

		static_assert(
			raii::detail::DeviceDestroyable<rhi::DescriptorSetHandle> && raii::detail::kBorrows<raii::DescriptorSet>,
			"a descriptor set has a Device::Destroy and is still borrowed. If that changed, the reason has to change with it"
		);

		static_assert(!raii::detail::DeviceDestroyable<rhi::DescriptorArenaHandle>);

		SUCCEED();
	}

	TEST(Raii, ANullDeviceIsHarmlessToDestroy)
	{
		raii::Device device;
		EXPECT_FALSE(device.is_valid());

		raii::Device moved = std::move(device);
		EXPECT_FALSE(moved.is_valid());

		static_assert(!std::is_copy_constructible_v<raii::Device>);
		static_assert(std::is_move_constructible_v<raii::Device>);
	}

	TEST_P(RaiiTest, gate_TierParity)
	{
		raii::Device device = Adopt();
		ASSERT_TRUE(device.is_valid());

		const rhi::TextureHandle never{};

		rhi::Error flat{};
		const rhi::TextureViewHandle fromFlat = device.get().create_texture_view(never, rhi::TextureViewDesc{}, flat);
		ASSERT_FALSE(fromFlat.is_valid()) << "the flat API accepted a texture it never handed out, so there is no failure to compare";

		const rhi::Result<rhi::TextureViewHandle> fromTierOne = device.get().create_texture_view_with_result(never, rhi::TextureViewDesc{});
		ASSERT_FALSE(fromTierOne.has_value());

		const rhi::Result<raii::TextureView> fromTierTwo = device.create_texture_view(never, rhi::TextureViewDesc{});
		ASSERT_FALSE(fromTierTwo.has_value());

		EXPECT_EQ(fromTierOne.get_error().code, flat.code) << "the Result form and the out-Error form disagree about why this failed";
		EXPECT_EQ(fromTierTwo.get_error().code, flat.code) << "tier two reports a different code than the flat API it forwards to";
		EXPECT_TRUE(test::ErrorIsPopulated(fromTierTwo.get_error())) << "tier two lost the diagnostic on its way out";
	}

	TEST_P(RaiiTest, VendsWhatItCreatesAndDestroysItAtScopeExit)
	{
		raii::Device device = Adopt();
		ASSERT_TRUE(device.is_valid());

		rhi::BufferHandle raw{};
		{
			rhi::Result<raii::Buffer> buffer = device.create_buffer(test::samples::StorageBuffer());
			ASSERT_TRUE(test::Ok(buffer));
			ASSERT_TRUE(buffer.value().is_valid());
			raw = buffer.value().get();
		}

		if (test::kValidatesHandles)
		{
			rhi::Error error{};
			EXPECT_FALSE(device.get().destroy(raw, {}, error)) << "the scope ended without destroying the buffer tier two vended";
		}
	}

	TEST_P(RaiiTest, gate_DestructionOrder)
	{
		rhi::BufferHandle raw{};

		{
			raii::Device device = Adopt();
			ASSERT_TRUE(device.is_valid());

			rhi::Result<raii::Buffer> buffer = device.create_buffer(test::samples::StorageBuffer());
			ASSERT_TRUE(test::Ok(buffer));
			raw = buffer.value().get();

			rhi::Error error{};
			EXPECT_TRUE(test::Ok(buffer.value().reset(error), error));
			EXPECT_FALSE(buffer.value().is_valid());
		}

		EXPECT_TRUE(raw.is_valid()) << "the handle value itself is unaffected by any of this, which is why the hazard is silent";
	}

	TEST_P(RaiiTest, gate_ResetOutlivesItsBorrowings)
	{
		if constexpr (!test::kValidatesHandles)
		{
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;
		}

		raii::Device device = Adopt();
		ASSERT_TRUE(device.is_valid());

		const test::samples::UniformLayout layout;

		rhi::Error error{};
		const rhi::Result<rhi::DescriptorSetLayoutHandle> setLayout = device.get().create_descriptor_set_layout_with_result(layout.Desc());
		ASSERT_TRUE(test::Ok(setLayout));

		rhi::Result<raii::DescriptorArena> arena = device.create_descriptor_arena(test::samples::DescriptorArena());
		ASSERT_TRUE(test::Ok(arena));

		const raii::DescriptorSet borrowed = arena.value().allocate(
			rhi::DescriptorSetAllocDesc{
				.layout					 = setLayout.value(),
				.variableDescriptorCount = 0,
				.debugName				 = "azoth.rhi.test.borrowed",
			},
			error
		);
		ASSERT_TRUE(test::Ok(borrowed.is_valid(), error));

		ASSERT_TRUE(test::Ok(arena.value().reset(rhi::RetirePoint{}, error), error));

		error = {};
		EXPECT_FALSE(device.get().destroy(borrowed, {}, error)) << "a descriptor set survived the reset of the arena that owns it";
		EXPECT_TRUE(test::ErrorIsPopulated(error)) << "the refusal came back with no diagnostic";

		static_cast<void>(device.get().destroy(setLayout.value(), {}, error));
	}

} // namespace

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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	[[nodiscard]] rhi::Result<rhi::UniqueDevice> BuildAgainstNothing(const rhi::DeviceBuilder & builder)
	{
		rhi::GraphicsApiRegistry registry;
		constexpr std::array preferred{ rhi::NullApi::kId };
		return builder.build(registry, preferred);
	}

	TEST(DeviceBuilder, RejectsAnEmptyPreferredApiList)
	{
		rhi::GraphicsApiRegistry registry;
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.build(registry, std::span<const rhi::GraphicsApiId>{});

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
		EXPECT_TRUE(test::ErrorIsPopulated(device.get_error()));
	}

	TEST(DeviceBuilder, SuppliesOneGraphicsQueueWhenNothingWasAskedFor)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{});

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(DeviceBuilder, RejectsARequestWithNoQueuesAtAll)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.default_graphics_queue(false));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, RejectsAQueueRequestForZeroQueues)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.graphics_queue(0));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, RejectsRequiringASwapchainWithoutAGraphicsQueue)
	{
		const rhi::Result<rhi::UniqueDevice> device =
			BuildAgainstNothing(rhi::DeviceBuilder{}.default_graphics_queue(false).clear_queues().compute_queue().require_swapchain(true));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, AllowsAComputeOnlyDeviceOnceTheSwapchainRequirementIsDropped)
	{
		const rhi::Result<rhi::UniqueDevice> device =
			BuildAgainstNothing(rhi::DeviceBuilder{}.default_graphics_queue(false).clear_queues().compute_queue().require_swapchain(false));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi)) << "a headless compute request was rejected by validation";
	}

	TEST(DeviceBuilder, ClearQueuesDropsTheExplicitRequestsAndKeepsTheDefaultSetting)
	{
		const rhi::Result<rhi::UniqueDevice> cleared = BuildAgainstNothing(rhi::DeviceBuilder{}.compute_queue().copy_queue().clear_queues());
		EXPECT_TRUE(test::Failed(cleared, rhi::ErrorCode::eUnsupportedApi));

		const rhi::Result<rhi::UniqueDevice> empty = BuildAgainstNothing(rhi::DeviceBuilder{}.compute_queue().default_graphics_queue(false).clear_queues());
		EXPECT_TRUE(test::Failed(empty, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, ReplacesRatherThanDuplicatesARepeatedQueueType)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.graphics_queue(4).graphics_queue(1));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(DeviceBuilder, BuildsARealDeviceThroughARegisteredBackend)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::kId };
		const rhi::Result<rhi::UniqueDevice> device =
			rhi::DeviceBuilder{}.require_swapchain(false).debug_name("azoth.rhi.test.builtDevice").build(registry, preferred);

		ASSERT_TRUE(test::Ok(device));
		EXPECT_TRUE(device.value().is_valid());
		EXPECT_EQ(device.value().get().get_graphics_api_id(), rhi::NullApi::kId);
	}

	TEST(DeviceBuilder, PassesQueueRequestsThroughToTheBackendRatherThanSwallowingThem)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::kId };
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.require_swapchain(false).dedicated_compute_queue().build(registry, preferred);

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedFeature));
	}

	TEST(DeviceBuilder, CarriesTheRequestedQueueCountsIntoTheDeviceCaps)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::kId };
		const rhi::Result<rhi::UniqueDevice> device =
			rhi::DeviceBuilder{}.require_swapchain(false).clear_queues().graphics_queue(2).compute_queue(1).build(registry, preferred);

		ASSERT_TRUE(test::Ok(device));

		const rhi::DeviceCaps & caps = device.value().get().get_caps();
		EXPECT_EQ(caps.graphicsQueueCount, 2u);
		EXPECT_EQ(caps.computeQueueCount, 1u);

		EXPECT_EQ(caps.copyQueueCount, 0u);
	}

	TEST(DeviceBuilder, DefaultQueueSetAsksForAllThreeTypes)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::kId };
		rhi::DeviceDesc desc{};
		desc.requireSwapchain = false;

		const rhi::Result<rhi::UniqueDevice> device = rhi::create_device(registry, preferred, desc);
		ASSERT_TRUE(test::Ok(device));

		const rhi::DeviceCaps & caps = device.value().get().get_caps();
		EXPECT_EQ(caps.graphicsQueueCount, 1u);
		EXPECT_EQ(caps.computeQueueCount, 1u);
		EXPECT_EQ(caps.copyQueueCount, 1u);
	}

	TEST(DeviceBuilder, ValidationRunsBeforeTheBackendIsConsulted)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::kId };
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.require_swapchain(false).graphics_queue(0).build(registry, preferred);

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

}

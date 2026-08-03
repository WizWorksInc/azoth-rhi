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
		constexpr std::array preferred{ rhi::NullApi::id };
		return builder.Build(registry, preferred);
	}

	TEST(DeviceBuilder, RejectsAnEmptyPreferredApiList)
	{
		rhi::GraphicsApiRegistry registry;
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.Build(registry, std::span<const rhi::GraphicsApiId>{});

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
		EXPECT_TRUE(test::ErrorIsPopulated(device.GetError()));
	}

	TEST(DeviceBuilder, SuppliesOneGraphicsQueueWhenNothingWasAskedFor)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{});

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(DeviceBuilder, RejectsARequestWithNoQueuesAtAll)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.DefaultGraphicsQueue(false));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, RejectsAQueueRequestForZeroQueues)
	{
		// Asking for a minimum of none is a caller mistake and not a way to opt out of a queue and PlanQueues would otherwise silently promote it to one.
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.GraphicsQueue(0));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, RejectsRequiringASwapchainWithoutAGraphicsQueue)
	{
		const rhi::Result<rhi::UniqueDevice> device =
			BuildAgainstNothing(rhi::DeviceBuilder{}.DefaultGraphicsQueue(false).ClearQueues().ComputeQueue().RequireSwapchain(true));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, AllowsAComputeOnlyDeviceOnceTheSwapchainRequirementIsDropped)
	{
		const rhi::Result<rhi::UniqueDevice> device =
			BuildAgainstNothing(rhi::DeviceBuilder{}.DefaultGraphicsQueue(false).ClearQueues().ComputeQueue().RequireSwapchain(false));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi)) << "a headless compute request was rejected by validation";
	}

	TEST(DeviceBuilder, ClearQueuesDropsTheExplicitRequestsAndKeepsTheDefaultSetting)
	{
		const rhi::Result<rhi::UniqueDevice> cleared = BuildAgainstNothing(rhi::DeviceBuilder{}.ComputeQueue().CopyQueue().ClearQueues());
		EXPECT_TRUE(test::Failed(cleared, rhi::ErrorCode::eUnsupportedApi));

		const rhi::Result<rhi::UniqueDevice> empty = BuildAgainstNothing(rhi::DeviceBuilder{}.ComputeQueue().DefaultGraphicsQueue(false).ClearQueues());
		EXPECT_TRUE(test::Failed(empty, rhi::ErrorCode::eInvalidArgument));
	}

	TEST(DeviceBuilder, ReplacesRatherThanDuplicatesARepeatedQueueType)
	{
		const rhi::Result<rhi::UniqueDevice> device = BuildAgainstNothing(rhi::DeviceBuilder{}.GraphicsQueue(4).GraphicsQueue(1));

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(DeviceBuilder, BuildsARealDeviceThroughARegisteredBackend)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::RegisterBackend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::id };
		const rhi::Result<rhi::UniqueDevice> device =
			rhi::DeviceBuilder{}.RequireSwapchain(false).DebugName("azoth.rhi.test.builtDevice").Build(registry, preferred);

		ASSERT_TRUE(test::Ok(device));
		EXPECT_TRUE(device.Value().IsValid());
		EXPECT_EQ(device.Value().Get().GetGraphicsApiId(), rhi::NullApi::id);
	}

	TEST(DeviceBuilder, PassesQueueRequestsThroughToTheBackendRatherThanSwallowingThem)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::RegisterBackend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::id };
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.RequireSwapchain(false).DedicatedComputeQueue().Build(registry, preferred);

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedFeature));
	}

	TEST(DeviceBuilder, CarriesTheRequestedQueueCountsIntoTheDeviceCaps)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::RegisterBackend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::id };
		const rhi::Result<rhi::UniqueDevice> device =
			rhi::DeviceBuilder{}.RequireSwapchain(false).ClearQueues().GraphicsQueue(2).ComputeQueue(1).Build(registry, preferred);

		ASSERT_TRUE(test::Ok(device));

		const rhi::DeviceCaps & caps = device.Value().Get().GetCaps();
		EXPECT_EQ(caps.graphicsQueueCount, 2u);
		EXPECT_EQ(caps.computeQueueCount, 1u);

		EXPECT_EQ(caps.copyQueueCount, 0u);
	}

	TEST(DeviceBuilder, DefaultQueueSetAsksForAllThreeTypes)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::RegisterBackend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::id };
		rhi::DeviceDesc desc{};
		desc.requireSwapchain = false;

		const rhi::Result<rhi::UniqueDevice> device = rhi::CreateDevice(registry, preferred, desc);
		ASSERT_TRUE(test::Ok(device));

		const rhi::DeviceCaps & caps = device.Value().Get().GetCaps();
		EXPECT_EQ(caps.graphicsQueueCount, 1u);
		EXPECT_EQ(caps.computeQueueCount, 1u);
		EXPECT_EQ(caps.copyQueueCount, 1u);
	}

	TEST(DeviceBuilder, ValidationRunsBeforeTheBackendIsConsulted)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::RegisterBackend<rhi::NullApi>(registry)));

		constexpr std::array preferred{ rhi::NullApi::id };
		const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder{}.RequireSwapchain(false).GraphicsQueue(0).Build(registry, preferred);

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eInvalidArgument));
	}

} // namespace

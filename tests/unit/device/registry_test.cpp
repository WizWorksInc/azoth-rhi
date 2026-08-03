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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	[[nodiscard]] rhi::BackendCreateInfo StubBackend(const char * displayName)
	{
		rhi::BackendCreateInfo info{};
		info.info.displayName		   = displayName;
		info.info.canonicalName		   = "azoth.rhi.stub";
		info.info.supportsSurfaces	   = false;
		info.info.supportsDebugMarkers = true;
		info.createInstance			   = nullptr;
		return info;
	}

	TEST(GraphicsApiRegistry, StartsEmpty)
	{
		const rhi::GraphicsApiRegistry registry;

		EXPECT_TRUE(registry.EnumerateBackends().empty());
		EXPECT_FALSE(registry.IsRegistered(rhi::VulkanApi::id));
		EXPECT_FALSE(registry.IsRegistered(rhi::NullApi::id));
	}

	TEST(GraphicsApiRegistry, RegistersABackendUnderTheTagsId)
	{
		rhi::GraphicsApiRegistry registry;

		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(StubBackend("stub"))));

		EXPECT_TRUE(registry.IsRegistered(rhi::VulkanApi::id));
		EXPECT_FALSE(registry.IsRegistered(rhi::D3D12Api::id));
		EXPECT_EQ(registry.EnumerateBackends().size(), 1u);
	}

	TEST(GraphicsApiRegistry, StampsTheIdFromTheTagRatherThanFromTheCreateInfo)
	{
		rhi::GraphicsApiRegistry registry;

		rhi::BackendCreateInfo info = StubBackend("stub");
		info.info.id				= rhi::D3D12Api::id;

		ASSERT_TRUE(test::Ok(registry.Register<rhi::MetalApi>(info)));

		EXPECT_TRUE(registry.IsRegistered(rhi::MetalApi::id));
		EXPECT_FALSE(registry.IsRegistered(rhi::D3D12Api::id)) << "the create-info id overrode the tag";
		EXPECT_EQ(registry.EnumerateBackends().front().id, rhi::MetalApi::id);
	}

	TEST(GraphicsApiRegistry, RefusesToRegisterTheSameApiTwice)
	{
		// Silently overwriting would make the winner depend on initialization order, which is the kind of difference that only shows up on someone else's
		// machine.
		rhi::GraphicsApiRegistry registry;

		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(StubBackend("first"))));

		const rhi::Result<void> second = registry.Register<rhi::VulkanApi>(StubBackend("second"));
		EXPECT_TRUE(test::Failed(second, rhi::ErrorCode::eInvalidState));
		EXPECT_TRUE(test::ErrorIsPopulated(second.GetError()));

		ASSERT_EQ(registry.EnumerateBackends().size(), 1u);
		EXPECT_EQ(registry.EnumerateBackends().front().displayName, "first") << "the rejected registration still replaced the first";
	}

	TEST(GraphicsApiRegistry, HoldsSeveralBackendsAtOnceInRegistrationOrder)
	{
		rhi::GraphicsApiRegistry registry;

		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(StubBackend("vulkan"))));
		ASSERT_TRUE(test::Ok(registry.Register<rhi::MetalApi>(StubBackend("metal"))));
		ASSERT_TRUE(test::Ok(registry.Register<rhi::NullApi>(StubBackend("null"))));

		const std::span<const rhi::BackendInfo> backends = registry.EnumerateBackends();
		ASSERT_EQ(backends.size(), 3u);
		EXPECT_EQ(backends[0].id, rhi::VulkanApi::id);
		EXPECT_EQ(backends[1].id, rhi::MetalApi::id);
		EXPECT_EQ(backends[2].id, rhi::NullApi::id);

		EXPECT_TRUE(registry.IsRegistered(rhi::VulkanApi::id));
		EXPECT_TRUE(registry.IsRegistered(rhi::MetalApi::id));
		EXPECT_TRUE(registry.IsRegistered(rhi::NullApi::id));
		EXPECT_FALSE(registry.IsRegistered(rhi::D3D12Api::id));
	}

	TEST(GraphicsApiRegistry, KeepsTheStaticInformationABackendDeclared)
	{
		rhi::GraphicsApiRegistry registry;

		rhi::BackendCreateInfo info = StubBackend("stub");
		info.info.apiVersionMajor	= 1;
		info.info.apiVersionMinor	= 4;
		info.info.supportsSurfaces	= true;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(info)));

		const rhi::BackendInfo & stored = registry.EnumerateBackends().front();
		EXPECT_EQ(stored.apiVersionMajor, 1u);
		EXPECT_EQ(stored.apiVersionMinor, 4u);
		EXPECT_TRUE(stored.supportsSurfaces);
		EXPECT_TRUE(stored.supportsDebugMarkers);
		EXPECT_FALSE(stored.supportsExternalNativeAccess);
	}

	TEST(CreateInstance, ReportsAnUnsupportedApiWhenNothingPreferredIsRegistered)
	{
		rhi::GraphicsApiRegistry registry;
		constexpr std::array preferred{ rhi::VulkanApi::id };

		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});
		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eUnsupportedApi));
		EXPECT_TRUE(test::ErrorIsPopulated(instance.GetError()));
	}

	TEST(CreateInstance, ReportsAnUnsupportedApiForAnEmptyPreferenceList)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(StubBackend("stub"))));

		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, std::span<const rhi::GraphicsApiId>{}, rhi::InstanceDesc{});
		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(CreateInstance, SkipsARegisteredBackendThatOffersNoEntryPoint)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(StubBackend("no entry point"))));

		constexpr std::array preferred{ rhi::VulkanApi::id, rhi::MetalApi::id };
		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});

		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(CreateInstance, ReleasesTheBackendInstanceRatherThanLeavingItForProcessExit)
	{
		std::uint32_t created = 0;
		for (const test::Backend & backend : test::SelectedBackends())
		{
			rhi::GraphicsApiRegistry registry;
			ASSERT_TRUE(test::Ok(backend.RegisterInto(registry)));

			const std::array preferred{ backend.id };
			rhi::InstanceDesc desc{};
			desc.validation = test::kValidationMode;

			const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, desc);
			if (!instance)
			{
				continue; // no driver for this backend on this machine
			}

			++created;
			EXPECT_TRUE(instance.Value().IsValid());

			rhi::Error error{};
			std::uint32_t adapters = 0;
			EXPECT_TRUE(test::Ok(instance.Value().Get().EnumerateAdapters({}, adapters, error), error));
		}

		if (created == 0)
		{
			GTEST_SKIP() << "no selected backend could create an instance here";
		}
	}

	TEST(CreateDevice, ReportsAnUnsupportedApiRatherThanCrashingOnAnEmptyRegistry)
	{
		rhi::GraphicsApiRegistry registry;
		constexpr std::array preferred{ rhi::NullApi::id };

		rhi::DeviceDesc desc{};
		desc.requireSwapchain = false;

		const rhi::Result<rhi::UniqueDevice> device = rhi::CreateDevice(registry, preferred, desc);
		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eUnsupportedApi));
	}

	TEST(AvailableBackends, EveryOneRegistersUnderItsOwnId)
	{
		for (const test::Backend & backend : test::AvailableBackends())
		{
			rhi::GraphicsApiRegistry registry;
			ASSERT_NE(backend.RegisterInto, nullptr) << backend.shortName << " is listed as available with no entry point";
			ASSERT_TRUE(test::Ok(backend.RegisterInto(registry))) << backend.shortName << " failed to register";

			EXPECT_TRUE(registry.IsRegistered(backend.id)) << backend.shortName << " registered under a different id";
			ASSERT_EQ(registry.EnumerateBackends().size(), 1u);
			EXPECT_EQ(registry.EnumerateBackends().front().canonicalName, backend.canonicalName);
		}
	}

	TEST(AvailableBackends, CoexistInOneRegistry)
	{
		rhi::GraphicsApiRegistry registry;

		for (const test::Backend & backend : test::AvailableBackends())
		{
			EXPECT_TRUE(test::Ok(backend.RegisterInto(registry))) << backend.shortName << " could not join a registry holding the others";
		}

		EXPECT_EQ(registry.EnumerateBackends().size(), test::AvailableBackends().size());
	}

	namespace holed
	{

		struct StubInstance final
		{
			const rhi::BackendObject * object = nullptr;
		};

		inline rhi::InstanceApi g_table{};
		inline StubInstance g_instance{};

		[[nodiscard]] const void * QueryInterface(void *, const rhi::InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			if (id != rhi::InterfaceTraits<rhi::InstanceApi>::kId || minVersion > rhi::InterfaceTraits<rhi::InstanceApi>::kVersion)
			{
				return nullptr;
			}

			return &g_table;
		}

		constexpr rhi::BackendObject kPublished{ .queryInterface = &QueryInterface };
		inline std::uint32_t g_destroyCalls = 0;

		void DestroyInstance(void *) noexcept
		{
			++g_destroyCalls;
		}

		void ResetTable() noexcept
		{
			g_destroyCalls = 0;
			g_table		   = rhi::InstanceApi{};

			g_table.getGraphicsApiId = [](void *) noexcept
			{
				return rhi::VulkanApi::id;
			};
			g_table.enumerateAdapters = [](void *, std::span<rhi::AdapterInfo>, std::uint32_t *, rhi::Error *) noexcept
			{
				return false;
			};
			g_table.createDevice = [](void *, const rhi::DeviceDesc &, rhi::Error *) noexcept
			{
				return static_cast<void *>(nullptr);
			};
			g_table.destroyInstance = &DestroyInstance;

			g_instance.object = &kPublished;
		}

		[[nodiscard]] rhi::BackendCreateInfo Backend()
		{
			rhi::BackendCreateInfo info{};
			info.info.displayName	= "holed external backend";
			info.info.canonicalName = "azoth.rhi.holed";
			info.createInstance		= [](const void *, rhi::Error *) noexcept
			{
				return static_cast<void *>(&g_instance);
			};
			return info;
		}

	} // namespace holed

	TEST(CreateInstance, RejectsABackendThatLeftADispatchEntryUnsetRatherThanCallingThroughIt)
	{
		holed::ResetTable();
		holed::g_table.createDevice = nullptr;

		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(holed::Backend())));

		constexpr std::array preferred{ rhi::VulkanApi::id };
		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});

		ASSERT_TRUE(test::Failed(instance, rhi::ErrorCode::eValidationFailed));
		ASSERT_NE(instance.GetError().message, nullptr);
		EXPECT_EQ(std::string_view{ instance.GetError().message }, "InstanceApi::createDevice is null") << "the diagnostic did not name the gap";
	}

	TEST(CreateInstance, HandsBackAnInstanceItRejectsRatherThanStrandingIt)
	{
		holed::ResetTable();
		holed::g_table.enumerateAdapters = nullptr;

		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(holed::Backend())));

		constexpr std::array preferred{ rhi::VulkanApi::id };
		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});

		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eValidationFailed));
		EXPECT_EQ(holed::g_destroyCalls, 1u) << "the rejected instance was left for process exit";
	}

	TEST(CreateInstance, AbandonsARejectedInstanceWhenTheEntryThatWouldReleaseItIsTheMissingOne)
	{
		holed::ResetTable();
		holed::g_table.destroyInstance = nullptr;

		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(holed::Backend())));

		constexpr std::array preferred{ rhi::VulkanApi::id };
		const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});

		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eValidationFailed));
		EXPECT_EQ(holed::g_destroyCalls, 0u);
	}

	TEST(CreateDevice, RejectsAHoledInstanceTableBeforeReachingCreateDeviceThroughIt)
	{
		holed::ResetTable();
		holed::g_table.createDevice = nullptr;

		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(holed::Backend())));

		constexpr std::array preferred{ rhi::VulkanApi::id };
		const rhi::Result<rhi::UniqueDevice> device = rhi::CreateDevice(registry, preferred, rhi::DeviceDesc{});

		EXPECT_TRUE(test::Failed(device, rhi::ErrorCode::eValidationFailed));
		EXPECT_EQ(holed::g_destroyCalls, 1u);
	}

	TEST(CreateInstance, AcceptsAnExternalBackendWhoseTableIsWhole)
	{
		holed::ResetTable();

		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(registry.Register<rhi::VulkanApi>(holed::Backend())));

		constexpr std::array preferred{ rhi::VulkanApi::id };
		{
			const rhi::Result<rhi::UniqueInstance> instance = rhi::CreateInstance(registry, preferred, rhi::InstanceDesc{});
			ASSERT_TRUE(test::Ok(instance));
			EXPECT_TRUE(instance.Value().IsValid());
		}

		EXPECT_EQ(holed::g_destroyCalls, 1u) << "the owner released it on the way out";
	}

} // namespace

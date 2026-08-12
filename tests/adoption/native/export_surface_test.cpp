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

/**
 * \file
 * \brief Names every public out-of-line function once so a shared link has to resolve it.
 *
 * A shared build reports a missing AZO_RHI_API only on a symbol something references, so a declaration nobody calls stays broken until the first consumer who
 * calls it. What this cannot do is notice a function nobody added to it: a new public entry point earns a line below or it stays exactly as latent as before.
 */

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/core/build_config.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/host/profiler.hpp"
#include "azoth/rhi/module/backend_module.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"

#include <gtest/gtest.h>

#include <span>
#include <string_view>
#include <utility>

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN
	#include "azoth/rhi/native/vulkan_native.hpp"
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_METAL
	#include "azoth/rhi/native/metal_native.hpp"
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12
	#include "azoth/rhi/native/d3d12_native.hpp"
#endif

namespace rhi = azo::rhi;

namespace
{

	// A volatile store the optimiser has to perform, which is what makes the address a real reference in the object file rather than a folded constant.
	template <class Fn>
	void Reference(Fn function) noexcept
	{
		Fn volatile sink = function;
		static_cast<void>(sink);
	}

	TEST(ExportSurface, TheFreeEntryPointsResolve)
	{
		Reference(&rhi::detail::GuardsHeld);
		Reference(&rhi::detail::ReentrancyViolationCount);
		Reference(&rhi::detail::HostAllocatorSlot);
		Reference(&rhi::detail::DeviceAllocatorSlot);
		Reference(&rhi::detail::ProfilerSlot);

		Reference(&rhi::SetClipSpace);
		Reference(&rhi::GetClipSpace);

		Reference(&rhi::SelectGraphicsApi);
		Reference(&rhi::MakePresentationBackend);
		Reference(&rhi::native::ResolveVulkanLoader);

		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (*)(rhi::GraphicsApiRegistry &, std::span<const rhi::GraphicsApiId>, const rhi::InstanceDesc &)>(
			&rhi::CreateInstance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (*)(rhi::GraphicsApiRegistry &, std::span<const rhi::GraphicsApiId>, const rhi::DeviceDesc &)>(
			&rhi::CreateDevice));

		// Null is the one specialization every build has. The rest are unresolved where their backend was not bundled, so each is named beside its own backend below.
		Reference(&rhi::CreateDevice<rhi::NullApi>);

		Reference(&rhi::AvailableBackends);
		Reference(static_cast<const rhi::BackendEntry * (*)(std::string_view) noexcept>(&rhi::FindAvailableBackend));
		Reference(static_cast<const rhi::BackendEntry * (*)(rhi::GraphicsApiId) noexcept>(&rhi::FindAvailableBackend));
		Reference(&rhi::SelfRegisteredBackends);
		Reference(static_cast<rhi::Result<void> (*)(rhi::GraphicsApiRegistry &, rhi::GraphicsApiId)>(&rhi::RegisterBackend));
	}

	TEST(ExportSurface, TheOutOfLineMembersResolve)
	{
		Reference(static_cast<rhi::Result<void> (rhi::BackendSelection::*)(const rhi::BackendEntry &)>(&rhi::BackendSelection::Add));
		Reference(&rhi::BackendSelection::AddAll);
		Reference(&rhi::BackendSelection::AddAvailable);
		Reference(&rhi::BackendSelection::AddSelfRegistered);
		Reference(&rhi::BackendSelection::AddModule);
		Reference(&rhi::BackendSelection::AddCatalog);
		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (rhi::BackendSelection::*)(const rhi::InstanceDesc &)>(&rhi::BackendSelection::CreateInstance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (rhi::BackendSelection::*)(const rhi::DeviceDesc &)>(&rhi::BackendSelection::CreateDevice));
		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (rhi::BackendSelection::*)(rhi::GraphicsApiId, const rhi::InstanceDesc &)>(
			&rhi::BackendSelection::CreateInstance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (rhi::BackendSelection::*)(rhi::GraphicsApiId, const rhi::DeviceDesc &)>(
			&rhi::BackendSelection::CreateDevice));

		Reference(&rhi::BackendModule::Load);
		Reference(&rhi::BackendModule::LiveObjects);
		Reference(&rhi::BackendModule::Unload);
	}

	// A constructor and a destructor have no address, so the only way to name one is to run it. Both of these do nothing beyond their own bookkeeping.
	TEST(ExportSurface, TheConstructorsAndDestructorsResolve)
	{
		rhi::BackendSelection selection(rhi::BackendPreference{});
		rhi::BackendSelection moved(std::move(selection));
		selection = std::move(moved);

		rhi::BackendModule module;
		rhi::BackendModule adopted(std::move(module));
		module = std::move(adopted);

		EXPECT_FALSE(module.IsLoaded());
	}

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(ExportSurface, TheVulkanNativeEntryPointsResolve)
	{
		Reference(&rhi::CreateDevice<rhi::VulkanApi>);
		Reference(&rhi::GetVulkanNativeDevice);
		Reference(&rhi::GetVulkanNativeSwapchain);
		Reference(&rhi::GetVulkanSemaphore);
		Reference(&rhi::GetVulkanCommandBuffer);
		Reference(&rhi::GetVulkanCommandPool);
		Reference(&rhi::GetVulkanQueueView);
		Reference(&rhi::native::NativeAccess<rhi::VulkanApi>::MakeCommandListView);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL3

	TEST(ExportSurface, TheMetal3NativeEntryPointsResolve)
	{
		Reference(&rhi::CreateDevice<rhi::MetalApi>);
		Reference(&rhi::GetMetalNativeDevice);
		Reference(&rhi::GetMetalCommandBuffer);
		Reference(&rhi::GetMetalRenderCommandEncoder);
		Reference(&rhi::GetMetalQueueView);
		Reference(&rhi::native::NativeAccess<rhi::MetalApi>::MakeCommandListView);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL4

	TEST(ExportSurface, TheMetal4NativeEntryPointsResolve)
	{
		Reference(&rhi::CreateDevice<rhi::Metal4Api>);
		Reference(&rhi::GetMetal4NativeDevice);
		Reference(&rhi::GetMetal4CommandBuffer);
		Reference(&rhi::GetMetal4RenderCommandEncoder);
		Reference(&rhi::GetMetal4ComputeCommandEncoder);
		Reference(&rhi::GetMetal4ArgumentTable);
		Reference(&rhi::GetMetal4QueueView);
		Reference(&rhi::native::NativeAccess<rhi::Metal4Api>::MakeCommandListView);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12

	TEST(ExportSurface, TheDirect3DNativeEntryPointsResolve)
	{
		Reference(&rhi::CreateDevice<rhi::D3D12Api>);
		Reference(&rhi::GetD3D12NativeDevice);
		Reference(&rhi::GetD3D12NativeSwapchain);
		Reference(&rhi::GetD3D12CommandList);
		Reference(&rhi::GetD3D12CommandAllocator);
		Reference(&rhi::GetD3D12QueueView);
		Reference(&rhi::native::NativeAccess<rhi::D3D12Api>::MakeCommandListView);
	}

#endif

} // namespace

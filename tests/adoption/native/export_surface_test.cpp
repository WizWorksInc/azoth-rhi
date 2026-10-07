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

	template <class Fn>
	void Reference(Fn function) noexcept
	{
		Fn volatile sink = function;
		static_cast<void>(sink);
	}

	TEST(ExportSurface, TheFreeEntryPointsResolve)
	{
		Reference(&rhi::detail::guards_held);
		Reference(&rhi::detail::reentrancy_violation_count);
		Reference(&rhi::detail::host_allocator_slot);
		Reference(&rhi::detail::device_allocator_slot);
		Reference(&rhi::detail::profiler_slot);

		Reference(&rhi::set_clip_space);
		Reference(&rhi::get_clip_space);

		Reference(&rhi::select_graphics_api);
		Reference(&rhi::make_presentation_backend);
		Reference(&rhi::native::resolve_vulkan_loader);

		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (*)(rhi::GraphicsApiRegistry &, std::span<const rhi::GraphicsApiId>, const rhi::InstanceDesc &)>(
			&rhi::CreateInstance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (*)(rhi::GraphicsApiRegistry &, std::span<const rhi::GraphicsApiId>, const rhi::DeviceDesc &)>(
			&rhi::create_device));

		Reference(&rhi::create_device<rhi::NullApi>);

		Reference(&rhi::available_backends);
		Reference(static_cast<const rhi::BackendEntry * (*)(std::string_view) noexcept>(&rhi::find_available_backend));
		Reference(static_cast<const rhi::BackendEntry * (*)(rhi::GraphicsApiId) noexcept>(&rhi::find_available_backend));
		Reference(&rhi::self_registered_backends);
		Reference(static_cast<rhi::Result<void> (*)(rhi::GraphicsApiRegistry &, rhi::GraphicsApiId)>(&rhi::register_backend));
	}

	TEST(ExportSurface, TheOutOfLineMembersResolve)
	{
		Reference(static_cast<rhi::Result<void> (rhi::BackendSelection::*)(const rhi::BackendEntry &)>(&rhi::BackendSelection::add));
		Reference(&rhi::BackendSelection::add_all);
		Reference(&rhi::BackendSelection::AddAvailable);
		Reference(&rhi::BackendSelection::add_self_registered);
		Reference(&rhi::BackendSelection::add_module);
		Reference(&rhi::BackendSelection::add_catalog);
		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (rhi::BackendSelection::*)(const rhi::InstanceDesc &)>(&rhi::BackendSelection::create_instance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (rhi::BackendSelection::*)(const rhi::DeviceDesc &)>(&rhi::BackendSelection::CreateDevice));
		Reference(static_cast<rhi::Result<rhi::UniqueInstance> (rhi::BackendSelection::*)(rhi::GraphicsApiId, const rhi::InstanceDesc &)>(
			&rhi::BackendSelection::create_instance));
		Reference(static_cast<rhi::Result<rhi::UniqueDevice> (rhi::BackendSelection::*)(rhi::GraphicsApiId, const rhi::DeviceDesc &)>(
			&rhi::BackendSelection::CreateDevice));

		Reference(&rhi::BackendModule::load);
		Reference(&rhi::BackendModule::live_objects);
		Reference(&rhi::BackendModule::unload);
	}

	TEST(ExportSurface, TheConstructorsAndDestructorsResolve)
	{
		rhi::BackendSelection selection(rhi::BackendPreference{});
		rhi::BackendSelection moved(std::move(selection));
		selection = std::move(moved);

		rhi::BackendModule module;
		rhi::BackendModule adopted(std::move(module));
		module = std::move(adopted);

		EXPECT_FALSE(module.is_loaded());
	}

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(ExportSurface, TheVulkanNativeEntryPointsResolve)
	{
		Reference(&rhi::create_device<rhi::VulkanApi>);
		Reference(&rhi::get_vulkan_native_device);
		Reference(&rhi::get_vulkan_native_swapchain);
		Reference(&rhi::get_vulkan_semaphore);
		Reference(&rhi::get_vulkan_command_buffer);
		Reference(&rhi::get_vulkan_command_pool);
		Reference(&rhi::GetVulkanQueueView);
		Reference(&rhi::native::NativeAccess<rhi::VulkanApi>::make_command_list_view);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL3

	TEST(ExportSurface, TheMetal3NativeEntryPointsResolve)
	{
		Reference(&rhi::create_device<rhi::MetalApi>);
		Reference(&rhi::get_metal_native_device);
		Reference(&rhi::get_metal_command_buffer);
		Reference(&rhi::get_metal_render_command_encoder);
		Reference(&rhi::get_metal_queue_view);
		Reference(&rhi::native::NativeAccess<rhi::MetalApi>::make_command_list_view);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL4

	TEST(ExportSurface, TheMetal4NativeEntryPointsResolve)
	{
		Reference(&rhi::create_device<rhi::Metal4Api>);
		Reference(&rhi::get_metal4_native_device);
		Reference(&rhi::get_metal4_command_buffer);
		Reference(&rhi::get_metal4_render_command_encoder);
		Reference(&rhi::get_metal4_compute_command_encoder);
		Reference(&rhi::get_metal4_argument_table);
		Reference(&rhi::get_metal4_queue_view);
		Reference(&rhi::native::NativeAccess<rhi::Metal4Api>::make_command_list_view);
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

}

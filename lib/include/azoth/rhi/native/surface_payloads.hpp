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

#pragma once

/**
 * \file
 * \brief Native surface payloads requested through SurfaceSource.
 */

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/core/api.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi::native
{

	/**
	 * \brief Vulkan loader payload exchanged between host and RHI.
	 *
	 * A host that already loaded Vulkan fills this so the RHI uses the same loader as the window system. A host that has not can leave it null and read
	 * back the loader the RHI resolved.
	 */
	struct VulkanLoaderPayload final
	{
		static constexpr InterfaceId kId = MakeInterfaceId("azoth.rhi.surface.vulkanLoader");

		/**
		 * \brief vkGetInstanceProcAddr, kept untyped so this header does not include Vulkan.
		 */
		void * getInstanceProcAddr = nullptr;
	};

	/**
	 * \brief Vulkan surface payload created by the host for an RHI-owned Vulkan instance.
	 *
	 * The RHI fills instance before the request. The host fills surface, and ownership of that surface transfers to the RHI.
	 */
	struct VulkanSurfacePayload final
	{
		static constexpr InterfaceId kId = MakeInterfaceId("azoth.rhi.surface.vulkan");

		/**
		 * \brief VkInstance supplied by the RHI, kept untyped so this header does not include Vulkan.
		 */
		void * instance = nullptr;

		/**
		 * \brief VkSurfaceKHR supplied by the host.
		 *
		 * Vulkan makes VkSurfaceKHR non-dispatchable, so it can travel as a 64-bit integer on supported targets.
		 */
		std::uint64_t surface = 0;
	};

	/**
	 * \brief Metal layer payload supplied by the host.
	 *
	 * The layer stays host-owned and must outlive the swapchain created over it.
	 */
	struct MetalSurfacePayload final
	{
		static constexpr InterfaceId kId = MakeInterfaceId("azoth.rhi.surface.metal");

		/**
		 * \brief CAMetalLayer supplied by the host.
		 */
		void * layer = nullptr;
	};

	/**
	 * \brief Win32 window payload supplied by the host.
	 *
	 * The window stays host-owned and must outlive the swapchain created for it.
	 */
	struct Win32SurfacePayload final
	{
		static constexpr InterfaceId kId = MakeInterfaceId("azoth.rhi.surface.win32");

		/**
		 * \brief HWND supplied by the host.
		 */
		void * window = nullptr;
	};

	/**
	 * \brief Resolves vkGetInstanceProcAddr for this build's Vulkan backend.
	 *
	 * Returns null when this build has no Vulkan backend or cannot find a loader. Use this before window-library initialization when the library can accept a
	 * loader entry point directly.
	 */
	[[nodiscard]] AZO_RHI_API void * ResolveVulkanLoader();

} // namespace azo::rhi::native

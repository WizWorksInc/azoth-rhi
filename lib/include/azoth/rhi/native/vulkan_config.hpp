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
 * \brief Vulkan device configuration block.
 */

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/device_config.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi::native
{

	/**
	 * \brief How the Vulkan backend lowers a rendering scope.
	 *
	 * Invisible through BeginRendering either way, which is why this is backend policy, not a device feature.
	 */
	enum class VulkanRenderingLowering : std::uint8_t
	{
		/**
		 * \brief Record dynamic rendering where the adapter has it, and lower onto render-pass objects where it does not.
		 */
		eAutomatic,

		/**
		 * \brief Device creation fails when the adapter cannot record dynamic rendering.
		 */
		eDynamicRendering,

		/**
		 * \brief Always lower a rendering scope onto cached render-pass and framebuffer objects.
		 */
		eRenderPassObjects,
	};

	/**
	 * \brief Vulkan device configuration, borrowed for the duration of device creation.
	 *
	 * Names and numbers only, so including this never needs the Vulkan headers.
	 */
	struct VulkanDeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(VulkanDeviceConfig), .version = 1 };

		/**
		 * \brief Lowest Vulkan version the loader has to offer for the instance to be created.
		 *
		 * {0, 0} selects the backend default.
		 */
		ApiVersion minimumInstanceVersion{};

		/**
		 * \brief Vulkan version the device is created against, which the adapter has to support.
		 *
		 * {0, 0} selects the backend default.
		 */
		ApiVersion deviceVersion{};

		VulkanRenderingLowering renderingLowering = VulkanRenderingLowering::eAutomatic;

		/**
		 * \brief Instance extensions to enable on top of the ones the backend requires.
		 */
		std::span<const char * const> instanceExtensions;

		/**
		 * \brief Device extensions to enable on top of the ones the backend requires.
		 */
		std::span<const char * const> deviceExtensions;
	};

	/**
	 * \brief Declares VulkanDeviceConfig as the Vulkan backend's configuration block.
	 */
	template <>
	struct DeviceConfigFor<VulkanApi> final
	{
		using Config = VulkanDeviceConfig;
	};

} // namespace azo::rhi::native

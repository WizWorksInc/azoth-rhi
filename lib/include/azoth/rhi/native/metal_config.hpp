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
 * \brief Metal 3 and Metal 4 device configuration blocks.
 */

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/device_config.hpp"

namespace azo::rhi::native
{

	/**
	 * \brief Metal 3 device configuration, borrowed for the duration of device creation.
	 *
	 * Names and numbers only, so including this never needs the Metal headers.
	 */
	struct MetalDeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(MetalDeviceConfig), .version = 1 };

		/**
		 * \brief Metal generation this device is pinned to. {0, 0} takes the generation this backend is.
		 *
		 * Metal 4 is a backend of its own, so pinning it here is refused and never redirected. Both platform owners tell callers to pin a tested baseline, so a
		 * caller stepping around a driver bug is believed.
		 */
		ApiVersion generation{};
	};

	/**
	 * \brief Declares MetalDeviceConfig as the Metal 3 backend's configuration block.
	 */
	template <>
	struct DeviceConfigFor<MetalApi> final
	{
		using Config = MetalDeviceConfig;
	};

	/**
	 * \brief Metal 4 device configuration, on the same terms as the Metal 3 one.
	 */
	struct Metal4DeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(Metal4DeviceConfig), .version = 1 };

		/**
		 * \brief Metal generation this device is pinned to. {0, 0} takes the generation this backend is.
		 *
		 * A generation below 4 names the Metal 3 backend and is refused here.
		 */
		ApiVersion generation{};
	};

	/**
	 * \brief Declares Metal4DeviceConfig as the Metal 4 backend's configuration block.
	 */
	template <>
	struct DeviceConfigFor<Metal4Api> final
	{
		using Config = Metal4DeviceConfig;
	};

} // namespace azo::rhi::native

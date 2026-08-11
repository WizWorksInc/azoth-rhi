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
 * \brief Direct3D 12 device configuration block.
 */

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/device_config.hpp"

namespace azo::rhi::native
{

	/**
	 * \brief Direct3D 12 device configuration, borrowed for the duration of device creation.
	 *
	 * Names and numbers only, so including this never needs the Direct3D headers.
	 */
	struct D3D12DeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(D3D12DeviceConfig), .version = 1 };

		/**
		 * \brief Lowest feature level an adapter has to create at, written as a Direct3D version. {0, 0} takes the 12_0 floor.
		 *
		 * A floor, not a pin: the device is still created at the highest level the chosen adapter supports, and DeviceCaps::apiVersion reports that.
		 */
		ApiVersion minimumFeatureLevel{};
	};

	/**
	 * \brief Declares D3D12DeviceConfig as the Direct3D 12 backend's configuration block.
	 */
	template <>
	struct DeviceConfigFor<D3D12Api> final
	{
		using Config = D3D12DeviceConfig;
	};

} // namespace azo::rhi::native

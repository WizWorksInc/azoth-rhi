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

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/device_config.hpp"

namespace azo::rhi::native
{

	struct MetalDeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(MetalDeviceConfig), .version = 1 };

		ApiVersion generation{};
	};

	template <>
	struct DeviceConfigFor<MetalApi> final
	{
		using Config = MetalDeviceConfig;
	};

	struct Metal4DeviceConfig final
	{
		InterfaceHeader header{ .byteSize = sizeof(Metal4DeviceConfig), .version = 1 };

		ApiVersion generation{};
	};

	template <>
	struct DeviceConfigFor<Metal4Api> final
	{
		using Config = Metal4DeviceConfig;
	};

} // namespace azo::rhi::native

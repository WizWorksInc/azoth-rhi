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

#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/device/device.hpp"

namespace azo::rhi::test::oracle
{

	struct BackendIdentity final
	{
		GraphicsApiId id{};

		// What the device is expected to answer GetGraphicsApiName with, for example Vulkan.
		CString displayName = nullptr;
	};

	void CheckDeviceIdentity(Device device, const BackendIdentity & expected);

	void CheckQueueAvailability(Device device);

	void CheckResourceLifecycle(Device device);

	void CheckFailuresCarryDiagnostics(Device device);

	void CheckCommandListLifecycle(Device device);

	void CheckGarbageCollection(Device device);

	void CheckWholeContract(Device device, const BackendIdentity & expected);

} // namespace azo::rhi::test::oracle

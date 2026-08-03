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

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/device/device.hpp" // DriverId

#include <cstdint>
#include <string>

namespace azo::rhi
{
	/*
	 * Formats a Vulkan-reported driver version as "major.minor".
	 *
	 * Decodes rawVersion (VkPhysicalDeviceProperties::driverVersion) the way the identified driver packs it. The drivers that deviate from the standard Vulkan
	 * layout (MoltenVK, NVIDIA, Intel on Windows, Qualcomm) are handled explicitly and every other driver falls back to the standard layout.
	 */
	[[nodiscard]] detail::HostString FormatVulkanDriverVersion(DriverId id, std::uint32_t rawVersion);

	/*
	 * Formats a D3D12/DXGI user-mode driver version as "product.version.subversion.build".
	 *
	 * umdVersion is the LARGE_INTEGER value from IDXGIAdapter::CheckInterfaceSupport, a uniform four-by-16-bit WDDM packing that every vendor shares so one
	 * decode serves all adapters.
	 */
	[[nodiscard]] detail::HostString FormatD3D12DriverVersion(std::uint64_t umdVersion);
} // namespace azo::rhi

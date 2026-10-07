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
#include "azoth/rhi/core/api.hpp"

#include <cstdint> // NOLINT

namespace azo::rhi::native
{

	struct VulkanLoaderPayload final
	{
		static constexpr InterfaceId kId = make_interface_id("azoth.rhi.surface.vulkanLoader");

		void * getInstanceProcAddr = nullptr;
	};

	struct VulkanSurfacePayload final
	{
		static constexpr InterfaceId kId = make_interface_id("azoth.rhi.surface.vulkan");

		void * instance = nullptr;

		std::uint64_t surface = 0;
	};

	struct MetalSurfacePayload final
	{
		static constexpr InterfaceId kId = make_interface_id("azoth.rhi.surface.metal");

		void * layer = nullptr;
	};

	struct Win32SurfacePayload final
	{
		static constexpr InterfaceId kId = make_interface_id("azoth.rhi.surface.win32");

		void * window = nullptr;
	};

	[[nodiscard]] AZO_RHI_API void * resolve_vulkan_loader();

} // namespace azo::rhi::native

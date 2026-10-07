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

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/surface_source.hpp"
#include "azoth/rhi/present/swapchain.hpp"

namespace azo::rhi
{

	class PresentationBackend
	{
	public:
		PresentationBackend()										 = default;
		PresentationBackend(const PresentationBackend &)			 = delete;
		PresentationBackend & operator=(const PresentationBackend &) = delete;
		PresentationBackend(PresentationBackend &&)					 = delete;
		PresentationBackend & operator=(PresentationBackend &&)		 = delete;
		virtual ~PresentationBackend()								 = default;

		[[nodiscard]] virtual bool init_instance_loader(SurfaceSource & source) = 0;

		[[nodiscard]] virtual SurfaceHandle create_surface(SurfaceSource & source, Device device) = 0;
	};

	[[nodiscard]] AZO_RHI_API GraphicsApiId select_graphics_api(const char * requestedOverride = nullptr);

	[[nodiscard]] AZO_RHI_API HostUniquePtr<PresentationBackend> make_presentation_backend(GraphicsApiId api);

}

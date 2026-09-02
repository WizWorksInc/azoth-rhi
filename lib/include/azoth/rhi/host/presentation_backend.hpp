// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		[[nodiscard]] virtual bool InitInstanceLoader(SurfaceSource & source) = 0;

		[[nodiscard]] virtual SurfaceHandle CreateSurface(SurfaceSource & source, Device device) = 0;
	};

	[[nodiscard]] AZO_RHI_API GraphicsApiId SelectGraphicsApi(const char * requestedOverride = nullptr);

	[[nodiscard]] AZO_RHI_API HostUniquePtr<PresentationBackend> MakePresentationBackend(GraphicsApiId api);

}

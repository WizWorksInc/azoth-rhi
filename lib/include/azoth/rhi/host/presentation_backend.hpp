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
 * \brief Host presentation backend selection and surface creation.
 */

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/surface_source.hpp"
#include "azoth/rhi/present/swapchain.hpp"

namespace azo::rhi
{

	/**
	 * \brief Host-side presentation support for one graphics API.
	 *
	 * Implementations own any native surface resources they create and release them on destruction.
	 */
	class PresentationBackend
	{
	public:
		PresentationBackend()										 = default;
		PresentationBackend(const PresentationBackend &)			 = delete;
		PresentationBackend & operator=(const PresentationBackend &) = delete;
		PresentationBackend(PresentationBackend &&)					 = delete;
		PresentationBackend & operator=(PresentationBackend &&)		 = delete;
		virtual ~PresentationBackend()								 = default;

		/**
		 * \brief Loads presentation entry points needed before device creation.
		 *
		 * Vulkan uses this to seed dynamic dispatch. Directly linked backends can return success without work.
		 */
		[[nodiscard]] virtual bool InitInstanceLoader(SurfaceSource & source) = 0;

		/**
		 * \brief Creates a presentation surface for the source window and device.
		 *
		 * Returns a default-constructed handle on failure.
		 */
		[[nodiscard]] virtual SurfaceHandle CreateSurface(SurfaceSource & source, Device device) = 0;
	};

	/**
	 * \brief Selects the graphics API requested for presentation.
	 *
	 * Precedence is requestedOverride, AZOTH_RHI_BACKEND, then the build default. An unrecognized name falls back to Vulkan.
	 */
	[[nodiscard]] GraphicsApiId SelectGraphicsApi(const char * requestedOverride = nullptr);

	/**
	 * \brief Creates presentation support for one API.
	 *
	 * Returns null when this build has no presentation backend for api. The object is host-allocated so an installed HostAllocator sees it, and the deleter
	 * preserves the concrete allocation size even though the returned owner is base-typed.
	 */
	[[nodiscard]] HostUniquePtr<PresentationBackend> MakePresentationBackend(GraphicsApiId api);

} // namespace azo::rhi

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
 * \brief Swapchain ABI block published by backend swapchain objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Required backend swapchain table for image acquisition, presentation, resize, and back-buffer queries.
	 *
	 * Back-buffer texture and view handles are owned by the swapchain. resize may replace them, so callers must re-query handles after a successful resize.
	 */
	struct SwapchainApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SwapchainApi), .version = 1 };

		/// Acquire and present.

		/**
		 * \brief Acquires the next presentable image from the backend swapchain.
		 *
		 * \note timeoutNanoseconds is expressed as a duration in nanoseconds.
		 */
		AcquireResult (*acquireNextImage)(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Presents an acquired image after waiting for the supplied render-finished semaphore.
		 *
		 * \attention queueImpl must be a backend queue object compatible with presentation for this swapchain.
		 */
		PresentResult (*present)(
			void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept = nullptr;

		/// Back-buffer resources.

		/**
		 * \brief Returns the swapchain-owned texture handle for an image index.
		 *
		 * \attention Handles returned by this function may become stale after resize succeeds.
		 */
		TextureHandle (*getBackBuffer)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain-owned texture-view handle for an image index.
		 *
		 * \attention Handles returned by this function may become stale after resize succeeds.
		 */
		TextureViewHandle (*getBackBufferView)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain-owned semaphore used for presentation synchronization for one image index.
		 */
		BinarySemaphoreHandle (*getPerImagePresentSemaphore)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/// Current configuration.

		Format (*getFormat)(void * impl) noexcept			 = nullptr;
		PresentMode (*getPresentMode)(void * impl) noexcept	 = nullptr;
		std::uint32_t (*getImageCount)(void * impl) noexcept = nullptr;
		std::uint32_t (*getWidth)(void * impl) noexcept		 = nullptr;
		std::uint32_t (*getHeight)(void * impl) noexcept	 = nullptr;

		/// Reconfiguration.

		/**
		 * \brief Recreates or resizes the backend swapchain to the requested pixel extent.
		 *
		 * \attention A successful resize may invalidate previously queried back-buffer textures, views, and per-image semaphores.
		 */
		bool (*resize)(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept = nullptr;

		/**
		 * \brief Requests a new presentation mode for future presents.
		 *
		 * \note The active mode is the value returned by getPresentMode after the backend accepts or maps the request.
		 */
		bool (*setPresentMode)(void * impl, PresentMode mode, Error * error) noexcept = nullptr;

		/**
		 * \brief Reports whether swapchain images can be copied for frame capture or diagnostics.
		 */
		bool (*supportsReadback)(void * impl) noexcept = nullptr;
	};

} // namespace azo::rhi

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

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint> // NOLINT: (JB): Fix later.

namespace azo::rhi
{

	/**
	 * \brief Swapchain dimensions use pixels, and unavailable back buffers return invalid handles.
	 */
	struct SwapchainApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(SwapchainApi), .version = 1 };

		/**
		 * \brief Acquisition synchronization and timeout support depend on the backend.
		 */
		AcquireResult (*acquireNextImage)(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Only acquired images can be presented.
		 * \param impl Swapchain.
		 * \param imageIndex Acquired image index.
		 * \param renderFinished Semaphore signaled after rendering.
		 * \param queueImpl Backend presentation queue instance.
		 * \param[out] error Optional error details.
		 */
		PresentResult (*present)(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Returns the texture for a back buffer image.
		 */
		TextureHandle (*getBackBuffer)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the view for a back buffer image.
		 */
		TextureViewHandle (*getBackBufferView)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns a semaphore to signal before presentation, or an invalid handle if none is provided.
		 */
		BinarySemaphoreHandle (*getPerImagePresentSemaphore)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain's image format.
		 */
		Format (*getFormat)(void * impl) noexcept			 = nullptr;

		/**
		 * \brief Returns the effective presentation mode.
		 */
		PresentMode (*getPresentMode)(void * impl) noexcept	 = nullptr;

		/**
		 * \brief Returns the number of swapchain images.
		 */
		std::uint32_t (*getImageCount)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain image width.
		 */
		std::uint32_t (*getWidth)(void * impl) noexcept		 = nullptr;

		/**
		 * \brief Returns the swapchain image height.
		 */
		std::uint32_t (*getHeight)(void * impl) noexcept	 = nullptr;

		/**
		 * \brief Resizes the swapchain images.
		 */
		bool (*resize)(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept = nullptr;

		/**
		 * \brief Requests a presentation mode for the swapchain.
		 */
		bool (*setPresentMode)(void * impl, PresentMode mode, Error * error) noexcept = nullptr;

		/**
		 * \brief Reports whether swapchain images support readback.
		 */
		bool (*supportsReadback)(void * impl) noexcept = nullptr;
	};

} // namespace azo::rhi

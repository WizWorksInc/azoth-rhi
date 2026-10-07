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
	 * \brief Callbacks for image acquisition, presentation and swapchain settings.
	 */
	struct SwapchainApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(SwapchainApi), .version = 1 };

		/**
		 * \brief Acquires the next image for rendering.
		 * \param impl Backend swapchain instance.
		 * \param timeoutNanoseconds Requested acquisition timeout in nanoseconds.
		 * \param[out] error Optional output for failure details.
		 * \return Acquisition status, image index and optional image availability semaphore.
		 */
		AcquireResult (*acquireNextImage)(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept = nullptr;

		/**
		 * \brief Presents an acquired image.
		 * \param impl Backend swapchain instance.
		 * \param imageIndex Acquired image index.
		 * \param renderFinished Semaphore signaled when rendering is complete.
		 * \param queueImpl Backend presentation queue instance.
		 * \param[out] error Optional output for failure details.
		 * \return Presentation status.
		 */
		PresentResult (*present)(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Returns the texture for a back buffer image.
		 * \param impl Backend swapchain instance.
		 * \param imageIndex Swapchain image index.
		 * \return Back buffer texture handle, or an invalid handle if unavailable.
		 */
		TextureHandle (*getBackBuffer)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the view for a back buffer image.
		 * \param impl Backend swapchain instance.
		 * \param imageIndex Swapchain image index.
		 * \return Back buffer view handle, or an invalid handle if unavailable.
		 */
		TextureViewHandle (*getBackBufferView)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the render completion semaphore for an image.
		 * \param impl Backend swapchain instance.
		 * \param imageIndex Swapchain image index.
		 * \return Semaphore to signal before presentation, or an invalid handle if none is provided.
		 */
		BinarySemaphoreHandle (*getPerImagePresentSemaphore)(void * impl, std::uint32_t imageIndex) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain's image format.
		 * \param impl Backend swapchain instance.
		 * \return Swapchain image format.
		 */
		Format (*getFormat)(void * impl) noexcept			 = nullptr;

		/**
		 * \brief Returns the swapchain's effective presentation mode.
		 * \param impl Backend swapchain instance.
		 * \return Effective presentation mode.
		 */
		PresentMode (*getPresentMode)(void * impl) noexcept	 = nullptr;

		/**
		 * \brief Returns the number of swapchain images.
		 * \param impl Backend swapchain instance.
		 * \return Swapchain image count.
		 */
		std::uint32_t (*getImageCount)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns the swapchain image width.
		 * \param impl Backend swapchain instance.
		 * \return Image width in pixels.
		 */
		std::uint32_t (*getWidth)(void * impl) noexcept		 = nullptr;

		/**
		 * \brief Returns the swapchain image height.
		 * \param impl Backend swapchain instance.
		 * \return Image height in pixels.
		 */
		std::uint32_t (*getHeight)(void * impl) noexcept	 = nullptr;

		/**
		 * \brief Resizes the swapchain images.
		 * \param impl Backend swapchain instance.
		 * \param width Requested image width in pixels.
		 * \param height Requested image height in pixels.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*resize)(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept = nullptr;

		/**
		 * \brief Requests a presentation mode for the swapchain.
		 * \param impl Backend swapchain instance.
		 * \param mode Requested presentation mode.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*setPresentMode)(void * impl, PresentMode mode, Error * error) noexcept = nullptr;

		/**
		 * \brief Reports whether swapchain images support readback.
		 * \param impl Backend swapchain instance.
		 * \return True if readback is supported, false otherwise.
		 */
		bool (*supportsReadback)(void * impl) noexcept = nullptr;
	};

} // namespace azo::rhi

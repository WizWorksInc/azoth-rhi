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
 * \brief Presentation surface handles, swapchain creation, acquire, present, and resize.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{
	class Queue;

	namespace detail
	{
		struct FacadeBuilder;
	} // namespace detail

	struct SwapchainApi;

	/**
	 * \brief Opaque presentation surface handle created by platform code.
	 *
	 * Platform headers stay outside the core RHI. The backend interprets value according to the API that created the swapchain.
	 */
	struct SurfaceHandle final
	{
		std::uint64_t value = 0;
	};

	/**
	 * \brief Swapchain creation request.
	 *
	 * Fallback spans and debugName are borrowed for the duration of creation.
	 */
	struct SwapchainDesc final
	{
		SurfaceHandle surface{};
		std::uint32_t width	 = 1;
		std::uint32_t height = 1;

		/**
		 * \brief Preferred color format.
		 *
		 * Defaults to sRGB. Use a UNorm format explicitly for a linear back buffer.
		 */
		Format preferredFormat	 = Format::eBGRA8Srgb;
		PresentMode presentMode	 = PresentMode::eFifo;
		std::uint32_t imageCount = 3;

		/**
		 * \brief Ordered color formats tried after preferredFormat.
		 *
		 * The first surface-supported format wins.
		 */
		std::span<const Format> formatFallbacks;

		/**
		 * \brief Ordered present modes tried after presentMode.
		 *
		 * The first surface-supported mode wins.
		 */
		std::span<const PresentMode> presentModeFallbacks;

		/**
		 * \brief Allows tearing when the platform and chosen present mode support it.
		 */
		bool allowTearing = false;

		const char * debugName = nullptr;
	};

	/**
	 * \brief Acquire or present status.
	 */
	enum class SwapchainStatus : std::uint8_t
	{
		eOk,

		/**
		 * \brief Operation succeeded, but swapchain recreation is recommended.
		 *
		 * Rendering can usually continue until a convenient recreation point.
		 */
		eSuboptimal,

		/**
		 * \brief Swapchain no longer matches the surface.
		 */
		eOutOfDate,

		/**
		 * \brief Presentation surface is no longer usable.
		 */
		eSurfaceLost,

		/**
		 * \brief Device was lost during acquire or present.
		 */
		eDeviceLost,

		eTimeout,
		eError,
	};

	/**
	 * \brief The acquired back buffer, what is needed to render to it and both ends of its presentation edge.
	 *
	 * Wait imageAvailable before GPU use. It does not prove older work on the same back buffer has finished.
	 *
	 * texture, view and renderFinished are what the accessors answer for imageIndex, resolved once here so a frame never pairs one image's view with another's
	 * semaphore. They are filled only when the acquire succeeded. Check status first.
	 */
	struct AcquireResult final
	{
		SwapchainStatus status	 = SwapchainStatus::eOk;
		std::uint32_t imageIndex = 0;
		BinarySemaphoreHandle imageAvailable{};

		TextureHandle texture{};
		TextureViewHandle view{};
		BinarySemaphoreHandle renderFinished{};
	};

	/**
	 * \brief Result of presenting one acquired swapchain image.
	 */
	struct PresentResult final
	{
		SwapchainStatus status = SwapchainStatus::eOk;
	};

	/**
	 * \brief Presentation swapchain over a platform surface.
	 *
	 * Resize invalidates acquired back buffers, back-buffer views, and native swapchain views. Synchronize externally before resizing.
	 */
	class AZO_RHI_API Swapchain final
	{
	public:
		Swapchain() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_dispatch != nullptr;
		}

		[[nodiscard]] AcquireResult AcquireNextImage(std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()) noexcept;
		[[nodiscard]] AcquireResult AcquireNextImage(std::uint64_t timeoutNanoseconds, Error & error) noexcept;
		[[nodiscard]] Result<AcquireResult> AcquireNextImageWithResult(std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()) noexcept;
		[[nodiscard]] PresentResult Present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept;
		[[nodiscard]] PresentResult Present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, Error & error) noexcept;
		[[nodiscard]] Result<PresentResult> PresentWithResult(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept;

		[[nodiscard]] TextureHandle GetBackBuffer(std::uint32_t imageIndex) const noexcept;
		[[nodiscard]] TextureViewHandle GetBackBufferView(std::uint32_t imageIndex) const noexcept;

		/**
		 * \brief Returns the per-image present wait semaphore.
		 *
		 * One semaphore per image prevents reuse before presentation has finished waiting on the previous submission for that image.
		 */
		[[nodiscard]] BinarySemaphoreHandle GetPerImagePresentSemaphore(std::uint32_t imageIndex) const noexcept;

		[[nodiscard]] Format GetFormat() const noexcept;
		[[nodiscard]] std::uint32_t GetImageCount() const noexcept;
		[[nodiscard]] std::uint32_t GetWidth() const noexcept;
		[[nodiscard]] std::uint32_t GetHeight() const noexcept;

		/**
		 * \brief Recreates swapchain-owned back buffers for a new extent.
		 *
		 * Callers must ensure no in-flight work still uses the old back buffers or views.
		 */
		[[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height) noexcept;
		[[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height, Error & error) noexcept;

		/**
		 * \brief Requests a new presentation mode.
		 *
		 * The change takes effect on the next present or on the next resize for backends that fold mode changes into swapchain recreation.
		 */
		[[nodiscard]] bool SetPresentMode(PresentMode mode) noexcept;

		/**
		 * \brief Returns the presentation mode actually in use.
		 *
		 * A request is only a preference. Read this before pacing frames from the present mode.
		 */
		[[nodiscard]] PresentMode GetPresentMode() const noexcept;

		/**
		 * \brief Returns whether back buffers can be copied from for readback or capture.
		 *
		 * Support depends on the platform and swapchain creation path.
		 */
		[[nodiscard]] bool SupportsReadback() const noexcept;

	private:
		friend struct detail::FacadeBuilder;

		Swapchain(void * impl, const SwapchainApi * dispatch) noexcept : m_impl(impl), m_dispatch(dispatch) {}

		void * m_impl					= nullptr;
		const SwapchainApi * m_dispatch = nullptr;
	};

} // namespace azo::rhi

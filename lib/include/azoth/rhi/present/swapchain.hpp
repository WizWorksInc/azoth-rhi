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
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{
	class Queue;

	namespace detail
	{
		struct FacadeBuilder;
	}

	struct SwapchainApi;

	struct SurfaceHandle final
	{
		std::uint64_t value = 0;
	};

	struct SwapchainDesc final
	{
		SurfaceHandle surface{};
		std::uint32_t width	 = 1;
		std::uint32_t height = 1;

		Format preferredFormat	 = Format::eBGRA8Srgb;
		PresentMode presentMode	 = PresentMode::eFifo;
		std::uint32_t imageCount = 3;

		std::span<const Format> formatFallbacks;

		std::span<const PresentMode> presentModeFallbacks;

		bool allowTearing = false;

		const char * debugName = nullptr;
	};

	enum class SwapchainStatus : std::uint8_t
	{
		eOk,

		eSuboptimal,

		eOutOfDate,

		eSurfaceLost,

		eDeviceLost,

		eTimeout,
		eError,
	};

	struct AcquireResult final
	{
		SwapchainStatus status	 = SwapchainStatus::eOk;
		std::uint32_t imageIndex = 0;
		BinarySemaphoreHandle imageAvailable{};

		TextureHandle texture{};
		TextureViewHandle view{};
		BinarySemaphoreHandle renderFinished{};
	};

	struct PresentResult final
	{
		SwapchainStatus status = SwapchainStatus::eOk;
	};

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

		[[nodiscard]] BinarySemaphoreHandle GetPerImagePresentSemaphore(std::uint32_t imageIndex) const noexcept;

		[[nodiscard]] Format GetFormat() const noexcept;
		[[nodiscard]] std::uint32_t GetImageCount() const noexcept;
		[[nodiscard]] std::uint32_t GetWidth() const noexcept;
		[[nodiscard]] std::uint32_t GetHeight() const noexcept;

		[[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height) noexcept;
		[[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height, Error & error) noexcept;

		[[nodiscard]] bool SetPresentMode(PresentMode mode) noexcept;

		[[nodiscard]] PresentMode GetPresentMode() const noexcept;

		[[nodiscard]] bool SupportsReadback() const noexcept;

	private:
		friend struct detail::FacadeBuilder;

		Swapchain(void * impl, const SwapchainApi * dispatch) noexcept : m_impl(impl), m_dispatch(dispatch) {}

		void * m_impl					= nullptr;
		const SwapchainApi * m_dispatch = nullptr;
	};

}

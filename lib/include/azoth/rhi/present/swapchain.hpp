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

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr && m_dispatch != nullptr;
		}

		[[nodiscard]] AcquireResult acquire_next_image(std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()) noexcept;
		[[nodiscard]] AcquireResult acquire_next_image(std::uint64_t timeoutNanoseconds, Error & error) noexcept;
		[[nodiscard]] Result<AcquireResult> acquire_next_image_with_result(
			std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()
		) noexcept;
		[[nodiscard]] PresentResult present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept;
		[[nodiscard]] PresentResult present(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, Error & error) noexcept;
		[[nodiscard]] Result<PresentResult> present_with_result(Queue & queue, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished) noexcept;

		[[nodiscard]] TextureHandle get_back_buffer(std::uint32_t imageIndex) const noexcept;
		[[nodiscard]] TextureViewHandle get_back_buffer_view(std::uint32_t imageIndex) const noexcept;

		[[nodiscard]] BinarySemaphoreHandle get_per_image_present_semaphore(std::uint32_t imageIndex) const noexcept;

		[[nodiscard]] Format get_format() const noexcept;
		[[nodiscard]] std::uint32_t get_image_count() const noexcept;
		[[nodiscard]] std::uint32_t get_width() const noexcept;
		[[nodiscard]] std::uint32_t get_height() const noexcept;

		[[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height) noexcept;
		[[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height, Error & error) noexcept;

		[[nodiscard]] bool set_present_mode(PresentMode mode) noexcept;

		[[nodiscard]] PresentMode get_present_mode() const noexcept;

		[[nodiscard]] bool supports_readback() const noexcept;

	private:
		friend struct detail::FacadeBuilder;

		Swapchain(void * impl, const SwapchainApi * dispatch) noexcept : m_impl(impl), m_dispatch(dispatch) {}

		void * m_impl					= nullptr;
		const SwapchainApi * m_dispatch = nullptr;
	};

}

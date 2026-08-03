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
 * \brief Builder for swapchain descriptions.
 */

#include "azoth/rhi/present/swapchain.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace azo::rhi
{
	/**
	 * \brief Builds swapchain descriptions backed by fixed fallback storage and owned debug-name storage.
	 *
	 * Fallback formats and present modes are tried in insertion order after the preferred value. Extra fallback entries past the fixed limits are ignored.
	 */
	class SwapchainBuilder final
	{
	public:
		static constexpr std::size_t kMaxFormatFallbacks	  = 8;
		static constexpr std::size_t kMaxPresentModeFallbacks = 3;

		SwapchainBuilder & Surface(SurfaceHandle surface) noexcept
		{
			m_desc.surface = surface;
			return *this;
		}

		/**
		 * \brief Sets the swapchain extent in pixels.
		 *
		 * \param width Swapchain image width in pixels.
		 * \param height Swapchain image height in pixels.
		 */
		SwapchainBuilder & Extent(std::uint32_t width, std::uint32_t height) noexcept
		{
			m_desc.width  = width;
			m_desc.height = height;
			return *this;
		}

		SwapchainBuilder & PreferredFormat(Format format) noexcept
		{
			m_desc.preferredFormat = format;
			return *this;
		}

		/**
		 * \brief Appends a color-format fallback tried when the preferred format is unavailable.
		 *
		 * \note Extra fallbacks past kMaxFormatFallbacks are ignored.
		 */
		SwapchainBuilder & FallbackFormat(Format format) noexcept
		{
			if (m_formatFallbackCount < m_formatFallbacks.size())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				m_formatFallbacks[m_formatFallbackCount] = format;
				++m_formatFallbackCount;
			}

			return *this;
		}

		/**
		 * \brief Replaces the format preference order.
		 *
		 * The first entry becomes preferredFormat and later entries become fallbacks.
		 * \note An empty span clears the fallback list and leaves preferredFormat unchanged.
		 */
		SwapchainBuilder & PreferredFormats(std::span<const Format> formats) noexcept
		{
			m_formatFallbackCount = 0;
			if (formats.empty())
			{
				return *this;
			}

			m_desc.preferredFormat = formats.front();
			for (const Format format : formats.subspan(1))
			{
				FallbackFormat(format);
			}

			return *this;
		}

		// This setter's own name hides the enum for everything declared after it, which is why the rest of the class spells the type out.
		SwapchainBuilder & PresentMode(PresentMode presentMode) noexcept
		{
			m_desc.presentMode = presentMode;
			return *this;
		}

		/**
		 * \brief Appends a present-mode fallback tried when the preferred present mode is unavailable.
		 *
		 * \note Extra fallbacks past kMaxPresentModeFallbacks are ignored.
		 */
		SwapchainBuilder & FallbackPresentMode(azo::rhi::PresentMode presentMode) noexcept
		{
			if (m_presentModeFallbackCount < m_presentModeFallbacks.size())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				m_presentModeFallbacks[m_presentModeFallbackCount] = presentMode;
				++m_presentModeFallbackCount;
			}

			return *this;
		}

		/**
		 * \brief Replaces the present-mode preference order.
		 *
		 * The first entry becomes presentMode and later entries become fallbacks.
		 * \note An empty span clears the fallback list and leaves presentMode unchanged.
		 */
		SwapchainBuilder & PreferredPresentModes(std::span<const azo::rhi::PresentMode> presentModes) noexcept
		{
			m_presentModeFallbackCount = 0;
			if (presentModes.empty())
			{
				return *this;
			}

			m_desc.presentMode = presentModes.front();
			for (const azo::rhi::PresentMode presentMode : presentModes.subspan(1))
			{
				FallbackPresentMode(presentMode);
			}

			return *this;
		}

		SwapchainBuilder & ImageCount(std::uint32_t imageCount) noexcept
		{
			m_desc.imageCount = imageCount;
			return *this;
		}

		SwapchainBuilder & AllowTearing(bool enabled = true) noexcept
		{
			m_desc.allowTearing = enabled;
			return *this;
		}

		SwapchainBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a swapchain description that borrows this builder's fallback and debug-name storage.
		 *
		 * \attention The returned fallback spans and debugName pointer stay valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] SwapchainDesc Build() const noexcept
		{
			SwapchainDesc desc		  = m_desc;
			desc.debugName			  = m_debugName.empty() ? nullptr : m_debugName.c_str();
			desc.formatFallbacks	  = std::span{ m_formatFallbacks.data(), m_formatFallbackCount };
			desc.presentModeFallbacks = std::span{ m_presentModeFallbacks.data(), m_presentModeFallbackCount };
			return desc;
		}

	private:
		SwapchainDesc m_desc{};
		std::array<Format, kMaxFormatFallbacks> m_formatFallbacks{};
		std::size_t m_formatFallbackCount = 0;
		std::array<azo::rhi::PresentMode, kMaxPresentModeFallbacks> m_presentModeFallbacks{};
		std::size_t m_presentModeFallbackCount = 0;
		std::string m_debugName;
	};
} // namespace azo::rhi

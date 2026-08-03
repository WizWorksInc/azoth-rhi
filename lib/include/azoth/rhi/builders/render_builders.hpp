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
 * \brief Builders for dynamic rendering descriptions.
 */

#include "azoth/rhi/commands/render.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <vector>

namespace azo::rhi
{
	/**
	 * \brief Builds rendering-attachment descriptions.
	 */
	class RenderingAttachmentBuilder final
	{
	public:
		RenderingAttachmentBuilder & View(TextureViewHandle view) noexcept
		{
			m_desc.view = view;
			return *this;
		}

		RenderingAttachmentBuilder & State(ResourceState state) noexcept
		{
			m_desc.state = state;
			return *this;
		}

		RenderingAttachmentBuilder & Load(LoadOp op) noexcept
		{
			m_desc.load = op;
			return *this;
		}

		RenderingAttachmentBuilder & Store(StoreOp op) noexcept
		{
			m_desc.store = op;
			return *this;
		}

		/**
		 * \brief Sets a color clear value and switches the attachment load operation to LoadOp::eClear.
		 */
		RenderingAttachmentBuilder & ClearColor(float r, float g, float b, float a) noexcept
		{
			m_desc.load		  = LoadOp::eClear;
			m_desc.clearColor = azo::rhi::ClearColor{
				.r = r,
				.g = g,
				.b = b,
				.a = a,
			};
			return *this;
		}

		/**
		 * \brief Sets a depth-stencil clear value and switches the attachment load operation to LoadOp::eClear.
		 */
		RenderingAttachmentBuilder & ClearDepthStencil(float depth, std::uint32_t stencil = 0) noexcept
		{
			m_desc.load				 = LoadOp::eClear;
			m_desc.clearDepthStencil = azo::rhi::ClearDepthStencil{
				.depth	 = depth,
				.stencil = stencil,
			};
			return *this;
		}

		[[nodiscard]] constexpr RenderingAttachment Build() const noexcept
		{
			return m_desc;
		}

	private:
		RenderingAttachment m_desc{};
	};

	/**
	 * \brief Builds begin-rendering descriptions backed by owned color-attachment storage.
	 */
	class BeginRenderingBuilder final
	{
	public:
		BeginRenderingBuilder & Color(RenderingAttachment attachment)
		{
			m_colors.push_back(attachment);
			return *this;
		}

		BeginRenderingBuilder & Colors(std::span<const RenderingAttachment> attachments)
		{
			m_colors.assign(attachments.begin(), attachments.end());
			return *this;
		}

		BeginRenderingBuilder & DepthStencil(RenderingAttachment attachment) noexcept
		{
			m_depthStencil	  = attachment;
			m_hasDepthStencil = true;
			return *this;
		}

		BeginRenderingBuilder & NoDepthStencil() noexcept
		{
			m_hasDepthStencil = false;
			return *this;
		}

		/**
		 * \brief Sets the render area in framebuffer coordinates.
		 *
		 * \param x Left pixel coordinate of the render area.
		 * \param y Top pixel coordinate of the render area.
		 * \param width Render-area width in pixels.
		 * \param height Render-area height in pixels.
		 */
		BeginRenderingBuilder & RenderArea(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) noexcept
		{
			m_x		 = x;
			m_y		 = y;
			m_width	 = width;
			m_height = height;
			return *this;
		}

		/**
		 * \brief Sets the render area size while leaving its origin unchanged.
		 *
		 * \param width Render-area width in pixels.
		 * \param height Render-area height in pixels.
		 */
		BeginRenderingBuilder & Extent(std::uint32_t width, std::uint32_t height) noexcept
		{
			m_width	 = width;
			m_height = height;
			return *this;
		}

		/**
		 * \brief Sets the number of view layers rendered by this pass.
		 */
		BeginRenderingBuilder & Layers(std::uint32_t layers) noexcept
		{
			m_layers = layers;
			return *this;
		}

		/**
		 * \brief Builds a begin-rendering description that borrows this builder's attachment storage.
		 *
		 * \attention The returned color span and depthStencil pointer stay valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] BeginRenderingDesc Build() const noexcept
		{
			return BeginRenderingDesc{
				.colors		  = std::span<const RenderingAttachment>{ m_colors.data(), m_colors.size() },
				.depthStencil = m_hasDepthStencil ? &m_depthStencil : nullptr,
				.x			  = m_x,
				.y			  = m_y,
				.width		  = m_width,
				.height		  = m_height,
				.layers		  = m_layers,
			};
		}

	private:
		std::vector<RenderingAttachment> m_colors;
		RenderingAttachment m_depthStencil{};
		bool m_hasDepthStencil = false;
		std::uint32_t m_x	   = 0;
		std::uint32_t m_y	   = 0;
		std::uint32_t m_width  = 0;
		std::uint32_t m_height = 0;
		std::uint32_t m_layers = 1;
	};
} // namespace azo::rhi

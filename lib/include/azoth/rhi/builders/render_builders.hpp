// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/render.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace azo::rhi
{
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

		BeginRenderingBuilder & RenderArea(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) noexcept
		{
			m_x		 = x;
			m_y		 = y;
			m_width	 = width;
			m_height = height;
			return *this;
		}

		BeginRenderingBuilder & Extent(std::uint32_t width, std::uint32_t height) noexcept
		{
			m_width	 = width;
			m_height = height;
			return *this;
		}

		BeginRenderingBuilder & Layers(std::uint32_t layers) noexcept
		{
			m_layers = layers;
			return *this;
		}

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
}

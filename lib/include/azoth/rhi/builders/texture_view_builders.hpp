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
 * \brief Builders for texture-view and sampler descriptions.
 */

#include "azoth/rhi/resources/texture_view.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <string>
#include <string_view>

namespace azo::rhi
{
	/**
	 * \brief Builds texture-view descriptions backed by owned debug-name storage.
	 *
	 * Subresource helpers update only their part of the range, so Mips, Layers, and Aspects can be combined without replacing the whole range.
	 */
	class TextureViewBuilder final
	{
	public:
		TextureViewBuilder & Type(TextureViewType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		TextureViewBuilder & Format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		TextureViewBuilder & Range(TextureSubresourceRange range) noexcept
		{
			m_desc.range = range;
			return *this;
		}

		/**
		 * \brief Sets the mip slice range without changing the selected layers or aspects.
		 *
		 * \param baseMip First mip level in the view.
		 * \param mipCount Number of mip levels in the view.
		 */
		TextureViewBuilder & Mips(std::uint32_t baseMip, std::uint32_t mipCount) noexcept
		{
			m_desc.range.baseMip  = baseMip;
			m_desc.range.mipCount = mipCount;
			return *this;
		}

		/**
		 * \brief Sets the array-layer range without changing the selected mips or aspects.
		 *
		 * \param baseLayer First array layer in the view.
		 * \param layerCount Number of array layers in the view.
		 */
		TextureViewBuilder & Layers(std::uint32_t baseLayer, std::uint32_t layerCount) noexcept
		{
			m_desc.range.baseLayer	= baseLayer;
			m_desc.range.layerCount = layerCount;
			return *this;
		}

		TextureViewBuilder & Aspects(Flags<TextureAspect> aspects) noexcept
		{
			m_desc.range.aspects = aspects;
			return *this;
		}

		TextureViewBuilder & Swizzle(ComponentMapping swizzle) noexcept
		{
			m_desc.swizzle = swizzle;
			return *this;
		}

		TextureViewBuilder & Swizzle(ComponentSwizzle r, ComponentSwizzle g, ComponentSwizzle b, ComponentSwizzle a) noexcept
		{
			m_desc.swizzle = ComponentMapping{ .r = r, .g = g, .b = b, .a = a };
			return *this;
		}

		TextureViewBuilder & Usage(Flags<TextureUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		TextureViewBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a texture-view description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] TextureViewDesc Build() const noexcept
		{
			TextureViewDesc desc = m_desc;
			desc.debugName		 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		TextureViewDesc m_desc{};
		std::string m_debugName;
	};

	/**
	 * \brief Builds sampler descriptions backed by owned debug-name storage.
	 *
	 * \note Linear and Nearest set magnification, minification, and mipmap mode. Later Mipmap calls can override only mip selection.
	 */
	class SamplerBuilder final
	{
	public:
		SamplerBuilder & Filter(Filter mag, Filter min) noexcept
		{
			m_desc.magFilter = mag;
			m_desc.minFilter = min;
			return *this;
		}

		SamplerBuilder & Linear() noexcept
		{
			return Filter(Filter::eLinear, Filter::eLinear).Mipmap(MipmapMode::eLinear);
		}

		SamplerBuilder & Nearest() noexcept
		{
			return Filter(Filter::eNearest, Filter::eNearest).Mipmap(MipmapMode::eNearest);
		}

		SamplerBuilder & Mipmap(MipmapMode mode) noexcept
		{
			m_desc.mipmapMode = mode;
			return *this;
		}

		/**
		 * \brief Sets addressing modes for the U, V, and W texture coordinates.
		 */
		SamplerBuilder & Address(AddressMode u, AddressMode v, AddressMode w) noexcept
		{
			m_desc.addressU = u;
			m_desc.addressV = v;
			m_desc.addressW = w;
			return *this;
		}

		SamplerBuilder & AddressAll(AddressMode mode) noexcept
		{
			return Address(mode, mode, mode);
		}

		/**
		 * \brief Sets sampler LOD bounds and mip bias.
		 *
		 * \param minLod Minimum mip level selectable by the sampler.
		 * \param maxLod Maximum mip level selectable by the sampler.
		 * \param bias Bias added to the computed mip level before clamping.
		 */
		SamplerBuilder & Lod(float minLod, float maxLod, float bias = 0.0f) noexcept
		{
			m_desc.minLod	  = minLod;
			m_desc.maxLod	  = maxLod;
			m_desc.mipLodBias = bias;
			return *this;
		}

		/**
		 * \brief Sets the maximum anisotropy and optionally enables anisotropic filtering.
		 */
		SamplerBuilder & Anisotropy(float maxAnisotropy, bool enabled = true) noexcept
		{
			m_desc.anisotropyEnable = enabled;
			m_desc.maxAnisotropy	= maxAnisotropy;
			return *this;
		}

		/**
		 * \brief Sets the comparison operation and optionally enables comparison sampling.
		 */
		SamplerBuilder & Compare(CompareOp op, bool enabled = true) noexcept
		{
			m_desc.compareEnable = enabled;
			m_desc.compareOp	 = op;
			return *this;
		}

		SamplerBuilder & Border(BorderColor color) noexcept
		{
			m_desc.borderColor = color;
			return *this;
		}

		SamplerBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a sampler description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] SamplerDesc Build() const noexcept
		{
			SamplerDesc desc = m_desc;
			desc.debugName	 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		SamplerDesc m_desc{};
		std::string m_debugName;
	};
} // namespace azo::rhi

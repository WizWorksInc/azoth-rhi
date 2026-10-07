// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/resources/texture_view.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace azo::rhi
{
	class TextureViewBuilder final
	{
	public:
		TextureViewBuilder & type(TextureViewType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		TextureViewBuilder & format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		TextureViewBuilder & range(TextureSubresourceRange range) noexcept
		{
			m_desc.range = range;
			return *this;
		}

		TextureViewBuilder & mips(std::uint32_t baseMip, std::uint32_t mipCount) noexcept
		{
			m_desc.range.baseMip  = baseMip;
			m_desc.range.mipCount = mipCount;
			return *this;
		}

		TextureViewBuilder & layers(std::uint32_t baseLayer, std::uint32_t layerCount) noexcept
		{
			m_desc.range.baseLayer	= baseLayer;
			m_desc.range.layerCount = layerCount;
			return *this;
		}

		TextureViewBuilder & aspects(Flags<TextureAspect> aspects) noexcept
		{
			m_desc.range.aspects = aspects;
			return *this;
		}

		TextureViewBuilder & swizzle(ComponentMapping swizzle) noexcept
		{
			m_desc.swizzle = swizzle;
			return *this;
		}

		TextureViewBuilder & swizzle(ComponentSwizzle r, ComponentSwizzle g, ComponentSwizzle b, ComponentSwizzle a) noexcept
		{
			m_desc.swizzle = ComponentMapping{ .r = r, .g = g, .b = b, .a = a };
			return *this;
		}

		TextureViewBuilder & usage(Flags<TextureUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		TextureViewBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] TextureViewDesc build() const noexcept
		{
			TextureViewDesc desc = m_desc;
			desc.debugName		 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		TextureViewDesc m_desc{};
		std::string m_debugName;
	};

	class SamplerBuilder final
	{
	public:
		SamplerBuilder & filter(Filter mag, Filter min) noexcept
		{
			m_desc.magFilter = mag;
			m_desc.minFilter = min;
			return *this;
		}

		SamplerBuilder & linear() noexcept
		{
			return filter(Filter::eLinear, Filter::eLinear).mipmap(MipmapMode::eLinear);
		}

		SamplerBuilder & nearest() noexcept
		{
			return filter(Filter::eNearest, Filter::eNearest).mipmap(MipmapMode::eNearest);
		}

		SamplerBuilder & mipmap(MipmapMode mode) noexcept
		{
			m_desc.mipmapMode = mode;
			return *this;
		}

		SamplerBuilder & address(AddressMode u, AddressMode v, AddressMode w) noexcept
		{
			m_desc.addressU = u;
			m_desc.addressV = v;
			m_desc.addressW = w;
			return *this;
		}

		SamplerBuilder & address_all(AddressMode mode) noexcept
		{
			return address(mode, mode, mode);
		}

		SamplerBuilder & lod(float minLod, float maxLod, float bias = 0.0f) noexcept
		{
			m_desc.minLod	  = minLod;
			m_desc.maxLod	  = maxLod;
			m_desc.mipLodBias = bias;
			return *this;
		}

		SamplerBuilder & anisotropy(float maxAnisotropy, bool enabled = true) noexcept
		{
			m_desc.anisotropyEnable = enabled;
			m_desc.maxAnisotropy	= maxAnisotropy;
			return *this;
		}

		SamplerBuilder & compare(CompareOp op, bool enabled = true) noexcept
		{
			m_desc.compareEnable = enabled;
			m_desc.compareOp	 = op;
			return *this;
		}

		SamplerBuilder & border(BorderColor color) noexcept
		{
			m_desc.borderColor = color;
			return *this;
		}

		SamplerBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] SamplerDesc build() const noexcept
		{
			SamplerDesc desc = m_desc;
			desc.debugName	 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		SamplerDesc m_desc{};
		std::string m_debugName;
	};
}

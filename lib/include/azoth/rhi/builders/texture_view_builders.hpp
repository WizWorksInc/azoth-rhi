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

		TextureViewBuilder & Mips(std::uint32_t baseMip, std::uint32_t mipCount) noexcept
		{
			m_desc.range.baseMip  = baseMip;
			m_desc.range.mipCount = mipCount;
			return *this;
		}

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

		SamplerBuilder & Lod(float minLod, float maxLod, float bias = 0.0f) noexcept
		{
			m_desc.minLod	  = minLod;
			m_desc.maxLod	  = maxLod;
			m_desc.mipLodBias = bias;
			return *this;
		}

		SamplerBuilder & Anisotropy(float maxAnisotropy, bool enabled = true) noexcept
		{
			m_desc.anisotropyEnable = enabled;
			m_desc.maxAnisotropy	= maxAnisotropy;
			return *this;
		}

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
}

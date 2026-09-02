// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <cstdint>

namespace azo::rhi
{

	struct SamplerYcbcrConversionDesc;

	enum class TextureViewType : std::uint8_t
	{
		eTex1D,
		eTex1DArray,
		eTex2D,
		eTex2DArray,
		eTex3D,
		eTexCube,
		eTexCubeArray,
	};

	enum class ComponentSwizzle : std::uint8_t
	{
		eIdentity,

		eZero,
		eOne,
		eR,
		eG,
		eB,
		eA,
	};

	struct ComponentMapping final
	{
		ComponentSwizzle r = ComponentSwizzle::eIdentity;
		ComponentSwizzle g = ComponentSwizzle::eIdentity;
		ComponentSwizzle b = ComponentSwizzle::eIdentity;
		ComponentSwizzle a = ComponentSwizzle::eIdentity;

		[[nodiscard]] constexpr bool IsIdentity() const noexcept
		{
			const auto channelIsIdentity = [](const ComponentSwizzle swizzle, const ComponentSwizzle self) noexcept
			{
				return swizzle == ComponentSwizzle::eIdentity || swizzle == self;
			};

			return channelIsIdentity(r, ComponentSwizzle::eR) && channelIsIdentity(g, ComponentSwizzle::eG) && channelIsIdentity(b, ComponentSwizzle::eB) &&
				   channelIsIdentity(a, ComponentSwizzle::eA);
		}

		[[nodiscard]] friend constexpr bool operator==(ComponentMapping lhs, ComponentMapping rhs) noexcept = default;
	};

	[[nodiscard]] constexpr bool UsageForbidsSwizzle(const Flags<TextureUsage> usage) noexcept
	{
		return usage.Contains(TextureUsage::eStorage) || usage.Contains(TextureUsage::eColorAttachment) ||
			   usage.Contains(TextureUsage::eDepthStencilAttachment) || usage.Contains(TextureUsage::eTransientAttachment);
	}

	[[nodiscard]] constexpr Flags<TextureUsage> ResolveViewUsage(const Flags<TextureUsage> viewUsage, const Flags<TextureUsage> textureUsage) noexcept
	{
		return viewUsage.Empty() ? textureUsage : viewUsage;
	}

	struct TextureViewDesc final
	{
		TextureViewType type = TextureViewType::eTex2D;

		Format format = Format::eUndefined;

		TextureSubresourceRange range{};

		ComponentMapping swizzle{};

		const SamplerYcbcrConversionDesc * ycbcrConversion = nullptr;

		Flags<TextureUsage> usage;

		const char * debugName = nullptr;
	};

	enum class Filter : std::uint8_t
	{
		eNearest,
		eLinear,
	};

	enum class MipmapMode : std::uint8_t
	{
		eNearest,
		eLinear,
	};

	enum class AddressMode : std::uint8_t
	{
		eRepeat,

		eMirroredRepeat,

		eClampToEdge,

		eClampToBorder,

		eMirrorClampToEdge,
	};

	enum class BorderColor : std::uint8_t
	{
		eTransparentBlackFloat,
		eTransparentBlackInt,
		eOpaqueBlackFloat,
		eOpaqueBlackInt,
		eOpaqueWhiteFloat,
		eOpaqueWhiteInt,
	};

	enum class YcbcrModel : std::uint8_t
	{
		eRgbIdentity,

		eYcbcrIdentity,

		eYcbcr709,
		eYcbcr601,
		eYcbcr2020,
	};

	enum class YcbcrRange : std::uint8_t
	{
		eFull,

		eNarrow,
	};

	enum class ChromaLocation : std::uint8_t
	{
		eCositedEven,

		eMidpoint,
	};

	struct SamplerYcbcrConversionDesc final
	{
		Format format = Format::eUndefined;

		YcbcrModel model = YcbcrModel::eYcbcr601;
		YcbcrRange range = YcbcrRange::eNarrow;

		ComponentMapping components{};

		ChromaLocation xChromaOffset = ChromaLocation::eMidpoint;
		ChromaLocation yChromaOffset = ChromaLocation::eMidpoint;

		Filter chromaFilter = Filter::eLinear;

		[[nodiscard]] friend constexpr bool operator==(const SamplerYcbcrConversionDesc & lhs, const SamplerYcbcrConversionDesc & rhs) noexcept = default;
	};

	struct SamplerDesc final
	{
		Filter magFilter	  = Filter::eLinear;
		Filter minFilter	  = Filter::eLinear;
		MipmapMode mipmapMode = MipmapMode::eLinear;

		AddressMode addressU = AddressMode::eRepeat;
		AddressMode addressV = AddressMode::eRepeat;
		AddressMode addressW = AddressMode::eRepeat;

		float mipLodBias = 0.0f;

		bool anisotropyEnable = false;

		float maxAnisotropy = 1.0f;

		bool compareEnable = false;

		CompareOp compareOp = CompareOp::eAlways;

		float minLod = 0.0f;
		float maxLod = 1000.0f;

		BorderColor borderColor = BorderColor::eOpaqueBlackFloat;

		const SamplerYcbcrConversionDesc * ycbcrConversion = nullptr;

		const char * debugName = nullptr;
	};

}

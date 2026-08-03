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
 * \brief Texture view descriptions, component swizzles, and sampler state.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/resources/resources.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	// Declared with the sampler state it belongs beside, further down, and named by a view before that.
	struct SamplerYcbcrConversionDesc;

	/**
	 * \brief Texture shape exposed through a view.
	 *
	 * Cube views are commonly created from compatible 2D array textures whose layers represent cube faces.
	 */
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

	/**
	 * \brief Source component a view channel reads, or the constant it is forced to.
	 */
	enum class ComponentSwizzle : std::uint8_t
	{
		/**
		 * \brief Reads the source component in the same channel position.
		 */
		eIdentity,

		eZero,
		eOne,
		eR,
		eG,
		eB,
		eA,
	};

	/**
	 * \brief Per-channel remap applied to texels read through a view.
	 *
	 * Only sampled reads see this mapping. Storage image and attachment use require identity mapping on every backend.
	 */
	struct ComponentMapping final
	{
		ComponentSwizzle r = ComponentSwizzle::eIdentity;
		ComponentSwizzle g = ComponentSwizzle::eIdentity;
		ComponentSwizzle b = ComponentSwizzle::eIdentity;
		ComponentSwizzle a = ComponentSwizzle::eIdentity;

		/**
		 * \brief Returns true when every channel reads the source component in its own position.
		 *
		 * Naming the same component explicitly still counts as identity.
		 */
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

	/**
	 * \brief Returns true when a texture-view usage requires identity swizzle.
	 *
	 * Storage and attachment paths either ignore swizzles or forbid them natively, so the RHI refuses non-identity mappings for those uses.
	 */
	[[nodiscard]] constexpr bool UsageForbidsSwizzle(const Flags<TextureUsage> usage) noexcept
	{
		return usage.Contains(TextureUsage::eStorage) || usage.Contains(TextureUsage::eColorAttachment) ||
			   usage.Contains(TextureUsage::eDepthStencilAttachment) || usage.Contains(TextureUsage::eTransientAttachment);
	}

	/**
	 * \brief Resolves a view's effective usage.
	 *
	 * Empty view usage inherits the source texture usage. A non-empty view usage narrows the view to those uses.
	 */
	[[nodiscard]] constexpr Flags<TextureUsage> ResolveViewUsage(const Flags<TextureUsage> viewUsage, const Flags<TextureUsage> textureUsage) noexcept
	{
		return viewUsage.Empty() ? textureUsage : viewUsage;
	}

	/**
	 * \brief Texture subresource view creation request.
	 *
	 * The source texture is supplied to the view creation call. debugName is borrowed for the duration of creation.
	 */
	struct TextureViewDesc final
	{
		TextureViewType type = TextureViewType::eTex2D;

		/**
		 * \brief Format exposed through the view.
		 *
		 * eUndefined inherits the source texture format.
		 */
		Format format = Format::eUndefined;

		/**
		 * \brief Mips, layers, and aspects covered by the view.
		 */
		TextureSubresourceRange range{};

		/**
		 * \brief Channel remap applied to sampled reads through this view.
		 */
		ComponentMapping swizzle{};

		/**
		 * \brief Y'CbCr conversion this view is sampled through, or null for an ordinary view.
		 *
		 * Borrowed for the duration of creation. Required on a view of a multi-planar format that names no plane aspect, sampling such a format as a whole
		 * being defined only through a conversion. A view naming a plane reads that plane's raw texels and takes none.
		 */
		const SamplerYcbcrConversionDesc * ycbcrConversion = nullptr;

		/**
		 * \brief Pipeline uses this view is created for.
		 *
		 * Empty inherits the source texture usage. Narrowing to eSampled can allow a swizzled view over a texture that also supports storage or attachment use.
		 */
		Flags<TextureUsage> usage;

		const char * debugName = nullptr;
	};

	/**
	 * \brief Texture filtering mode for magnification or minification.
	 */
	enum class Filter : std::uint8_t
	{
		eNearest,
		eLinear,
	};

	/**
	 * \brief Mipmap selection filter.
	 */
	enum class MipmapMode : std::uint8_t
	{
		eNearest,
		eLinear,
	};

	/**
	 * \brief Texture coordinate addressing outside the unit range.
	 */
	enum class AddressMode : std::uint8_t
	{
		eRepeat,

		/**
		 * \brief Repeats while mirroring every other interval.
		 */
		eMirroredRepeat,

		eClampToEdge,

		/**
		 * \brief Samples the configured border color outside the texture range.
		 */
		eClampToBorder,

		/**
		 * \brief Mirrors once at the edge, then clamps.
		 */
		eMirrorClampToEdge,
	};

	/**
	 * \brief Predefined border color used by clamp-to-border addressing.
	 */
	enum class BorderColor : std::uint8_t
	{
		eTransparentBlackFloat,
		eTransparentBlackInt,
		eOpaqueBlackFloat,
		eOpaqueBlackInt,
		eOpaqueWhiteFloat,
		eOpaqueWhiteInt,
	};

	/**
	 * \brief Colour model a Y'CbCr conversion resolves the source channels through.
	 */
	enum class YcbcrModel : std::uint8_t
	{
		/**
		 * \brief The channels are already RGB, so only the range transform applies.
		 */
		eRgbIdentity,

		/**
		 * \brief Y'CbCr channels moved into RGB positions with no colour transform.
		 */
		eYcbcrIdentity,

		eYcbcr709,
		eYcbcr601,
		eYcbcr2020,
	};

	/**
	 * \brief Numeric range the encoded samples occupy.
	 */
	enum class YcbcrRange : std::uint8_t
	{
		/**
		 * \brief The samples span the full range of their bit depth.
		 */
		eFull,

		/**
		 * \brief Studio swing: luma in 16 to 235 at eight bits, chroma in 16 to 240.
		 */
		eNarrow,
	};

	/**
	 * \brief Where a chroma sample sits relative to the luma samples it was reduced from.
	 */
	enum class ChromaLocation : std::uint8_t
	{
		/**
		 * \brief Aligned with the even luma sample.
		 */
		eCositedEven,

		/**
		 * \brief Halfway between the two luma samples.
		 */
		eMidpoint,
	};

	/**
	 * \brief A Y'CbCr conversion the sampler performs and not the shader.
	 *
	 * Named by both a sampler and the view it reads, which must describe the same conversion. Needs DeviceCaps::supportsSamplerYcbcrConversion, which only
	 * Vulkan reports. Sampling the planes separately through TextureAspect::ePlane0 works everywhere.
	 *
	 * \attention A sampler carrying one is only bindable through DescriptorBinding::immutableSamplers, the conversion being compiled into the pipeline rather
	 * than chosen per draw.
	 */
	struct SamplerYcbcrConversionDesc final
	{
		/**
		 * \brief The multi-planar format this conversion reads, which must match the sampled texture's.
		 */
		Format format = Format::eUndefined;

		YcbcrModel model = YcbcrModel::eYcbcr601;
		YcbcrRange range = YcbcrRange::eNarrow;

		/**
		 * \brief Channel remap applied before the colour transform, for sources whose planes arrive in another order.
		 */
		ComponentMapping components{};

		ChromaLocation xChromaOffset = ChromaLocation::eMidpoint;
		ChromaLocation yChromaOffset = ChromaLocation::eMidpoint;

		/**
		 * \brief Filter reconstructing chroma up to luma resolution.
		 */
		Filter chromaFilter = Filter::eLinear;

		[[nodiscard]] friend constexpr bool operator==(const SamplerYcbcrConversionDesc & lhs, const SamplerYcbcrConversionDesc & rhs) noexcept = default;
	};

	/**
	 * \brief Sampler state for shader texture fetches.
	 *
	 * The sampled texture is not part of the sampler object.
	 */
	struct SamplerDesc final
	{
		Filter magFilter	  = Filter::eLinear;
		Filter minFilter	  = Filter::eLinear;
		MipmapMode mipmapMode = MipmapMode::eLinear;

		AddressMode addressU = AddressMode::eRepeat;
		AddressMode addressV = AddressMode::eRepeat;
		AddressMode addressW = AddressMode::eRepeat;

		/**
		 * \brief Bias added to the computed mip level before sampling.
		 */
		float mipLodBias = 0.0f;

		/**
		 * \brief Enables anisotropic filtering when supported.
		 */
		bool anisotropyEnable = false;

		/**
		 * \brief Requested anisotropy level when anisotropy is enabled.
		 */
		float maxAnisotropy = 1.0f;

		/**
		 * \brief Enables depth comparison sampling.
		 */
		bool compareEnable = false;

		CompareOp compareOp = CompareOp::eAlways;

		/**
		 * \brief Inclusive mip LOD clamp range.
		 */
		float minLod = 0.0f;
		float maxLod = 1000.0f;

		BorderColor borderColor = BorderColor::eOpaqueBlackFloat;

		/**
		 * \brief Y'CbCr conversion this sampler performs, or null for an ordinary sampler.
		 *
		 * Borrowed for the duration of creation. The view this sampler reads has to name an equal conversion, which is what lets the backend hand both
		 * the same native object.
		 */
		const SamplerYcbcrConversionDesc * ycbcrConversion = nullptr;

		const char * debugName = nullptr;
	};

} // namespace azo::rhi

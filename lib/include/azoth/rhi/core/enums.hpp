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
 * \brief Core backend-neutral enums and small value types.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Queue capability requested for command submission.
	 *
	 * Backends may map these to dedicated native queues or to a shared universal queue.
	 */
	enum class QueueType : std::uint8_t
	{
		eGraphics,
		eCompute,
		eCopy,
	};

	/**
	 * \brief Resource, view, attachment, and vertex-input format.
	 *
	 * Support varies by adapter, usage, and backend. Query support before relying on a format for a specific usage.
	 */
	enum class Format : std::uint16_t // NOLINT(performance-enum-size)
	{
		/**
		 * \brief No concrete format.
		 *
		 * Used for optional format fields, absent attachments, and invalid format slots.
		 */
		eUndefined,

		eR8UNorm,
		eR8UInt,
		eR8SInt,

		eRG8UNorm,

		eRGBA8UNorm,
		eRGBA8Srgb,
		eBGRA8UNorm,
		eBGRA8Srgb,

		eR16UInt,
		eR16SInt,
		eR16Float,
		eRG16Float,
		eRGBA16Float,

		/**
		 * \brief Packed 32-bit HDR RGB format.
		 *
		 * Uses half the bandwidth of RGBA16F, drops alpha, and spends the whole word on RGB.
		 */
		eR11G11B10Float,

		/**
		 * \brief Packed 32-bit unsigned-normalized RGBA format with 2-bit alpha.
		 */
		eRGB10A2UNorm,

		/**
		 * \brief Packed 32-bit HDR RGB format with one shared exponent across all three channels.
		 *
		 * Intended for sampling. Do not assume render-target or storage support.
		 */
		eRGB9E5Float,

		eR32UInt,
		eR32SInt,
		eR32Float,
		eRG32Float,
		eRGB32Float,
		eRGBA32Float,

		eD16UNorm,

		eD24UNormS8UInt,

		eD32Float,

		eD32FloatS8UInt,

		/**
		 * \brief Depth-only 24-bit unorm format with 8 padding bits.
		 *
		 * Vulkan exposes a depth-only 24-bit format. Direct3D 12 substitutes D24_UNORM_S8_UINT and leaves stencil unused. Metal reports no support.
		 */
		eX8D24UNorm,

		eBC1RGBAUNorm,

		eBC1RGBASrgb,

		/**
		 * \brief BC3 block-compressed RGBA format.
		 */
		eBC3UNorm,

		/**
		 * \brief BC3 block-compressed RGBA format with sRGB transfer.
		 */
		eBC3Srgb,

		/**
		 * \brief BC5 block-compressed two-channel unsigned-normalized format.
		 */
		eBC5UNorm,

		/**
		 * \brief BC5 block-compressed two-channel signed-normalized format.
		 */
		eBC5SNorm,

		/**
		 * \brief BC7 block-compressed RGBA format.
		 */
		eBC7UNorm,

		/**
		 * \brief BC7 block-compressed RGBA format with sRGB transfer.
		 */
		eBC7Srgb,

		/**
		 * \brief BC6H unsigned half-float HDR block-compressed format.
		 *
		 * Common for compressed HDR cubemaps and IBL prefilter maps.
		 */
		eBC6HUFloat,

		/**
		 * \brief BC6H signed half-float HDR block-compressed format.
		 */
		eBC6HSFloat,

		/**
		 * \brief Biplanar 4:2:0 Y'CbCr, eight bits per sample. NV12 by its usual name.
		 *
		 * Plane 0 carries luma at full extent, plane 1 interleaved chroma at half in both dimensions. Reached a plane at a time through TextureAspect::ePlane0
		 * and ePlane1, the shader doing the colour conversion. Needs DeviceCaps::supportsMultiPlanarFormats.
		 */
		eG8B8R8Biplanar420UNorm,

		/**
		 * \brief Triplanar 4:2:0 Y'CbCr, eight bits per sample. I420 by its usual name, chroma split across two planes, not interleaved.
		 */
		eG8B8R8Triplanar420UNorm,

		/**
		 * \brief Biplanar 4:2:0 Y'CbCr, ten bits per sample held in the high bits of sixteen. P010 by its usual name.
		 */
		eG10B10R10Biplanar420UNorm,
	};

	/**
	 * \brief Number of formats the enum declares.
	 *
	 * Use this to walk or key an array by Format so adding one updates the bound in a single place. Enumerators are contiguous from eUndefined, which
	 * is what makes a cast from every value below this a valid Format.
	 */
	inline constexpr std::size_t kFormatCount = static_cast<std::size_t>(Format::eG10B10R10Biplanar420UNorm) + 1;

	/**
	 * \brief Number of separately addressable planes a format stores its texels in. One for every ordinary format.
	 */
	[[nodiscard]] constexpr std::uint32_t PlaneCountOf(const Format format) noexcept
	{
		switch (format)
		{
		case Format::eG8B8R8Biplanar420UNorm:
		case Format::eG10B10R10Biplanar420UNorm: return 2;
		case Format::eG8B8R8Triplanar420UNorm:	 return 3;
		default:								 return 1;
		}
	}

	[[nodiscard]] constexpr bool IsMultiPlanarFormat(const Format format) noexcept
	{
		return PlaneCountOf(format) > 1;
	}

	/**
	 * \brief The single-plane format one plane of a multi-planar format is sampled as, or eUndefined when the plane does not exist.
	 *
	 * Every backend exposes a plane this way and not as the multi-planar format itself, so this is what a plane view and its descriptor carry.
	 */
	[[nodiscard]] constexpr Format PlaneFormatOf(const Format format, const std::uint32_t plane) noexcept
	{
		if (plane >= PlaneCountOf(format))
		{
			return Format::eUndefined;
		}

		switch (format)
		{
		case Format::eG8B8R8Biplanar420UNorm:	 return plane == 0 ? Format::eR8UNorm : Format::eRG8UNorm;
		case Format::eG8B8R8Triplanar420UNorm:	 return Format::eR8UNorm;
		case Format::eG10B10R10Biplanar420UNorm: return plane == 0 ? Format::eR16UInt : Format::eRG16Float;
		default:								 return format;
		}
	}

	/**
	 * \brief How far a plane's extent divides down from the texture's own, per axis. Chroma planes of a 4:2:0 format halve both.
	 */
	[[nodiscard]] constexpr std::uint32_t PlaneExtentDivisorOf(const Format format, const std::uint32_t plane) noexcept
	{
		if (plane == 0 || plane >= PlaneCountOf(format))
		{
			return 1;
		}

		switch (format)
		{
		case Format::eG8B8R8Biplanar420UNorm:
		case Format::eG8B8R8Triplanar420UNorm:
		case Format::eG10B10R10Biplanar420UNorm: return 2;
		default:								 return 1;
		}
	}

	/**
	 * \brief Returns true for formats with a depth aspect.
	 */
	[[nodiscard]] constexpr bool IsDepthFormat(Format format) noexcept
	{
		return format == Format::eD16UNorm || format == Format::eD24UNormS8UInt || format == Format::eD32Float || format == Format::eD32FloatS8UInt ||
			   format == Format::eX8D24UNorm;
	}

	enum class TextureType : std::uint8_t
	{
		eTex1D,
		eTex2D,
		eTex3D,

		/**
		 * \brief Cube texture with six faces per array element.
		 */
		eTexCube,
	};

	/**
	 * \brief Per-pixel sample count.
	 *
	 * Support depends on adapter, format, and usage.
	 */
	enum class SampleCount : std::uint8_t
	{
		e1	= 1,
		e2	= 2,
		e4	= 4,
		e8	= 8,
		e16 = 16,
	};

	enum class PrimitiveTopology : std::uint8_t
	{
		ePointList,
		eLineList,
		eLineStrip,
		eTriangleList,
		eTriangleStrip,

		/**
		 * \brief Control-point patches fed to the tessellator.
		 *
		 * VertexInputDesc::patchControlPoints says how many vertices make one patch and is required with this topology. Needs tessellation stages in the
		 * pipeline, so a backend without them refuses without rasterizing the control points as triangles.
		 */
		ePatchList,
	};

	/**
	 * \brief Polygon rasterization mode.
	 *
	 * Wireframe may require backend feature support.
	 */
	enum class FillMode : std::uint8_t
	{
		eSolid,
		eWireframe,
	};

	enum class CullMode : std::uint8_t
	{
		eNone,
		eFront,
		eBack,
	};

	/**
	 * \brief Winding order treated as front facing.
	 *
	 * Backends apply this through their viewport and clip-space conventions.
	 */
	enum class FrontFace : std::uint8_t
	{
		eCounterClockwise,
		eClockwise,
	};

	enum class CompareOp : std::uint8_t
	{
		eNever,
		eLess,
		eEqual,
		eLessOrEqual,
		eGreater,
		eNotEqual,
		eGreaterOrEqual,
		eAlways,
	};

	enum class StencilOp : std::uint8_t
	{
		eKeep,
		eZero,
		eReplace,
		eIncrementClamp,
		eDecrementClamp,
		eInvert,
		eIncrementWrap,
		eDecrementWrap,
	};

	/**
	 * \brief Blend factor used by color and alpha blending.
	 *
	 * Source factors read the fragment output, destination factors read the current attachment, and constant factors read blend constants.
	 */
	enum class BlendFactor : std::uint8_t
	{
		eZero,
		eOne,
		eSrcColor,
		eOneMinusSrcColor,
		eDstColor,
		eOneMinusDstColor,
		eSrcAlpha,
		eOneMinusSrcAlpha,
		eDstAlpha,
		eOneMinusDstAlpha,

		/**
		 * \brief Uses the configured constant blend color.
		 */
		eConstantColor,

		eOneMinusConstantColor,

		/**
		 * \brief Uses the alpha component of the configured constant blend color.
		 */
		eConstantAlpha,

		eOneMinusConstantAlpha,
	};

	enum class BlendOp : std::uint8_t
	{
		eAdd,
		eSubtract,
		eReverseSubtract,
		eMin,
		eMax,
	};

	/**
	 * \brief Attachment load behavior at the start of a rendering scope.
	 */
	enum class LoadOp : std::uint8_t
	{
		eLoad,
		eClear,

		/**
		 * \brief Previous attachment contents are not needed.
		 */
		eDontCare,
	};

	/**
	 * \brief Attachment store behavior at the end of a rendering scope.
	 */
	enum class StoreOp : std::uint8_t
	{
		eStore,

		/**
		 * \brief Rendered attachment contents do not need to be preserved.
		 */
		eDontCare,
	};

	/**
	 * \brief Swapchain presentation policy.
	 *
	 * Support is platform-specific. eFifo is the one every platform has.
	 */
	enum class PresentMode : std::uint8_t
	{
		/**
		 * \brief Vsync FIFO presentation.
		 */
		eFifo,

		/**
		 * \brief Vsync FIFO that may tear instead of stalling when a frame misses vblank.
		 */
		eFifoRelaxed,

		/**
		 * \brief Replaces queued frames before display when supported.
		 */
		eMailbox,

		/**
		 * \brief Presents without waiting for display refresh.
		 */
		eImmediate,
	};

	/**
	 * \brief Backend-neutral 2D pixel size used for swapchain and framebuffer extents.
	 */
	struct Extent2D final
	{
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;
	};

} // namespace azo::rhi

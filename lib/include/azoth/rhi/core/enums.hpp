// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>

namespace azo::rhi
{

	enum class QueueType : std::uint8_t
	{
		eGraphics,
		eCompute,
		eCopy,
	};

	enum class Format : std::uint16_t // NOLINT(performance-enum-size)
	{
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

		eR11G11B10Float,

		eRGB10A2UNorm,

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

		eX8D24UNorm,

		eBC1RGBAUNorm,

		eBC1RGBASrgb,

		eBC3UNorm,

		eBC3Srgb,

		eBC5UNorm,

		eBC5SNorm,

		eBC7UNorm,

		eBC7Srgb,

		eBC6HUFloat,

		eBC6HSFloat,

		eG8B8R8Biplanar420UNorm,

		eG8B8R8Triplanar420UNorm,

		eG10B10R10Biplanar420UNorm,
	};

	inline constexpr std::size_t kFormatCount = static_cast<std::size_t>(Format::eG10B10R10Biplanar420UNorm) + 1;

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

		eTexCube,
	};

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

		ePatchList,
	};

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

		eConstantColor,

		eOneMinusConstantColor,

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

	enum class LoadOp : std::uint8_t
	{
		eLoad,
		eClear,

		eDontCare,
	};

	enum class StoreOp : std::uint8_t
	{
		eStore,

		eDontCare,
	};

	enum class PresentMode : std::uint8_t
	{
		eFifo,

		eFifoRelaxed,

		eMailbox,

		eImmediate,
	};

	struct Extent2D final
	{
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;
	};

}

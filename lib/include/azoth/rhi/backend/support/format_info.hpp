// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/enums.hpp"

#include <cstdint>

namespace azo::rhi::detail
{
	struct FormatBlockInfo final
	{
		std::uint32_t blockWidth	= 1;
		std::uint32_t blockHeight	= 1;
		std::uint32_t bytesPerBlock = 0;
	};

	[[nodiscard]] constexpr FormatBlockInfo BlockInfoOf(Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UNorm:
		case Format::eR8UInt:
		case Format::eR8SInt:  return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 1 };

		case Format::eRG8UNorm:
		case Format::eR16UInt:
		case Format::eR16SInt:
		case Format::eR16Float:
		case Format::eD16UNorm: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 2 };

		case Format::eRGBA8UNorm:
		case Format::eRGBA8Srgb:
		case Format::eBGRA8UNorm:
		case Format::eBGRA8Srgb:
		case Format::eR32UInt:
		case Format::eR32SInt:
		case Format::eR32Float:
		case Format::eRG16Float:
		case Format::eR11G11B10Float:
		case Format::eRGB10A2UNorm:
		case Format::eRGB9E5Float:
		case Format::eD32Float:
		case Format::eX8D24UNorm:	  return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 4 };

		case Format::eRGBA16Float:
		case Format::eRG32Float:   return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 8 };

		case Format::eRGB32Float: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 12 };

		case Format::eRGBA32Float: return { .blockWidth = 1, .blockHeight = 1, .bytesPerBlock = 16 };

		case Format::eBC1RGBAUNorm:
		case Format::eBC1RGBASrgb:	return { .blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 8 };

		case Format::eBC3UNorm:
		case Format::eBC3Srgb:
		case Format::eBC5UNorm:
		case Format::eBC5SNorm:
		case Format::eBC7UNorm:
		case Format::eBC7Srgb:
		case Format::eBC6HUFloat:
		case Format::eBC6HSFloat: return { .blockWidth = 4, .blockHeight = 4, .bytesPerBlock = 16 };

		case Format::eD24UNormS8UInt:
		case Format::eD32FloatS8UInt:
		case Format::eG8B8R8Biplanar420UNorm:
		case Format::eG8B8R8Triplanar420UNorm:
		case Format::eG10B10R10Biplanar420UNorm:
		case Format::eUndefined:				 return {};
		}

		return {};
	}

	[[nodiscard]] constexpr bool IsCompressedFormat(Format format) noexcept
	{
		return BlockInfoOf(format).blockWidth > 1;
	}

	[[nodiscard]] constexpr bool IsIntegerFormat(Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UInt:
		case Format::eR8SInt:
		case Format::eR16UInt:
		case Format::eR16SInt:
		case Format::eR32UInt:
		case Format::eR32SInt: return true;
		default:			   return false;
		}
	}

	[[nodiscard]] constexpr bool HasLinearLayout(Format format) noexcept
	{
		return BlockInfoOf(format).bytesPerBlock != 0;
	}

	[[nodiscard]] constexpr std::uint32_t BlockColumns(Format format, std::uint32_t widthTexels) noexcept
	{
		const std::uint32_t blockWidth = BlockInfoOf(format).blockWidth;
		return (widthTexels + blockWidth - 1) / blockWidth;
	}

	[[nodiscard]] constexpr std::uint32_t BlockRows(Format format, std::uint32_t heightTexels) noexcept
	{
		const std::uint32_t blockHeight = BlockInfoOf(format).blockHeight;
		return (heightTexels + blockHeight - 1) / blockHeight;
	}

	[[nodiscard]] constexpr std::uint64_t TightRowPitch(Format format, std::uint32_t widthTexels) noexcept
	{
		return static_cast<std::uint64_t>(BlockColumns(format, widthTexels)) * BlockInfoOf(format).bytesPerBlock;
	}

	[[nodiscard]] constexpr std::uint64_t TightSlicePitch(Format format, std::uint32_t widthTexels, std::uint32_t heightTexels) noexcept
	{
		return TightRowPitch(format, widthTexels) * BlockRows(format, heightTexels);
	}
}

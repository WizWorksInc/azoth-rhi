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
 * \brief Texture-format block metadata used by buffer-texture copy pitch calculations.
 */

#include "azoth/rhi/core/enums.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi::detail
{
	/**
	 * \brief Linear-copy block layout for one texture format.
	 *
	 * Copy row and slice pitches are measured in blocks. Uncompressed formats use one texel per block, while block-compressed formats use multi-texel blocks.
	 * \note bytesPerBlock is zero when this helper cannot express the format as one linear copy block.
	 */
	struct FormatBlockInfo final
	{
		std::uint32_t blockWidth	= 1;
		std::uint32_t blockHeight	= 1;
		std::uint32_t bytesPerBlock = 0;
	};

	/**
	 * \brief Returns block layout metadata for linear copy pitch math.
	 *
	 * \note Undefined and combined depth-stencil formats return bytesPerBlock zero because one byte stride cannot describe their copy layout here.
	 */
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
		// A multi-planar format has no single block: each plane has its own element size and extent, so copies address one plane at a time and take their block
		// layout from PlaneFormatOf instead.
		case Format::eG8B8R8Biplanar420UNorm:
		case Format::eG8B8R8Triplanar420UNorm:
		case Format::eG10B10R10Biplanar420UNorm:
		case Format::eUndefined:				 return {};
		}

		return {};
	}

	/**
	 * \brief Returns true when copy pitches advance in multi-texel blocks instead of individual texels.
	 */
	[[nodiscard]] constexpr bool IsCompressedFormat(Format format) noexcept
	{
		return BlockInfoOf(format).blockWidth > 1;
	}

	/**
	 * \brief Returns true when this helper can express the format with one byte count per linear copy block.
	 */
	[[nodiscard]] constexpr bool HasLinearLayout(Format format) noexcept
	{
		return BlockInfoOf(format).bytesPerBlock != 0;
	}

	/**
	 * \brief Rounds a texel width up to the number of copy blocks needed, including a partial edge block.
	 */
	[[nodiscard]] constexpr std::uint32_t BlockColumns(Format format, std::uint32_t widthTexels) noexcept
	{
		const std::uint32_t blockWidth = BlockInfoOf(format).blockWidth;
		return (widthTexels + blockWidth - 1) / blockWidth;
	}

	/**
	 * \brief Rounds a texel height up to the number of copy block rows needed, including a partial edge block.
	 */
	[[nodiscard]] constexpr std::uint32_t BlockRows(Format format, std::uint32_t heightTexels) noexcept
	{
		const std::uint32_t blockHeight = BlockInfoOf(format).blockHeight;
		return (heightTexels + blockHeight - 1) / blockHeight;
	}

	/**
	 * \brief Returns the unaligned byte pitch for one tightly packed row of copy blocks.
	 *
	 * \attention Unsupported formats return zero through bytesPerBlock. Call HasLinearLayout first when zero is not a valid pitch.
	 */
	[[nodiscard]] constexpr std::uint64_t TightRowPitch(Format format, std::uint32_t widthTexels) noexcept
	{
		return static_cast<std::uint64_t>(BlockColumns(format, widthTexels)) * BlockInfoOf(format).bytesPerBlock;
	}

	/**
	 * \brief Returns the unaligned byte pitch for one tightly packed 2D slice of copy blocks.
	 *
	 * \attention Unsupported formats return zero through bytesPerBlock. Call HasLinearLayout first when zero is not a valid pitch.
	 */
	[[nodiscard]] constexpr std::uint64_t TightSlicePitch(Format format, std::uint32_t widthTexels, std::uint32_t heightTexels) noexcept
	{
		return TightRowPitch(format, widthTexels) * BlockRows(format, heightTexels);
	}
} // namespace azo::rhi::detail

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
 * \brief Texture copy, resolve, and blit region descriptions.
 */

#include "azoth/rhi/commands/sync.hpp"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Signed texture coordinate offset in texels.
	 */
	struct Offset3D final
	{
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::int32_t z = 0;
	};

	/**
	 * \brief Texture region extent in texels.
	 */
	struct Extent3D final
	{
		std::uint32_t width	 = 1;
		std::uint32_t height = 1;
		std::uint32_t depth	 = 1;
	};

	/**
	 * \brief Copy region between a buffer byte range and one texture subresource.
	 *
	 * bufferRowLength and bufferImageHeight are measured in texels. Zero means tightly packed using textureExtent.
	 */
	struct BufferTextureCopy final
	{
		std::uint64_t bufferOffset		= 0;
		std::uint32_t bufferRowLength	= 0;
		std::uint32_t bufferImageHeight = 0;
		TextureSubresource subresource{};
		Offset3D textureOffset{};
		Extent3D textureExtent{};
	};

	/**
	 * \brief Copy region between two texture subresources.
	 *
	 * extent is measured in texels and applies to both the source box and destination box.
	 */
	struct TextureCopy final
	{
		TextureSubresource srcSubresource{};
		Offset3D srcOffset{};
		TextureSubresource dstSubresource{};
		Offset3D dstOffset{};
		Extent3D extent{};
	};

	/**
	 * \brief Multisample resolve region between two texture subresources.
	 *
	 * extent is measured in texels and applies to both the source box and destination box.
	 */
	struct TextureResolve final
	{
		TextureSubresource srcSubresource{};
		Offset3D srcOffset{};
		TextureSubresource dstSubresource{};
		Offset3D dstOffset{};
		Extent3D extent{};
	};

	/**
	 * \brief Scaled-copy region between two texture subresources.
	 *
	 * Each offset array stores the box corners as minimum then maximum. Differing source and destination box sizes are resampled by Blit.
	 */
	struct TextureBlit final
	{
		TextureSubresource srcSubresource{};
		std::array<Offset3D, 2> srcOffsets{};
		TextureSubresource dstSubresource{};
		std::array<Offset3D, 2> dstOffsets{};
	};

} // namespace azo::rhi

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
 * \brief Texture subresource-range normalization helpers.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/constants.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi::detail
{
	/**
	 * \brief Concrete texture subresource range after sentinel counts have been resolved and bounds have been clamped.
	 */
	struct ResolvedSubresourceRange final
	{
		std::uint32_t baseMip	 = 0;
		std::uint32_t mipCount	 = 0;
		std::uint32_t baseLayer	 = 0;
		std::uint32_t layerCount = 0;

		/**
		 * \brief Returns true when the range names no texture subresources.
		 */
		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return mipCount == 0 || layerCount == 0;
		}
	};

	/**
	 * \brief Resolves kAllMips and kAllLayers into concrete counts and clamps the result to the texture bounds.
	 *
	 * \param range Requested range. kAllMips and kAllLayers are sentinel counts consumed here.
	 * \param mipLevels Total mip count, the upper clamp bound.
	 * \param arrayLayers Total array-layer count, the upper clamp bound.
	 * \note A baseMip or baseLayer outside the texture keeps the requested base and gives zero count on both axes.
	 */
	[[nodiscard]] constexpr ResolvedSubresourceRange ResolveSubresourceRange(
		const TextureSubresourceRange & range, std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept
	{
		ResolvedSubresourceRange out{};
		out.baseMip	  = range.baseMip;
		out.baseLayer = range.baseLayer;

		if (range.baseMip >= mipLevels || range.baseLayer >= arrayLayers)
		{
			return out;
		}

		const std::uint32_t remainingMips	= mipLevels - range.baseMip;
		const std::uint32_t remainingLayers = arrayLayers - range.baseLayer;

		out.mipCount   = range.mipCount == kAllMips ? remainingMips : range.mipCount;
		out.layerCount = range.layerCount == kAllLayers ? remainingLayers : range.layerCount;

		out.mipCount   = out.mipCount < remainingMips ? out.mipCount : remainingMips;
		out.layerCount = out.layerCount < remainingLayers ? out.layerCount : remainingLayers;
		return out;
	}

	/**
	 * \brief Returns true when a resolved range covers exactly every mip level and array layer in the texture.
	 *
	 * \param range Resolved range to compare against the full texture extent.
	 * \param mipLevels Total mip count expected in a whole-texture range.
	 * \param arrayLayers Total array-layer count expected in a whole-texture range.
	 * \attention The input range must already be resolved. Sentinel counts are not handled here.
	 */
	[[nodiscard]] constexpr bool CoversWholeTexture(const ResolvedSubresourceRange & range, std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept
	{
		return range.baseMip == 0 && range.baseLayer == 0 && range.mipCount == mipLevels && range.layerCount == arrayLayers;
	}
} // namespace azo::rhi::detail

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
#include "azoth/rhi/core/constants.hpp"

#include <bit>
#include <cstdint>

namespace azo::rhi::detail
{
	[[nodiscard]] constexpr std::uint32_t MaxMipLevels(const std::uint32_t width, const std::uint32_t height, const std::uint32_t depth) noexcept
	{
		const std::uint32_t largest = width > height ? (width > depth ? width : depth) : (height > depth ? height : depth);
		return static_cast<std::uint32_t>(std::bit_width(largest));
	}

	struct ResolvedSubresourceRange final
	{
		std::uint32_t baseMip	 = 0;
		std::uint32_t mipCount	 = 0;
		std::uint32_t baseLayer	 = 0;
		std::uint32_t layerCount = 0;

		[[nodiscard]] constexpr bool IsEmpty() const noexcept
		{
			return mipCount == 0 || layerCount == 0;
		}
	};

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

	[[nodiscard]] constexpr bool CoversWholeTexture(const ResolvedSubresourceRange & range, std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept
	{
		return range.baseMip == 0 && range.baseLayer == 0 && range.mipCount == mipLevels && range.layerCount == arrayLayers;
	}
}

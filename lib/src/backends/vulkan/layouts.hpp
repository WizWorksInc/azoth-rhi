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
#include "azoth/rhi/core/flags.hpp"

#include <vulkan/vulkan.hpp>

namespace azo::rhi::vulkan
{

	[[nodiscard]] constexpr vk::ImageLayout LayoutForUse(const Flags<ResourceUse> use, const bool unifiedLayouts) noexcept
	{
		if (use.Contains(ResourceUse::eDiscard))
		{
			return vk::ImageLayout::eUndefined;
		}
		if (use.Contains(ResourceUse::ePresent))
		{
			return vk::ImageLayout::ePresentSrcKHR;
		}
		if (unifiedLayouts)
		{
			return vk::ImageLayout::eGeneral;
		}

		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			return vk::ImageLayout::eGeneral;
		}

		if (use.Contains(ResourceUse::eDepthStencilRead) && use.Contains(ResourceUse::eSampledRead))
		{
			return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
		}

		vk::ImageLayout chosen = vk::ImageLayout::eUndefined;
		bool conflict		   = false;

		const auto want = [&](const vk::ImageLayout layout)
		{
			conflict = conflict || (chosen != vk::ImageLayout::eUndefined && chosen != layout);
			chosen	 = layout;
		};

		if (use.Contains(ResourceUse::eColorTarget))
		{
			want(vk::ImageLayout::eColorAttachmentOptimal);
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			want(vk::ImageLayout::eDepthStencilAttachmentOptimal);
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			want(vk::ImageLayout::eDepthStencilReadOnlyOptimal);
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			want(vk::ImageLayout::eShaderReadOnlyOptimal);
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			want(vk::ImageLayout::eTransferSrcOptimal);
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			want(vk::ImageLayout::eTransferDstOptimal);
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			want(vk::ImageLayout::eTransferSrcOptimal);
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			want(vk::ImageLayout::eTransferDstOptimal);
		}

		return conflict ? vk::ImageLayout::eGeneral : chosen;
	}

	static_assert(LayoutForUse(ResourceUse::eSampledRead, false) == vk::ImageLayout::eShaderReadOnlyOptimal &&
					  LayoutForUse(ResourceUse::eColorTarget, false) == vk::ImageLayout::eColorAttachmentOptimal &&
					  LayoutForUse(ResourceUse::eDepthStencilTarget, false) == vk::ImageLayout::eDepthStencilAttachmentOptimal &&
					  LayoutForUse(ResourceUse::eDepthStencilRead, false) == vk::ImageLayout::eDepthStencilReadOnlyOptimal,
		"the attachment and sampled uses each have one optimal layout, and picking the wrong one is a layout mismatch the driver cannot correct");

	static_assert(LayoutForUse(ResourceUse::eCopySrc, false) == vk::ImageLayout::eTransferSrcOptimal &&
					  LayoutForUse(ResourceUse::eCopyDst, false) == vk::ImageLayout::eTransferDstOptimal &&
					  LayoutForUse(ResourceUse::eResolveSrc, false) == vk::ImageLayout::eTransferSrcOptimal &&
					  LayoutForUse(ResourceUse::eResolveDst, false) == vk::ImageLayout::eTransferDstOptimal,
		"Vulkan has no resolve layout of its own, so an explicit resolve reads and writes through the transfer layouts a copy uses");

	static_assert(LayoutForUse(ResourceUse::eStorageRead, false) == vk::ImageLayout::eGeneral &&
					  LayoutForUse(ResourceUse::eStorageWrite, false) == vk::ImageLayout::eGeneral,
		"a storage image binding is legal in GENERAL alone, so a storage use can never narrow to an optimal layout");

	static_assert(
		LayoutForUse(Flags<ResourceUse>(ResourceUse::eDepthStencilRead) | ResourceUse::eSampledRead, false) == vk::ImageLayout::eDepthStencilReadOnlyOptimal,
		"testing depth while sampling it is the one read pair Vulkan has a layout for, and falling to GENERAL there gives up depth compression");

	static_assert(LayoutForUse(Flags<ResourceUse>(ResourceUse::eSampledRead) | ResourceUse::eColorTarget, false) == vk::ImageLayout::eGeneral,
		"uses wanting different layouts have to meet at GENERAL, since a barrier carries one layout for the whole subresource range");

	static_assert(LayoutForUse(Flags<ResourceUse>(ResourceUse::eCopySrc) | ResourceUse::eResolveSrc, false) == vk::ImageLayout::eTransferSrcOptimal,
		"two uses wanting the same layout agree rather than conflict, which is what folding by layout buys over counting uses");

	static_assert(LayoutForUse(ResourceUse::eSampledRead, true) == vk::ImageLayout::eGeneral &&
					  LayoutForUse(ResourceUse::eColorTarget, true) == vk::ImageLayout::eGeneral &&
					  LayoutForUse(ResourceUse::eCopyDst, true) == vk::ImageLayout::eGeneral &&
					  LayoutForUse(Flags<ResourceUse>(ResourceUse::eDepthStencilRead) | ResourceUse::eSampledRead, true) == vk::ImageLayout::eGeneral,
		"under VK_KHR_unified_image_layouts every layout describing a GPU read collapses, and no ICD here can run this branch at all");

	static_assert(LayoutForUse(ResourceUse::eDiscard, true) == vk::ImageLayout::eUndefined &&
					  LayoutForUse(ResourceUse::ePresent, true) == vk::ImageLayout::ePresentSrcKHR,
		"discard and present outlive the collapse, one initialising the image and the other naming a handover the compositor owns");

	static_assert(LayoutForUse(Flags<ResourceUse>(ResourceUse::eDiscard) | ResourceUse::eCopyDst, false) == vk::ImageLayout::eUndefined,
		"discard wins over whatever it is paired with, since a before state that preserves nothing is what lets the driver skip the transition");

	static_assert(LayoutForUse(Flags<ResourceUse>(ResourceUse::eDepthStencilTarget) | ResourceUse::eDepthStencilRead, false) == vk::ImageLayout::eGeneral,
		"a depth attachment both tested and written wants two different layouts at once, and only GENERAL serves both halves of that pair");

	static_assert(LayoutForUse(Flags<ResourceUse>(), false) == vk::ImageLayout::eUndefined,
		"a use naming nothing reaches no layout, which is legal as a barrier before-state and is the caller's error anywhere else");

}

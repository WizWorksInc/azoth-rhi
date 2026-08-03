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
 * \brief Common public sentinel constants.
 */

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>

namespace azo::rhi
{

	/**
	 * \brief Invalid uint32 index sentinel used by empty handles and descriptor-index fields.
	 */
	inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

	/**
	 * \brief Queue-family sentinel used when a barrier performs no ownership transfer.
	 */
	inline constexpr std::uint32_t kIgnoreQueueFamily = std::numeric_limits<std::uint32_t>::max();

	/**
	 * \brief Queue-family sentinel naming whatever holds a resource across an external sharing boundary.
	 *
	 * Use it as the destination of a barrier before exporting a resource and as the source of one after importing it.
	 *
	 * \attention Not optional where the backend has queue family ownership to transfer. On Vulkan a resource handed across without it has undefined contents.
	 * Direct3D 12 treats this as inert. Record the barrier either way.
	 */
	inline constexpr std::uint32_t kExternalQueueFamily = std::numeric_limits<std::uint32_t>::max() - 1;

	/**
	 * \brief TextureSubresourceRange mip-count sentinel meaning every remaining mip from baseMip.
	 *
	 * This is for barriers whose emitter does not know the concrete texture mip count. Backends lower it to the native whole-range form or resolve it
	 * to concrete subresources when required.
	 *
	 * \attention Valid for barriers only. Do not use this sentinel in texture views.
	 */
	inline constexpr std::uint32_t kAllMips = std::numeric_limits<std::uint32_t>::max();

	/**
	 * \brief TextureSubresourceRange layer-count sentinel meaning every remaining layer from baseLayer.
	 *
	 * This is the layer counterpart to kAllMips and carries the same barrier-only restriction.
	 *
	 * \attention Valid for barriers only. Do not use this sentinel in texture views.
	 */
	inline constexpr std::uint32_t kAllLayers = std::numeric_limits<std::uint32_t>::max();

} // namespace azo::rhi

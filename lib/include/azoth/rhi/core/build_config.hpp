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
 * \brief Process-wide clip-space convention selection.
 */

#include "azoth/rhi/core/api.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief NDC Y direction exposed by the RHI to vertex-stage projection code.
	 *
	 * Native APIs disagree on clip-space Y. The RHI presents one process-wide convention and backends flip the viewport when needed.
	 *
	 * \note Viewport flipping does not rewrite winding. Front-face orientation stays as authored.
	 */
	enum class ClipSpaceConvention : std::uint8_t
	{
		/**
		 * \brief NDC Y increases downward.
		 *
		 * This is native to Vulkan. Direct3D 12 and Metal backends flip the viewport to present it.
		 */
		eYDown,

		/**
		 * \brief NDC Y increases upward.
		 *
		 * This is native to Direct3D 12 and Metal. Vulkan backends flip the viewport to present it.
		 */
		eYUp,
	};

	/**
	 * \brief Settles the process-wide clip-space convention.
	 *
	 * Call this at startup before creating the first device. Only the first call takes effect. Later calls return false even if they request the already-set
	 * convention.
	 *
	 * \attention Backends read this during viewport setup. Changing the convention after rendering starts would make frame output inconsistent.
	 */
	[[nodiscard]] AZO_RHI_API bool SetClipSpace(ClipSpaceConvention convention) noexcept;

	/**
	 * \brief Returns the process-wide clip-space convention used by application projection code.
	 *
	 * The default is eYUp until SetClipSpace settles a different convention.
	 */
	[[nodiscard]] AZO_RHI_API ClipSpaceConvention GetClipSpace() noexcept;

} // namespace azo::rhi

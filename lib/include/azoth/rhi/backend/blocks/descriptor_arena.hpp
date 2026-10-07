// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/blocks/common.hpp"

namespace azo::rhi
{

	/**
	 * \brief Callbacks for allocating descriptor sets and resetting the arena.
	 */
	struct DescriptorArenaApi final
	{
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(DescriptorArenaApi),
			.version  = 1,
		};

		/**
		 * \brief Allocates a descriptor set from the arena.
		 * \param impl Backend descriptor arena instance.
		 * \param desc Descriptor set layout and allocation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Descriptor set handle, or an invalid handle on failure.
		 */
		DescriptorSetHandle (*allocate)(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Resets the arena for reuse.
		 * \param impl Backend descriptor arena instance.
		 * \param safeAfter Retire point for work using the arena's descriptor sets.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*reset)(void * impl, RetirePoint safeAfter, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

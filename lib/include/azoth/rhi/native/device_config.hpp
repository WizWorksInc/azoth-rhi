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
 * \brief Per-backend device configuration blocks carried by DeviceDesc.
 */

#include "azoth/rhi/device/api_tags.hpp"

#include <cstddef>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief One backend's device configuration block, keyed by the backend it configures.
	 *
	 * config points at that backend's block type, whose first member is an InterfaceHeader carrying its size and version. Like the other spans and string
	 * pointers in DeviceDesc, the block is borrowed for the duration of device creation.
	 */
	struct DeviceConfigEntry final
	{
		GraphicsApiId api{};
		const void * config = nullptr;
	};

	namespace native
	{

		/**
		 * \brief Names the device configuration block a backend accepts.
		 *
		 * A specialization declares Config as that backend's block type. The primary template is undefined, so naming a backend that has no configuration block
		 * is a compile error and not a silently empty configuration. Keying on the tag, not a closed enum, is what lets an out-of-tree backend declare its own
		 * block and be configured on the same terms as a bundled one.
		 */
		template <GraphicsApiTag Api>
		struct DeviceConfigFor;

		/**
		 * \brief What a lookup for one backend's configuration block found.
		 *
		 * block is null unless a usable entry was present. malformed separates the two ways that happens, since a device coming up on its defaults and a device
		 * handed a block it cannot read are different answers and collapsing them is how a caller's mistake becomes silence.
		 */
		template <class Config>
		struct DeviceConfigLookup final
		{
			const Config * block = nullptr;
			bool malformed		 = false;
		};

		/**
		 * \brief Finds the configuration block a backend was given.
		 *
		 * An absent entry is legal and leaves the backend on its defaults, which is what DeviceDesc::backendConfigs promises. A block declaring fewer bytes than
		 * the one this backend reads, or a version other than the one it was built against, is malformed rather than absent: it is reported, never read past,
		 * and never quietly defaulted.
		 *
		 * \note A longer block is a caller built against a newer header, whose appended fields this reader ignores. That is the rule InterfaceHeader already
		 * carries for dispatch tables and surface payloads, and it is copied here rather than decided again.
		 */
		template <GraphicsApiTag Api>
		[[nodiscard]] constexpr DeviceConfigLookup<typename DeviceConfigFor<Api>::Config> FindDeviceConfig(std::span<const DeviceConfigEntry> entries) noexcept
		{
			using Config = typename DeviceConfigFor<Api>::Config;

			for (const DeviceConfigEntry & entry : entries)
			{
				if (entry.api != Api::id || entry.config == nullptr)
				{
					continue;
				}

				const auto * block = static_cast<const Config *>(entry.config);
				if (block->header.byteSize < sizeof(Config) || block->header.version != Config{}.header.version)
				{
					return DeviceConfigLookup<Config>{ .block = nullptr, .malformed = true };
				}

				return DeviceConfigLookup<Config>{ .block = block, .malformed = false };
			}

			return DeviceConfigLookup<Config>{};
		}

	} // namespace native

} // namespace azo::rhi

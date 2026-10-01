// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/device/api_tags.hpp"

#include <cstddef>
#include <span>

namespace azo::rhi
{

	struct DeviceConfigEntry final
	{
		GraphicsApiId api{};
		const void * config = nullptr;
	};

	using InstanceConfigEntry = DeviceConfigEntry;

	namespace native
	{

		template <GraphicsApiTag Api>
		struct DeviceConfigFor;

		template <GraphicsApiTag Api>
		struct InstanceConfigFor;

		template <class Config>
		struct DeviceConfigLookup final
		{
			const Config * block = nullptr;
			bool malformed		 = false;
		};

		template <class Config>
		[[nodiscard]] constexpr DeviceConfigLookup<Config> FindConfigBlock(std::span<const DeviceConfigEntry> entries, GraphicsApiId api) noexcept
		{
			for (const DeviceConfigEntry & entry : entries)
			{
				if (entry.api != api || entry.config == nullptr)
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

		template <GraphicsApiTag Api>
		[[nodiscard]] constexpr auto FindDeviceConfig(std::span<const DeviceConfigEntry> entries) noexcept
		{
			return FindConfigBlock<typename DeviceConfigFor<Api>::Config>(entries, Api::id);
		}

		template <GraphicsApiTag Api>
		[[nodiscard]] constexpr auto FindInstanceConfig(std::span<const InstanceConfigEntry> entries) noexcept
		{
			return FindConfigBlock<typename InstanceConfigFor<Api>::Config>(entries, Api::id);
		}

	}

}

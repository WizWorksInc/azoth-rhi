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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/module/entry_point.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace
{

	struct ConsumerModuleApi final : azo::rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "consumer.rhi.module";
		static constexpr std::string_view displayName	= "Out Of Tree Module";
		static constexpr azo::rhi::GraphicsApiId id		= azo::rhi::MakeGraphicsApiId(canonicalName);
	};

	azo::rhi::InstanceApi g_table{};

	[[nodiscard]] const void * QueryInterface(void *, const azo::rhi::InterfaceId, const std::uint32_t) noexcept
	{
		return &g_table;
	}

	constexpr azo::rhi::BackendObject kPublished{ .queryInterface = &QueryInterface };
	azo::rhi::BackendObject g_object = kPublished;

	azo::rhi::Result<void> RegisterConsumerModule(azo::rhi::GraphicsApiRegistry & registry)
	{
		azo::rhi::BackendCreateInfo info{};
		info.info.canonicalName = ConsumerModuleApi::canonicalName;
		info.info.displayName	= ConsumerModuleApi::displayName;
		info.createInstance		= [](const void *, azo::rhi::Error *) noexcept
		{
			return static_cast<void *>(nullptr);
		};

		return registry.Register<ConsumerModuleApi>(info);
	}

	[[nodiscard]] std::size_t LiveObjects() noexcept
	{
		return 0;
	}

	constinit std::array<azo::rhi::BackendEntry, 1> g_entries{};

	[[nodiscard]] bool Describe(azo::rhi::ModuleDescription & description) noexcept
	{
		g_entries[0] = azo::rhi::BackendEntry{
			.id			   = ConsumerModuleApi::id,
			.canonicalName = ConsumerModuleApi::canonicalName,
			.displayName   = ConsumerModuleApi::displayName,
			.Register	   = &RegisterConsumerModule,
		};

		description.entries			= g_entries.data();
		description.entryCount		= g_entries.size();
		description.liveObjectCount = &LiveObjects;
		return true;
	}

} // namespace

AZO_RHI_DEFINE_MODULE(Describe)

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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace
{

	struct LoadableApi final : azo::rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "studio.rhi.loadable";
		static constexpr std::string_view displayName	= "Studio Loadable";
		static constexpr azo::rhi::GraphicsApiId id		= azo::rhi::MakeGraphicsApiId(canonicalName);
	};

	static_assert(azo::rhi::GraphicsApiTag<LoadableApi>);

	std::atomic<std::size_t> g_liveInstances{ 0 };

	struct Instance final
	{
		const azo::rhi::BackendObject * object = nullptr;
	};

	azo::rhi::InstanceApi g_table{};
	Instance g_instance{};

	[[nodiscard]] const void * QueryInterface(void *, const azo::rhi::InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		using Traits = azo::rhi::InterfaceTraits<azo::rhi::InstanceApi>;
		if (id != Traits::kId || minVersion > Traits::kVersion)
		{
			return nullptr;
		}

		return &g_table;
	}

	constexpr azo::rhi::BackendObject kPublished{ .queryInterface = &QueryInterface };

	void DestroyInstance(void *) noexcept
	{
		g_liveInstances.fetch_sub(1, std::memory_order_relaxed);
	}

	[[nodiscard]] azo::rhi::BackendCreateInfo CreateInfo() noexcept
	{
		azo::rhi::BackendCreateInfo info{};
		info.info.canonicalName = LoadableApi::canonicalName;
		info.info.displayName	= LoadableApi::displayName;
		info.createInstance		= [](const void *, azo::rhi::Error *) noexcept
		{
			g_liveInstances.fetch_add(1, std::memory_order_relaxed);
			g_instance.object = &kPublished;
			return static_cast<void *>(&g_instance);
		};

		return info;
	}

	azo::rhi::Result<void> RegisterLoadable(azo::rhi::GraphicsApiRegistry & registry)
	{
		g_table					 = azo::rhi::InstanceApi{};
		g_table.getGraphicsApiId = [](void *) noexcept
		{
			return LoadableApi::id;
		};
		g_table.enumerateAdapters = [](void *, std::span<azo::rhi::AdapterInfo>, std::uint32_t *, azo::rhi::Error *) noexcept
		{
			return false;
		};
		g_table.createDevice = [](void *, const azo::rhi::DeviceDesc &, azo::rhi::Error *) noexcept
		{
			return static_cast<void *>(nullptr);
		};
		g_table.destroyInstance = &DestroyInstance;

		return registry.Register<LoadableApi>(CreateInfo());
	}

	[[nodiscard]] std::size_t LiveObjects() noexcept
	{
		return g_liveInstances.load(std::memory_order_relaxed);
	}

	constinit std::array<azo::rhi::BackendEntry, 1> g_entries{};

	[[nodiscard]] bool Describe(azo::rhi::ModuleDescription & description) noexcept
	{
		g_entries[0] = azo::rhi::BackendEntry{
			.id			   = LoadableApi::id,
			.canonicalName = LoadableApi::canonicalName,
			.displayName   = LoadableApi::displayName,
			.Register	   = &RegisterLoadable,
			.rank		   = azo::rhi::BackendRank::eHardware,
		};

		description.entries			= g_entries.data();
		description.entryCount		= g_entries.size();
		description.liveObjectCount = &LiveObjects;
		return true;
	}

} // namespace

AZO_RHI_DEFINE_MODULE(Describe)

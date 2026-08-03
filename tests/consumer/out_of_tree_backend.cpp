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
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

namespace
{

	struct ConsumerApi final : azo::rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "consumer.rhi.outoftree";
		static constexpr std::string_view displayName	= "Out Of Tree Consumer";
		static constexpr azo::rhi::GraphicsApiId id		= azo::rhi::MakeGraphicsApiId(canonicalName);
	};

	static_assert(azo::rhi::GraphicsApiTag<ConsumerApi>);

	struct Instance final
	{
		const azo::rhi::BackendObject * object = nullptr;
	};

	azo::rhi::InstanceApi g_table{};
	Instance g_instance{};
	bool g_destroyed = false;

	[[nodiscard]] const void * QueryInterface(void *, const azo::rhi::InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		using Traits = azo::rhi::InterfaceTraits<azo::rhi::InstanceApi>;
		return (id == Traits::kId && minVersion <= Traits::kVersion) ? &g_table : nullptr;
	}

	constexpr azo::rhi::BackendObject kPublished{ .queryInterface = &QueryInterface };

	azo::rhi::Result<void> RegisterConsumer(azo::rhi::GraphicsApiRegistry & registry)
	{
		g_table					 = azo::rhi::InstanceApi{};
		g_table.getGraphicsApiId = [](void *) noexcept
		{
			return ConsumerApi::id;
		};
		g_table.enumerateAdapters = [](void *, std::span<azo::rhi::AdapterInfo>, std::uint32_t *, azo::rhi::Error *) noexcept
		{
			return false;
		};
		g_table.createDevice = [](void *, const azo::rhi::DeviceDesc &, azo::rhi::Error *) noexcept
		{
			return static_cast<void *>(nullptr);
		};
		g_table.destroyInstance = [](void *) noexcept
		{
			g_destroyed = true;
		};

		azo::rhi::BackendCreateInfo info{};
		info.info.canonicalName = ConsumerApi::canonicalName;
		info.info.displayName	= ConsumerApi::displayName;
		info.createInstance		= [](const void *, azo::rhi::Error *) noexcept
		{
			g_instance.object = &kPublished;
			return static_cast<void *>(&g_instance);
		};

		return registry.Register<ConsumerApi>(info);
	}

	struct Tag final
	{
	};

	struct Payload final
	{
		int value = 0;
	};

	[[nodiscard]] bool ExerciseTheToolkit()
	{
		azo::rhi::SpinLock lock;
		lock.lock();
		lock.unlock();

		azo::rhi::detail::HostVector<int> numbers;
		numbers.push_back(7);

		azo::rhi::SlotMap<Tag, Payload> slots;
		slots.Rebind(1);
		const auto handle = slots.Store(Payload{ .value = 42 });

		azo::rhi::detail::TypedObjectPool<Payload> pool{ 8 };
		Payload * pooled = pool.New(Payload{ .value = 9 });

		return numbers.size() == 1 && handle.IsValid() && pooled != nullptr && pooled->value == 9;
	}

} // namespace

int main()
{
	if (!ExerciseTheToolkit())
	{
		std::puts("the backend SDK toolkit did not behave");
		return 1;
	}

	azo::rhi::BackendSelection backends{ azo::rhi::BackendPreference{ .requested = "outoftree", .includeAvailable = false } };

	const azo::rhi::Result<void> added = backends.Add(azo::rhi::BackendEntry{
		.id			   = ConsumerApi::id,
		.canonicalName = ConsumerApi::canonicalName,
		.displayName   = ConsumerApi::displayName,
		.Register	   = &RegisterConsumer,
	});

	if (!added)
	{
		std::puts("the out of tree backend was refused registration");
		return 1;
	}

	{
		const azo::rhi::Result<azo::rhi::UniqueInstance> instance = backends.CreateInstance();
		if (!instance)
		{
			std::puts("the out of tree backend could not bring up an instance");
			return 1;
		}

		if (instance.Value().Get().GetGraphicsApiId() != ConsumerApi::id)
		{
			std::puts("the selection reached a backend other than the one registered");
			return 1;
		}
	}

	if (!g_destroyed)
	{
		std::puts("the instance was not taken back down");
		return 1;
	}

	std::puts("out of tree backend: registered, created, and released through installed headers only");
	return 0;
}

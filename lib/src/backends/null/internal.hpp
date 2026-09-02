// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/profiling.hpp"

#include "backends/registration.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace azo::rhi::null
{
	struct NullDevice;

	inline constexpr std::uint32_t kNullSwapchainImages = 3;

	struct NullObject final
	{
		const BackendObject * object = nullptr;
		NullDevice * owner			 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		std::array<TextureHandle, kNullSwapchainImages> backBuffers{};
		std::array<TextureViewHandle, kNullSwapchainImages> backBufferViews{};
		std::array<BinarySemaphoreHandle, kNullSwapchainImages> presentSemaphores{};

		detail::HostVector<NullObject *> lists;
		std::size_t handedOut = 0;
	};

	struct NullInstance final
	{
		const BackendObject * object = nullptr;
	};

	struct NullSlotTag final
	{
	};

	struct NullHandleRecord final
	{
		SlotLifetime lifetime = SlotLifetime::eOwned;

		TextureDesc desc{};
		BufferDesc bufferDesc{};
	};

	struct NullDevice final
	{
		const BackendObject * object = nullptr;
		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		NullInstance * instanceWrapper = nullptr;

		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		detail::TypedObjectPool<NullObject> objects{ 64, 0, "rhi.null.objects" };

		detail::ResourceTables<NullSlotTag, NullHandleRecord> handles;

		~NullDevice()
		{
			objects.Reset();
		}

		NullDevice()							   = default;
		NullDevice(const NullDevice &)			   = delete;
		NullDevice & operator=(const NullDevice &) = delete;
		NullDevice(NullDevice &&)				   = delete;
		NullDevice & operator=(NullDevice &&)	   = delete;
	};

}

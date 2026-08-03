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

	// Every null swapchain reports this many images so the handles it lends out fit in fixed storage.
	inline constexpr std::uint32_t kNullSwapchainImages = 3;

	struct NullObject final
	{
		const BackendObject * object = nullptr;
		NullDevice * owner			 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		/*
		 * Swapchain only. Registered once when the swapchain is created so a back buffer resolves well enough to build a view from and so one image index
		 * always names the same handle and not a fresh one per call. They are borrowed: destroying one through the device is refused and they go when the
		 * device's registry does, since a swapchain lives until then.
		 */
		std::array<TextureHandle, kNullSwapchainImages> backBuffers{};
		std::array<TextureViewHandle, kNullSwapchainImages> backBufferViews{};
		std::array<BinarySemaphoreHandle, kNullSwapchainImages> presentSemaphores{};

		/*
		 * Command pool only. Every list this pool has built and how many of them are currently out, since a reset takes them all back and the next frame is handed
		 * the same ones. Allocating a fresh object per call instead would grow the device's object pool by one a frame for as long as the device lives. The cursor
		 * rewinds at Reset, so within a frame each allocate still answers with a list nothing else is recording.
		 */
		detail::HostVector<NullObject *> lists;
		std::size_t handedOut = 0;
	};

	struct NullInstance final
	{
		const BackendObject * object = nullptr;
	};

	// Empty tag for the null device's handle registry, which tracks only generation and liveness for stale-handle validation (the null backend has no native
	// payload to resolve).
	struct NullSlotTag final
	{
	};

	/*
	 * What a registry slot holds.
	 *
	 * The lifetime names who owns the object behind a handle, which for this backend is the device or a swapchain. Reaching for the shared enum rather
	 * than a bool keeps one vocabulary across the backends even though eAdopted is unreachable here, this backend having no native object to adopt.
	 */
	struct NullHandleRecord final
	{
		SlotLifetime lifetime = SlotLifetime::eOwned;

		// Filled by createTexture and createBuffer, read by the two introspection queries, left at their defaults by every other kind. One record type
		// serves all of them here, so these are the fields that do not apply to all, which is cheaper than two more tables for two queries.
		TextureDesc desc{};
		BufferDesc bufferDesc{};
	};

	struct NullDevice final
	{
		const BackendObject * object = nullptr;
		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		// The instance this device was made from, borrowed or null in the static CreateDevice form that makes no instance. Used only so teardown can retire the
		// instance alongside its last device.
		NullInstance * instanceWrapper = nullptr;

		// Tags this device's handles so another device rejects them. Returned to the pool at teardown.
		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		detail::TypedObjectPool<NullObject> objects{ 64, 0, "rhi.null.objects" };

		// One table per kind, matching the guard the RHI takes for that kind. Nothing guards it here: writers are serialized by that guard and readers need
		// nothing because the storage never moves.
		detail::ResourceTables<NullSlotTag, NullHandleRecord> handles;

		~NullDevice()
		{
			objects.Reset();
		}

		// Held through a HostUniquePtr in the backend's device list and pointed at by every facade built off it so it is never copied or moved. The default
		// constructor is restored because declaring the four below is what would otherwise take it away and HostNew needs it.
		NullDevice()							   = default;
		NullDevice(const NullDevice &)			   = delete;
		NullDevice & operator=(const NullDevice &) = delete;
		NullDevice(NullDevice &&)				   = delete;
		NullDevice & operator=(NullDevice &&)	   = delete;
	};

} // namespace azo::rhi::null

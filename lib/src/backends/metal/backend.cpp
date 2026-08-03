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

#ifdef __APPLE__

	#include "azoth/rhi/backend/dispatch.hpp"
	#include "azoth/rhi/backend/support/object_pool.hpp"
	#include "azoth/rhi/backend/support/slot_map.hpp"
	#include "azoth/rhi/backend/table_validation.hpp"
	#include "azoth/rhi/core/c_string.hpp"
	#include "azoth/rhi/core/profiling.hpp"
	#include "azoth/rhi/native/metal_native.hpp"

	#include "backends/metal/internal.hpp"
	#include "backends/registration.hpp"

	#include <Foundation/Foundation.hpp>
	#include <Metal/Metal.hpp>
	#include <QuartzCore/QuartzCore.hpp>

	#include <algorithm>
	#include <atomic>
	#include <chrono>
	#include <cstdint>
	#include <limits>
	#include <memory>
	#include <new>
	#include <span>
	#include <string>
	#include <string_view>
	#include <thread>
	#include <tuple>
	#include <unordered_map>
	#include <utility>
	#include <variant>
	#include <vector>

namespace azo::rhi
{

	Result<void> RegisterMetalBackend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName				   = MetalApi::canonicalName;
		info.info.displayName				   = MetalApi::displayName;
		info.info.apiVersionMajor			   = 3;
		info.info.supportsSurfaces			   = true;
		info.info.supportsDebugMarkers		   = true;
		info.info.supportsExternalNativeAccess = true;
		info.createInstance					   = &metal::MetalCreateInstance;
		return registry.Register<MetalApi>(info);
	}

	template <>
	Result<UniqueDevice> CreateDevice<MetalApi>(const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		// No instance in the static form so the device has none to retire when it goes.
		const char * refusedReason	= nullptr;
		metal::MetalDevice * device = metal::MakeOwnedDevice(nullptr, desc, refusedReason);
		if (device == nullptr)
		{
			return Error{
				.code	 = refusedReason != nullptr ? ErrorCode::eUnsupportedFeature : ErrorCode::eNativeApiError,
				.message = refusedReason != nullptr ? refusedReason : "no Metal device available",
			};
		}

		Error error{};
		void * deviceImpl		 = device;
		BackendBlockSet * blocks = detail::ResolveDeviceBlocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueDevice(deviceImpl, blocks);
	}

	namespace native
	{
		MetalCommandListView NativeAccess<MetalApi>::MakeCommandListView(void * commandListImpl) noexcept
		{
			return MetalCommandListView{ .commandBuffer = CmdBufferOf(static_cast<metal::MetalObject *>(commandListImpl)) };
		}
	} // namespace native

	Result<MetalNativeDevice> GetMetalNativeDevice(Device device)
	{
		if (device.GetGraphicsApiId() != MetalApi::id)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalNativeDevice called on a non-Metal device",
			};
		}

		auto * impl = static_cast<metal::MetalDevice *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(device), metal::CoreDeviceBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalNativeDevice reached something other than a Metal device behind the facade",
			};
		}

		return MetalNativeDevice{
			.device = impl->device.get(),
			.queue	= impl->CommandQueueFor(QueueType::eGraphics),
		};
	}

	MTL::CommandBuffer * GetMetalCommandBuffer(CommandList commandList)
	{
		auto * object = static_cast<metal::MetalObject *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandList), metal::RenderCommandBlock()));
		return object != nullptr && object->list != nullptr ? object->list->commandBuffer.get() : nullptr;
	}

	MTL::RenderCommandEncoder * GetMetalRenderCommandEncoder(CommandList commandList)
	{
		auto * object = static_cast<metal::MetalObject *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandList), metal::RenderCommandBlock()));
		return object != nullptr && object->list != nullptr ? object->list->renderEncoder.get() : nullptr;
	}

} // namespace azo::rhi

#endif // __APPLE__

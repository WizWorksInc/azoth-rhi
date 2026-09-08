// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		Error refusal{};
		metal::MetalDevice * device = metal::MakeOwnedDevice(nullptr, desc, refusal);
		if (device == nullptr)
		{
			return refusal.code != ErrorCode::eOk ? refusal : Error{ .code = ErrorCode::eNativeApiError, .message = "no Metal device available" };
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
			auto * object = static_cast<metal::MetalObject *>(detail::NativeImplOf(commandListImpl, metal::RenderCommandBlock()));
			return MetalCommandListView{ .commandBuffer = object != nullptr ? CmdBufferOf(object) : nullptr };
		}
	}

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

	Result<native::MetalQueueView> GetMetalQueueView(Queue queue)
	{
		const auto * object = static_cast<metal::MetalObject *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(queue), metal::QueueBlock()));
		if (object == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalQueueView called on a queue that is not a Metal 3 one",
			};
		}

		return native::MetalQueueView{ .queue = object->owner->CommandQueueFor(object->queueType) };
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

}

#endif

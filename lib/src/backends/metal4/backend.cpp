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

	#include "backends/metal4/internal.hpp"
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

	Result<void> RegisterMetal4Backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName				   = Metal4Api::canonicalName;
		info.info.displayName				   = Metal4Api::displayName;
		info.info.apiVersionMajor			   = 4;
		info.info.supportsSurfaces			   = true;
		info.info.supportsDebugMarkers		   = true;
		info.info.supportsExternalNativeAccess = true;
		info.createInstance					   = &metal4::Metal4CreateInstance;
		return registry.Register<Metal4Api>(info);
	}

	template <>
	Result<UniqueDevice> CreateDevice<Metal4Api>(const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		// No instance in the static form so the device has none to retire when it goes.
		const char * refusedReason	  = nullptr;
		metal4::Metal4Device * device = metal4::MakeOwnedDevice(nullptr, desc, refusedReason);
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
		Metal4CommandListView NativeAccess<Metal4Api>::MakeCommandListView(void * commandListImpl) noexcept
		{
			metal4::CmdList * list = metal4::ListOf(static_cast<metal4::Metal4Object *>(commandListImpl));
			return Metal4CommandListView{ .commandBuffer = list != nullptr ? list->commandBuffer.get() : nullptr };
		}
	} // namespace native

	Result<Metal4NativeDevice> GetMetal4NativeDevice(Device device)
	{
		if (device.GetGraphicsApiId() != Metal4Api::id)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetal4NativeDevice called on a device that is not backed by Metal 4",
			};
		}

		auto * impl = static_cast<metal4::Metal4Device *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(device), metal4::CoreDeviceBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetal4NativeDevice reached something other than a Metal 4 device behind the facade",
			};
		}

		return Metal4NativeDevice{
			.device = impl->device.get(),
			.queue	= impl->CommandQueueFor(QueueType::eGraphics),
		};
	}

	/*
	 * The four Metal 4 command accessors.
	 *
	 * Each resolves against this backend's command block, so a list from the other one does not match and the answer is null and not a pointer to the wrong
	 * kind of object. That is what makes it safe to ask both without first testing which device you have, though Device::GetGraphicsApiId says so directly.
	 */

	namespace
	{
		[[nodiscard]] metal4::CmdList * ListBehind(CommandList commandList) noexcept
		{
			auto * object = static_cast<metal4::Metal4Object *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandList), metal4::RenderCommandBlock()));
			return metal4::ListOf(object);
		}
	} // namespace

	MTL4::CommandBuffer * GetMetal4CommandBuffer(CommandList commandList)
	{
		metal4::CmdList * list = ListBehind(commandList);
		return list != nullptr ? list->commandBuffer.get() : nullptr;
	}

	MTL4::RenderCommandEncoder * GetMetal4RenderCommandEncoder(CommandList commandList)
	{
		metal4::CmdList * list = ListBehind(commandList);
		return list != nullptr ? list->renderEncoder.get() : nullptr;
	}

	MTL4::ComputeCommandEncoder * GetMetal4ComputeCommandEncoder(CommandList commandList)
	{
		metal4::CmdList * list = ListBehind(commandList);
		return list != nullptr ? list->computeEncoder.get() : nullptr;
	}

	MTL4::ArgumentTable * GetMetal4ArgumentTable(CommandList commandList)
	{
		metal4::CmdList * list = ListBehind(commandList);
		return list != nullptr ? list->argumentTable.get() : nullptr;
	}

} // namespace azo::rhi

#endif // __APPLE__

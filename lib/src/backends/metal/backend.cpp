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
	#include "azoth/rhi/backend/table_validation.hpp"
	#include "azoth/rhi/native/metal_native.hpp"

	#include "backends/metal/internal.hpp"
	#include "backends/registration.hpp"

	#include <string_view>

namespace azo::rhi
{

	Result<void> register_metal_backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName				   = MetalApi::kCanonicalName;
		info.info.displayName				   = MetalApi::kDisplayName;
		info.info.apiVersionMajor			   = 3;
		info.info.supportsSurfaces			   = true;
		info.info.supportsDebugMarkers		   = true;
		info.info.supportsExternalNativeAccess = true;
		info.createInstance					   = &metal::metal_create_instance;
		return registry.Register<MetalApi>(info);
	}

	template <>
	Result<UniqueDevice> create_device<MetalApi>(const DeviceDesc & desc)
	{
		if (const Result<void> checked = detail::check_device_desc(desc); !checked)
		{
			return checked.get_error();
		}

		Error refusal{};
		metal::MetalDevice * device = metal::make_owned_device(nullptr, desc, refusal);
		if (device == nullptr)
		{
			return refusal.code != ErrorCode::eOk ? refusal : Error{ .code = ErrorCode::eNativeApiError, .message = "no Metal device available" };
		}

		Error error{};
		void * deviceImpl		 = device;
		BackendBlockSet * blocks = detail::resolve_device_blocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::make_unique_device(deviceImpl, blocks);
	}

	namespace native
	{
		MetalCommandListView NativeAccess<MetalApi>::make_command_list_view(void * commandListImpl) noexcept
		{
			auto * object = static_cast<metal::MetalObject *>(detail::native_impl_of(commandListImpl, metal::render_command_block()));
			return MetalCommandListView{ .commandBuffer = object != nullptr ? cmd_buffer_of(object) : nullptr };
		}
	}

	Result<MetalNativeDevice> get_metal_native_device(Device device)
	{
		if (device.get_graphics_api_id() != MetalApi::kId)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalNativeDevice called on a non-Metal device",
			};
		}

		auto * impl = static_cast<metal::MetalDevice *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(device), metal::core_device_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalNativeDevice reached something other than a Metal device behind the facade",
			};
		}

		return MetalNativeDevice{
			.device = impl->device.get(),
			.queue	= impl->command_queue_for(QueueType::eGraphics),
		};
	}

	Result<native::MetalQueueView> get_metal_queue_view(Queue queue)
	{
		const auto * object = static_cast<metal::MetalObject *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(queue), metal::queue_block()));
		if (object == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetalQueueView called on a queue that is not a Metal 3 one",
			};
		}

		return native::MetalQueueView{ .queue = object->owner->command_queue_for(object->queueType) };
	}

	MTL::CommandBuffer * get_metal_command_buffer(CommandList commandList)
	{
		auto * object = static_cast<metal::MetalObject *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(commandList), metal::render_command_block()));
		return object != nullptr && object->list != nullptr ? object->list->commandBuffer.get() : nullptr;
	}

	MTL::RenderCommandEncoder * get_metal_render_command_encoder(CommandList commandList)
	{
		auto * object = static_cast<metal::MetalObject *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(commandList), metal::render_command_block()));
		return object != nullptr && object->list != nullptr ? object->list->renderEncoder.get() : nullptr;
	}

}

#endif

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
	#include "azoth/rhi/backend/table_validation.hpp"
	#include "azoth/rhi/native/metal_native.hpp"

	#include "backends/metal4/internal.hpp"
	#include "backends/registration.hpp"


	#include <string_view>

namespace azo::rhi
{

	Result<void> register_metal4_backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName				   = Metal4Api::kCanonicalName;
		info.info.displayName				   = Metal4Api::kDisplayName;
		info.info.apiVersionMajor			   = 4;
		info.info.supportsSurfaces			   = true;
		info.info.supportsDebugMarkers		   = true;
		info.info.supportsExternalNativeAccess = true;
		info.createInstance					   = &metal4::metal4_create_instance;
		return registry.Register<Metal4Api>(info);
	}

	template <>
	Result<UniqueDevice> create_device<Metal4Api>(const DeviceDesc & desc)
	{
		if (const Result<void> checked = detail::check_device_desc(desc); !checked)
		{
			return checked.get_error();
		}

		Error refusal{};
		metal4::Metal4Device * device = metal4::make_owned_device(nullptr, desc, refusal);
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
		Metal4CommandListView NativeAccess<Metal4Api>::make_command_list_view(void * commandListImpl) noexcept
		{
			metal4::CmdList * list = metal4::list_of(static_cast<metal4::Metal4Object *>(detail::native_impl_of(commandListImpl, metal4::render_command_block())));
			return Metal4CommandListView{ .commandBuffer = list != nullptr ? list->commandBuffer.get() : nullptr };
		}
	}

	Result<Metal4NativeDevice> get_metal4_native_device(Device device)
	{
		if (device.get_graphics_api_id() != Metal4Api::kId)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetal4NativeDevice called on a device that is not backed by Metal 4",
			};
		}

		auto * impl = static_cast<metal4::Metal4Device *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(device), metal4::core_device_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetal4NativeDevice reached something other than a Metal 4 device behind the facade",
			};
		}

		return Metal4NativeDevice{
			.device = impl->device.get(),
			.queue	= impl->command_queue_for(QueueType::eGraphics),
		};
	}

	Result<native::Metal4QueueView> get_metal4_queue_view(Queue queue)
	{
		const auto * object = static_cast<metal4::Metal4Object *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(queue), metal4::queue_block()));
		if (object == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetMetal4QueueView called on a queue that is not a Metal 4 one",
			};
		}

		return native::Metal4QueueView{ .queue = object->owner->command_queue_for(object->queueType) };
	}

	namespace
	{
		[[nodiscard]] metal4::CmdList * list_behind(CommandList commandList) noexcept
		{
			auto * object = static_cast<metal4::Metal4Object *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(commandList), metal4::render_command_block()));
			return metal4::list_of(object);
		}
	}

	MTL4::CommandBuffer * get_metal4_command_buffer(CommandList commandList)
	{
		metal4::CmdList * list = list_behind(commandList);
		return list != nullptr ? list->commandBuffer.get() : nullptr;
	}

	MTL4::RenderCommandEncoder * get_metal4_render_command_encoder(CommandList commandList)
	{
		metal4::CmdList * list = list_behind(commandList);
		return list != nullptr ? list->renderEncoder.get() : nullptr;
	}

	MTL4::ComputeCommandEncoder * get_metal4_compute_command_encoder(CommandList commandList)
	{
		metal4::CmdList * list = list_behind(commandList);
		return list != nullptr ? list->computeEncoder.get() : nullptr;
	}

	MTL4::ArgumentTable * get_metal4_argument_table(CommandList commandList)
	{
		metal4::CmdList * list = list_behind(commandList);
		return list != nullptr ? list->argumentTable.get() : nullptr;
	}

}

#endif

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

#include "azoth/rhi/commands/sync.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal4
{
	TimelineHandle metal4_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
	{
		if (!metal4_refuse_unexportable(
				desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a timeline only through MTLSharedEventHandle, and this asked for another handle type",
				error
			))
		{
			return TimelineHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createTimeline");

		auto * device = static_cast<Metal4Device *>(impl);

		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}

		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(desc.initialValue);

		const TimelineHandle handle = device->timelines.store(Metal4Timeline{ .event = std::move(event), .exportableHandleTypes = desc.exportableHandleTypes });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal timeline tracking failed");
		}

		return return_value(handle, error);
	}

	[[nodiscard]] MTL::Buffer * resolve_buffer(Metal4Device * device, BufferHandle handle) noexcept
	{
		const auto * tracked = device->buffers.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->buffer.get() : nullptr;
	}

	[[nodiscard]] MTL::Texture * resolve_texture(Metal4Device * device, TextureHandle handle) noexcept
	{
		const auto * tracked = device->textures.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	[[nodiscard]] Format resolve_texture_format(Metal4Device * device, TextureHandle handle) noexcept
	{
		auto * const tracked = device->textures.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->format : Format::eRGBA8UNorm;
	}

	BinarySemaphoreHandle metal4_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (!metal4_refuse_unexportable(
				desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a binary semaphore only through MTLSharedEventHandle, and this asked for another handle type",
				error
			))
		{
			return BinarySemaphoreHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createBinarySemaphore");

		auto * device		   = static_cast<Metal4Device *>(impl);
		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}
		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(0);

		const BinarySemaphoreHandle handle = device->binarySemaphores.store(
			Metal4BinarySemaphore{
				.event				   = std::move(event),
				.value				   = 0,
				.exportableHandleTypes = desc.exportableHandleTypes,
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal binary semaphore tracking failed");
		}

		return return_value(handle, error);
	}

}

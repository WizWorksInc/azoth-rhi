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
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLBlitCommandEncoder.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal
{
	TimelineHandle metal_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
	{
		if (!metal_refuse_unexportable(
				desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a timeline only through MTLSharedEventHandle, and this asked for another handle type",
				error
			))
		{
			return TimelineHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createTimeline");

		auto * device = static_cast<MetalDevice *>(impl);

		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}

		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(desc.initialValue);

		const TimelineHandle handle = device->timelines.store(MetalTimeline{ .event = std::move(event), .exportableHandleTypes = desc.exportableHandleTypes });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal timeline tracking failed");
		}

		return return_value(handle, error);
	}

	[[nodiscard]] MTL::Buffer * resolve_buffer(MetalDevice * device, BufferHandle handle) noexcept
	{
		const auto * tracked = device->buffers.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->buffer.get() : nullptr;
	}

	[[nodiscard]] MTL::Texture * resolve_texture(MetalDevice * device, TextureHandle handle) noexcept
	{
		const auto * tracked = device->textures.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	[[nodiscard]] Format resolve_texture_format(MetalDevice * device, TextureHandle handle) noexcept
	{
		auto * const tracked = device->textures.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->format : Format::eRGBA8UNorm;
	}

	[[nodiscard]] MTL::CommandBuffer * cmd_buffer_of(MetalObject * object) noexcept
	{
		return (object->list != nullptr) ? object->list->commandBuffer.get() : nullptr;
	}

	void MetalCmdList::end_encoders() noexcept
	{
		if (renderEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(this, renderEncoder.get());
			renderEncoder->endEncoding();
			renderEncoder.reset();
		}
		if (computeEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(this, computeEncoder.get());
			computeEncoder->endEncoding();
			computeEncoder.reset();
		}
	}

	// Metal treats an encoder released without endEncoding as fatal, so a list cannot be allowed to carry one to its grave.
	MetalCmdList::~MetalCmdList()
	{
		end_encoders();
	}

	void end_active_encoders(MetalObject * object) noexcept
	{
		if (object->list == nullptr)
		{
			return;
		}

		object->list->end_encoders();
	}

	void release_cmd_buffer(MetalDevice * device, MetalCmdList * rec, QueueType queueType) noexcept
	{
		if (rec == nullptr)
		{
			return;
		}

		rec->end_encoders();
		rec->commandBuffer.reset();
		rec->lifecycle = ListLifecycle::eFresh;

		// The flag and not the lifecycle decides this, so a list that was submitted and gave its slot back then cannot give it back twice.
		if (rec->holdsListSlot)
		{
			rec->holdsListSlot = false;
			device->openLists.close(queueType);
		}
	}

	void consume_alias_wait(MetalCmdList * rec, MTL::RenderCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get(), MTL::RenderStageVertex);
			rec->aliasWaitPending = false;
		}
	}

	void consume_alias_wait(MetalCmdList * rec, MTL::ComputeCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get());
			rec->aliasWaitPending = false;
		}
	}

	void consume_alias_wait(MetalCmdList * rec, MTL::BlitCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get());
			rec->aliasWaitPending = false;
		}
	}

	[[nodiscard]] MTL::BlitCommandEncoder * begin_blit(MetalObject * object, Error * error) noexcept
	{
		if (object->list->renderEncoder.get() != nullptr)
		{
			return fail_value<MTL::BlitCommandEncoder *>(
				error,
				ErrorCode::eInvalidState,
				"a transfer command cannot be recorded inside a rendering scope, so record it between passes"
			);
		}

		end_active_encoders(object);
		MTL::BlitCommandEncoder * encoder = object->list->commandBuffer->blitCommandEncoder();
		if (encoder == nullptr)
		{
			return fail_value<MTL::BlitCommandEncoder *>(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
		}

		consume_alias_wait(object->list, encoder);
		return encoder;
	}

	[[nodiscard]] MetalCmdList * new_cmd_list(MetalDevice * device)
	{
		auto record = host_new<MetalCmdList>();
		if (record == nullptr)
		{
			return nullptr;
		}

		MetalCmdList * raw = record.get();
		if (!detail::try_push_back(device->cmdLists, std::move(record)))
		{
			return nullptr;
		}

		return raw;
	}

	BinarySemaphoreHandle metal_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (!metal_refuse_unexportable(
				desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a binary semaphore only through MTLSharedEventHandle, and this asked for another handle type",
				error
			))
		{
			return BinarySemaphoreHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createBinarySemaphore");

		auto * device		   = static_cast<MetalDevice *>(impl);
		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}
		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(0);

		const BinarySemaphoreHandle handle = device->binarySemaphores.store(
			MetalBinarySemaphore{
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

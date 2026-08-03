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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	TimelineHandle MetalCreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
	{
		if (!MetalRefuseUnexportable(desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a timeline only through MTLSharedEventHandle, and this asked for another handle type",
				error))
		{
			return TimelineHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createTimeline");

		auto * device = static_cast<MetalDevice *>(impl);

		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}

		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(desc.initialValue);

		const TimelineHandle handle = device->timelines.Store(MetalTimeline{ .event = std::move(event), .exportableHandleTypes = desc.exportableHandleTypes });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal timeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

	[[nodiscard]] MTL::Buffer * ResolveBuffer(MetalDevice * device, BufferHandle handle) noexcept
	{
		const auto * tracked = device->buffers.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->buffer.get() : nullptr;
	}

	[[nodiscard]] MTL::Texture * ResolveTexture(MetalDevice * device, TextureHandle handle) noexcept
	{
		const auto * tracked = device->textures.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	[[nodiscard]] Format ResolveTextureFormat(MetalDevice * device, TextureHandle handle) noexcept
	{
		auto *const tracked = device->textures.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->format : Format::eRGBA8UNorm;
	}

	[[nodiscard]] MTL::CommandBuffer * CmdBufferOf(MetalObject * object) noexcept
	{
		return (object->list != nullptr) ? object->list->commandBuffer.get() : nullptr;
	}

	// Ends any open render or compute encoder. Metal allows only one encoder open at a time so a blit or a new rendering scope must close the previous one
	// first.
	void EndActiveEncoders(MetalObject * object) noexcept
	{
		if (object->list == nullptr)
		{
			return;
		}
		if (object->list->renderEncoder.get() != nullptr)
		{
			object->list->renderEncoder->endEncoding();
			object->list->renderEncoder.reset();
		}
		if (object->list->computeEncoder.get() != nullptr)
		{
			object->list->computeEncoder->endEncoding();
			object->list->computeEncoder.reset();
		}
	}

	// Takes the wait an alias barrier left behind so the first encoder opened after one is ordered against the encoder that ran before it. Clearing the flag on
	// the way through means only that encoder pays for it: a barrier orders the next scope, not every scope after it.
	void ConsumeAliasWait(MetalCmdList * rec, MTL::RenderCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get(), MTL::RenderStageVertex);
			rec->aliasWaitPending = false;
		}
	}

	void ConsumeAliasWait(MetalCmdList * rec, MTL::ComputeCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get());
			rec->aliasWaitPending = false;
		}
	}

	void ConsumeAliasWait(MetalCmdList * rec, MTL::BlitCommandEncoder * encoder) noexcept
	{
		if (rec->aliasWaitPending && encoder != nullptr)
		{
			encoder->waitForFence(rec->aliasFence.get());
			rec->aliasWaitPending = false;
		}
	}

	// Closes any open encoder and opens a fresh blit encoder for a copy or clear command.
	[[nodiscard]] MTL::BlitCommandEncoder * BeginBlit(MetalObject * object) noexcept
	{
		EndActiveEncoders(object);
		MTL::BlitCommandEncoder * encoder = object->list->commandBuffer->blitCommandEncoder();
		ConsumeAliasWait(object->list, encoder);
		return encoder;
	}

	[[nodiscard]] MetalCmdList * NewCmdList(MetalDevice * device, QueueType queueType)
	{
		auto record = HostNew<MetalCmdList>();
		if (record == nullptr)
		{
			return nullptr;
		}

		if (MTL::CommandQueue * commandQueue = device->CommandQueueFor(queueType); commandQueue != nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			MTL::CommandBuffer * commandBuffer			  = commandQueue->commandBuffer();
			if (commandBuffer != nullptr)
			{
				record->commandBuffer = NS::RetainPtr(commandBuffer);
			}
		}

		MetalCmdList * raw = record.get();
		if (!detail::TryPushBack(device->cmdLists, std::move(record)))
		{
			return nullptr;
		}

		return raw;
	}

	BinarySemaphoreHandle MetalCreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (!MetalRefuseUnexportable(desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedEvent,
				"Metal exports a binary semaphore only through MTLSharedEventHandle, and this asked for another handle type",
				error))
		{
			return BinarySemaphoreHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createBinarySemaphore");

		auto * device		   = static_cast<MetalDevice *>(impl);
		MTL::SharedEvent * raw = device->device->newSharedEvent();
		if (raw == nullptr)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Metal shared event creation failed");
		}
		NS::SharedPtr<MTL::SharedEvent> event = NS::TransferPtr(raw);
		event->setSignaledValue(0);

		const BinarySemaphoreHandle handle = device->binarySemaphores.Store(MetalBinarySemaphore{
			.event				   = std::move(event),
			.value				   = 0,
			.exportableHandleTypes = desc.exportableHandleTypes,
		});
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Metal binary semaphore tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal

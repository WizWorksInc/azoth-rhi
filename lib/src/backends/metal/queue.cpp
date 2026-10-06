// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	QueueType MetalQueueTypeOf(void * impl) noexcept
	{
		return static_cast<MetalObject *>(impl)->queueType;
	}

	bool MetalQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.submit");

		auto * queue		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = queue->owner;

		MTL::CommandQueue * commandQueue = device->CommandQueueFor(queue->queueType);
		if (commandQueue == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "submit on a queue type the device did not create");
		}

		// This backend never resubmits, so the pending question cannot arise and is answered false.
		if (const char * refusal = SubmitRefusalForLists(
				desc.commandLists,
				device->caps.supportsCommandListResubmit,
				[](const CommandList & list)
				{
					return static_cast<const MetalObject *>(detail::UnwrappedImplOf(list))->list;
				},
				[](const MetalCmdList &)
				{
					return false;
				});
			refusal != nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, refusal);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TimelinePoint & wait : desc.waits)
		{
			const auto * tracked = device->timelines.Resolve(wait.timeline, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}
			MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
			commandBuffer->encodeWait(tracked->event.get(), wait.value);
			commandBuffer->commit();
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			const auto * tracked = device->binarySemaphores.Resolve(sync.acquired, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}
			MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
			commandBuffer->encodeWait(tracked->event.get(), tracked->value);
			commandBuffer->commit();
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}
			auto * listObject  = static_cast<MetalObject *>(detail::UnwrappedImplOf(*list));
			MetalCmdList * rec = listObject->list;
			if (rec == nullptr || rec->lifecycle != ListLifecycle::eEnded)
			{
				continue;
			}

			rec->commandBuffer->commit();
			rec->lifecycle = ListLifecycle::eSubmitted;
			if (rec->holdsListSlot)
			{
				rec->holdsListSlot = false;
				device->openLists.Close(listObject->queueType);
			}
		}

		for (const TimelinePoint & signal : desc.signals)
		{
			const auto * tracked = device->timelines.Resolve(signal.timeline, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}
			MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
			commandBuffer->encodeSignalEvent(tracked->event.get(), signal.value);
			commandBuffer->commit();
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			auto * tracked = device->binarySemaphores.Resolve(sync.renderFinished, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}

			tracked->value += 1;
			MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
			commandBuffer->encodeSignalEvent(tracked->event.get(), tracked->value);
			commandBuffer->commit();
		}

		return Succeed(error);
	}

	bool MetalQueueWaitIdle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.waitIdle");

		auto * queue					 = static_cast<MetalObject *>(impl);
		MetalDevice * device			 = queue->owner;
		MTL::CommandQueue * commandQueue = device->CommandQueueFor(queue->queueType);
		if (commandQueue == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "waitIdle on a queue type the device did not create");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::CommandBuffer * commandBuffer			  = commandQueue->commandBuffer();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		return Succeed(error);
	}

	bool MetalQueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		auto * device = static_cast<MetalObject *>(impl)->owner;

		const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			*out = 0;
			return Fail(error, ErrorCode::eInvalidHandle, "completed value of a timeline this device never created");
		}

		*out = tracked->event->signaledValue();
		return Succeed(error);
	}

	bool MetalQueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		auto * device = static_cast<MetalObject *>(impl)->owner;

		const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "signal of a timeline this device never created");
		}

		tracked->event->setSignaledValue(value);
		return Succeed(error);
	}

	bool MetalQueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * device = static_cast<MetalObject *>(impl)->owner;

		MTL::SharedEvent * event = nullptr;
		{
			const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "wait on a timeline this device never created");
			}
			event = tracked->event.get();
		}

		return MetalWaitForEvent(event, value, timeoutNanoseconds, error);
	}

	bool MetalQueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		return Succeed(error);
	}

	bool MetalQueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return Succeed(error);
	}

	namespace
	{
		const void * MetalCommandListQueryInterface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const MetalObject *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			return QueryPublished<Published<RenderCommandApi, &RenderCommandBlock>,
				Published<AliasingCommandApi, &AliasingCommandBlock>,
				Published<QueryCommandApi, &QueryCommandBlock>,
				Published<IndirectApi, &IndirectBlock>,
				Published<NativeEscapeApi, &NativeEscapeBlock>>(object, id, minVersion);
		}

		const BackendObject * CommandListObject() noexcept
		{
			static constexpr BackendObject object{ .queryInterface = &MetalCommandListQueryInterface };
			return &object;
		}
	}

	void * MetalCommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.allocate");

		MetalDevice * device	  = static_cast<MetalObject *>(impl)->owner;
		const QueueType queueType = static_cast<MetalObject *>(impl)->queueType;
		MetalCmdPool * owner	  = static_cast<MetalObject *>(impl)->pool;

		if (owner != nullptr && owner->handedOut < owner->lists.size())
		{
			MetalObject * recycled = owner->lists[owner->handedOut];
			++owner->handedOut;
			recycled->list->debugName = debugName != nullptr ? debugName : "";
			return ReturnValue(static_cast<void *>(recycled), error);
		}

		auto * listObject = static_cast<MetalObject *>(AllocObject(device, CommandListObject(), queueType));
		if (listObject == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list allocation failed");
		}

		listObject->list = NewCmdList(device);
		if (listObject->list == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list allocation failed");
		}

		listObject->list->debugName = debugName != nullptr ? debugName : "";

		if (owner != nullptr)
		{
			if (!detail::TryPushBack(owner->lists, listObject))
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list tracking failed");
			}
			++owner->handedOut;
		}

		return ReturnValue(static_cast<void *>(listObject), error);
	}

	bool MetalCommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.reset");

		auto * poolObject = static_cast<MetalObject *>(impl);
		if (poolObject->pool != nullptr)
		{
			for (MetalObject * listObject : poolObject->pool->lists)
			{
				ReleaseCmdBuffer(poolObject->owner, listObject->list, listObject->queueType);
			}

			poolObject->pool->handedOut = 0;
		}

		return Succeed(error);
	}

}

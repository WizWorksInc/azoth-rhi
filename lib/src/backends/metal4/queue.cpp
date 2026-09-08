// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	QueueType Metal4QueueTypeOf(void * impl) noexcept
	{
		return static_cast<Metal4Object *>(impl)->queueType;
	}

	namespace
	{
		[[nodiscard]] MTL4::CommandBuffer * CommandBufferOf(const CommandList * list) noexcept
		{
			if (list == nullptr)
			{
				return nullptr;
			}

			auto * object	 = static_cast<Metal4Object *>(detail::UnwrappedImplOf(*list));
			CmdList * record = ListOf(object);
			return record != nullptr ? record->commandBuffer.get() : nullptr;
		}
	}

	bool Metal4QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.submit");

		auto * queue		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = queue->owner;

		MTL4::CommandQueue * commandQueue = device->CommandQueueFor(queue->queueType);
		if (commandQueue == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "submit on a queue type the device did not create");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TimelinePoint & wait : desc.waits)
		{
			if (const auto * tracked = device->timelines.Resolve(wait.timeline, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->wait(tracked->event.get(), wait.value);
			}
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (const auto * tracked = device->binarySemaphores.Resolve(sync.acquired, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->wait(tracked->event.get(), tracked->value);
			}
		}

		detail::HostVector<const MTL4::CommandBuffer *> buffers;
		if (!detail::TryReserve(buffers, desc.commandLists.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "submit could not gather its command buffers");
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}

			auto * listObject	   = static_cast<Metal4Object *>(detail::UnwrappedImplOf(*list));
			const CmdList * record = ListOf(listObject);
			if (record == nullptr || record->commandBuffer.get() == nullptr || record->lifecycle < 2)
			{
				continue;
			}

			if (!detail::TryPushBack(buffers, record->commandBuffer.get()))
			{
				return Fail(error, ErrorCode::eOutOfHostMemory, "submit could not gather its command buffers");
			}
		}

		if (!buffers.empty())
		{
			commandQueue->commit(buffers.data(), buffers.size());

			for (const CommandList * list : desc.commandLists)
			{
				if (list == nullptr)
				{
					continue;
				}

				auto * listObject = static_cast<Metal4Object *>(detail::UnwrappedImplOf(*list));
				if (CmdList * record = ListOf(listObject); record != nullptr && record->commandBuffer.get() != nullptr)
				{
					record->lifecycle = 3;
				}
			}
		}

		for (const TimelinePoint & signal : desc.signals)
		{
			if (const auto * tracked = device->timelines.Resolve(signal.timeline, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->signalEvent(tracked->event.get(), signal.value);
			}
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			auto * tracked = device->binarySemaphores.Resolve(sync.renderFinished, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}

			tracked->value += 1;
			commandQueue->signalEvent(tracked->event.get(), tracked->value);
		}

		return Succeed(error);
	}

	bool Metal4QueueWaitIdle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.waitIdle");

		auto * queue		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = queue->owner;

		MTL4::CommandQueue * commandQueue = device->CommandQueueFor(queue->queueType);
		if (commandQueue == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "waitIdle on a queue type the device did not create");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL::SharedEvent * drain = device->drainEvent.get();
		if (drain == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "this device has no drain event to wait on");
		}

		const std::uint64_t target = device->drainValue.fetch_add(1, std::memory_order_acq_rel) + 1;
		commandQueue->signalEvent(drain, target);

		while (drain->signaledValue() < target)
		{
			std::this_thread::yield();
		}

		return Succeed(error);
	}

	bool Metal4QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		auto * device = static_cast<Metal4Object *>(impl)->owner;

		const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			*out = 0;
			return Fail(error, ErrorCode::eInvalidHandle, "completed value of a timeline this device never created");
		}

		*out = tracked->event->signaledValue();
		return Succeed(error);
	}

	bool Metal4QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Object *>(impl)->owner;

		const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "signal of a timeline this device never created");
		}

		tracked->event->setSignaledValue(value);
		return Succeed(error);
	}

	bool Metal4QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Object *>(impl)->owner;

		MTL::SharedEvent * event = nullptr;
		{
			const auto * tracked = device->timelines.Resolve(timeline, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "wait on a timeline this device never created");
			}
			event = tracked->event.get();
		}

		const bool untilSignaled = (timeoutNanoseconds == std::numeric_limits<std::uint64_t>::max());
		const auto deadline		 = std::chrono::steady_clock::now() + std::chrono::nanoseconds(untilSignaled ? 0 : timeoutNanoseconds);

		while (event->signaledValue() < value)
		{
			if (!untilSignaled && std::chrono::steady_clock::now() >= deadline)
			{
				return Fail(error, ErrorCode::eTimeout, "timeline wait timed out");
			}
			std::this_thread::yield();
		}

		return Succeed(error);
	}

	bool Metal4QueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		return Succeed(error);
	}

	bool Metal4QueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return Succeed(error);
	}

	namespace
	{
		const void * Metal4CommandListQueryInterface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const Metal4Object *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			return QueryPublished<Published<RenderCommandApi, &RenderCommandBlock>,
				Published<QueryCommandApi, &QueryCommandBlock>,
				Published<AliasingCommandApi, &AliasingCommandBlock>,
				Published<IndirectApi, &IndirectBlock>,
				Published<NativeEscapeApi, &NativeEscapeBlock>>(object, id, minVersion);
		}

		const BackendObject * CommandListObject() noexcept
		{
			static constexpr BackendObject object{ .queryInterface = &Metal4CommandListQueryInterface };
			return &object;
		}
	}

	void * Metal4CommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.allocate");

		auto * poolObject		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device	  = poolObject->owner;
		const QueueType queueType = poolObject->queueType;
		CmdPool * owner			  = poolObject->pool;

		if (owner != nullptr && owner->handedOut < owner->lists.size())
		{
			Metal4Object * recycled = owner->lists[owner->handedOut];
			++owner->handedOut;
			recycled->list->debugName = debugName != nullptr ? debugName : "";
			return ReturnValue(static_cast<void *>(recycled), error);
		}

		auto * listObject = static_cast<Metal4Object *>(AllocObject(device, CommandListObject(), queueType));
		if (listObject == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list allocation failed");
		}

		auto record = HostNew<CmdList>();
		if (record == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list allocation failed");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL4::CommandAllocator * allocator = device->device->newCommandAllocator();
		if (allocator == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command allocator creation failed");
		}
		record->allocator = NS::TransferPtr(allocator);

		MTL4::CommandBuffer * commandBuffer = device->device->newCommandBuffer();
		if (commandBuffer == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command buffer creation failed");
		}
		record->commandBuffer = NS::TransferPtr(commandBuffer);

		NS::SharedPtr<MTL4::ArgumentTableDescriptor> tableDesc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
		tableDesc->setMaxBufferBindCount(kMetalMaxBufferArguments);
		tableDesc->setMaxTextureBindCount(kMetalMaxTextureArguments);
		tableDesc->setMaxSamplerStateBindCount(kMetalMaxSamplerArguments);

		tableDesc->setInitializeBindings(true);

		NS::Error * tableError		   = nullptr;
		MTL4::ArgumentTable * argTable = device->device->newArgumentTable(tableDesc.get(), &tableError);
		if (argTable == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Metal 4 argument table creation failed");
		}
		record->argumentTable = NS::TransferPtr(argTable);

		{
			const NS::SharedPtr<MTL::ResidencySetDescriptor> residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());

			NS::Error * residencyError	  = nullptr;
			MTL::ResidencySet * residency = device->device->newResidencySet(residencyDesc.get(), &residencyError);
			if (residency == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command list residency set creation failed");
			}

			record->residency = NS::TransferPtr(residency);
		}

		record->debugName = debugName != nullptr ? debugName : "";

		CmdList * raw = record.get();
		if (!detail::TryPushBack(device->cmdLists, std::move(record)))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list tracking failed");
		}

		listObject->list = raw;

		if (owner != nullptr)
		{
			if (!detail::TryPushBack(owner->lists, listObject))
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list tracking failed");
			}
			++owner->handedOut;
		}

		return ReturnValue(static_cast<void *>(listObject), error);
	}

	bool Metal4CommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.reset");

		auto * poolObject = static_cast<Metal4Object *>(impl);
		if (poolObject->pool != nullptr)
		{
			poolObject->pool->handedOut = 0;
		}

		return Succeed(error);
	}

	void NoteListAllocation(CmdList * list, const MTL::Allocation * allocation) noexcept
	{
		if (list == nullptr || allocation == nullptr || list->residency.get() == nullptr)
		{
			return;
		}

		list->residency->addAllocation(allocation);
		list->residency->commit();
		list->residency->requestResidency();
	}

	void Metal4Device::NoteAllocation(const Residency kind, const MTL::Allocation * allocation) noexcept
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): kind is an enumerator of the array's own size.
		const NS::SharedPtr<MTL::ResidencySet> & set = residencySets[static_cast<std::size_t>(kind)];
		if (allocation == nullptr || set.get() == nullptr)
		{
			return;
		}

		set->addAllocation(allocation);

		set->commit();
		set->requestResidency();
	}

}

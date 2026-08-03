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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	QueueType QueueTypeOf(void * impl) noexcept
	{
		return static_cast<Metal4Object *>(impl)->queueType;
	}

	std::uint32_t QueueFamilyIndex(void * impl) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<Metal4Object *>(impl)->queueType);
	}

	namespace
	{
		// One command list's command buffer, or null when it holds none.
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
	} // namespace

	/*
	 * Submission, which is where this generation is plainly better than the one it replaces.
	 *
	 * Metal 3 has no queue level wait or signal, so that backend commits an empty command buffer per synchronization point. A queue here takes both directly,
	 * so a submit with four waits and two signals is six queue calls and one commit instead of seven command buffers.
	 *
	 * The commit is also a batch: every list goes in one call, one trip into the driver.
	 */
	bool QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
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

		// Waits first, so everything committed below is gated behind them.
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

		/*
		 * Gathered into one array so the whole submit is a single commit. A list with no command buffer is skipped, not refused, matching what the other
		 * generation does with the same input.
		 */
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

			/*
			 * Only a list that reached End has anything to commit. Its command buffer is made with the list and is never null, so a null test asks a different
			 * question, and committing one that never began asserts inside the queue instead of coming back as an error.
			 */
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

		// Signals after the work, so they fire once it has completed.
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

	/*
	 * Draining the queue.
	 *
	 * Metal 3 commits an empty command buffer and waits on it, which works because that queue is serial FIFO. There is no command buffer to wait on here
	 * without building one out of an allocator, so this signals an event past every value it has held and waits for that on the host, which is the same barrier
	 * expressed through the primitive this generation does have.
	 */
	bool QueueWaitIdle(void * impl, Error * error) noexcept
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

		// Blocks until the GPU has passed everything committed before the signal above.
		while (drain->signaledValue() < target)
		{
			std::this_thread::yield();
		}

		return Succeed(error);
	}

	bool QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
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

	bool QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
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

	bool QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
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

	/*
	 * Metal scopes debug groups to a command buffer or an encoder and has nothing at the queue. These succeed and emit nothing without failing, matching
	 * what the Vulkan backend does on a device without VK_EXT_debug_utils: a label is instrumentation so a tool that cannot show it is not a reason for the
	 * frame's call to start reporting failure. Command-list labels are the ones that carry the pass structure and those are recorded.
	 */
	bool QueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		return Succeed(error);
	}

	bool QueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return Succeed(error);
	}

	/*
	 * What a command list from this device publishes, which is not the same on every adapter.
	 *
	 * A timestamp needs a counter heap, so a list from an adapter carrying no timestamp counters declines the block instead of publishing entries that would
	 * refuse at the call. DeviceCaps reads supportsTimestampQueries back off exactly this.
	 *
	 * AliasingCommandApi is absent, and that is a claim, not an oversight: the ordinary barrier already covers the same hazard through
	 * VisibilityOptionResourceAlias.
	 */
	namespace
	{
		const void * CommandListQueryInterface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const Metal4Object *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			return QueryPublished<Published<RenderCommandApi, &RenderCommandBlock>,
				Published<QueryCommandApi, &QueryCommandBlock>,
				Published<IndirectApi, &IndirectBlock>,
				Published<NativeEscapeApi, &NativeEscapeBlock>>(object, id, minVersion);
		}

		const BackendObject * CommandListObject() noexcept
		{
			static constexpr BackendObject object{ .queryInterface = &CommandListQueryInterface };
			return &object;
		}
	} // namespace

	/*
	 * A command list, which on this generation is three objects, not one.
	 *
	 * The allocator and the argument table are made here and kept for the list's life. The command buffer is made here too but is only meaningful between Begin
	 * and End, since beginning one takes its memory from the allocator and resetting the allocator invalidates whatever it handed out before.
	 */
	void * CommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.allocate");

		auto * poolObject		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device	  = poolObject->owner;
		const QueueType queueType = poolObject->queueType;
		CmdPool * owner			  = poolObject->pool;

		// A list this pool built before and has since taken back. Nothing about it needs remaking: Begin resets the allocator, rebuilds the command buffer over
		// it and rewinds everything else the previous recording left.
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

		/*
		 * Sized from the same per-stage limits a Metal 3 set is bounded by, so what a shader can reach is the same on both generations and the ABI does not
		 * change with the backend.
		 */
		NS::SharedPtr<MTL4::ArgumentTableDescriptor> tableDesc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
		tableDesc->setMaxBufferBindCount(kMetalMaxBufferArguments);
		tableDesc->setMaxTextureBindCount(kMetalMaxTextureArguments);
		tableDesc->setMaxSamplerStateBindCount(kMetalMaxSamplerArguments);

		// Cleared on creation, so a table read at a slot nothing bound is a defined null, not stale bytes.
		tableDesc->setInitializeBindings(true);

		NS::Error * tableError		   = nullptr;
		MTL4::ArgumentTable * argTable = device->device->newArgumentTable(tableDesc.get(), &tableError);
		if (argTable == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Metal 4 argument table creation failed");
		}
		record->argumentTable = NS::TransferPtr(argTable);

		/*
		 * The list's own residency set, for the push constant blocks and staging buffers it makes while recording.
		 *
		 * Here and not at the device, because recording takes no RHI lock and the device's sets are written under the guard for a resource kind. One per list
		 * is written by one thread.
		 *
		 * Declared on the command buffer at Begin, not added to the queues. A queue holds at most 32 residency sets and the device already spends four.
		 */
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

		// The command buffer is rebuilt at every Begin so the name is kept here and applied there, not set once.
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

	/*
	 * A reset takes every list back, and the caller has already waited for the point it names.
	 *
	 * Nothing here touches Metal. The allocator each list holds is reset where that list next begins, which is the only place that knows the command buffer
	 * over it is about to be rebuilt, and resetting one here would strand the other lists this pool has out.
	 */
	bool CommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.reset");

		auto * poolObject = static_cast<Metal4Object *>(impl);
		if (poolObject->pool != nullptr)
		{
			poolObject->pool->handedOut = 0;
		}

		return Succeed(error);
	}

	/*
	 * Residency, which replaces Metal 3's per-encoder useResource.
	 *
	 * A Metal 3 encoder is told about each resource an argument buffer reaches, one call per resource per encoder. There is no such call here: reachability
	 * comes from a residency set the queue holds, and a resource missing from it faults instead of reading zeros.
	 */
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

		/*
		 * Committed per addition, not batched. Metal takes a commit as the point the set's contents become what the queue sees, so deferring it would leave a
		 * resource created mid-frame unreachable until something else happened to commit. Adding is cheap and the commit is what makes it true.
		 */
		set->commit();
		set->requestResidency();
	}

} // namespace azo::rhi::metal4

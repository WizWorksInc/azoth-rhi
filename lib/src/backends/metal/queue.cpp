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
	QueueType MetalQueueTypeOf(void * impl) noexcept
	{
		return static_cast<MetalObject *>(impl)->queueType;
	}

	std::uint32_t MetalQueueFamilyIndex(void * impl) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<MetalObject *>(impl)->queueType);
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

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// The command queue is serial FIFO. Waits are committed on their own command buffers before the work so they gate it and signals are committed
		// after so they fire once the work completes. Recorded lists already hold a command buffer made from this same type queue so the order holds.
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
			auto * listObject = static_cast<MetalObject *>(detail::UnwrappedImplOf(*list));
			if (listObject->list != nullptr && listObject->list->commandBuffer.get() != nullptr)
			{
				listObject->list->commandBuffer->commit();
				listObject->list->lifecycle = 3;
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
		// An empty command buffer drains the serial queue: when it completes, all prior work has finished.
		MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
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
	bool MetalQueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		return Succeed(error);
	}

	bool MetalQueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return Succeed(error);
	}

	/*
	 * What a command list from this device publishes, which is not the same on every adapter.
	 *
	 * A timestamp needs both a counter set to sample into and a point Metal will sample at, and an adapter can lack either, so a list from one that does
	 * declines the block without publishing entries that would refuse at the call. The block is then the only place the answer lives and DeviceCaps reads
	 * supportsTimestampQueries back off exactly this.
	 */
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
	} // namespace

	void * MetalCommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.allocate");

		MetalDevice * device	  = static_cast<MetalObject *>(impl)->owner;
		const QueueType queueType = static_cast<MetalObject *>(impl)->queueType;
		MetalCmdPool * owner	  = static_cast<MetalObject *>(impl)->pool;

		// A list this pool built before and has since taken back. Begin drops the old command buffer for a fresh one and clears what the previous recording
		// left, so a recycled list starts where a new one would.
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

		listObject->list = NewCmdList(device, queueType);
		if (listObject->list == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list allocation failed");
		}

		// The command buffer itself is made fresh at every Begin so the name is kept here and applied there, not set once.
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

	/*
	 * A reset takes every list back, and the caller has already waited for the point it names.
	 *
	 * Nothing here touches Metal. A list's command buffer is remade where that list next begins, which is the only place that knows the previous one is
	 * finished with.
	 */
	bool MetalCommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.reset");

		auto * poolObject = static_cast<MetalObject *>(impl);
		if (poolObject->pool != nullptr)
		{
			poolObject->pool->handedOut = 0;
		}

		return Succeed(error);
	}

} // namespace azo::rhi::metal

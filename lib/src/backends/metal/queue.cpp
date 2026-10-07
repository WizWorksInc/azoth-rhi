// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/command_list.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLCommandQueue.hpp>

#include <cstdint>

namespace azo::rhi::metal
{
	QueueType metal_queue_type_of(void * impl) noexcept
	{
		return static_cast<MetalObject *>(impl)->queueType;
	}

	bool metal_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.submit");

		auto * queue		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = queue->owner;

		MTL::CommandQueue * commandQueue = device->command_queue_for(queue->queueType);
		if (commandQueue == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "submit on a queue type the device did not create");
		}

		// This backend never resubmits, so the pending question cannot arise and is answered false.
		if (const char * refusal = submit_refusal_for_lists(
				desc.commandLists,
				device->caps.supportsCommandListResubmit,
				[](const CommandList & list)
				{
					return static_cast<const MetalObject *>(detail::unwrapped_impl_of(list))->list;
				},
				[](const MetalCmdList &)
				{
					return false;
				}
			);
			refusal != nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, refusal);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TimelinePoint & wait : desc.waits)
		{
			const auto * tracked = device->timelines.resolve(wait.timeline, kHandleAlreadyChecked);
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
			const auto * tracked = device->binarySemaphores.resolve(sync.acquired, kHandleAlreadyChecked);
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
			auto * listObject  = static_cast<MetalObject *>(detail::unwrapped_impl_of(*list));
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
				device->openLists.close(listObject->queueType);
			}
		}

		for (const TimelinePoint & signal : desc.signals)
		{
			const auto * tracked = device->timelines.resolve(signal.timeline, kHandleAlreadyChecked);
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
			auto * tracked = device->binarySemaphores.resolve(sync.renderFinished, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}

			tracked->value += 1;
			MTL::CommandBuffer * commandBuffer = commandQueue->commandBuffer();
			commandBuffer->encodeSignalEvent(tracked->event.get(), tracked->value);
			commandBuffer->commit();
		}

		return succeed(error);
	}

	bool metal_queue_wait_idle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.waitIdle");

		auto * queue					 = static_cast<MetalObject *>(impl);
		MetalDevice * device			 = queue->owner;
		MTL::CommandQueue * commandQueue = device->command_queue_for(queue->queueType);
		if (commandQueue == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "waitIdle on a queue type the device did not create");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::CommandBuffer * commandBuffer			  = commandQueue->commandBuffer();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		return succeed(error);
	}

	bool metal_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		auto * device = static_cast<MetalObject *>(impl)->owner;

		const auto * tracked = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			*out = 0;
			return fail(error, ErrorCode::eInvalidHandle, "completed value of a timeline this device never created");
		}

		*out = tracked->event->signaledValue();
		return succeed(error);
	}

	bool metal_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		auto * device = static_cast<MetalObject *>(impl)->owner;

		const auto * tracked = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "signal of a timeline this device never created");
		}

		tracked->event->setSignaledValue(value);
		return succeed(error);
	}

	bool metal_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * device = static_cast<MetalObject *>(impl)->owner;

		MTL::SharedEvent * event = nullptr;
		{
			const auto * tracked = device->timelines.resolve(timeline, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "wait on a timeline this device never created");
			}
			event = tracked->event.get();
		}

		return metal_wait_for_event(event, value, timeoutNanoseconds, error);
	}

	bool metal_queue_begin_debug_label(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] CString name,
		[[maybe_unused]] std::uint32_t color,
		Error * error
	) noexcept
	{
		return succeed(error);
	}

	bool metal_queue_end_debug_label([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return succeed(error);
	}

	namespace
	{
		const void * metal_command_list_query_interface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const MetalObject *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			return query_published<
				Published<RenderCommandApi, &render_command_block>,
				Published<AliasingCommandApi, &aliasing_command_block>,
				Published<QueryCommandApi, &query_command_block>,
				Published<IndirectApi, &indirect_block>,
				Published<NativeEscapeApi, &native_escape_block>>(object, id, minVersion);
		}

		const BackendObject * command_list_object() noexcept
		{
			static constexpr BackendObject kObject{ .queryInterface = &metal_command_list_query_interface };
			return &kObject;
		}
	}

	void * metal_command_pool_allocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.allocate");

		MetalDevice * device	  = static_cast<MetalObject *>(impl)->owner;
		const QueueType queueType = static_cast<MetalObject *>(impl)->queueType;
		MetalCmdPool * owner	  = static_cast<MetalObject *>(impl)->pool;

		if (owner != nullptr && owner->handedOut < owner->lists.size())
		{
			MetalObject * recycled = azo::rhi::detail::at(owner->lists, owner->handedOut);
			++owner->handedOut;
			recycled->list->debugName = debugName != nullptr ? debugName : "";
			return return_value(static_cast<void *>(recycled), error);
		}

		auto * listObject = static_cast<MetalObject *>(alloc_object(device, command_list_object(), queueType));
		if (listObject == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list allocation failed");
		}

		listObject->list = new_cmd_list(device);
		if (listObject->list == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list allocation failed");
		}

		listObject->list->debugName = debugName != nullptr ? debugName : "";

		if (owner != nullptr)
		{
			if (!detail::try_push_back(owner->lists, listObject))
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command list tracking failed");
			}
			++owner->handedOut;
		}

		return return_value(static_cast<void *>(listObject), error);
	}

	bool metal_command_pool_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.commandPool.reset");

		auto * poolObject = static_cast<MetalObject *>(impl);
		if (poolObject->pool != nullptr)
		{
			for (MetalObject * listObject : poolObject->pool->lists)
			{
				release_cmd_buffer(poolObject->owner, listObject->list, listObject->queueType);
			}

			poolObject->pool->handedOut = 0;
		}

		return succeed(error);
	}

}

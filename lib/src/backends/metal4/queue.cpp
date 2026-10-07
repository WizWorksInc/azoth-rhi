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
#include "azoth/rhi/host/allocator.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTL4ArgumentTable.hpp>
#include <Metal/MTL4CommandAllocator.hpp>
#include <Metal/MTL4CommandBuffer.hpp>
#include <Metal/MTL4CommandQueue.hpp>
#include <Metal/MTLAllocation.hpp>
#include <Metal/MTLEvent.hpp>
#include <Metal/MTLResidencySet.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint> // NOLINT
#include <limits>
#include <utility>

namespace azo::rhi::metal4
{
	QueueType metal4_queue_type_of(void * impl) noexcept
	{
		return static_cast<Metal4Object *>(impl)->queueType;
	}

	namespace
	{
		[[nodiscard]] MTL4::CommandBuffer * command_buffer_of(const CommandList * list) noexcept
		{
			if (list == nullptr)
			{
				return nullptr;
			}

			auto * object	 = static_cast<Metal4Object *>(detail::unwrapped_impl_of(*list));
			CmdList * record = list_of(object);
			return record != nullptr ? record->commandBuffer.get() : nullptr;
		}
	} // namespace

	[[nodiscard]] static bool submittable_list(const CmdList * record) noexcept
	{
		return record != nullptr && record->commandBuffer.get() != nullptr && record->lifecycle == ListLifecycle::eEnded;
	}

	bool metal4_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.submit");

		auto * queue		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = queue->owner;

		MTL4::CommandQueue * commandQueue = device->command_queue_for(queue->queueType);
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
					return list_of(static_cast<Metal4Object *>(detail::unwrapped_impl_of(list)));
				},
				[](const CmdList &)
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
			if (const auto * tracked = device->timelines.resolve(wait.timeline, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->wait(tracked->event.get(), wait.value);
			}
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (const auto * tracked = device->binarySemaphores.resolve(sync.acquired, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->wait(tracked->event.get(), tracked->value);
			}
		}

		detail::HostVector<const MTL4::CommandBuffer *> buffers;
		if (!detail::try_reserve(buffers, desc.commandLists.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "submit could not gather its command buffers");
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}

			auto * listObject	   = static_cast<Metal4Object *>(detail::unwrapped_impl_of(*list));
			const CmdList * record = list_of(listObject);
			if (!submittable_list(record))
			{
				continue;
			}

			if (!detail::try_push_back(buffers, record->commandBuffer.get()))
			{
				return fail(error, ErrorCode::eOutOfHostMemory, "submit could not gather its command buffers");
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

				// The same test the gathering loop used, so a list it skipped is not marked as though it had been submitted.
				auto * listObject = static_cast<Metal4Object *>(detail::unwrapped_impl_of(*list));
				if (CmdList * record = list_of(listObject); submittable_list(record))
				{
					record->lifecycle = ListLifecycle::eSubmitted;
				}
			}
		}

		for (const TimelinePoint & signal : desc.signals)
		{
			if (const auto * tracked = device->timelines.resolve(signal.timeline, kHandleAlreadyChecked); tracked != nullptr)
			{
				commandQueue->signalEvent(tracked->event.get(), signal.value);
			}
		}
		for (const SwapchainSync & sync : desc.swapchains)
		{
			auto * tracked = device->binarySemaphores.resolve(sync.renderFinished, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				continue;
			}

			tracked->value += 1;
			commandQueue->signalEvent(tracked->event.get(), tracked->value);
		}

		return succeed(error);
	}

	bool metal4_queue_wait_idle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.waitIdle");

		auto * queue		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = queue->owner;

		MTL4::CommandQueue * commandQueue = device->command_queue_for(queue->queueType);
		if (commandQueue == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "waitIdle on a queue type the device did not create");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL::SharedEvent * drain = device->drainEvent.get();
		if (drain == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "this device has no drain event to wait on");
		}

		const std::uint64_t target = device->drainValue.fetch_add(1, std::memory_order_acq_rel) + 1;
		commandQueue->signalEvent(drain, target);

		return metal_wait_for_event(drain, target, std::numeric_limits<std::uint64_t>::max(), error);
	}

	bool metal4_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		auto * device = static_cast<Metal4Object *>(impl)->owner;

		const auto * tracked = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			*out = 0;
			return fail(error, ErrorCode::eInvalidHandle, "completed value of a timeline this device never created");
		}

		*out = tracked->event->signaledValue();
		return succeed(error);
	}

	bool metal4_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Object *>(impl)->owner;

		const auto * tracked = device->timelines.resolve(timeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "signal of a timeline this device never created");
		}

		tracked->event->setSignaledValue(value);
		return succeed(error);
	}

	bool metal4_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Object *>(impl)->owner;

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

	bool metal4_queue_begin_debug_label(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] CString name,
		[[maybe_unused]] std::uint32_t color,
		Error * error
	) noexcept
	{
		return succeed(error);
	}

	bool metal4_queue_end_debug_label([[maybe_unused]] void * impl, Error * error) noexcept
	{
		return succeed(error);
	}

	namespace
	{
		const void * metal4_command_list_query_interface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const Metal4Object *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			return query_published<
				Published<RenderCommandApi, &render_command_block>,
				Published<QueryCommandApi, &query_command_block>,
				Published<AliasingCommandApi, &aliasing_command_block>,
				Published<IndirectApi, &indirect_block>,
				Published<NativeEscapeApi, &native_escape_block>>(object, id, minVersion);
		}

		const BackendObject * command_list_object() noexcept
		{
			static constexpr BackendObject kObject{ .queryInterface = &metal4_command_list_query_interface };
			return &kObject;
		}
	} // namespace

	void * metal4_command_pool_allocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.allocate");

		auto * poolObject		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device	  = poolObject->owner;
		const QueueType queueType = poolObject->queueType;
		CmdPool * owner			  = poolObject->pool;

		if (owner != nullptr && owner->handedOut < owner->lists.size())
		{
			Metal4Object * recycled = azo::rhi::detail::at(owner->lists, owner->handedOut);
			++owner->handedOut;
			recycled->list->debugName = debugName != nullptr ? debugName : "";
			return return_value(static_cast<void *>(recycled), error);
		}

		auto * listObject = static_cast<Metal4Object *>(alloc_object(device, command_list_object(), queueType));
		if (listObject == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list allocation failed");
		}

		auto record = host_new<CmdList>();
		if (record == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list allocation failed");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL4::CommandAllocator * allocator = device->device->newCommandAllocator();
		if (allocator == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command allocator creation failed");
		}
		record->allocator = NS::TransferPtr(allocator);

		MTL4::CommandBuffer * commandBuffer = device->device->newCommandBuffer();
		if (commandBuffer == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command buffer creation failed");
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
			return fail_value<void *>(error, ErrorCode::eNativeApiError, "Metal 4 argument table creation failed");
		}
		record->argumentTable = NS::TransferPtr(argTable);

		{
			const NS::SharedPtr<MTL::ResidencySetDescriptor> residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());

			NS::Error * residencyError	  = nullptr;
			MTL::ResidencySet * residency = device->device->newResidencySet(residencyDesc.get(), &residencyError);
			if (residency == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eNativeApiError, "Metal 4 command list residency set creation failed");
			}

			record->residency = NS::TransferPtr(residency);
		}

		record->debugName = debugName != nullptr ? debugName : "";

		CmdList * raw = record.get();
		if (!detail::try_push_back(device->cmdLists, std::move(record)))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list tracking failed");
		}

		listObject->list = raw;

		if (owner != nullptr)
		{
			if (!detail::try_push_back(owner->lists, listObject))
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal 4 command list tracking failed");
			}
			++owner->handedOut;
		}

		return return_value(static_cast<void *>(listObject), error);
	}

	bool metal4_command_pool_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.commandPool.reset");

		auto * poolObject = static_cast<Metal4Object *>(impl);
		if (poolObject->pool != nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			for (Metal4Object * listObject : poolObject->pool->lists)
			{
				CmdList * record = list_of(listObject);
				if (record == nullptr)
				{
					continue;
				}

				if (record->lifecycle == ListLifecycle::eRecording)
				{
					end_active_encoders(record);
					record->commandBuffer->endCommandBuffer();
				}

				record->lifecycle = ListLifecycle::eFresh;
			}

			poolObject->pool->handedOut = 0;
		}

		return succeed(error);
	}

	void note_list_allocation(CmdList * list, const MTL::Allocation * allocation) noexcept
	{
		if (list == nullptr || allocation == nullptr || list->residency.get() == nullptr)
		{
			return;
		}

		list->residency->addAllocation(allocation);
		list->residency->commit();
		list->residency->requestResidency();
	}

	void Metal4Device::note_allocation(const Residency kind, const MTL::Allocation * allocation) noexcept
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): kind is an enumerator of the array's own size.
		const NS::SharedPtr<MTL::ResidencySet> & set = azo::rhi::detail::at(residencySets, static_cast<std::size_t>(kind));
		if (allocation == nullptr || set.get() == nullptr)
		{
			return;
		}

		set->addAllocation(allocation);

		set->commit();
		set->requestResidency();
	}

} // namespace azo::rhi::metal4

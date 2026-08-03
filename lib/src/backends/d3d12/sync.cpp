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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	[[nodiscard]] D3D12_COMMAND_LIST_TYPE MapCommandListType(QueueType type) noexcept
	{
		switch (type)
		{
		case QueueType::eCompute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case QueueType::eCopy:	   return D3D12_COMMAND_LIST_TYPE_COPY;
		case QueueType::eGraphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
		}
		return D3D12_COMMAND_LIST_TYPE_DIRECT;
	}

	// Blocks until a fence reaches value: WAIT_OBJECT_0 done, WAIT_TIMEOUT expired, WAIT_FAILED setup error. Nanosecond timeouts round down to whole
	// milliseconds and the sentinel waits forever.
	[[nodiscard]] DWORD WaitFenceHost(ID3D12Fence * fence, std::uint64_t value, std::uint64_t timeoutNanoseconds) noexcept
	{
		if (fence->GetCompletedValue() >= value)
		{
			return WAIT_OBJECT_0;
		}

		const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (event == nullptr)
		{
			return WAIT_FAILED;
		}
		if (FAILED(fence->SetEventOnCompletion(value, event)))
		{
			CloseHandle(event);
			return WAIT_FAILED;
		}

		const DWORD timeoutMs = timeoutNanoseconds == std::numeric_limits<std::uint64_t>::max()
									? INFINITE
									: static_cast<DWORD>(std::min<std::uint64_t>(timeoutNanoseconds / 1'000'000ULL, INFINITE - 1));
		const DWORD result	  = WaitForSingleObject(event, timeoutMs);
		CloseHandle(event);
		return result;
	}

	[[nodiscard]] TimelineSlot * ResolveTimeline(D3D12Device * device, TimelineHandle handle) noexcept
	{
		return device->timelineSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] BinarySemaphoreSlot * ResolveBinarySemaphore(D3D12Device * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphoreSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	TimelineHandle D3D12CreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
	{
		if (!D3D12RefuseUnexportable(desc.exportableHandleTypes,
				Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Fence,
				"timeline creation asked for an external handle type Direct3D 12 cannot export",
				error))
		{
			return TimelineHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createTimeline");

		auto * device = static_cast<D3D12Device *>(impl);

		// A fence takes the shared flag at creation and cannot gain it later, which is the same rule the resource declarations carry.
		const D3D12_FENCE_FLAGS flags = desc.exportableHandleTypes.Empty() ? D3D12_FENCE_FLAG_NONE : D3D12_FENCE_FLAG_SHARED;

		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->device->CreateFence(desc.initialValue, flags, IID_PPV_ARGS(fence.GetAddressOf()))))
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateFence failed for a timeline");
		}

		return ReturnValue(device->timelineSlots.Store(TimelineSlot{ .fence = std::move(fence), .exportableHandleTypes = desc.exportableHandleTypes }), error);
	}

	bool D3D12DestroyTimeline(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const TimelineHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		TimelineSlot * slot = device->timelineSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid timeline handle");
		}

		slot->fence.Reset();
		static_cast<void>(device->timelineSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	BinarySemaphoreHandle D3D12CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
	{
		if (!D3D12RefuseUnexportable(desc.exportableHandleTypes,
				Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Fence,
				"binary semaphore creation asked for an external handle type Direct3D 12 cannot export",
				error))
		{
			return BinarySemaphoreHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createBinarySemaphore");

		auto * device				  = static_cast<D3D12Device *>(impl);
		const D3D12_FENCE_FLAGS flags = desc.exportableHandleTypes.Empty() ? D3D12_FENCE_FLAG_NONE : D3D12_FENCE_FLAG_SHARED;

		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->device->CreateFence(0, flags, IID_PPV_ARGS(fence.GetAddressOf()))))
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateFence failed for a binary semaphore");
		}

		return ReturnValue(
			device->binarySemaphoreSlots.Store(BinarySemaphoreSlot{ .fence = std::move(fence), .exportableHandleTypes = desc.exportableHandleTypes }), error);
	}

	bool D3D12DestroyBinarySemaphore(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const BinarySemaphoreHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		BinarySemaphoreSlot * slot = device->binarySemaphoreSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid binary semaphore handle");
		}

		slot->fence.Reset();
		static_cast<void>(device->binarySemaphoreSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	void * D3D12CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createCommandPool");

		auto * device					   = static_cast<D3D12Device *>(impl);
		const D3D12_COMMAND_LIST_TYPE type = MapCommandListType(desc.queueType);

		ComPtr<ID3D12CommandAllocator> allocator;
		if (FAILED(device->device->CreateCommandAllocator(type, IID_PPV_ARGS(allocator.GetAddressOf()))))
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateCommandAllocator failed");
		}

		auto pool		= HostNew<D3D12CommandPool>();
		pool->object	= PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>();
		pool->owner		= device;
		pool->allocator = std::move(allocator);
		pool->type		= type;
		pool->queueType = desc.queueType;

		D3D12CommandPool * raw = pool.get();
		device->commandPools.push_back(std::move(pool));
		Succeed(error);
		return raw;
	}

	void * D3D12CommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.commandPool.allocate");

		auto * pool			 = static_cast<D3D12CommandPool *>(impl);
		D3D12Device * device = pool->owner;

		// A list this pool built before and has since taken back. Begin resets it onto the allocator this pool already reset, which is all that stood between the
		// previous recording and this one.
		if (pool->handedOut < pool->lists.size())
		{
			D3D12CommandList * recycled = pool->lists[pool->handedOut];
			++pool->handedOut;
			NameD3D12Object(recycled->list.Get(), debugName, device->debugNames);
			Succeed(error);
			return recycled;
		}

		ComPtr<ID3D12GraphicsCommandList> list;
		if (FAILED(device->device->CreateCommandList(0, pool->type, pool->allocator.Get(), nullptr, IID_PPV_ARGS(list.GetAddressOf()))))
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateCommandList failed");
		}
		// CreateCommandList returns a list in the recording state. Close it so Begin can reset it.
		list->Close();

		auto cmd	   = HostNew<D3D12CommandList>();
		cmd->object	   = PublishingObject<Published<RenderCommandApi, &RenderCommandBlock>,
			Published<AliasingCommandApi, &AliasingCommandBlock>,
			Published<QueryCommandApi, &QueryCommandBlock>,
			Published<IndirectApi, &IndirectBlock>,
			Published<IndirectCountApi, &IndirectCountBlock>,
			Published<NativeEscapeApi, &NativeEscapeBlock>>();
		cmd->owner	   = device;
		cmd->list	   = std::move(list);
		cmd->allocator = pool->allocator.Get();
		cmd->pool	   = pool;
		cmd->type	   = pool->type;
		cmd->queueType = pool->queueType;

		NameD3D12Object(cmd->list.Get(), debugName, device->debugNames);

		D3D12CommandList * raw = cmd.get();
		device->commandLists.push_back(std::move(cmd));

		// The device owns the record from here, so a pool that cannot remember it still has to refuse: handing it out unrecorded would build a second list for it on
		// the next frame and neither would ever be recycled.
		if (!detail::TryPushBack(pool->lists, raw))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "D3D12 command list allocation failed");
		}
		++pool->handedOut;

		Succeed(error);
		return raw;
	}

	bool D3D12CommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.commandPool.reset");

		auto * pool = static_cast<D3D12CommandPool *>(impl);
		if (FAILED(pool->allocator->Reset()))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12CommandAllocator::Reset failed");
		}

		// The allocator's memory is back, so the lists recorded onto it are the ones the next frame is handed.
		pool->handedOut = 0;

		return Succeed(error);
	}

	bool D3D12CommandListBegin(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);
		if (FAILED(list->list->Reset(list->allocator, nullptr)))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12GraphicsCommandList::Reset failed");
		}

		// The pool reset means the prior recording's GPU work is done so its transient clear descriptors go back and the cursor rewinds.
		if (!list->transientRtvs.empty() || !list->transientDsvs.empty())
		{
			D3D12Device * device = list->owner;
			for (const std::uint32_t index : list->transientRtvs)
			{
				device->rtvHeap.Free(index);
			}
			for (const std::uint32_t index : list->transientDsvs)
			{
				device->dsvHeap.Free(index);
			}
			list->transientRtvs.clear();
			list->transientDsvs.clear();
		}
		list->clearHeapNext = 0;
		list->retiredClearHeaps.clear();
		list->retiredCopyScratch.clear();
		list->retiredCopyAllocs.clear();

		list->boundResourceHeap	   = nullptr;
		list->boundSamplerHeap	   = nullptr;
		list->computePipelineBound = false;
		list->pendingSets		   = {};
		return Succeed(error);
	}

	bool D3D12CommandListEnd(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);
		if (FAILED(list->list->Close()))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12GraphicsCommandList::Close failed");
		}
		return Succeed(error);
	}

	// UPLOAD heaps sit in GENERIC_READ, READBACK in COPY_DEST and a DEFAULT buffer in COMMON is implicitly promoted on a copy so the usual upload and readback
	// need no barrier here.
	bool D3D12CmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.copyBuffer");

		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;

		BufferSlot * srcSlot = ResolveBuffer(device, src);
		BufferSlot * dstSlot = ResolveBuffer(device, dst);
		if (srcSlot == nullptr || dstSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBuffer with an invalid buffer handle");
		}

		list->list->CopyBufferRegion(dstSlot->resource.Get(), dstOffset, srcSlot->resource.Get(), srcOffset, size);
		return Succeed(error);
	}

	// Lowers a submit to GPU waits, ExecuteCommandLists, then GPU signals. Binary semaphores resolve to their fence at the last signal.

} // namespace azo::rhi::d3d12

#endif // _WIN32

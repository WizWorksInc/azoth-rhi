// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		const D3D12_FENCE_FLAGS flags = desc.exportableHandleTypes.Empty() ? D3D12_FENCE_FLAG_NONE : D3D12_FENCE_FLAG_SHARED;

		ComPtr<ID3D12Fence> fence;
		const HRESULT hr = device->device->CreateFence(desc.initialValue, flags, IID_PPV_ARGS(fence.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValueNative<TimelineHandle>(error, hr, "ID3D12Device::CreateFence failed for a timeline");
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
		const HRESULT hr = device->device->CreateFence(0, flags, IID_PPV_ARGS(fence.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValueNative<BinarySemaphoreHandle>(error, hr, "ID3D12Device::CreateFence failed for a binary semaphore");
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

	namespace
	{
		[[nodiscard]] bool CreateRecording(D3D12Device * device, D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandAllocator> & allocator,
			ComPtr<ID3D12GraphicsCommandList> & list, ComPtr<ID3D12GraphicsCommandList7> & list7, Error * error) noexcept
		{
			const HRESULT allocated = device->device->CreateCommandAllocator(type, IID_PPV_ARGS(allocator.GetAddressOf()));
			if (FAILED(allocated))
			{
				return FailNative(error, allocated, "ID3D12Device::CreateCommandAllocator failed");
			}

			const HRESULT hr = device->device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(list.GetAddressOf()));
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12Device::CreateCommandList failed");
			}
			list->Close();

			if (FAILED(list.As(&list7)))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "recording barriers requires ID3D12GraphicsCommandList7");
			}

			return true;
		}

		void SweepRetiredRecordings(D3D12CommandPool * pool) noexcept
		{
			std::size_t kept = 0;
			for (std::size_t at = 0; at < pool->retired.size(); ++at)
			{
				if (!SubmissionStillRunning(pool->retired[at].submitFence, pool->retired[at].submitValue))
				{
					continue;
				}

				if (kept != at)
				{
					pool->retired[kept] = std::move(pool->retired[at]);
				}
				++kept;
			}

			pool->retired.resize(kept);
		}

		[[nodiscard]] bool RetireRunningRecording(D3D12CommandList * list, Error * error) noexcept
		{
			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList> fresh;
			ComPtr<ID3D12GraphicsCommandList7> fresh7;
			if (!CreateRecording(list->owner, list->type, allocator, fresh, fresh7, error))
			{
				return false;
			}

			if (!detail::TryPushBack(list->pool->retired,
					RetiredCommandRecording{
						.allocator		  = list->allocator,
						.list			  = list->list,
						.clearGpuHeap	  = list->clearGpuHeap,
						.clearStagingHeap = list->clearStagingHeap,
						.submitFence	  = list->submitFence,
						.submitValue	  = list->submitValue,
					}))
			{
				return Fail(error, ErrorCode::eOutOfHostMemory, "could not park a command list that is still executing");
			}

			RetiredCommandRecording & parked = list->pool->retired.back();
			parked.clearHeaps.swap(list->retiredClearHeaps);
			parked.copyScratch.swap(list->retiredCopyScratch);
			parked.copyAllocs.swap(list->retiredCopyAllocs);

			list->allocator = std::move(allocator);
			list->list		= std::move(fresh);
			list->list7		= std::move(fresh7);
			list->clearGpuHeap.Reset();
			list->clearStagingHeap.Reset();
			return true;
		}
	}

	void * D3D12CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createCommandPool");

		auto * device = static_cast<D3D12Device *>(impl);

		auto pool					= HostNew<D3D12CommandPool>();
		pool->object				= PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>();
		pool->owner					= device;
		pool->type					= MapCommandListType(desc.queueType);
		pool->queueType				= desc.queueType;
		pool->resetsIndividualLists = desc.reuse == ListReuse::ePerListReset;

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

		if (pool->handedOut < pool->lists.size())
		{
			D3D12CommandList * recycled = pool->lists[pool->handedOut];
			++pool->handedOut;
			NameD3D12Object(recycled->list.Get(), debugName, device->debugNames);
			Succeed(error);
			return recycled;
		}

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12GraphicsCommandList7> list7;
		if (!CreateRecording(device, pool->type, allocator, list, list7, error))
		{
			return nullptr;
		}

		auto cmd	   = HostNew<D3D12CommandList>();
		cmd->object	   = PublishingObject<Published<RenderCommandApi, &RenderCommandBlock>,
			Published<AliasingCommandApi, &AliasingCommandBlock>,
			Published<QueryCommandApi, &QueryCommandBlock>,
			Published<IndirectApi, &IndirectBlock>,
			Published<IndirectCountApi, &IndirectCountBlock>,
			Published<NativeEscapeApi, &NativeEscapeBlock>>();
		cmd->owner	   = device;
		cmd->list	   = std::move(list);
		cmd->list7	   = std::move(list7);
		cmd->allocator = std::move(allocator);
		cmd->pool	   = pool;
		cmd->type	   = pool->type;
		cmd->queueType = pool->queueType;

		NameD3D12Object(cmd->list.Get(), debugName, device->debugNames);

		D3D12CommandList * raw = cmd.get();
		device->commandLists.push_back(std::move(cmd));

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

		SweepRetiredRecordings(pool);
		if (!pool->retired.empty())
		{
			return Fail(error, ErrorCode::eInvalidState, kResetOfPoolWithRunningList);
		}

		// Resetting an allocator whose lists are still executing is the caller's to avoid per the ID3D12CommandAllocator::Reset docs, so refuse it here.
		for (const D3D12CommandList * list : pool->lists)
		{
			if (ListStillRunning(*list))
			{
				return Fail(error, ErrorCode::eInvalidState, kResetOfPoolWithRunningList);
			}
		}

		// ID3D12CommandAllocator::Reset fails while a list on that allocator is still recording.
		for (D3D12CommandList * list : pool->lists)
		{
			if (list->lifecycle == ListLifecycle::eRecording)
			{
				static_cast<void>(list->list->Close());
			}
		}

		// Marked before the native reset rather than after, so a reset that fails cannot leave a list looking submittable.
		for (D3D12CommandList * list : pool->lists)
		{
			list->lifecycle = ListLifecycle::eFresh;
		}

		for (D3D12CommandList * list : pool->lists)
		{
			const HRESULT hr = list->allocator->Reset();
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandAllocator::Reset failed");
			}
		}

		pool->handedOut = 0;

		return Succeed(error);
	}

	bool D3D12CommandListBegin(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);

		SweepRetiredRecordings(list->pool);
		// ExecuteCommandLists drops a list whose previous execution has not completed, so the new recording needs its own list and allocator.
		if (ListStillRunning(*list))
		{
			if (!RetireRunningRecording(list, error))
			{
				return false;
			}
		}
		else
		{
			// ID3D12GraphicsCommandList::Reset fails on a list that is not closed, and Begin over a recording discards it.
			if (list->lifecycle == ListLifecycle::eRecording)
			{
				static_cast<void>(list->list->Close());
			}

			if (list->pool->resetsIndividualLists)
			{
				const HRESULT reset = list->allocator->Reset();
				if (FAILED(reset))
				{
					return FailNative(error, reset, "ID3D12CommandAllocator::Reset failed");
				}
			}
		}

		const HRESULT hr = list->list->Reset(list->allocator.Get(), nullptr);
		if (FAILED(hr))
		{
			return FailNative(error, hr, "ID3D12GraphicsCommandList::Reset failed");
		}

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

		// Reset on the list discards whatever it held, so a prior recording is already gone here.
		list->lifecycle = ListLifecycle::eRecording;
		return Succeed(error);
	}

	bool D3D12CommandListEnd(void * impl, Error * error) noexcept
	{
		auto * list		 = static_cast<D3D12CommandList *>(impl);
		const HRESULT hr = list->list->Close();
		if (FAILED(hr))
		{
			return FailNative(error, hr, "ID3D12GraphicsCommandList::Close failed");
		}

		list->lifecycle = ListLifecycle::eEnded;
		return Succeed(error);
	}

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

}

#endif

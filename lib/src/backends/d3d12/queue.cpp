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
	bool D3D12QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.submit");

		auto * queue		 = static_cast<D3D12Queue *>(impl);
		D3D12Device * device = queue->owner;

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.acquired.IsValid())
			{
				continue;
			}
			BinarySemaphoreSlot * slot = ResolveBinarySemaphore(device, sync.acquired);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid acquire semaphore");
			}
			slot->waitValue += 1;
			const HRESULT hr = queue->queue->Wait(slot->fence.Get(), slot->waitValue);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Wait failed");
			}
		}
		for (const TimelinePoint & wait : desc.waits)
		{
			TimelineSlot * slot = ResolveTimeline(device, wait.timeline);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid timeline");
			}
			const HRESULT hr = queue->queue->Wait(slot->fence.Get(), wait.value);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Wait failed");
			}
		}

		detail::HostVector<ID3D12CommandList *> lists;
		lists.reserve(desc.commandLists.size());
		for (const CommandList * list : desc.commandLists)
		{
			lists.push_back(static_cast<D3D12CommandList *>(detail::UnwrappedImplOf(*list))->list.Get());
		}
		if (!lists.empty())
		{
			queue->queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
		}

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.renderFinished.IsValid())
			{
				continue;
			}
			BinarySemaphoreSlot * slot = ResolveBinarySemaphore(device, sync.renderFinished);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid present semaphore");
			}
			slot->signalValue += 1;
			const HRESULT hr = queue->queue->Signal(slot->fence.Get(), slot->signalValue);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Signal failed");
			}
		}
		for (const TimelinePoint & signal : desc.signals)
		{
			TimelineSlot * slot = ResolveTimeline(device, signal.timeline);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid timeline");
			}
			const HRESULT hr = queue->queue->Signal(slot->fence.Get(), signal.value);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Signal failed");
			}
		}

		return Succeed(error);
	}

	bool BindSparseBuffer(D3D12Device * device, D3D12Queue * queue, const SparseBufferBind & bind, Error * error) noexcept
	{
		const bool unbind = !bind.page.heap.IsValid();

		if ((bind.resourceOffset % kD3D12TileSizeBytes) != 0 || (bind.page.size % kD3D12TileSizeBytes) != 0)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse buffer bind offset and size must be multiples of the 64 KiB tile size");
		}
		if (!unbind && (bind.page.heapOffset % kD3D12TileSizeBytes) != 0)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse buffer bind heap offset must be a multiple of the 64 KiB tile size");
		}

		ComPtr<ID3D12Resource> resource;
		{
			BufferSlot * slot = ResolveBuffer(device, bind.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid buffer handle");
			}
			if (!slot->reserved)
			{
				return Fail(error, ErrorCode::eValidationFailed, "sparse bind targets a buffer not created with allowSparseBinding");
			}
			resource = slot->resource;
		}

		ComPtr<ID3D12Heap> heapRef;
		if (!unbind)
		{
			HeapSlot * heapSlot = ResolveHeap(device, bind.page.heap);
			if (heapSlot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse buffer bind references an invalid heap");
			}
			heapRef = heapSlot->heap;
		}

		const UINT numTiles = static_cast<UINT>(bind.page.size / kD3D12TileSizeBytes);
		if (numTiles == 0)
		{
			return Succeed(error);
		}

		const D3D12_TILED_RESOURCE_COORDINATE coord{
			.X			 = static_cast<UINT>(bind.resourceOffset / kD3D12TileSizeBytes),
			.Y			 = 0,
			.Z			 = 0,
			.Subresource = 0,
		};
		D3D12_TILE_REGION_SIZE region{};
		region.NumTiles = numTiles;
		region.UseBox	= FALSE;

		const D3D12_TILE_RANGE_FLAGS rangeFlag = unbind ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
		const UINT heapTileOffset			   = unbind ? 0u : static_cast<UINT>(bind.page.heapOffset / kD3D12TileSizeBytes);
		UINT rangeTileCount					   = numTiles;

		queue->queue->UpdateTileMappings(
			resource.Get(), 1, &coord, &region, heapRef.Get(), 1, &rangeFlag, &heapTileOffset, &rangeTileCount, D3D12_TILE_MAPPING_FLAG_NONE);
		return Succeed(error);
	}

	bool BindSparseTexture(D3D12Device * device, D3D12Queue * queue, const SparseTextureBind & bind, Error * error) noexcept
	{
		const bool unbind = !bind.page.heap.IsValid();

		ComPtr<ID3D12Resource> resource;
		std::uint32_t mipLevels	  = 1;
		std::uint32_t arrayLayers = 1;
		{
			TextureSlot * slot = ResolveTexture(device, bind.texture);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid texture handle");
			}
			if (!slot->reserved)
			{
				return Fail(error, ErrorCode::eValidationFailed, "sparse bind targets a texture not created with allowSparseBinding");
			}
			resource	= slot->resource;
			mipLevels	= slot->mipLevels;
			arrayLayers = slot->arrayLayers;
		}

		D3D12_PACKED_MIP_INFO packedMip{};
		D3D12_TILE_SHAPE tileShape{};
		UINT numSubresourceTilings = 0;
		device->device->GetResourceTiling(resource.Get(), nullptr, &packedMip, &tileShape, &numSubresourceTilings, 0, nullptr);

		if (bind.subresource.mip >= mipLevels)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse texture bind names a mip past the texture mip count");
		}
		if (bind.subresource.layer >= arrayLayers)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse texture bind names a layer past the texture array size");
		}
		if (bind.subresource.mip >= packedMip.NumStandardMips)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse texture bind targets a packed mip, which cannot be tile-mapped individually");
		}
		if (bind.offset.x < 0 || bind.offset.y < 0 || bind.offset.z < 0)
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse texture bind offset must be non-negative");
		}
		if (tileShape.WidthInTexels != 0 && ((static_cast<std::uint32_t>(bind.offset.x) % tileShape.WidthInTexels) != 0 ||
												(static_cast<std::uint32_t>(bind.offset.y) % tileShape.HeightInTexels) != 0 ||
												(static_cast<std::uint32_t>(bind.offset.z) % tileShape.DepthInTexels) != 0))
		{
			return Fail(error, ErrorCode::eValidationFailed, "sparse texture bind offset must be tile aligned in texels");
		}

		if (tileShape.WidthInTexels == 0 || tileShape.HeightInTexels == 0 || tileShape.DepthInTexels == 0)
		{
			return Fail(error, ErrorCode::eNativeApiError, "GetResourceTiling returned no standard tile shape for a sparse texture");
		}

		ComPtr<ID3D12Heap> heapRef;
		if (!unbind)
		{
			HeapSlot * heapSlot = ResolveHeap(device, bind.page.heap);
			if (heapSlot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse texture bind references an invalid heap");
			}
			heapRef = heapSlot->heap;
		}

		const UINT subresource = static_cast<UINT>(bind.subresource.mip + bind.subresource.layer * mipLevels);

		const auto tilesFor = [](std::uint32_t texels, UINT tileTexels) noexcept -> UINT
		{
			return static_cast<UINT>((texels + tileTexels - 1) / tileTexels);
		};

		D3D12_TILE_REGION_SIZE region{};
		region.UseBox	= TRUE;
		region.Width	= tilesFor(bind.extent.width, tileShape.WidthInTexels);
		region.Height	= static_cast<UINT16>(tilesFor(bind.extent.height, tileShape.HeightInTexels));
		region.Depth	= static_cast<UINT16>(tilesFor(bind.extent.depth, tileShape.DepthInTexels));
		region.NumTiles = region.Width * region.Height * region.Depth;
		if (region.NumTiles == 0)
		{
			return Succeed(error);
		}

		const D3D12_TILED_RESOURCE_COORDINATE coord{ .X = static_cast<UINT>(bind.offset.x) / tileShape.WidthInTexels,
			.Y											= static_cast<UINT>(bind.offset.y) / tileShape.HeightInTexels,
			.Z											= static_cast<UINT>(bind.offset.z) / tileShape.DepthInTexels,
			.Subresource								= subresource };

		const D3D12_TILE_RANGE_FLAGS rangeFlag = unbind ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
		const UINT heapTileOffset			   = unbind ? 0u : static_cast<UINT>(bind.page.heapOffset / kD3D12TileSizeBytes);
		UINT rangeTileCount					   = region.NumTiles;

		queue->queue->UpdateTileMappings(
			resource.Get(), 1, &coord, &region, heapRef.Get(), 1, &rangeFlag, &heapTileOffset, &rangeTileCount, D3D12_TILE_MAPPING_FLAG_NONE);
		return Succeed(error);
	}

	bool D3D12QueueBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
	{
		auto * queue		 = static_cast<D3D12Queue *>(impl);
		D3D12Device * device = queue->owner;

		for (const TimelinePoint & wait : desc.timelineWaits)
		{
			TimelineSlot * slot = ResolveTimeline(device, wait.timeline);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind waits on an invalid timeline");
			}
			const HRESULT hr = queue->queue->Wait(slot->fence.Get(), wait.value);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Wait failed for a sparse bind");
			}
		}

		for (const SparseBufferBind & bind : desc.buffers)
		{
			if (!BindSparseBuffer(device, queue, bind, error))
			{
				return false;
			}
		}

		for (const SparseTextureBind & bind : desc.textures)
		{
			if (!BindSparseTexture(device, queue, bind, error))
			{
				return false;
			}
		}

		for (const TimelinePoint & signal : desc.timelineSignals)
		{
			TimelineSlot * slot = ResolveTimeline(device, signal.timeline);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind signals an invalid timeline");
			}
			const HRESULT hr = queue->queue->Signal(slot->fence.Get(), signal.value);
			if (FAILED(hr))
			{
				return FailNative(error, hr, "ID3D12CommandQueue::Signal failed for a sparse bind");
			}
		}

		return Succeed(error);
	}

	bool D3D12QueueWaitIdle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.waitIdle");

		auto * queue = static_cast<D3D12Queue *>(impl);
		if (!queue->idleFence)
		{
			return Fail(error, ErrorCode::eNativeApiError, "queue has no idle fence");
		}

		queue->idleValue += 1;
		const HRESULT hr = queue->queue->Signal(queue->idleFence.Get(), queue->idleValue);
		if (FAILED(hr))
		{
			return FailNative(error, hr, "ID3D12CommandQueue::Signal failed waiting for idle");
		}
		if (WaitFenceHost(queue->idleFence.Get(), queue->idleValue, std::numeric_limits<std::uint64_t>::max()) != WAIT_OBJECT_0)
		{
			return Fail(error, ErrorCode::eNativeApiError, "waiting for the queue to idle failed");
		}
		return Succeed(error);
	}

	bool D3D12QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}
		*out				 = 0;
		D3D12Device * device = static_cast<D3D12Queue *>(impl)->owner;
		TimelineSlot * slot	 = ResolveTimeline(device, timeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getCompletedValue on an invalid timeline");
		}

		*out = slot->fence->GetCompletedValue();
		return Succeed(error);
	}

	bool D3D12QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		D3D12Device * device = static_cast<D3D12Queue *>(impl)->owner;
		TimelineSlot * slot	 = ResolveTimeline(device, timeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "wait on an invalid timeline");
		}

		const DWORD result = WaitFenceHost(slot->fence.Get(), value, timeoutNanoseconds);
		if (result == WAIT_TIMEOUT)
		{
			return Fail(error, ErrorCode::eTimeout, "timeline wait timed out");
		}
		if (result != WAIT_OBJECT_0)
		{
			return Fail(error, ErrorCode::eNativeApiError, "waiting on a timeline failed");
		}
		return Succeed(error);
	}

	bool D3D12QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		D3D12Device * device = static_cast<D3D12Queue *>(impl)->owner;
		TimelineSlot * slot	 = ResolveTimeline(device, timeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "signal on an invalid timeline");
		}

		const HRESULT hr = slot->fence->Signal(value);
		if (FAILED(hr))
		{
			return FailNative(error, hr, "ID3D12Fence::Signal failed");
		}
		return Succeed(error);
	}

}

#endif

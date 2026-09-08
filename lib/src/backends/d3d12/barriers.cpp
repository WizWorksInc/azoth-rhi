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

	#include <new>

namespace azo::rhi::d3d12
{
	constexpr UINT kAllSubresources = 0xffffffffu;

	[[nodiscard]] TextureViewSlot * ResolveTextureView(D3D12Device * device, TextureViewHandle handle) noexcept
	{
		return device->textureViewSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] QueryPoolSlot * ResolveQueryPool(D3D12Device * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPoolSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] D3D12_QUERY_TYPE MapQueryType(QueryType type) noexcept
	{
		switch (type)
		{
		case QueryType::eOcclusion:			 return D3D12_QUERY_TYPE_OCCLUSION;
		case QueryType::ePipelineStatistics: return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
		case QueryType::eTimestamp:			 return D3D12_QUERY_TYPE_TIMESTAMP;
		}
		return D3D12_QUERY_TYPE_TIMESTAMP;
	}

	[[nodiscard]] D3D12_QUERY_HEAP_TYPE MapQueryHeapType(QueryType type) noexcept
	{
		switch (type)
		{
		case QueryType::eOcclusion:			 return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
		case QueryType::ePipelineStatistics: return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
		case QueryType::eTimestamp:			 return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		}
		return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	}

	[[nodiscard]] UINT SubresourceIndex(const TextureSubresource & sub, std::uint32_t mipLevels) noexcept
	{
		return sub.mip + sub.layer * mipLevels;
	}

	[[nodiscard]] ID3D12CommandSignature * GetCommandSignature(
		D3D12CommandList * list, D3D12_INDIRECT_ARGUMENT_TYPE type, std::uint32_t stride, Error * error) noexcept
	{
		D3D12CommandPool * pool = list->pool;
		if (pool == nullptr)
		{
			Fail(error, ErrorCode::eInvalidState, "an indirect command signature needs the command list's pool, which is gone");
			return nullptr;
		}

		for (const CommandSignatureEntry & entry : pool->commandSignatures)
		{
			if (entry.type == type && entry.stride == stride)
			{
				return entry.signature.Get();
			}
		}

		D3D12_INDIRECT_ARGUMENT_DESC arg{};
		arg.Type = type;
		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.ByteStride		  = stride;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs	  = &arg;

		ComPtr<ID3D12CommandSignature> signature;
		const HRESULT hr = pool->owner->device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(signature.GetAddressOf()));
		if (FAILED(hr))
		{
			FailNative(error, hr, "ID3D12Device::CreateCommandSignature failed");
			return nullptr;
		}

		if (!detail::TryPushBack(pool->commandSignatures,
				CommandSignatureEntry{
					.type	   = type,
					.stride	   = stride,
					.signature = signature,
				}))
		{
			Fail(error, ErrorCode::eOutOfHostMemory, "the command pool could not store another indirect command signature");
			return nullptr;
		}

		return signature.Get();
	}

	QueryPoolHandle D3D12CreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createQueryPool");

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.queryCount == 0)
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
		}

		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Type  = MapQueryHeapType(desc.type);
		heapDesc.Count = desc.queryCount;
		ComPtr<ID3D12QueryHeap> heap;
		const HRESULT hr = device->device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValueNative<QueryPoolHandle>(error, hr, "ID3D12Device::CreateQueryHeap failed");
		}

		ComPtr<ID3D12QueryHeap> copyHeap;
		if (desc.type == QueryType::eTimestamp && device->copyQueueTimestamps)
		{
			D3D12_QUERY_HEAP_DESC copyDesc{};
			copyDesc.Type		 = D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP;
			copyDesc.Count		 = desc.queryCount;
			const HRESULT copyHr = device->device->CreateQueryHeap(&copyDesc, IID_PPV_ARGS(copyHeap.GetAddressOf()));
			if (FAILED(copyHr))
			{
				return FailValueNative<QueryPoolHandle>(error, copyHr, "ID3D12Device::CreateQueryHeap failed for the copy queue timestamp heap");
			}
		}

		return ReturnValue(device->queryPoolSlots.Store(QueryPoolSlot{
							   .heap	   = std::move(heap),
							   .copyHeap   = std::move(copyHeap),
							   .type	   = desc.type,
							   .queryCount = desc.queryCount,
						   }),
			error);
	}

	bool D3D12DestroyQueryPool(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const QueryPoolHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		QueryPoolSlot * slot = device->queryPoolSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid query pool handle");
		}

		slot->heap.Reset();
		slot->copyHeap.Reset();
		static_cast<void>(device->queryPoolSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	bool D3D12CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		const QueueType queue = list->queueType;

		detail::HostVector<D3D12_GLOBAL_BARRIER> globals;
		detail::HostVector<D3D12_BUFFER_BARRIER> buffers;
		detail::HostVector<D3D12_TEXTURE_BARRIER> textures;
		globals.reserve(barriers.memory.size());
		buffers.reserve(barriers.buffers.size());
		textures.reserve(barriers.textures.size());

		for (const auto & m : barriers.memory)
		{
			globals.push_back(D3D12_GLOBAL_BARRIER{
				.SyncBefore	  = MapBarrierSync(m.before.stages, m.before.use, queue),
				.SyncAfter	  = MapBarrierSync(m.after.stages, m.after.use, queue),
				.AccessBefore = MapBarrierAccess(m.before.use, queue),
				.AccessAfter  = MapBarrierAccess(m.after.use, queue),
			});
		}

		for (const BufferBarrier & b : barriers.buffers)
		{
			// The slot outlives what it held, and a barrier needs the resource pointer itself, so a destroyed buffer has to be caught here rather than handed on
			// as a null the runtime refuses.
			BufferSlot * slot = ResolveBuffer(device, b.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "buffer barrier with an invalid buffer handle");
			}

			const D3D12_BARRIER_ACCESS accessBefore = ClampAccessToHeap(MapBarrierAccess(b.before.use, queue), slot->heapType);
			const D3D12_BARRIER_ACCESS accessAfter	= ClampAccessToHeap(MapBarrierAccess(b.after.use, queue), slot->heapType);

			if (slot->desc.usage.Contains(BufferUsage::eAccelerationStructureStorage) &&
				(!AccessLegalOnAccelerationStructure(accessBefore) || !AccessLegalOnAccelerationStructure(accessAfter)))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "a barrier on an acceleration structure buffer named a use it can never be in");
			}

			buffers.push_back(D3D12_BUFFER_BARRIER{
				.SyncBefore	  = MapBarrierSync(b.before.stages, b.before.use, queue),
				.SyncAfter	  = MapBarrierSync(b.after.stages, b.after.use, queue),
				.AccessBefore = accessBefore,
				.AccessAfter  = accessAfter,
				.pResource	  = slot->resource.Get(),
				.Offset		  = 0,
				.Size		  = std::numeric_limits<UINT64>::max(),
			});
		}

		for (const TextureBarrier & t : barriers.textures)
		{
			TextureSlot * slot = ResolveTexture(device, t.texture);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "texture barrier with an invalid texture handle");
			}

			const detail::ResolvedSubresourceRange range = detail::ResolveSubresourceRange(t.range, slot->mipLevels, slot->arrayLayers);
			if (range.IsEmpty())
			{
				continue;
			}

			const bool whole = detail::CoversWholeTexture(range, slot->mipLevels, slot->arrayLayers);

			D3D12_BARRIER_SUBRESOURCE_RANGE subresources{ .IndexOrFirstMipLevel = kAllSubresources };
			if (!whole)
			{
				subresources = D3D12_BARRIER_SUBRESOURCE_RANGE{
					.IndexOrFirstMipLevel = range.baseMip,
					.NumMipLevels		  = range.mipCount,
					.FirstArraySlice	  = range.baseLayer,
					.NumArraySlices		  = range.layerCount,
					.FirstPlane			  = 0,
					.NumPlanes			  = 1,
				};
			}

			// A shared texture crosses in the common layout, so claiming the caller's would assert a transition this device never made.
			const bool crossesIn  = t.ownership.op == OwnershipOp::eAcquireFromExternal;
			const bool crossesOut = t.ownership.op == OwnershipOp::eReleaseToExternal;

			const D3D12_BARRIER_LAYOUT before = crossesIn ? D3D12_BARRIER_LAYOUT_COMMON : MapBarrierLayout(t.before.use, queue);
			const D3D12_BARRIER_LAYOUT after  = crossesOut ? D3D12_BARRIER_LAYOUT_COMMON : MapBarrierLayout(t.after.use, queue);
			textures.push_back(D3D12_TEXTURE_BARRIER{
				.SyncBefore	  = MapBarrierSync(t.before.stages, t.before.use, queue),
				.SyncAfter	  = MapBarrierSync(t.after.stages, t.after.use, queue),
				.AccessBefore = crossesIn ? D3D12_BARRIER_ACCESS_COMMON : MapBarrierAccess(t.before.use, queue),
				.AccessAfter  = crossesOut ? D3D12_BARRIER_ACCESS_COMMON : MapBarrierAccess(t.after.use, queue),
				.LayoutBefore = before,
				.LayoutAfter  = after,
				.pResource	  = slot->resource.Get(),
				.Subresources = subresources,
				.Flags		  = TextureBarrierFlags(before),
			});
		}

		std::array<D3D12_BARRIER_GROUP, 3> groups{};
		UINT32 groupCount = 0;
		if (!globals.empty())
		{
			groups[groupCount].Type			   = D3D12_BARRIER_TYPE_GLOBAL;
			groups[groupCount].NumBarriers	   = static_cast<UINT32>(globals.size());
			groups[groupCount].pGlobalBarriers = globals.data();
			++groupCount;
		}
		if (!buffers.empty())
		{
			groups[groupCount].Type			   = D3D12_BARRIER_TYPE_BUFFER;
			groups[groupCount].NumBarriers	   = static_cast<UINT32>(buffers.size());
			groups[groupCount].pBufferBarriers = buffers.data();
			++groupCount;
		}
		if (!textures.empty())
		{
			groups[groupCount].Type				= D3D12_BARRIER_TYPE_TEXTURE;
			groups[groupCount].NumBarriers		= static_cast<UINT32>(textures.size());
			groups[groupCount].pTextureBarriers = textures.data();
			++groupCount;
		}

		if (groupCount != 0)
		{
			list->list7->Barrier(groupCount, groups.data());
		}
		return Succeed(error);
	}

	bool D3D12CmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		const QueueType queue = list->queueType;

		if (barriers.empty())
		{
			return Succeed(error);
		}

		detail::HostVector<D3D12_TEXTURE_BARRIER> textures;
		textures.reserve(barriers.size());

		// Resolving alone answers whether the slot exists, not whether it still holds anything, and a destroyed resource keeps its slot. Every one of these four
		// has to ask the second question too or an alias barrier naming a destroyed resource is taken in every mode that has no validation layer above it.
		const auto liveBuffer = [device](const BufferHandle handle)
		{
			return !handle.IsValid() || ResolveBuffer(device, handle) != nullptr;
		};

		const auto liveTexture = [device](const TextureHandle handle)
		{
			return !handle.IsValid() || ResolveTexture(device, handle) != nullptr;
		};

		for (const AliasBarrier & alias : barriers)
		{
			if (!liveBuffer(alias.beforeBuffer) || !liveBuffer(alias.afterBuffer))
			{
				return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid buffer handle");
			}
			if (!liveTexture(alias.beforeTexture) || !liveTexture(alias.afterTexture))
			{
				return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid texture handle");
			}

			if (!alias.afterTexture.IsValid())
			{
				continue;
			}

			TextureSlot * slot = ResolveTexture(device, alias.afterTexture);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid texture handle");
			}

			const D3D12_BARRIER_LAYOUT before = MapBarrierLayout(ResourceUse::eDiscard, queue);
			textures.push_back(D3D12_TEXTURE_BARRIER{
				.SyncBefore	  = D3D12_BARRIER_SYNC_ALL,
				.SyncAfter	  = D3D12_BARRIER_SYNC_ALL,
				.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
				.AccessAfter  = D3D12_BARRIER_ACCESS_COMMON,
				.LayoutBefore = before,
				.LayoutAfter  = MapBarrierLayout(ResourceUse::eNone, queue),
				.pResource	  = slot->resource.Get(),
				.Subresources = D3D12_BARRIER_SUBRESOURCE_RANGE{ .IndexOrFirstMipLevel = kAllSubresources },
				.Flags		  = TextureBarrierFlags(before),
			});
		}

		const D3D12_GLOBAL_BARRIER global{
			.SyncBefore	  = D3D12_BARRIER_SYNC_ALL,
			.SyncAfter	  = D3D12_BARRIER_SYNC_ALL,
			.AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
			.AccessAfter  = D3D12_BARRIER_ACCESS_COMMON,
		};

		std::array<D3D12_BARRIER_GROUP, 2> groups{};
		UINT32 groupCount				   = 0;
		groups[groupCount].Type			   = D3D12_BARRIER_TYPE_GLOBAL;
		groups[groupCount].NumBarriers	   = 1;
		groups[groupCount].pGlobalBarriers = &global;
		++groupCount;
		if (!textures.empty())
		{
			groups[groupCount].Type				= D3D12_BARRIER_TYPE_TEXTURE;
			groups[groupCount].NumBarriers		= static_cast<UINT32>(textures.size());
			groups[groupCount].pTextureBarriers = textures.data();
			++groupCount;
		}

		list->list7->Barrier(groupCount, groups.data());
		return Succeed(error);
	}

}

#endif

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

	#include <new>

namespace azo::rhi::d3d12
{
	// Subresource index selecting every one of them, read as an index only when the range asks for no mip levels.
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

	// The D3D12 subresource index for a (mip, layer) pair within a texture of mipLevels mips.
	[[nodiscard]] UINT SubresourceIndex(const TextureSubresource & sub, std::uint32_t mipLevels) noexcept
	{
		return sub.mip + sub.layer * mipLevels;
	}

	/*
	 * Finds or creates a command signature per argument type and stride, cached on the list's pool so ExecuteIndirect does not rebuild it.
	 *
	 * On the pool and not the device because this is reached from recording, which takes no guard in any threading mode. A pool is recorded into by one thread at
	 * a time so the cache needs no lock of its own.
	 */
	[[nodiscard]] ID3D12CommandSignature * GetCommandSignature(D3D12CommandList * list, D3D12_INDIRECT_ARGUMENT_TYPE type, std::uint32_t stride) noexcept
	{
		D3D12CommandPool * pool = list->pool;
		if (pool == nullptr)
		{
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
		if (FAILED(pool->owner->device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(signature.GetAddressOf()))))
		{
			return nullptr;
		}

		// The cache entry is what holds the signature alive: the local ComPtr releases it on the way out, so a cache that could not grow has no signature to hand
		// back and not one the caller would record against after it was freed.
		if (!detail::TryPushBack(pool->commandSignatures,
				CommandSignatureEntry{
					.type	   = type,
					.stride	   = stride,
					.signature = signature,
				}))
		{
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
		if (FAILED(device->device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateQueryHeap failed");
		}

		return ReturnValue(device->queryPoolSlots.Store(QueryPoolSlot{
							   .heap	   = std::move(heap),
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
		static_cast<void>(device->queryPoolSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	// Every recorded barrier becomes a native one. Naming the same state on both sides is an ordering barrier here, not something to drop.
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

		// Deduced, not named: winnt.h takes MemoryBarrier for its own macro, so spelling the type here expands to a store fence intrinsic.
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
			BufferSlot * slot = ResolveBuffer(device, b.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "buffer barrier with an invalid buffer handle");
			}

			const D3D12_BARRIER_ACCESS accessBefore = ClampAccessToHeap(MapBarrierAccess(b.before.use, queue), slot->heapType);
			const D3D12_BARRIER_ACCESS accessAfter	= ClampAccessToHeap(MapBarrierAccess(b.after.use, queue), slot->heapType);

			// Refused rather than clamped, since recording one costs the device and a wrong use should be heard about.
			if (slot->desc.usage.Contains(BufferUsage::eAccelerationStructureStorage) &&
				(!AccessLegalOnAccelerationStructure(accessBefore) || !AccessLegalOnAccelerationStructure(accessAfter)))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "a barrier on an acceleration structure buffer named a use it can never be in");
			}

			// offset and size are not read: a buffer barrier covers the whole resource, and the API takes only zero and the whole size.
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

			// Leaving the mip count at zero reads the first field as a subresource index, and that index means every subresource.
			D3D12_BARRIER_SUBRESOURCE_RANGE subresources{ .IndexOrFirstMipLevel = kAllSubresources };
			if (!whole)
			{
				// One plane, as the legacy path indexed. A partial range over a depth-stencil texture leaves the stencil plane alone.
				subresources = D3D12_BARRIER_SUBRESOURCE_RANGE{
					.IndexOrFirstMipLevel = range.baseMip,
					.NumMipLevels		  = range.mipCount,
					.FirstArraySlice	  = range.baseLayer,
					.NumArraySlices		  = range.layerCount,
					.FirstPlane			  = 0,
					.NumPlanes			  = 1,
				};
			}

			const D3D12_BARRIER_LAYOUT before = MapBarrierLayout(t.before.use, queue);
			textures.push_back(D3D12_TEXTURE_BARRIER{
				.SyncBefore	  = MapBarrierSync(t.before.stages, t.before.use, queue),
				.SyncAfter	  = MapBarrierSync(t.after.stages, t.after.use, queue),
				.AccessBefore = MapBarrierAccess(t.before.use, queue),
				.AccessAfter  = MapBarrierAccess(t.after.use, queue),
				.LayoutBefore = before,
				.LayoutAfter  = MapBarrierLayout(t.after.use, queue),
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

	// Nothing in the enhanced model answers the legacy aliasing barrier, so the batch is built from the two things that barrier stood for.
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

		for (const AliasBarrier & alias : barriers)
		{
			// The outgoing half raises no barrier of its own, the global one below having flushed it. Resolved regardless so a stale handle is still refused.
			if (alias.beforeBuffer.IsValid())
			{
				if (ResolveBuffer(device, alias.beforeBuffer) == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid buffer handle");
				}
			}
			else if (alias.beforeTexture.IsValid())
			{
				if (ResolveTexture(device, alias.beforeTexture) == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid texture handle");
				}
			}

			// An incoming buffer has no layout and no metadata, so the global barrier is all of it. An absent discard flag is D3D12's asymmetry, not a gap.
			if (alias.afterBuffer.IsValid())
			{
				if (ResolveBuffer(device, alias.afterBuffer) == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "alias barrier with an invalid buffer handle");
				}
				continue;
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

			// Both layouts come from the table, so the copy queue's answer stays in one place.
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

		// One for the batch, not per pair: an alias barrier orders heap memory, which no resource over it names.
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

} // namespace azo::rhi::d3d12

#endif // _WIN32

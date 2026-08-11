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
	[[nodiscard]] TextureViewSlot * ResolveTextureView(D3D12Device * device, TextureViewHandle handle) noexcept
	{
		return device->textureViewSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] QueryPoolSlot * ResolveQueryPool(D3D12Device * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPoolSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	// Maps a texture layout onto the D3D12 states a barrier transitions to. ePresent is the common state, as the swapchain requires.
	[[nodiscard]] D3D12_RESOURCE_STATES MapTextureStates(TextureLayout layout) noexcept
	{
		switch (layout)
		{
		case TextureLayout::eUndefined:				 return D3D12_RESOURCE_STATE_COMMON;
		case TextureLayout::eGeneral:				 return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case TextureLayout::eColorAttachment:		 return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case TextureLayout::eDepthStencilAttachment: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case TextureLayout::eDepthStencilReadOnly:	 return D3D12_RESOURCE_STATE_DEPTH_READ;
		case TextureLayout::eShaderReadOnly:		 return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case TextureLayout::eCopySrc:				 return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case TextureLayout::eCopyDst:				 return D3D12_RESOURCE_STATE_COPY_DEST;
		case TextureLayout::eResolveSrc:			 return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		case TextureLayout::eResolveDst:			 return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		case TextureLayout::ePresent:				 return D3D12_RESOURCE_STATE_PRESENT;
		}
		return D3D12_RESOURCE_STATE_COMMON;
	}

	// A write access resolves to UNORDERED_ACCESS, which cannot combine with read states so reads fold in only when no write was asked for.
	[[nodiscard]] D3D12_RESOURCE_STATES MapBufferStates(Flags<Access> access) noexcept
	{
		if (access.Contains(Access::eShaderWrite))
		{
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}

		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;
		if (access.Contains(Access::eIndirectRead))
		{
			states |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		}
		if (access.Contains(Access::eVertexRead) || access.Contains(Access::eConstantRead))
		{
			states |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
		}
		if (access.Contains(Access::eShaderRead))
		{
			states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}
		if (access.Contains(Access::eCopyRead))
		{
			states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (access.Contains(Access::eCopyWrite))
		{
			states |= D3D12_RESOURCE_STATE_COPY_DEST;
		}
		return states;
	}

	[[nodiscard]] D3D12_BARRIER_SYNC DeriveBarrierSync(Flags<ResourceUse> use) noexcept
	{
		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (use.Contains(ResourceUse::eVertexBuffer) || use.Contains(ResourceUse::eIndexBuffer))
		{
			sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (use.Contains(ResourceUse::eUniformRead) || use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead) ||
			use.Contains(ResourceUse::eStorageWrite))
		{
			sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget) || use.Contains(ResourceUse::eDepthStencilRead))
		{
			sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eCopyDst))
		{
			sync |= D3D12_BARRIER_SYNC_COPY;
		}
		if (use.Contains(ResourceUse::eResolveSrc) || use.Contains(ResourceUse::eResolveDst))
		{
			sync |= D3D12_BARRIER_SYNC_RESOLVE;
		}
		if (use.Contains(ResourceUse::eAccelBuildInput) || use.Contains(ResourceUse::eAccelWrite))
		{
			sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
		}
		if (use.Contains(ResourceUse::eAccelRead))
		{
			sync |= D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
		}

		return sync == D3D12_BARRIER_SYNC_NONE ? D3D12_BARRIER_SYNC_ALL : sync;
	}

	[[nodiscard]] D3D12_BARRIER_SYNC MapBarrierSync(Flags<Stage> stages, Flags<ResourceUse> use) noexcept
	{
		if (stages.Empty())
		{
			return DeriveBarrierSync(use);
		}

		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;

		if (stages.Contains(Stage::eIndirectFetch))
		{
			sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (stages.Contains(Stage::eVertexWork))
		{
			sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (stages.Contains(Stage::eFragmentShading))
		{
			sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
		}
		if (stages.Contains(Stage::eDepthStencil))
		{
			sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		if (stages.Contains(Stage::eColorOutput))
		{
			sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (stages.Contains(Stage::eCompute))
		{
			sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		}
		if (stages.Contains(Stage::eCopy))
		{
			sync |= D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
		}
		if (stages.Contains(Stage::eResolve))
		{
			sync |= D3D12_BARRIER_SYNC_RESOLVE;
		}
		if (stages.Contains(Stage::eRayTracing))
		{
			sync |= D3D12_BARRIER_SYNC_RAYTRACING;
		}
		if (stages.Contains(Stage::eAccelBuild))
		{
			sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
					D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
		}
		if (stages.Contains(Stage::eAllGraphics))
		{
			sync |= D3D12_BARRIER_SYNC_DRAW;
		}
		if (stages.Contains(Stage::eAllCommands))
		{
			sync |= D3D12_BARRIER_SYNC_ALL;
		}

		return sync == D3D12_BARRIER_SYNC_NONE ? DeriveBarrierSync(use) : sync;
	}

	[[nodiscard]] D3D12_BARRIER_ACCESS MapBarrierAccess(Flags<ResourceUse> use) noexcept
	{
		if (use.Contains(ResourceUse::eDiscard))
		{
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}

		D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
		}
		if (use.Contains(ResourceUse::eVertexBuffer))
		{
			access |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		}
		if (use.Contains(ResourceUse::eIndexBuffer))
		{
			access |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		}
		if (use.Contains(ResourceUse::eUniformRead))
		{
			access |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			access |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			access |= D3D12_BARRIER_ACCESS_COPY_DEST;
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			access |= D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			access |= D3D12_BARRIER_ACCESS_RESOLVE_DEST;
		}
		// Build inputs are ordinary geometry buffers read as shader resources. The acceleration structure access below is for reading a built structure.
		if (use.Contains(ResourceUse::eAccelBuildInput))
		{
			access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (use.Contains(ResourceUse::eAccelRead))
		{
			access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
		}
		if (use.Contains(ResourceUse::eAccelWrite))
		{
			access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
		}

		return access;
	}

	[[nodiscard]] D3D12_BARRIER_LAYOUT MapBarrierLayout(Flags<ResourceUse> use, QueueType queue) noexcept
	{
		if (use.Contains(ResourceUse::eDiscard))
		{
			return D3D12_BARRIER_LAYOUT_UNDEFINED;
		}
		if (queue == QueueType::eCopy)
		{
			return D3D12_BARRIER_LAYOUT_COMMON;
		}
		if (use.Contains(ResourceUse::ePresent))
		{
			return D3D12_BARRIER_LAYOUT_PRESENT;
		}
		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead) && use.Contains(ResourceUse::eSampledRead))
		{
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		}

		D3D12_BARRIER_LAYOUT chosen = D3D12_BARRIER_LAYOUT_COMMON;
		int distinct				= 0;
		bool write					= false;

		if (use.Contains(ResourceUse::eColorTarget))
		{
			chosen = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			chosen = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			chosen = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
			++distinct;
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			chosen = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			chosen = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			chosen = D3D12_BARRIER_LAYOUT_COPY_DEST;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			chosen = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			chosen = D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
			write  = true;
			++distinct;
		}

		if (distinct <= 1)
		{
			return chosen;
		}

		// GENERIC_READ is read only, so a combination naming a write answers COMMON, which is legal against any access. Validation rejects the pairing upstream.
		return write ? D3D12_BARRIER_LAYOUT_COMMON : D3D12_BARRIER_LAYOUT_GENERIC_READ;
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

	bool D3D12CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;

		detail::HostVector<D3D12_RESOURCE_BARRIER> native;
		native.reserve(barriers.memory.size() + barriers.buffers.size() + barriers.textures.size());

		// A memory barrier lowers to a global UAV barrier, which orders read/write hazards on unordered access.
		for (std::size_t i = 0; i < barriers.memory.size(); ++i)
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type		  = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = nullptr;
			native.push_back(barrier);
		}

		for (const BufferBarrier & b : barriers.buffers)
		{
			BufferSlot * slot = ResolveBuffer(device, b.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "buffer barrier with an invalid buffer handle");
			}
			const D3D12_RESOURCE_STATES before = MapBufferStates(ExpandAccess(b.before.use));
			const D3D12_RESOURCE_STATES after  = MapBufferStates(ExpandAccess(b.after.use));
			if (before == after)
			{
				// A same state transition is a no-op except in UNORDERED_ACCESS, where a UAV barrier still orders the hazard: the legacy state model cannot tell
				// shader-read from shader-write so dropping it loses what Vulkan catches through access masks.
				if (before == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				{
					D3D12_RESOURCE_BARRIER uavBarrier{};
					uavBarrier.Type			 = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					uavBarrier.UAV.pResource = slot->resource.Get();
					native.push_back(uavBarrier);
				}
				continue;
			}
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = slot->resource.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = before;
			barrier.Transition.StateAfter  = after;
			native.push_back(barrier);
		}

		for (const TextureBarrier & t : barriers.textures)
		{
			TextureSlot * slot = ResolveTexture(device, t.texture);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "texture barrier with an invalid texture handle");
			}
			const D3D12_RESOURCE_STATES before = MapTextureStates(ExpandLayout(t.before.use));
			const D3D12_RESOURCE_STATES after  = MapTextureStates(ExpandLayout(t.after.use));
			if (before == after)
			{
				// Same as above for layouts: a storage image written then read across passes stays in eGeneral so only the access mask differs. Emit a per-resource UAV
				// barrier, not drop the hazard. Every storage-texture use lands here.
				if (before == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				{
					D3D12_RESOURCE_BARRIER uavBarrier{};
					uavBarrier.Type			 = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					uavBarrier.UAV.pResource = slot->resource.Get();
					native.push_back(uavBarrier);
				}
				continue;
			}

			/*
			 * Resolve the count sentinels against what the texture actually has before looping on them.
			 *
			 * The whole-resource test below only catches a range starting at zero so a barrier on one face of a cube with mipCount left at kAllMips took the
			 * per-subresource path with a count of 0xFFFFFFFF and pushed 32-byte barriers until it ran out of memory.
			 */
			const detail::ResolvedSubresourceRange range = detail::ResolveSubresourceRange(t.range, slot->mipLevels, slot->arrayLayers);
			if (range.IsEmpty())
			{
				continue;
			}

			if (detail::CoversWholeTexture(range, slot->mipLevels, slot->arrayLayers))
			{
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = slot->resource.Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = before;
				barrier.Transition.StateAfter  = after;
				native.push_back(barrier);
			}
			else
			{
				for (std::uint32_t layer = 0; layer < range.layerCount; ++layer)
				{
					for (std::uint32_t mip = 0; mip < range.mipCount; ++mip)
					{
						D3D12_RESOURCE_BARRIER barrier{};
						barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
						barrier.Transition.pResource   = slot->resource.Get();
						barrier.Transition.Subresource = (range.baseMip + mip) + (range.baseLayer + layer) * slot->mipLevels;
						barrier.Transition.StateBefore = before;
						barrier.Transition.StateAfter  = after;
						native.push_back(barrier);
					}
				}
			}
		}

		if (!native.empty())
		{
			list->list->ResourceBarrier(static_cast<UINT>(native.size()), native.data());
		}
		return Succeed(error);
	}

	bool D3D12CmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;

		detail::HostVector<D3D12_RESOURCE_BARRIER> native;
		native.reserve(barriers.size());
		for (const AliasBarrier & alias : barriers)
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type					 = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
			barrier.Aliasing.pResourceBefore = nullptr;
			barrier.Aliasing.pResourceAfter	 = nullptr;
			if (alias.beforeBuffer.IsValid())
			{
				if (BufferSlot * slot = ResolveBuffer(device, alias.beforeBuffer); slot != nullptr)
				{
					barrier.Aliasing.pResourceBefore = slot->resource.Get();
				}
			}
			else if (alias.beforeTexture.IsValid())
			{
				if (TextureSlot * slot = ResolveTexture(device, alias.beforeTexture); slot != nullptr)
				{
					barrier.Aliasing.pResourceBefore = slot->resource.Get();
				}
			}
			if (alias.afterBuffer.IsValid())
			{
				if (BufferSlot * slot = ResolveBuffer(device, alias.afterBuffer); slot != nullptr)
				{
					barrier.Aliasing.pResourceAfter = slot->resource.Get();
				}
			}
			else if (alias.afterTexture.IsValid())
			{
				if (TextureSlot * slot = ResolveTexture(device, alias.afterTexture); slot != nullptr)
				{
					barrier.Aliasing.pResourceAfter = slot->resource.Get();
				}
			}
			native.push_back(barrier);
		}

		if (!native.empty())
		{
			list->list->ResourceBarrier(static_cast<UINT>(native.size()), native.data());
		}
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

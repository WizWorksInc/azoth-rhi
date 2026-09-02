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
	[[nodiscard]] ID3D12Resource * AllocateCopyScratch(D3D12Device * device, D3D12CommandList * list, std::uint64_t bytes) noexcept
	{
		D3D12_RESOURCE_DESC bufferDesc{};
		bufferDesc.Dimension		= D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width			= bytes;
		bufferDesc.Height			= 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels		= 1;
		bufferDesc.Format			= DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout			= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12MA::ALLOCATION_DESC allocationDesc{};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		if (FAILED(device->allocator->CreateResource(
				&allocationDesc, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, allocation.GetAddressOf(), IID_PPV_ARGS(resource.GetAddressOf()))))
		{
			return nullptr;
		}
		ID3D12Resource * raw = resource.Get();
		list->retiredCopyScratch.push_back(std::move(resource));
		list->retiredCopyAllocs.push_back(std::move(allocation));
		return raw;
	}

	void BarrierScratchToSource(D3D12CommandList * list, ID3D12Resource * scratch) noexcept
	{
		const D3D12_BUFFER_BARRIER toSource{
			.SyncBefore	  = D3D12_BARRIER_SYNC_COPY,
			.SyncAfter	  = D3D12_BARRIER_SYNC_COPY,
			.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
			.AccessAfter  = D3D12_BARRIER_ACCESS_COPY_SOURCE,
			.pResource	  = scratch,
			.Offset		  = 0,
			.Size		  = std::numeric_limits<UINT64>::max(),
		};

		D3D12_BARRIER_GROUP group{};
		group.Type			  = D3D12_BARRIER_TYPE_BUFFER;
		group.NumBarriers	  = 1;
		group.pBufferBarriers = &toSource;
		list->list7->Barrier(1, &group);
	}

	[[nodiscard]] D3D12_RESOURCE_DESC RegionFootprintDesc(const D3D12_RESOURCE_DESC & texDesc, const Extent3D & extent) noexcept
	{
		D3D12_RESOURCE_DESC regionDesc = texDesc;
		regionDesc.Width			   = extent.width;
		regionDesc.Height			   = extent.height;
		regionDesc.MipLevels		   = 1;
		regionDesc.DepthOrArraySize	   = static_cast<UINT16>(texDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? extent.depth : 1);
		return regionDesc;
	}

	[[nodiscard]] D3D12_BOX RegionSourceBox(const Offset3D & offset, const Extent3D & extent) noexcept
	{
		D3D12_BOX box{};
		box.left   = static_cast<UINT>(offset.x);
		box.top	   = static_cast<UINT>(offset.y);
		box.front  = static_cast<UINT>(offset.z);
		box.right  = box.left + extent.width;
		box.bottom = box.top + extent.height;
		box.back   = box.front + extent.depth;
		return box;
	}

	[[nodiscard]] bool RegionIsEmpty(const Extent3D & extent) noexcept
	{
		return extent.width == 0 || extent.height == 0 || extent.depth == 0;
	}

	bool D3D12CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.copyBufferToTexture");

		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		TextureSlot * dstSlot = ResolveTexture(device, dst);
		BufferSlot * srcSlot  = ResolveBuffer(device, src);
		if (dstSlot == nullptr || srcSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture with an invalid handle");
		}

		const D3D12_RESOURCE_DESC texDesc = dstSlot->resource->GetDesc();
		for (const BufferTextureCopy & region : regions)
		{
			if (RegionIsEmpty(region.textureExtent))
			{
				continue;
			}

			const UINT subresource = SubresourceIndex(region.subresource, dstSlot->mipLevels);

			const D3D12_RESOURCE_DESC regionDesc = RegionFootprintDesc(texDesc, region.textureExtent);

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
			UINT numRows		  = 0;
			UINT64 rowSizeInBytes = 0;
			device->device->GetCopyableFootprints(&regionDesc, 0, 1, region.bufferOffset, &footprint, &numRows, &rowSizeInBytes, nullptr);

			const std::uint64_t srcRowPitch =
				region.bufferRowLength != 0 ? static_cast<std::uint64_t>(region.bufferRowLength) * rowSizeInBytes / footprint.Footprint.Width : rowSizeInBytes;

			D3D12_TEXTURE_COPY_LOCATION dstLoc{};
			dstLoc.pResource		= dstSlot->resource.Get();
			dstLoc.Type				= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dstLoc.SubresourceIndex = subresource;

			if (srcRowPitch != footprint.Footprint.RowPitch)
			{
				const UINT64 alignedPitch = footprint.Footprint.RowPitch;
				const UINT slices		  = footprint.Footprint.Depth;
				ID3D12Resource * scratch  = AllocateCopyScratch(device, list, alignedPitch * numRows * slices);
				if (scratch == nullptr)
				{
					return Fail(error, ErrorCode::eOutOfDeviceMemory, "failed to allocate a copy-repack scratch buffer");
				}
				for (UINT slice = 0; slice < slices; ++slice)
				{
					for (UINT row = 0; row < numRows; ++row)
					{
						const UINT64 scratchOff = (static_cast<UINT64>(slice) * numRows + row) * alignedPitch;
						const UINT64 srcOff		= region.bufferOffset + (static_cast<UINT64>(slice) * numRows + row) * srcRowPitch;
						list->list->CopyBufferRegion(scratch, scratchOff, srcSlot->resource.Get(), srcOff, rowSizeInBytes);
					}
				}
				BarrierScratchToSource(list, scratch);

				D3D12_PLACED_SUBRESOURCE_FOOTPRINT scratchFootprint = footprint;
				scratchFootprint.Offset								= 0;
				D3D12_TEXTURE_COPY_LOCATION srcLoc{};
				srcLoc.pResource	   = scratch;
				srcLoc.Type			   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				srcLoc.PlacedFootprint = scratchFootprint;
				list->list->CopyTextureRegion(&dstLoc,
					static_cast<UINT>(region.textureOffset.x),
					static_cast<UINT>(region.textureOffset.y),
					static_cast<UINT>(region.textureOffset.z),
					&srcLoc,
					nullptr);
				continue;
			}

			D3D12_TEXTURE_COPY_LOCATION srcLoc{};
			srcLoc.pResource	   = srcSlot->resource.Get();
			srcLoc.Type			   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			srcLoc.PlacedFootprint = footprint;
			list->list->CopyTextureRegion(&dstLoc,
				static_cast<UINT>(region.textureOffset.x),
				static_cast<UINT>(region.textureOffset.y),
				static_cast<UINT>(region.textureOffset.z),
				&srcLoc,
				nullptr);
		}
		return Succeed(error);
	}

	bool D3D12CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.copyTextureToBuffer");

		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		BufferSlot * dstSlot  = ResolveBuffer(device, dst);
		TextureSlot * srcSlot = ResolveTexture(device, src);
		if (dstSlot == nullptr || srcSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer with an invalid handle");
		}

		const D3D12_RESOURCE_DESC texDesc = srcSlot->resource->GetDesc();
		for (const BufferTextureCopy & region : regions)
		{
			if (RegionIsEmpty(region.textureExtent))
			{
				continue;
			}

			const UINT subresource = SubresourceIndex(region.subresource, srcSlot->mipLevels);

			const D3D12_RESOURCE_DESC regionDesc = RegionFootprintDesc(texDesc, region.textureExtent);
			const D3D12_BOX srcBox				 = RegionSourceBox(region.textureOffset, region.textureExtent);

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
			UINT numRows		  = 0;
			UINT64 rowSizeInBytes = 0;
			device->device->GetCopyableFootprints(&regionDesc, 0, 1, region.bufferOffset, &footprint, &numRows, &rowSizeInBytes, nullptr);

			const std::uint64_t dstRowPitch =
				region.bufferRowLength != 0 ? static_cast<std::uint64_t>(region.bufferRowLength) * rowSizeInBytes / footprint.Footprint.Width : rowSizeInBytes;

			D3D12_TEXTURE_COPY_LOCATION srcLoc{};
			srcLoc.pResource		= srcSlot->resource.Get();
			srcLoc.Type				= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			srcLoc.SubresourceIndex = subresource;

			if (dstRowPitch != footprint.Footprint.RowPitch)
			{
				const UINT64 alignedPitch = footprint.Footprint.RowPitch;
				const UINT slices		  = footprint.Footprint.Depth;
				ID3D12Resource * scratch  = AllocateCopyScratch(device, list, alignedPitch * numRows * slices);
				if (scratch == nullptr)
				{
					return Fail(error, ErrorCode::eOutOfDeviceMemory, "failed to allocate a copy-repack scratch buffer");
				}
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT scratchFootprint = footprint;
				scratchFootprint.Offset								= 0;
				D3D12_TEXTURE_COPY_LOCATION scratchDst{};
				scratchDst.pResource	   = scratch;
				scratchDst.Type			   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				scratchDst.PlacedFootprint = scratchFootprint;
				list->list->CopyTextureRegion(&scratchDst, 0, 0, 0, &srcLoc, &srcBox);

				BarrierScratchToSource(list, scratch);

				for (UINT slice = 0; slice < slices; ++slice)
				{
					for (UINT row = 0; row < numRows; ++row)
					{
						const UINT64 scratchOff = (static_cast<UINT64>(slice) * numRows + row) * alignedPitch;
						const UINT64 dstOff		= region.bufferOffset + (static_cast<UINT64>(slice) * numRows + row) * dstRowPitch;
						list->list->CopyBufferRegion(dstSlot->resource.Get(), dstOff, scratch, scratchOff, rowSizeInBytes);
					}
				}
				continue;
			}

			D3D12_TEXTURE_COPY_LOCATION dstLoc{};
			dstLoc.pResource	   = dstSlot->resource.Get();
			dstLoc.Type			   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dstLoc.PlacedFootprint = footprint;
			list->list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
		}
		return Succeed(error);
	}

	bool D3D12CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.copyTexture");

		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		TextureSlot * dstSlot = ResolveTexture(device, dst);
		TextureSlot * srcSlot = ResolveTexture(device, src);
		if (dstSlot == nullptr || srcSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTexture with an invalid handle");
		}

		for (const TextureCopy & region : regions)
		{
			D3D12_TEXTURE_COPY_LOCATION dstLoc{};
			dstLoc.pResource		= dstSlot->resource.Get();
			dstLoc.Type				= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dstLoc.SubresourceIndex = SubresourceIndex(region.dstSubresource, dstSlot->mipLevels);

			D3D12_TEXTURE_COPY_LOCATION srcLoc{};
			srcLoc.pResource		= srcSlot->resource.Get();
			srcLoc.Type				= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			srcLoc.SubresourceIndex = SubresourceIndex(region.srcSubresource, srcSlot->mipLevels);

			D3D12_BOX box{};
			box.left   = static_cast<UINT>(region.srcOffset.x);
			box.top	   = static_cast<UINT>(region.srcOffset.y);
			box.front  = static_cast<UINT>(region.srcOffset.z);
			box.right  = box.left + region.extent.width;
			box.bottom = box.top + region.extent.height;
			box.back   = box.front + region.extent.depth;

			list->list->CopyTextureRegion(
				&dstLoc, static_cast<UINT>(region.dstOffset.x), static_cast<UINT>(region.dstOffset.y), static_cast<UINT>(region.dstOffset.z), &srcLoc, &box);
		}
		return Succeed(error);
	}

	[[nodiscard]] bool IsDepthDxgiFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return true;
		default:							   return false;
		}
	}

	[[nodiscard]] bool IsBlockCompressedDxgiFormat(DXGI_FORMAT format) noexcept
	{
		return (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM) ||
			   (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
	}

	[[nodiscard]] bool IsIntegerDxgiFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT: return true;
		default:				   return false;
		}
	}

}

#endif

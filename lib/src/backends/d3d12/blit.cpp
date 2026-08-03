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
	bool D3D12CmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.clearBuffer");

		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;
		BufferSlot * slot	 = ResolveBuffer(device, buffer);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearBuffer with an invalid buffer handle");
		}

		/*
		 * This lowers to ClearUnorderedAccessViewUint, which needs a UAV, which needs the resource to carry ALLOW_UNORDERED_ACCESS. Creating one over a buffer
		 * without that flag is not diagnosed by the runtime. It builds a descriptor over a resource that cannot back it and the clear hangs the GPU. The failure
		 * arrives as a device removal on some later call.
		 *
		 * Read off the resource and not a copy of the usage flags, since the resource flag is the actual precondition.
		 */
		if ((slot->resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "clearBuffer needs BufferUsage::eStorage, which is what lets Direct3D 12 clear through a UAV");
		}

		if (list->clearGpuHeap && list->clearHeapNext >= list->clearHeapCapacity)
		{
			// The clear heap is full. Retire it (held until the next Begin, once the GPU has consumed it) and take a fresh one so a recording is never capped at a fixed
			// number of buffer clears.
			list->retiredClearHeaps.push_back(std::move(list->clearGpuHeap));
			list->retiredClearHeaps.push_back(std::move(list->clearStagingHeap));
			list->clearHeapNext = 0;
		}
		if (!list->clearGpuHeap)
		{
			constexpr std::uint32_t kClearHeapSize = 256;
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
			heapDesc.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = kClearHeapSize;
			heapDesc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (FAILED(device->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(list->clearGpuHeap.GetAddressOf()))))
			{
				return Fail(error, ErrorCode::eNativeApiError, "failed to create the clearBuffer shader-visible heap");
			}
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(device->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(list->clearStagingHeap.GetAddressOf()))))
			{
				return Fail(error, ErrorCode::eNativeApiError, "failed to create the clearBuffer staging heap");
			}
			list->clearHeapIncrement = device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			list->clearHeapCapacity	 = kClearHeapSize;
		}

		const std::uint64_t bytes = size == std::numeric_limits<std::uint64_t>::max() ? slot->size - offset : size;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format				= DXGI_FORMAT_R32_TYPELESS;
		uav.ViewDimension		= D3D12_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = offset / 4;
		uav.Buffer.NumElements	= static_cast<UINT>(bytes / 4);
		uav.Buffer.Flags		= D3D12_BUFFER_UAV_FLAG_RAW;

		const std::uint32_t index					 = list->clearHeapNext++;
		const D3D12_CPU_DESCRIPTOR_HANDLE stagingCpu = CpuHandleAt(list->clearStagingHeap.Get(), list->clearHeapIncrement, index);
		const D3D12_CPU_DESCRIPTOR_HANDLE gpuCpu	 = CpuHandleAt(list->clearGpuHeap.Get(), list->clearHeapIncrement, index);
		device->device->CreateUnorderedAccessView(slot->resource.Get(), nullptr, &uav, stagingCpu);
		device->device->CreateUnorderedAccessView(slot->resource.Get(), nullptr, &uav, gpuCpu);

		const std::array<ID3D12DescriptorHeap *, 1> heaps{ list->clearGpuHeap.Get() };
		list->list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		list->boundResourceHeap = list->clearGpuHeap.Get();
		list->boundSamplerHeap	= nullptr;

		const std::array<UINT, 4> values{ value, value, value, value };
		list->list->ClearUnorderedAccessViewUint(
			GpuHandleAt(list->clearGpuHeap.Get(), list->clearHeapIncrement, index), stagingCpu, slot->resource.Get(), values.data(), 0, nullptr);
		return Succeed(error);
	}

	// Clears a color texture through a transient RTV, which needs the texture in RENDER_TARGET. Depth goes through BeginRendering instead. The RTV returns to the
	// device heap at the next Begin, since the descriptor is consumed at execute time, not record time.
	bool D3D12CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;
		TextureSlot * slot	 = ResolveTexture(device, texture);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearTexture with an invalid texture handle");
		}
		if (IsDepthDxgiFormat(slot->format))
		{
			return Fail(error, ErrorCode::eInvalidArgument, "clearTexture clears color textures; clear depth via BeginRendering");
		}

		// Direct3D 12 clears a color texture through a render target view, which a resource created without ALLOW_RENDER_TARGET cannot have. Refused here
		//, not left to CreateRenderTargetView, which raises a debug layer error the caller has no way to read.
		if (!slot->usage.Contains(TextureUsage::eColorAttachment))
		{
			return Fail(
				error, ErrorCode::eInvalidArgument, "clearTexture needs a texture usable as a color attachment, which is what Direct3D 12 clears through");
		}

		const std::array<float, 4> rgba{ color.r, color.g, color.b, color.a };

		const auto clearRange = [&](const TextureSubresourceRange & range) -> bool
		{
			const std::uint32_t rtvIndex = device->rtvHeap.Allocate();
			if (rtvIndex == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eOutOfMemory, "out of RTV descriptors for clearTexture");
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtv{};
			rtv.Format = slot->format;
			if (slot->arrayLayers > 1 || range.baseLayer > 0 || range.layerCount > 1)
			{
				rtv.ViewDimension				   = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				rtv.Texture2DArray.MipSlice		   = range.baseMip;
				rtv.Texture2DArray.FirstArraySlice = range.baseLayer;
				rtv.Texture2DArray.ArraySize	   = std::max<std::uint32_t>(range.layerCount, 1);
			}
			else
			{
				rtv.ViewDimension	   = D3D12_RTV_DIMENSION_TEXTURE2D;
				rtv.Texture2D.MipSlice = range.baseMip;
			}
			device->device->CreateRenderTargetView(slot->resource.Get(), &rtv, device->rtvHeap.Handle(rtvIndex));
			list->list->ClearRenderTargetView(device->rtvHeap.Handle(rtvIndex), rgba.data(), 0, nullptr);
			list->transientRtvs.push_back(rtvIndex);
			return true;
		};

		if (ranges.empty())
		{
			return clearRange(TextureSubresourceRange{}) ? Succeed(error) : false;
		}
		for (const TextureSubresourceRange & range : ranges)
		{
			if (!clearRange(range))
			{
				return false;
			}
		}
		return Succeed(error);
	}

	bool D3D12CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		auto * list			  = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device  = list->owner;
		TextureSlot * dstSlot = ResolveTexture(device, dst);
		TextureSlot * srcSlot = ResolveTexture(device, src);
		if (dstSlot == nullptr || srcSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveTexture with an invalid handle");
		}

		// ResolveSubresource takes a whole subresource so a sub-rectangle goes through ResolveSubresourceRegion, not silently resolving everything, matching how
		// Vulkan honors the region.
		ComPtr<ID3D12GraphicsCommandList1> list1;
		const bool haveList1 = SUCCEEDED(list->list.As(&list1));
		for (const TextureResolve & region : regions)
		{
			const bool subRegion = region.srcOffset.x != 0 || region.srcOffset.y != 0 || region.dstOffset.x != 0 || region.dstOffset.y != 0;
			if (subRegion)
			{
				if (!haveList1)
				{
					return Fail(error, ErrorCode::eUnsupportedFeature, "a sub-region resolveTexture requires ID3D12GraphicsCommandList1");
				}
				const D3D12_RECT srcRect{ .left = region.srcOffset.x,
					.top						= region.srcOffset.y,
					.right						= region.srcOffset.x + static_cast<LONG>(region.extent.width),
					.bottom						= region.srcOffset.y + static_cast<LONG>(region.extent.height) };
				list1->ResolveSubresourceRegion(dstSlot->resource.Get(),
					SubresourceIndex(region.dstSubresource, dstSlot->mipLevels),
					static_cast<UINT>(region.dstOffset.x),
					static_cast<UINT>(region.dstOffset.y),
					srcSlot->resource.Get(),
					SubresourceIndex(region.srcSubresource, srcSlot->mipLevels),
					const_cast<D3D12_RECT *>(&srcRect),
					dstSlot->format,
					D3D12_RESOLVE_MODE_AVERAGE);
			}
			else
			{
				list->list->ResolveSubresource(dstSlot->resource.Get(),
					SubresourceIndex(region.dstSubresource, dstSlot->mipLevels),
					srcSlot->resource.Get(),
					SubresourceIndex(region.srcSubresource, srcSlot->mipLevels),
					dstSlot->format);
			}
		}
		return Succeed(error);
	}

	/*
	 * Direct3D 12 has no fixed-function scaled blit and this backend no longer carries a compute shader to stand in for one, and both entries refuse.
	 * DeviceCaps::supportsScaledBlit reports false to match and FormatSupport says the same per format, letting a caller find out before recording.
	 *
	 * Resampling is still reachable in the utility target above the RHI, which owns the shader, the descriptor budget and the choice of filter.
	 */
	bool D3D12CmdBlit(void * impl, [[maybe_unused]] TextureHandle dst, [[maybe_unused]] TextureHandle src,
		[[maybe_unused]] std::span<const TextureBlit> regions, [[maybe_unused]] Filter filter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.blit");
		static_cast<void>(impl);

		return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no scaled blit; resample through azoth::rhi-utils instead");
	}

	// generateMips is a chain of scaled blits, so it goes the same way and for the same reason.
	bool D3D12CmdGenerateMips(void * impl, [[maybe_unused]] TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.generateMips");
		static_cast<void>(impl);

		return Fail(error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no scaled blit to build a mip chain from; use azoth::rhi-utils instead");
	}

	// D3D12 query heaps have no reset, since each resolve overwrites the destination so this only validates the handle.

} // namespace azo::rhi::d3d12

#endif // _WIN32

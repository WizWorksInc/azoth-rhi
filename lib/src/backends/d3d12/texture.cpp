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
	[[nodiscard]] DXGI_FORMAT MapFormat(Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UNorm:		  return DXGI_FORMAT_R8_UNORM;
		case Format::eR8UInt:		  return DXGI_FORMAT_R8_UINT;
		case Format::eR8SInt:		  return DXGI_FORMAT_R8_SINT;
		case Format::eRG8UNorm:		  return DXGI_FORMAT_R8G8_UNORM;
		case Format::eRGBA8UNorm:	  return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Format::eRGBA8Srgb:	  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case Format::eBGRA8UNorm:	  return DXGI_FORMAT_B8G8R8A8_UNORM;
		case Format::eBGRA8Srgb:	  return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case Format::eR16UInt:		  return DXGI_FORMAT_R16_UINT;
		case Format::eR16SInt:		  return DXGI_FORMAT_R16_SINT;
		case Format::eR16Float:		  return DXGI_FORMAT_R16_FLOAT;
		case Format::eRG16Float:	  return DXGI_FORMAT_R16G16_FLOAT;
		case Format::eRGBA16Float:	  return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Format::eR11G11B10Float: return DXGI_FORMAT_R11G11B10_FLOAT;
		case Format::eRGB10A2UNorm:	  return DXGI_FORMAT_R10G10B10A2_UNORM;
		case Format::eRGB9E5Float:	  return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
		case Format::eR32UInt:		  return DXGI_FORMAT_R32_UINT;
		case Format::eR32SInt:		  return DXGI_FORMAT_R32_SINT;
		case Format::eR32Float:		  return DXGI_FORMAT_R32_FLOAT;
		case Format::eRG32Float:	  return DXGI_FORMAT_R32G32_FLOAT;
		case Format::eRGB32Float:	  return DXGI_FORMAT_R32G32B32_FLOAT;
		case Format::eRGBA32Float:	  return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Format::eD16UNorm:		  return DXGI_FORMAT_D16_UNORM;
		case Format::eD24UNormS8UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::eD32Float:		  return DXGI_FORMAT_D32_FLOAT;
		case Format::eD32FloatS8UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		case Format::eBC1RGBAUNorm:	  return DXGI_FORMAT_BC1_UNORM;
		case Format::eBC1RGBASrgb:	  return DXGI_FORMAT_BC1_UNORM_SRGB;
		case Format::eBC3UNorm:		  return DXGI_FORMAT_BC3_UNORM;
		case Format::eBC3Srgb:		  return DXGI_FORMAT_BC3_UNORM_SRGB;
		case Format::eBC5UNorm:		  return DXGI_FORMAT_BC5_UNORM;
		case Format::eBC5SNorm:		  return DXGI_FORMAT_BC5_SNORM;
		case Format::eBC7UNorm:		  return DXGI_FORMAT_BC7_UNORM;
		case Format::eBC7Srgb:		  return DXGI_FORMAT_BC7_UNORM_SRGB;
		case Format::eBC6HUFloat:	  return DXGI_FORMAT_BC6H_UF16;
		case Format::eBC6HSFloat:	  return DXGI_FORMAT_BC6H_SF16;
		// D3D12 has no pure 24-bit depth (D24 always pairs with stencil) so eX8D24UNorm maps to D24_UNORM_S8_UINT and leaves the stencil plane unused.
		case Format::eX8D24UNorm:				 return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::eG8B8R8Biplanar420UNorm:	 return DXGI_FORMAT_NV12;
		case Format::eG10B10R10Biplanar420UNorm: return DXGI_FORMAT_P010;
		// DXGI has no three-plane 4:2:0 format, its planar video formats all interleaving chroma, so this one reports unsupported here.
		case Format::eG8B8R8Triplanar420UNorm: return DXGI_FORMAT_UNKNOWN;
		case Format::eUndefined:			   return DXGI_FORMAT_UNKNOWN;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	[[nodiscard]] UINT MapSampleCount(SampleCount samples) noexcept
	{
		switch (samples)
		{
		case SampleCount::e2:  return 2;
		case SampleCount::e4:  return 4;
		case SampleCount::e8:  return 8;
		case SampleCount::e16: return 16;
		case SampleCount::e1:  return 1;
		}
		return 1;
	}

	[[nodiscard]] D3D12_RESOURCE_DIMENSION MapResourceDimension(TextureType type) noexcept
	{
		switch (type)
		{
		case TextureType::eTex1D:	return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		case TextureType::eTex3D:	return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		case TextureType::eTex2D:
		case TextureType::eTexCube: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		}
		return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	}

	[[nodiscard]] D3D12_RESOURCE_FLAGS MapTextureResourceFlags(Flags<TextureUsage> usage) noexcept
	{
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		if (usage.Contains(TextureUsage::eColorAttachment))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		if (usage.Contains(TextureUsage::eDepthStencilAttachment))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			if (!usage.Contains(TextureUsage::eSampled))
			{
				flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
			}
		}
		if (usage.Contains(TextureUsage::eStorage))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		return flags;
	}

	// D3D12 forbids a depth format in a shader resource view so a sampled depth texture is created typeless: the depth-stencil view still binds the depth
	// format while an SRV aliases the same memory as color. Non-depth formats pass through so callers can map unconditionally.
	[[nodiscard]] DXGI_FORMAT DepthTypelessResourceFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_D16_UNORM:			   return DXGI_FORMAT_R16_TYPELESS;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:	   return DXGI_FORMAT_R24G8_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT:			   return DXGI_FORMAT_R32_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
		default:							   return format;
		}
	}

	// The color format aliasing a depth format through a typeless resource: the depth bits become the red channel the shader samples.
	[[nodiscard]] DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_D16_UNORM:			   return DXGI_FORMAT_R16_UNORM;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:	   return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT:			   return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		default:							   return format;
		}
	}

	/*
	 * The typeless family a format belongs to, for a texture that allows views of another format. Direct3D 12 reinterprets texels only through a typeless
	 * resource, each view naming the concrete format it reads. Depth falls through to the depth mapping above. R11G11B10 and RGB9E5 have no typeless family and
	 * come back unchanged, which is what createTexture tests to refuse the request.
	 */
	[[nodiscard]] DXGI_FORMAT TypelessResourceFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SINT:			  return DXGI_FORMAT_R8_TYPELESS;
		case DXGI_FORMAT_R8G8_UNORM:		  return DXGI_FORMAT_R8G8_TYPELESS;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R16_FLOAT:			  return DXGI_FORMAT_R16_TYPELESS;
		case DXGI_FORMAT_R16G16_FLOAT:		  return DXGI_FORMAT_R16G16_TYPELESS;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:  return DXGI_FORMAT_R16G16B16A16_TYPELESS;
		case DXGI_FORMAT_R10G10B10A2_UNORM:	  return DXGI_FORMAT_R10G10B10A2_TYPELESS;
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R32_FLOAT:			  return DXGI_FORMAT_R32_TYPELESS;
		case DXGI_FORMAT_R32G32_FLOAT:		  return DXGI_FORMAT_R32G32_TYPELESS;
		case DXGI_FORMAT_R32G32B32_FLOAT:	  return DXGI_FORMAT_R32G32B32_TYPELESS;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_TYPELESS;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:	  return DXGI_FORMAT_BC1_TYPELESS;
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:	  return DXGI_FORMAT_BC3_TYPELESS;
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:			  return DXGI_FORMAT_BC5_TYPELESS;
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:			  return DXGI_FORMAT_BC6H_TYPELESS;
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:	  return DXGI_FORMAT_BC7_TYPELESS;
		default:							  return DepthTypelessResourceFormat(format);
		}
	}

	// A 3D texture sizes itself from depth, every other type from arrayLayers, which is the full subresource range and already counts a cube's six faces.
	[[nodiscard]] D3D12_RESOURCE_DESC MakeTextureResourceDesc(const TextureDesc & desc) noexcept
	{
		const std::uint32_t arrayOrDepth = desc.type == TextureType::eTex3D ? desc.depth : desc.arrayLayers;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension		  = MapResourceDimension(desc.type);
		resourceDesc.Width			  = desc.width;
		resourceDesc.Height			  = desc.height;
		resourceDesc.DepthOrArraySize = static_cast<UINT16>(arrayOrDepth);
		resourceDesc.MipLevels		  = static_cast<UINT16>(desc.mipLevels);
		resourceDesc.Format			  = MapFormat(desc.format);
		// A sampled depth texture needs a typeless resource so an SRV can alias it. An attachment-only one keeps its depth format.
		if (desc.usage.Contains(TextureUsage::eDepthStencilAttachment) && desc.usage.Contains(TextureUsage::eSampled))
		{
			resourceDesc.Format = DepthTypelessResourceFormat(resourceDesc.Format);
		}
		else if (desc.allowFormatViews)
		{
			resourceDesc.Format = TypelessResourceFormat(resourceDesc.Format);
		}
		resourceDesc.SampleDesc.Count = MapSampleCount(desc.samples);
		resourceDesc.Layout			  = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags			  = MapTextureResourceFlags(desc.usage);
		return resourceDesc;
	}

	TextureHandle D3D12CreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept
	{
		if (!D3D12RefuseUnexportable(desc.exportableHandleTypes,
				Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Resource,
				"texture creation asked for an external handle type Direct3D 12 cannot export",
				error))
		{
			return TextureHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createTexture");

		auto * device			 = static_cast<D3D12Device *>(impl);
		const DXGI_FORMAT format = MapFormat(desc.format);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "texture format is undefined or unsupported");
		}
		if (desc.allowFormatViews && TypelessResourceFormat(format) == format)
		{
			return FailValue<TextureHandle>(
				error, ErrorCode::eUnsupportedFormat, "allowFormatViews needs a format with a Direct3D 12 typeless family, which this one has not");
		}
		if (desc.width == 0 || desc.height == 0)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture dimensions must be non-zero");
		}
		// Reject a degenerate extent as Vulkan does: a 3D texture needs non-zero depth, everything else at least one array layer.
		if (desc.type == TextureType::eTex3D)
		{
			if (desc.depth == 0)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "a 3D texture must have a non-zero depth");
			}
		}
		else if (desc.arrayLayers == 0)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "a texture must have at least one array layer");
		}

		const D3D12_RESOURCE_DESC resourceDesc = MakeTextureResourceDesc(desc);

		// Attachments carry an optimized clear value: D3D12 fast-clears use it and the debug layer warns when one is created without it.
		D3D12_CLEAR_VALUE clearValue{};
		D3D12_CLEAR_VALUE * clearPtr = nullptr;
		if (desc.usage.Contains(TextureUsage::eColorAttachment))
		{
			clearValue.Format = format;
			clearPtr		  = &clearValue;
		}
		else if (desc.usage.Contains(TextureUsage::eDepthStencilAttachment))
		{
			clearValue.Format				= format;
			clearValue.DepthStencil.Depth	= 1.0f;
			clearValue.DepthStencil.Stencil = 0;
			clearPtr						= &clearValue;
		}

		// Reserved texture: virtual tiles bound later through bindSparse. These need the 64 KiB tiled undefined-swizzle layout, since CreateReservedResource
		// rejects a committed resource's D3D12_TEXTURE_LAYOUT_UNKNOWN.
		if (desc.allowSparseBinding)
		{
			D3D12_RESOURCE_DESC reservedDesc = resourceDesc;
			reservedDesc.Layout				 = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

			ComPtr<ID3D12Resource> reserved;
			if (FAILED(device->device->CreateReservedResource(&reservedDesc, D3D12_RESOURCE_STATE_COMMON, clearPtr, IID_PPV_ARGS(reserved.GetAddressOf()))))
			{
				return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "CreateReservedResource failed for a sparse texture");
			}

			NameD3D12Object(reserved.Get(), desc.debugName, device->debugNames);

			return ReturnValue(device->textureSlots.Store(TextureSlot{ .resource = std::move(reserved),
								   .format										 = format,
								   .rhiFormat									 = desc.format,
								   .type										 = desc.type,
								   .mipLevels									 = desc.mipLevels,
								   .arrayLayers									 = static_cast<std::uint32_t>(reservedDesc.DepthOrArraySize),
								   .usage										 = desc.usage,
								   .mutableFormat								 = desc.allowFormatViews,
								   .reserved									 = true,
								   .desc										 = detail::Recorded(desc) }),
				error);
		}

		// D3D12 bars textures from UPLOAD and READBACK heaps so a committed texture is always device-local and staging goes via a buffer.
		D3D12MA::ALLOCATION_DESC allocationDesc{};
		allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		// Committed in a shared heap, for the reason an exportable buffer is. See D3D12CreateBuffer.
		if (!desc.exportableHandleTypes.Empty())
		{
			allocationDesc.ExtraHeapFlags |= D3D12_HEAP_FLAG_SHARED;
			allocationDesc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
		}

		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		const HRESULT hr = device->allocator->CreateResource(
			&allocationDesc, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, clearPtr, allocation.GetAddressOf(), IID_PPV_ARGS(resource.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "D3D12MA::CreateResource failed for a texture");
		}

		NameD3D12Object(resource.Get(), desc.debugName, device->debugNames);

		return ReturnValue(device->textureSlots.Store(TextureSlot{ .allocation = std::move(allocation),
							   .resource									   = std::move(resource),
							   .format										   = format,
							   .rhiFormat									   = desc.format,
							   .type										   = desc.type,
							   .mipLevels									   = desc.mipLevels,
							   .arrayLayers									   = static_cast<std::uint32_t>(resourceDesc.DepthOrArraySize),
							   .usage										   = desc.usage,
							   .mutableFormat								   = desc.allowFormatViews,
							   .exportableHandleTypes						   = desc.exportableHandleTypes,
							   .desc										   = detail::Recorded(desc) }),
			error);
	}

	bool D3D12GetTextureInfo(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.getTextureInfo");

		auto * device = static_cast<D3D12Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
		}

		const TextureSlot * const slot = device->textureSlots.Resolve(texture, false);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
		}

		// A back buffer never came from a TextureDesc, so there is nothing here to report that would not be invented. The swapchain owns that description and
		// answers for it.
		if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
		}

		// Zero where this texture owns no allocation, which is a placed, adopted or reserved one. Its memory is the heap's or the caller's to account for.
		const std::uint64_t allocated = slot->allocation != nullptr ? slot->allocation->GetSize() : 0;

		*out = TextureInfo{ .desc = slot->desc, .allocationSize = allocated };
		return true;
	}

	bool D3D12GetBufferInfo(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.getBufferInfo");

		auto * device = static_cast<D3D12Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
		}

		const BufferSlot * const slot = device->bufferSlots.Resolve(buffer, false);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
		}

		// Zero where this buffer owns no allocation, which is a placed, adopted or reserved one.
		const std::uint64_t allocated = slot->allocation != nullptr ? slot->allocation->GetSize() : 0;

		// The two host-reachable heaps here are UPLOAD and READBACK and both are write-combined or cached in a way Direct3D 12 keeps coherent, so a
		// host-visible buffer needs no explicit flush and there is no eCpuVisible case to report.
		const MemoryAccess access = slot->hostVisible ? MemoryAccess::eCpuVisibleCoherent : MemoryAccess::eGpuOnly;

		*out = BufferInfo{ .desc = slot->desc, .allocationSize = allocated, .memoryAccess = access };
		return true;
	}

	bool D3D12GetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.getTextureMemoryInfo");

		auto * device = static_cast<D3D12Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "memory info output is null");
		}
		if (MapFormat(desc.format) == DXGI_FORMAT_UNKNOWN)
		{
			return Fail(error, ErrorCode::eUnsupportedFormat, "texture format is undefined or unsupported");
		}

		const D3D12_RESOURCE_DESC resourceDesc	  = MakeTextureResourceDesc(desc);
		const D3D12_RESOURCE_ALLOCATION_INFO info = device->device->GetResourceAllocationInfo(0, 1, &resourceDesc);
		*out									  = MemoryInfo{
			.size	   = info.SizeInBytes,
			.alignment = info.Alignment,
		};
		return Succeed(error);
	}

	bool D3D12DestroyTexture(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const TextureHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		TextureSlot * slot = device->textureSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid texture handle");
		}
		// A swapchain back buffer's lifetime is the swapchain's so a public destroy does nothing. An adopted resource is the caller's, so the slot is
		// retired and the resource left alone, which is what keeps repeated adopt and destroy from leaking slots.
		if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
		{
			return Succeed(error);
		}

		if (slot->lifetime == SlotLifetime::eAdopted)
		{
			static_cast<void>(device->textureSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		slot->resource.Reset();
		slot->allocation.Reset();
		static_cast<void>(device->textureSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	[[nodiscard]] TextureSlot * ResolveTexture(D3D12Device * device, TextureHandle handle) noexcept
	{
		return device->textureSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	// Fills a render-target view from the view type, format and range. Cube views render through the 2D-array dimension, one face a slice.
	void FillRtvDesc(D3D12_RENDER_TARGET_VIEW_DESC & rtv, DXGI_FORMAT format, const TextureViewDesc & desc) noexcept
	{
		const TextureSubresourceRange & range = desc.range;
		rtv.Format							  = format;
		switch (desc.type)
		{
		case TextureViewType::eTex1D:
			rtv.ViewDimension	   = D3D12_RTV_DIMENSION_TEXTURE1D;
			rtv.Texture1D.MipSlice = range.baseMip;
			break;
		case TextureViewType::eTex1DArray:
			rtv.ViewDimension				   = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			rtv.Texture1DArray.MipSlice		   = range.baseMip;
			rtv.Texture1DArray.FirstArraySlice = range.baseLayer;
			rtv.Texture1DArray.ArraySize	   = range.layerCount;
			break;
		case TextureViewType::eTex3D:
			rtv.ViewDimension		  = D3D12_RTV_DIMENSION_TEXTURE3D;
			rtv.Texture3D.MipSlice	  = range.baseMip;
			rtv.Texture3D.FirstWSlice = range.baseLayer;
			rtv.Texture3D.WSize		  = range.layerCount;
			break;
		case TextureViewType::eTex2DArray:
		case TextureViewType::eTexCube:
		case TextureViewType::eTexCubeArray:
			rtv.ViewDimension				   = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			rtv.Texture2DArray.MipSlice		   = range.baseMip;
			rtv.Texture2DArray.FirstArraySlice = range.baseLayer;
			rtv.Texture2DArray.ArraySize	   = range.layerCount;
			break;
		case TextureViewType::eTex2D:
			rtv.ViewDimension	   = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtv.Texture2D.MipSlice = range.baseMip;
			break;
		}
	}

	void FillDsvDesc(D3D12_DEPTH_STENCIL_VIEW_DESC & dsv, DXGI_FORMAT format, const TextureViewDesc & desc) noexcept
	{
		const TextureSubresourceRange & range = desc.range;
		dsv.Format							  = format;
		dsv.Flags							  = D3D12_DSV_FLAG_NONE;
		switch (desc.type)
		{
		case TextureViewType::eTex1D:
			dsv.ViewDimension	   = D3D12_DSV_DIMENSION_TEXTURE1D;
			dsv.Texture1D.MipSlice = range.baseMip;
			break;
		case TextureViewType::eTex1DArray:
			dsv.ViewDimension				   = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			dsv.Texture1DArray.MipSlice		   = range.baseMip;
			dsv.Texture1DArray.FirstArraySlice = range.baseLayer;
			dsv.Texture1DArray.ArraySize	   = range.layerCount;
			break;
		case TextureViewType::eTex2DArray:
		case TextureViewType::eTexCube:
		case TextureViewType::eTexCubeArray:
			dsv.ViewDimension				   = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			dsv.Texture2DArray.MipSlice		   = range.baseMip;
			dsv.Texture2DArray.FirstArraySlice = range.baseLayer;
			dsv.Texture2DArray.ArraySize	   = range.layerCount;
			break;
		case TextureViewType::eTex3D: // depth has no 3D view, fall back to 2D
		case TextureViewType::eTex2D:
			dsv.ViewDimension	   = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv.Texture2D.MipSlice = range.baseMip;
			break;
		}
	}

	[[nodiscard]] UINT D3D12ComponentSource(ComponentSwizzle swizzle, UINT self) noexcept
	{
		switch (swizzle)
		{
		case ComponentSwizzle::eIdentity: return self;
		case ComponentSwizzle::eZero:	  return D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0;
		case ComponentSwizzle::eOne:	  return D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1;
		case ComponentSwizzle::eR:		  return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0;
		case ComponentSwizzle::eG:		  return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1;
		case ComponentSwizzle::eB:		  return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2;
		case ComponentSwizzle::eA:		  return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3;
		}

		return self;
	}

	// Direct3D 12 names a concrete source per output component with no identity enumerant, so eIdentity resolves to the component's own position. The
	// all-identity encoding is what D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING already expands to.
	[[nodiscard]] UINT D3D12ShaderComponentMapping(ComponentMapping mapping) noexcept
	{
		return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(D3D12ComponentSource(mapping.r, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
			D3D12ComponentSource(mapping.g, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1),
			D3D12ComponentSource(mapping.b, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2),
			D3D12ComponentSource(mapping.a, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3));
	}

	TextureViewHandle D3D12CreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createTextureView");

		auto * device = static_cast<D3D12Device *>(impl);

		// Copy out what the view needs and release the texture mutex before touching the descriptor heaps. The ref keeps it alive between.
		ComPtr<ID3D12Resource> resource;
		Flags<TextureUsage> usage;
		DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;
		bool mutableFormat		  = false;
		Format rhiFormat		  = Format::eUndefined;
		{
			TextureSlot * slot = ResolveTexture(device, texture);
			if (slot == nullptr)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of an invalid texture handle");
			}
			resource	  = slot->resource;
			usage		  = slot->usage;
			textureFormat = slot->format;
			mutableFormat = slot->mutableFormat;
			rhiFormat	  = slot->rhiFormat;
		}

		/*
		 * A view naming a plane exposes that plane's single-plane format and selects it through the shader resource view's PlaneSlice, which is how Direct3D 12
		 * reaches the texels of NV12 and P010. Selecting a plane is not a format cast so it needs no allowFormatViews, and the reinterpretation check below is
		 * skipped for it.
		 */
		const std::uint32_t plane = PlaneIndexOf(desc.range.aspects);
		if (desc.ycbcrConversion != nullptr)
		{
			return FailValue<TextureViewHandle>(
				error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no sampler Y'CbCr conversion, so convert in the shader over per-plane views");
		}
		// A shader resource view over a multi-planar format has to name a plane through PlaneSlice. There is no descriptor that reads all planes at once, so a
		// view that names none would read plane zero while claiming to be the whole texture.
		if (plane == kNoPlane && IsMultiPlanarFormat(rhiFormat))
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "a texture view of a multi-planar format must name a plane aspect");
		}

		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		if (plane != kNoPlane)
		{
			const Format planeFormat = PlaneFormatOf(rhiFormat, plane);
			if (planeFormat == Format::eUndefined)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view names a plane the source texture's format does not have");
			}
			format = desc.format == Format::eUndefined ? MapFormat(planeFormat) : MapFormat(desc.format);
		}
		else
		{
			format = desc.format == Format::eUndefined ? textureFormat : MapFormat(desc.format);
		}
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedFormat, "texture view format is unsupported");
		}

		// Casting to another format needs the resource to have been created typeless, which the texture declares through allowFormatViews. Without it
		// the descriptor is only rejected by the debug layer, so refuse it here instead.
		if (plane == kNoPlane && format != textureFormat && !mutableFormat)
		{
			return FailValue<TextureViewHandle>(
				error, ErrorCode::eInvalidArgument, "texture view names a format the source texture was not created with allowFormatViews for");
		}

		// Only the shader resource view carries a component mapping. A render target or depth-stencil view has no field for it, so a swizzle reaching
		// one would be dropped, not applied.
		if (!desc.swizzle.IsIdentity() && UsageForbidsSwizzle(ResolveViewUsage(desc.usage, usage)))
		{
			return FailValue<TextureViewHandle>(
				error, ErrorCode::eInvalidArgument, "a swizzled texture view must be sampled only, so narrow TextureViewDesc::usage to eSampled");
		}

		std::uint32_t rtvIndex = kInvalidIndex;
		std::uint32_t dsvIndex = kInvalidIndex;
		if (usage.Contains(TextureUsage::eDepthStencilAttachment))
		{
			dsvIndex = device->dsvHeap.Allocate();
			if (dsvIndex == kInvalidIndex)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfDeviceMemory, "out of D3D12 depth-stencil view descriptors");
			}
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
			FillDsvDesc(dsv, format, desc);
			device->device->CreateDepthStencilView(resource.Get(), &dsv, device->dsvHeap.Handle(dsvIndex));
		}
		if (usage.Contains(TextureUsage::eColorAttachment))
		{
			rtvIndex = device->rtvHeap.Allocate();
			if (rtvIndex == kInvalidIndex)
			{
				device->dsvHeap.Free(dsvIndex);
				return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfDeviceMemory, "out of D3D12 render-target view descriptors");
			}
			D3D12_RENDER_TARGET_VIEW_DESC rtv{};
			FillRtvDesc(rtv, format, desc);
			device->device->CreateRenderTargetView(resource.Get(), &rtv, device->rtvHeap.Handle(rtvIndex));
		}

		return ReturnValue(device->textureViewSlots.Store(TextureViewSlot{ .texture = texture,
							   .format												= format,
							   .type												= desc.type,
							   .range												= desc.range,
							   .rtvIndex											= rtvIndex,
							   .dsvIndex											= dsvIndex,
							   .planeSlice											= plane == kNoPlane ? 0u : plane,
							   .shaderComponentMapping								= D3D12ShaderComponentMapping(desc.swizzle) }),
			error);
	}

	bool D3D12DestroyTextureView(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const TextureViewHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		TextureViewSlot * slot = device->textureViewSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid texture view handle");
		}
		if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
		{
			// A swapchain back-buffer view: the swapchain frees its RTV on resize and teardown.
			return Succeed(error);
		}

		if (slot->lifetime == SlotLifetime::eAdopted)
		{
			static_cast<void>(device->textureViewSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		device->rtvHeap.Free(slot->rtvIndex);
		device->dsvHeap.Free(slot->dsvIndex);
		static_cast<void>(device->textureViewSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

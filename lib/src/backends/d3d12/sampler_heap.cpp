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
	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE MapAddressMode(AddressMode mode) noexcept
	{
		switch (mode)
		{
		case AddressMode::eMirroredRepeat:	  return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case AddressMode::eClampToEdge:		  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case AddressMode::eClampToBorder:	  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		case AddressMode::eMirrorClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		case AddressMode::eRepeat:			  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		}
		return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}

	[[nodiscard]] D3D12_COMPARISON_FUNC MapCompareOp(CompareOp op) noexcept
	{
		switch (op)
		{
		case CompareOp::eNever:			 return D3D12_COMPARISON_FUNC_NEVER;
		case CompareOp::eLess:			 return D3D12_COMPARISON_FUNC_LESS;
		case CompareOp::eEqual:			 return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareOp::eLessOrEqual:	 return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareOp::eGreater:		 return D3D12_COMPARISON_FUNC_GREATER;
		case CompareOp::eNotEqual:		 return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareOp::eGreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareOp::eAlways:		 return D3D12_COMPARISON_FUNC_ALWAYS;
		}
		return D3D12_COMPARISON_FUNC_ALWAYS;
	}

	void FillBorderColor(float (&out)[4], BorderColor color) noexcept
	{
		switch (color)
		{
		case BorderColor::eOpaqueBlackFloat:
		case BorderColor::eOpaqueBlackInt:
			out[0] = out[1] = out[2] = 0.0f;
			out[3]					 = 1.0f;
			break;
		case BorderColor::eOpaqueWhiteFloat:
		case BorderColor::eOpaqueWhiteInt:		  out[0] = out[1] = out[2] = out[3] = 1.0f; break;
		case BorderColor::eTransparentBlackFloat:
		case BorderColor::eTransparentBlackInt:	  out[0] = out[1] = out[2] = out[3] = 0.0f; break;
		}
	}

	// Encodes the RHI filter and comparison state into a single D3D12_FILTER value.
	[[nodiscard]] D3D12_FILTER MapFilter(const SamplerDesc & desc) noexcept
	{
		const D3D12_FILTER_REDUCTION_TYPE reduction = desc.compareEnable ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
		if (desc.anisotropyEnable)
		{
			return D3D12_ENCODE_ANISOTROPIC_FILTER(reduction);
		}
		const D3D12_FILTER_TYPE minFilter = desc.minFilter == Filter::eLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
		const D3D12_FILTER_TYPE magFilter = desc.magFilter == Filter::eLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
		const D3D12_FILTER_TYPE mipFilter = desc.mipmapMode == MipmapMode::eLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
		return D3D12_ENCODE_BASIC_FILTER(minFilter, magFilter, mipFilter, reduction);
	}

	SamplerHandle D3D12CreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createSampler");

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.ycbcrConversion != nullptr)
		{
			return FailValue<SamplerHandle>(
				error, ErrorCode::eUnsupportedFeature, "Direct3D 12 has no sampler Y'CbCr conversion, so convert in the shader over per-plane views");
		}

		D3D12_SAMPLER_DESC sampler{};
		sampler.Filter		   = MapFilter(desc);
		sampler.AddressU	   = MapAddressMode(desc.addressU);
		sampler.AddressV	   = MapAddressMode(desc.addressV);
		sampler.AddressW	   = MapAddressMode(desc.addressW);
		sampler.MipLODBias	   = desc.mipLodBias;
		sampler.MaxAnisotropy  = static_cast<UINT>(desc.maxAnisotropy);
		sampler.ComparisonFunc = desc.compareEnable ? MapCompareOp(desc.compareOp) : D3D12_COMPARISON_FUNC_NEVER;
		sampler.MinLOD		   = desc.minLod;
		sampler.MaxLOD		   = desc.maxLod;
		FillBorderColor(sampler.BorderColor, desc.borderColor);

		return ReturnValue(device->samplerSlots.Store(SamplerSlot{ .desc = sampler }), error);
	}

	bool D3D12DestroySampler(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const SamplerHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		if (device->samplerSlots.Resolve(slotHandle, true) == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid sampler handle");
		}
		static_cast<void>(device->samplerSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	[[nodiscard]] D3D12_HEAP_TYPE MapHeapClass(HeapType type) noexcept
	{
		switch (type)
		{
		case HeapType::eCpuUpload:	 return D3D12_HEAP_TYPE_UPLOAD;
		case HeapType::eCpuReadback: return D3D12_HEAP_TYPE_READBACK;
		case HeapType::eGpuLocal:
		case HeapType::eTransient:	 return D3D12_HEAP_TYPE_DEFAULT;
		}
		return D3D12_HEAP_TYPE_DEFAULT;
	}

	[[nodiscard]] HeapSlot * ResolveHeap(D3D12Device * device, HeapHandle handle) noexcept
	{
		return device->heapSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	HeapHandle D3D12CreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept
	{
		if (!D3D12RefuseUnexportable(desc.exportableHandleTypes,
				Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Heap,
				"heap creation asked for an external handle type Direct3D 12 cannot export",
				error))
		{
			return HeapHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createHeap");

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.size == 0)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be greater than zero");
		}

		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes	 = desc.size;
		heapDesc.Properties.Type = MapHeapClass(desc.type);
		heapDesc.Alignment		 = desc.alignment != 0 ? desc.alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		// Resource-heap tier 2 allows buffers and textures in one heap, which the transient allocator needs. Buffer-only heaps narrow it.
		heapDesc.Flags = desc.allowTextures ? D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES : D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

		// A shared heap is the granularity a caller placing its own resources shares at, and it is device local only: Direct3D 12 does not share upload
		// or readback memory.
		if (!desc.exportableHandleTypes.Empty())
		{
			if (heapDesc.Properties.Type != D3D12_HEAP_TYPE_DEFAULT)
			{
				return FailValue<HeapHandle>(
					error, ErrorCode::eUnsupportedFeature, "Direct3D 12 cannot share upload or readback memory, so an exportable heap must be device local");
			}

			heapDesc.Flags |= D3D12_HEAP_FLAG_SHARED;
		}

		ComPtr<ID3D12Heap> heap;
		if (FAILED(device->device->CreateHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
		{
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "ID3D12Device::CreateHeap failed");
		}

		return ReturnValue(device->heapSlots.Store(HeapSlot{
							   .heap				  = std::move(heap),
							   .type				  = heapDesc.Properties.Type,
							   .size				  = desc.size,
							   .exportableHandleTypes = desc.exportableHandleTypes,
						   }),
			error);
	}

	BufferHandle D3D12CreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createPlacedBuffer");

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.buffer.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be greater than zero");
		}

		ComPtr<ID3D12Heap> heap;
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
		{
			HeapSlot * heapSlot = ResolveHeap(device, desc.heap);
			if (heapSlot == nullptr)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer references an invalid heap");
			}
			heap	 = heapSlot->heap;
			heapType = heapSlot->type;
		}

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension		  = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width			  = desc.buffer.size;
		resourceDesc.Height			  = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels		  = 1;
		resourceDesc.Format			  = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout			  = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags			  = MapBufferResourceFlags(desc.buffer.usage);

		ComPtr<ID3D12Resource> resource;
		if (FAILED(device->device->CreatePlacedResource(
				heap.Get(), desc.offset, &resourceDesc, InitialBufferState(heapType), nullptr, IID_PPV_ARGS(resource.GetAddressOf()))))
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "CreatePlacedResource failed for a buffer");
		}

		// A placed buffer owns no D3D12MA allocation: its memory belongs to the heap. The heap type fixes mappability, ignoring the desc memory hint.
		const bool hostVisible = heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK;
		return ReturnValue(device->bufferSlots.Store(BufferSlot{
							   .resource	= std::move(resource),
							   .size		= desc.buffer.size,
							   .hostVisible = hostVisible,
							   .desc		= detail::Recorded(desc.buffer),
						   }),
			error);
	}

	TextureHandle D3D12CreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createPlacedTexture");

		auto * device			 = static_cast<D3D12Device *>(impl);
		const DXGI_FORMAT format = MapFormat(desc.texture.format);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "placed texture format is undefined or unsupported");
		}
		if (desc.texture.allowFormatViews && TypelessResourceFormat(format) == format)
		{
			return FailValue<TextureHandle>(
				error, ErrorCode::eUnsupportedFormat, "allowFormatViews needs a format with a Direct3D 12 typeless family, which this one has not");
		}

		ComPtr<ID3D12Heap> heap;
		{
			HeapSlot * heapSlot = ResolveHeap(device, desc.heap);
			if (heapSlot == nullptr)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture references an invalid heap");
			}
			heap = heapSlot->heap;
		}

		const D3D12_RESOURCE_DESC resourceDesc = MakeTextureResourceDesc(desc.texture);
		ComPtr<ID3D12Resource> resource;
		if (FAILED(device->device->CreatePlacedResource(
				heap.Get(), desc.offset, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.GetAddressOf()))))
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "CreatePlacedResource failed for a texture");
		}

		return ReturnValue(device->textureSlots.Store(TextureSlot{ .resource = std::move(resource),
							   .format										 = format,
							   .rhiFormat									 = desc.texture.format,
							   .type										 = desc.texture.type,
							   .mipLevels									 = desc.texture.mipLevels,
							   .arrayLayers									 = static_cast<std::uint32_t>(resourceDesc.DepthOrArraySize),
							   .usage										 = desc.texture.usage,
							   .mutableFormat								 = desc.texture.allowFormatViews,
							   .desc										 = detail::Recorded(desc.texture) }),
			error);
	}

	bool D3D12DestroyHeap(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const HeapHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		HeapSlot * slot = device->heapSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid heap handle");
		}
		slot->heap.Reset();
		static_cast<void>(device->heapSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

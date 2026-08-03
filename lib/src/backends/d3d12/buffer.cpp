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
	[[nodiscard]] BufferSlot * ResolveBuffer(D3D12Device * device, BufferHandle handle) noexcept
	{
		return device->bufferSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	// Maps a MemoryUsage onto a D3D12 heap type, reporting whether the result is CPU-mappable.
	[[nodiscard]] D3D12_HEAP_TYPE MapHeapType(MemoryUsage memory, bool & hostVisible) noexcept
	{
		switch (memory)
		{
		case MemoryUsage::eCpuUpload:
		case MemoryUsage::eCpuToGpu:	hostVisible = true; return D3D12_HEAP_TYPE_UPLOAD;
		case MemoryUsage::eCpuReadback:
		case MemoryUsage::eGpuToCpu:	hostVisible = true; return D3D12_HEAP_TYPE_READBACK;
		default:						hostVisible = false; return D3D12_HEAP_TYPE_DEFAULT;
		}
	}

	// The initial resource state D3D12 requires for a buffer placed in the given heap.
	[[nodiscard]] D3D12_RESOURCE_STATES InitialBufferState(D3D12_HEAP_TYPE heap) noexcept
	{
		switch (heap)
		{
		case D3D12_HEAP_TYPE_UPLOAD:   return D3D12_RESOURCE_STATE_GENERIC_READ;
		case D3D12_HEAP_TYPE_READBACK: return D3D12_RESOURCE_STATE_COPY_DEST;
		default:					   return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	[[nodiscard]] D3D12_RESOURCE_FLAGS MapBufferResourceFlags(Flags<BufferUsage> usage) noexcept
	{
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		if (usage.Contains(BufferUsage::eStorage))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		return flags;
	}

	// Clamps a map request to the buffer. The whole-buffer sentinel resolves to the bytes left after offset, any other overrun fails.
	[[nodiscard]] bool BoundBufferRange(std::uint64_t bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept
	{
		if (offset > bufferSize)
		{
			return false;
		}
		if (size == std::numeric_limits<std::uint64_t>::max())
		{
			size = bufferSize - offset;
			return true;
		}
		return offset + size <= bufferSize;
	}

	BufferHandle D3D12CreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept
	{
		if (!D3D12RefuseUnexportable(desc.exportableHandleTypes,
				Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Resource,
				"buffer creation asked for an external handle type Direct3D 12 cannot export",
				error))
		{
			return BufferHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createBuffer");

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
		}

		bool hostVisible			   = false;
		const D3D12_HEAP_TYPE heapType = MapHeapType(desc.memory, hostVisible);

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension		  = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width			  = desc.size;
		resourceDesc.Height			  = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels		  = 1;
		resourceDesc.Format			  = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout			  = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags			  = MapBufferResourceFlags(desc.usage);
		// D3D12 rejects ALLOW_UNORDERED_ACCESS on a host-visible heap (debug-layer error #638). A host-visible storage buffer is CPU-written and shader-read so it
		// needs no UAV: drop the flag, not fail the create.
		if (hostVisible)
		{
			resourceDesc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		// D3D12 rounds a CBV's SizeInBytes up to 256 so a smaller allocation leaves the view reading past the end of the resource (debug-layer error #649, hit by 96
		// and 128-byte uniform buffers). Pad to the same multiple so the rounded CBV always fits.
		if (desc.usage.Contains(BufferUsage::eUniform))
		{
			resourceDesc.Width = (desc.size + 255) & ~static_cast<std::uint64_t>(255);
		}

		// Reserved buffer: a virtual range with no backing store, tiled in later through bindSparse. It owns no D3D12MA allocation and cannot be host-visible until
		// tiles are mapped so the memory hint is ignored. Created in COMMON like any device-local buffer.
		if (desc.allowSparseBinding)
		{
			ComPtr<ID3D12Resource> reserved;
			if (FAILED(device->device->CreateReservedResource(&resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(reserved.GetAddressOf()))))
			{
				return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "CreateReservedResource failed for a sparse buffer");
			}

			NameD3D12Object(reserved.Get(), desc.debugName, device->debugNames);

			return ReturnValue(device->bufferSlots.Store(BufferSlot{
								   .resource	= std::move(reserved),
								   .size		= desc.size,
								   .hostVisible = false,
								   .reserved	= true,
								   .desc		= detail::Recorded(desc),
							   }),
				error);
		}

		D3D12MA::ALLOCATION_DESC allocationDesc{};
		allocationDesc.HeapType = heapType;

		/*
		 * An exportable buffer is a committed resource in a shared heap.
		 *
		 * Committed, not placed because CreateSharedHandle names a resource only when that resource owns its whole heap. A placed one is shared through its heap's
		 * handle instead, which is what the heap declaration exists for, and it is also why the flag alone would not do: D3D12MA is free to satisfy extra heap flags
		 * with a separate heap and a placed resource in it.
		 */
		if (!desc.exportableHandleTypes.Empty())
		{
			if (heapType != D3D12_HEAP_TYPE_DEFAULT)
			{
				return FailValue<BufferHandle>(
					error, ErrorCode::eUnsupportedFeature, "Direct3D 12 cannot share upload or readback memory, so an exportable buffer must be device local");
			}

			allocationDesc.ExtraHeapFlags |= D3D12_HEAP_FLAG_SHARED;
			allocationDesc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
		}

		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		const HRESULT hr = device->allocator->CreateResource(
			&allocationDesc, &resourceDesc, InitialBufferState(heapType), nullptr, allocation.GetAddressOf(), IID_PPV_ARGS(resource.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "D3D12MA::CreateResource failed for a buffer");
		}

		NameD3D12Object(resource.Get(), desc.debugName, device->debugNames);

		return ReturnValue(device->bufferSlots.Store(BufferSlot{ .allocation = std::move(allocation),
							   .resource									 = std::move(resource),
							   .size										 = desc.size,
							   .hostVisible									 = hostVisible,
							   .exportableHandleTypes						 = desc.exportableHandleTypes,
							   .desc										 = detail::Recorded(desc) }),
			error);
	}

	MappedMemory D3D12Map(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.map");

		auto * device	  = static_cast<D3D12Device *>(impl);
		BufferSlot * slot = ResolveBuffer(device, handle);
		if (slot == nullptr)
		{
			return FailValue<MappedMemory>(error, ErrorCode::eInvalidHandle, "map of an invalid buffer handle");
		}
		if (!slot->hostVisible)
		{
			return FailValue<MappedMemory>(error, ErrorCode::eInvalidArgument, "map of a buffer whose memory is not host visible");
		}

		std::uint64_t mapSize = desc.size;
		if (!BoundBufferRange(slot->size, desc.offset, mapSize))
		{
			return FailValue<MappedMemory>(error, ErrorCode::eInvalidArgument, "map range is outside the buffer");
		}

		void * mapped = nullptr;
		// A null read range tells D3D12 the CPU may access the whole resource. The RHI enforces the bounds.
		if (FAILED(slot->resource->Map(0, nullptr, &mapped)))
		{
			return FailValue<MappedMemory>(error, ErrorCode::eNativeApiError, "ID3D12Resource::Map failed");
		}

		// D3D12 UPLOAD and READBACK heaps present a coherent mapping so flush and invalidate are no-ops.
		return ReturnValue(
			MappedMemory{
				.data	  = static_cast<std::uint8_t *>(mapped) + desc.offset,
				.size	  = mapSize,
				.coherent = true,
			},
			error);
	}

	bool D3D12Unmap(void * impl, BufferHandle handle, Error * error) noexcept
	{
		auto * device	  = static_cast<D3D12Device *>(impl);
		BufferSlot * slot = ResolveBuffer(device, handle);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "unmap of an invalid buffer handle");
		}
		slot->resource->Unmap(0, nullptr);
		return Succeed(error);
	}

	bool D3D12FlushMappedRange(
		void * impl, BufferHandle handle, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] std::uint64_t size, Error * error) noexcept
	{
		auto * device = static_cast<D3D12Device *>(impl);
		return ResolveBuffer(device, handle) != nullptr ? Succeed(error) : Fail(error, ErrorCode::eInvalidHandle, "flush of an invalid buffer handle");
	}

	bool D3D12InvalidateMappedRange(
		void * impl, BufferHandle handle, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] std::uint64_t size, Error * error) noexcept
	{
		auto * device = static_cast<D3D12Device *>(impl);
		return ResolveBuffer(device, handle) != nullptr ? Succeed(error) : Fail(error, ErrorCode::eInvalidHandle, "invalidate of an invalid buffer handle");
	}

	bool D3D12GetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.getBufferMemoryInfo");

		auto * device = static_cast<D3D12Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "memory info output is null");
		}

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension		  = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width			  = desc.size;
		resourceDesc.Height			  = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels		  = 1;
		resourceDesc.Format			  = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout			  = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags			  = MapBufferResourceFlags(desc.usage);

		const D3D12_RESOURCE_ALLOCATION_INFO info = device->device->GetResourceAllocationInfo(0, 1, &resourceDesc);
		*out									  = MemoryInfo{
			.size	   = info.SizeInBytes,
			.alignment = info.Alignment,
		};
		return Succeed(error);
	}

	// Releases a buffer and frees its slot. Without timeline retire tracking, the caller must not destroy an in-use buffer.
	bool D3D12DestroyBuffer(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const BufferHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		BufferSlot * slot = device->bufferSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid buffer handle");
		}
		if (slot->lifetime == SlotLifetime::eAdopted)
		{
			// The caller owns the resource so leave it alone. The slot is still retired so the handle stops resolving and the reference the slot held goes when the slot
			// is reused.
			static_cast<void>(device->bufferSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		slot->resource.Reset();
		slot->allocation.Reset();
		static_cast<void>(device->bufferSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	// Maps an RHI format onto DXGI. eUndefined and anything unmapped return DXGI_FORMAT_UNKNOWN, which texture creation rejects.

} // namespace azo::rhi::d3d12

#endif // _WIN32

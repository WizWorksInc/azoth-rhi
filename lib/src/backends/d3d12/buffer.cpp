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
	[[nodiscard]] BufferSlot * ResolveBuffer(D3D12Device * device, BufferHandle handle) noexcept
	{
		return device->bufferSlots.Resolve(handle, kHandleAlreadyChecked);
	}

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

	[[nodiscard]] D3D12_RESOURCE_STATES InitialBufferState(D3D12_HEAP_TYPE heap, Flags<BufferUsage> usage) noexcept
	{
		if (usage.Contains(BufferUsage::eAccelerationStructureStorage))
		{
			return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		}

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
		if (usage.Contains(BufferUsage::eStorage) || usage.Contains(BufferUsage::eAccelerationStructureStorage))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		return flags;
	}

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

		if (desc.usage.Contains(BufferUsage::eAccelerationStructureStorage) && heapType != D3D12_HEAP_TYPE_DEFAULT)
		{
			return FailValue<BufferHandle>(error,
				ErrorCode::eInvalidArgument,
				"an acceleration structure buffer must be device local, since Direct3D 12 places one only in the default heap");
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
		if (hostVisible)
		{
			resourceDesc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		if (desc.usage.Contains(BufferUsage::eUniform))
		{
			resourceDesc.Width = (desc.size + 255) & ~static_cast<std::uint64_t>(255);
		}

		if (desc.allowSparseBinding)
		{
			ComPtr<ID3D12Resource> reserved;
			if (FAILED(device->device->CreateReservedResource(
					&resourceDesc, InitialBufferState(D3D12_HEAP_TYPE_DEFAULT, desc.usage), nullptr, IID_PPV_ARGS(reserved.GetAddressOf()))))
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
		const HRESULT hr = device->allocator->CreateResource(&allocationDesc,
			&resourceDesc,
			InitialBufferState(heapType, desc.usage),
			nullptr,
			allocation.GetAddressOf(),
			IID_PPV_ARGS(resource.GetAddressOf()));
		if (FAILED(hr))
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "D3D12MA::CreateResource failed for a buffer");
		}

		NameD3D12Object(resource.Get(), desc.debugName, device->debugNames);

		return ReturnValue(device->bufferSlots.Store(BufferSlot{ .allocation = std::move(allocation),
							   .resource									 = std::move(resource),
							   .size										 = desc.size,
							   .hostVisible									 = hostVisible,
							   .heapType									 = heapType,
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
		if (FAILED(slot->resource->Map(0, nullptr, &mapped)))
		{
			return FailValue<MappedMemory>(error, ErrorCode::eNativeApiError, "ID3D12Resource::Map failed");
		}

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
			static_cast<void>(device->bufferSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		slot->resource.Reset();
		slot->allocation.Reset();
		static_cast<void>(device->bufferSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

}

#endif

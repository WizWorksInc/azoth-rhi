// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	[[nodiscard]] MTL::Heap * ResolveHeap(MetalDevice * device, HeapHandle handle) noexcept
	{
		const auto * tracked = device->heaps.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->get() : nullptr;
	}

	HeapHandle MetalCreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept
	{
		if (!MetalRefuseUnexportable(desc.exportableHandleTypes, {}, "Metal exports no heaps, so a heap cannot be created exportable", error))
		{
			return HeapHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createHeap");

		if (desc.size == 0)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be non-zero");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		NS::SharedPtr<MTL::HeapDescriptor> descriptor = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
		descriptor->setType(MTL::HeapTypePlacement);
		descriptor->setStorageMode(MetalHeapStorage(desc.type));
		descriptor->setSize(desc.size);

		MTL::Heap * raw = device->device->newHeap(descriptor.get());
		if (raw == nullptr)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal heap allocation failed");
		}
		if (desc.debugName != nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> labelPool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			raw->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}
		NS::SharedPtr<MTL::Heap> heap = NS::TransferPtr(raw);

		device->NoteAllocation(MetalDevice::Residency::eHeaps, heap.get());

		const HeapHandle handle = device->heaps.Store(std::move(heap));
		if (!handle.IsValid())
		{
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Metal heap handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	BufferHandle MetalCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPlacedBuffer");

		if (desc.buffer.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be non-zero");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		MTL::Heap * heap = ResolveHeap(device, desc.heap);
		if (heap == nullptr)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer names a heap this device never created");
		}

		MTL::Buffer * raw = heap->newBuffer(static_cast<NS::UInteger>(desc.buffer.size), MetalResourceOptions(heap->storageMode()), desc.offset);
		if (raw == nullptr)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal placed buffer allocation failed");
		}
		SetMetalLabel(raw, desc.buffer.debugName);
		NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(raw);

		const BufferHandle handle = device->buffers.Store(MetalBufferSlot{ .buffer = std::move(buffer), .desc = detail::Recorded(desc.buffer) });
		if (!handle.IsValid())
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed buffer handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TextureHandle MetalCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPlacedTexture");

		auto * device = static_cast<MetalDevice *>(impl);

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = BuildTextureDescriptor(desc.texture, error);
		if (descriptor.get() == nullptr)
		{
			return {};
		}

		MTL::Heap * heap = ResolveHeap(device, desc.heap);
		if (heap == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture names a heap this device never created");
		}
		descriptor->setStorageMode(heap->storageMode());

		MTL::Texture * raw = heap->newTexture(descriptor.get(), desc.offset);
		if (raw == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal placed texture allocation failed");
		}
		SetMetalLabel(raw, desc.texture.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		const TextureHandle handle = device->textures.Store(MetalTextureSlot{ .texture = std::move(texture),
			.format																	   = desc.texture.format,
			.usage																	   = desc.texture.usage,
			.mutableFormat															   = desc.texture.allowFormatViews,
			.desc																	   = detail::Recorded(desc.texture) });
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed texture handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

}

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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	[[nodiscard]] MTL::Heap * ResolveHeap(Metal4Device * device, HeapHandle handle) noexcept
	{
		const auto * tracked = device->heaps.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->get() : nullptr;
	}

	HeapHandle CreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept
	{
		// Metal has no shared heap, so any declaration is refused, the same way a buffer is.
		if (!RefuseUnexportable(desc.exportableHandleTypes, {}, "Metal exports no heaps, so a heap cannot be created exportable", error))
		{
			return HeapHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createHeap");

		// Metal traps on a heap with no size instead of answering null, so this is refused here and not there. The same refusal a zero-sized buffer already
		// gets, for the same reason.
		if (desc.size == 0)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be non-zero");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		NS::SharedPtr<MTL::HeapDescriptor> descriptor = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
		// Placement heaps let the RHI place resources at explicit offsets, matching the desc contract.
		descriptor->setType(MTL::HeapTypePlacement);
		descriptor->setStorageMode(MetalHeapStorage(desc.type));
		descriptor->setSize(desc.size);

		MTL::Heap * raw = device->device->newHeap(descriptor.get());
		if (raw == nullptr)
		{
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal heap allocation failed");
		}
		// MTLHeap is not an MTLResource so it carries its own label setter without going through SetMetalLabel.
		if (desc.debugName != nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> labelPool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			raw->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}
		NS::SharedPtr<MTL::Heap> heap = NS::TransferPtr(raw);

		// A heap is an MTLAllocation, so making the heap resident covers everything placed in it.
		device->NoteAllocation(Metal4Device::Residency::eHeaps, heap.get());

		const HeapHandle handle = device->heaps.Store(std::move(heap));
		if (!handle.IsValid())
		{
			return FailValue<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Metal heap handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	BufferHandle CreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createPlacedBuffer");

		if (desc.buffer.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be non-zero");
		}

		auto * device = static_cast<Metal4Device *>(impl);

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

		const BufferHandle handle = device->buffers.Store(Metal4BufferSlot{ .buffer = std::move(buffer), .desc = detail::Recorded(desc.buffer) });
		if (!handle.IsValid())
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed buffer handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TextureHandle CreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createPlacedTexture");

		auto * device = static_cast<Metal4Device *>(impl);

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
		// A placed texture inherits its heap's storage mode.
		descriptor->setStorageMode(heap->storageMode());

		MTL::Texture * raw = heap->newTexture(descriptor.get(), desc.offset);
		if (raw == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal placed texture allocation failed");
		}
		SetMetalLabel(raw, desc.texture.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		const TextureHandle handle = device->textures.Store(Metal4TextureSlot{ .texture = std::move(texture),
			.format																		= desc.texture.format,
			.usage																		= desc.texture.usage,
			.mutableFormat																= desc.texture.allowFormatViews,
			.desc																		= detail::Recorded(desc.texture) });
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed texture handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal4

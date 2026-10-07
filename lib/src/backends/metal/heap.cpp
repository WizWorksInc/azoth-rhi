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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLHeap.hpp>
#include <Metal/MTLResource.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal
{
	[[nodiscard]] MTL::Heap * resolve_heap(MetalDevice * device, HeapHandle handle) noexcept
	{
		const auto * tracked = device->heaps.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->get() : nullptr;
	}

	HeapHandle metal_create_heap(void * impl, const HeapDesc & desc, Error * error) noexcept
	{
		if (!metal_refuse_unexportable(desc.exportableHandleTypes, {}, "Metal exports no heaps, so a heap cannot be created exportable", error))
		{
			return HeapHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createHeap");

		if (desc.size == 0)
		{
			return fail_value<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be non-zero");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		NS::SharedPtr<MTL::HeapDescriptor> descriptor = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
		descriptor->setType(MTL::HeapTypePlacement);
		descriptor->setStorageMode(metal_heap_storage(desc.type));
		descriptor->setHazardTrackingMode(MTL::HazardTrackingModeTracked);
		descriptor->setSize(desc.size);

		MTL::Heap * raw = device->device->newHeap(descriptor.get());
		if (raw == nullptr)
		{
			return fail_value<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal heap allocation failed");
		}
		if (desc.debugName != nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> labelPool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			raw->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}
		NS::SharedPtr<MTL::Heap> heap = NS::TransferPtr(raw);

		device->note_allocation(MetalDevice::Residency::eHeaps, heap.get());

		const HeapHandle handle = device->heaps.store(std::move(heap));
		if (!handle.is_valid())
		{
			return fail_value<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Metal heap handle tracking failed");
		}

		return return_value(handle, error);
	}

	BufferHandle metal_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPlacedBuffer");

		if (desc.buffer.size == 0)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be non-zero");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		MTL::Heap * heap = resolve_heap(device, desc.heap);
		if (heap == nullptr)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer names a heap this device never created");
		}

		MTL::Buffer * raw = heap->newBuffer(static_cast<NS::UInteger>(desc.buffer.size), metal_resource_options(heap->storageMode()), desc.offset);
		if (raw == nullptr)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal placed buffer allocation failed");
		}
		set_metal_label(raw, desc.buffer.debugName);
		NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(raw);

		const BufferHandle handle = device->buffers.store(MetalBufferSlot{ .buffer = std::move(buffer), .desc = detail::recorded(desc.buffer) });
		if (!handle.is_valid())
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed buffer handle tracking failed");
		}

		return return_value(handle, error);
	}

	TextureHandle metal_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPlacedTexture");

		auto * device = static_cast<MetalDevice *>(impl);

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = build_texture_descriptor(desc.texture, error);
		if (descriptor.get() == nullptr)
		{
			return {};
		}

		MTL::Heap * heap = resolve_heap(device, desc.heap);
		if (heap == nullptr)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture names a heap this device never created");
		}
		descriptor->setStorageMode(heap->storageMode());

		MTL::Texture * raw = heap->newTexture(descriptor.get(), desc.offset);
		if (raw == nullptr)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal placed texture allocation failed");
		}
		set_metal_label(raw, desc.texture.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		const TextureHandle handle = device->textures.store(
			MetalTextureSlot{
				.texture	   = std::move(texture),
				.format		   = desc.texture.format,
				.usage		   = desc.texture.usage,
				.mutableFormat = desc.texture.allowFormatViews,
				.desc		   = detail::recorded(desc.texture),
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal placed texture handle tracking failed");
		}

		return return_value(handle, error);
	}

}

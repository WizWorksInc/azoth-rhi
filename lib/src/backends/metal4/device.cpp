// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLResource.hpp>
#include <atomic>
#include <cstdint>
#include <limits>

namespace azo::rhi::metal4
{
	MappedMemory metal4_map(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.map");

		auto * device = static_cast<Metal4Device *>(impl);

		auto * tracked = device->buffers.resolve(buffer, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			fail(error, ErrorCode::eInvalidHandle, "Map of a buffer this device never created");
			return {};
		}

		MTL::Buffer * raw = tracked->buffer.get();

		if (raw->storageMode() == MTL::StorageModePrivate && !device->allowDeviceLocalMapping)
		{
			fail(error, ErrorCode::eInvalidArgument, "map of a buffer whose memory is not host visible, without DeviceDesc::allowDeviceLocalMapping");
			return {};
		}

		void * contents = raw->contents();
		if (contents == nullptr)
		{
			fail(error, ErrorCode::eUnsupportedFeature, "Map of a buffer without CPU-visible storage");
			return {};
		}

		const auto length = static_cast<std::uint64_t>(raw->length());
		if (desc.offset > length)
		{
			fail(error, ErrorCode::eInvalidArgument, "Map offset is beyond the buffer length");
			return {};
		}

		const std::uint64_t size = (desc.size == std::numeric_limits<std::uint64_t>::max()) ? (length - desc.offset) : desc.size;
		if (size > length - desc.offset)
		{
			fail(error, ErrorCode::eInvalidArgument, "Map range extends beyond the buffer length");
			return {};
		}

		if (!tracked->mapCount.try_acquire())
		{
			fail(error, ErrorCode::eInvalidState, kMapCountWouldOverflow);
			return {};
		}

		succeed(error);
		return MappedMemory{
			.data	  = static_cast<char *>(contents) + desc.offset,
			.size	  = size,
			.coherent = true,
		};
	}

	bool metal4_unmap(void * impl, BufferHandle buffer, Error * error) noexcept
	{
		auto * tracked = static_cast<Metal4Device *>(impl)->buffers.resolve(buffer, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "unmap of a buffer this device never created");
		}
		if (!tracked->mapCount.try_release())
		{
			return fail(error, ErrorCode::eInvalidState, "unmap of a buffer with no map outstanding");
		}

		return succeed(error);
	}

	bool metal4_query_memory_budget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "memory budget output pointer is null");
		}

		auto * device			   = static_cast<Metal4Device *>(impl);
		const std::uint64_t budget = device->device->recommendedMaxWorkingSetSize();
		const std::uint64_t usage  = device->device->currentAllocatedSize();

		*out = MemoryBudgetInfo{
			.heap						  = heap,
			.budgetBytes				  = budget,
			.usageBytes					  = usage,
			.availableForReservationBytes = (budget > usage) ? (budget - usage) : 0,
			.budgetIsPrecise			  = false,
		};
		return succeed(error);
	}

	bool metal4_destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.destroy");

		auto * device = static_cast<Metal4Device *>(impl);

		constexpr bool kMatchIdentity = true;

		if (type == ResourceType::eTexture)
		{
			const Metal4TextureSlot * const slot = device->textures.resolve(typed<TextureHandle>(handle), kMatchIdentity);
			if (slot != nullptr && slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer texture is not allowed");
			}
		}
		else if (type == ResourceType::eTextureView)
		{
			const Metal4TextureViewSlot * const slot = device->textureViews.resolve(typed<TextureViewHandle>(handle), kMatchIdentity);
			if (slot != nullptr && slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer view is not allowed");
			}
		}

		bool retired = false;
		switch (type)
		{
		case ResourceType::eBuffer:			  retired = device->buffers.retire(typed<BufferHandle>(handle), kMatchIdentity); break;
		case ResourceType::eTexture:		  retired = device->textures.retire(typed<TextureHandle>(handle), kMatchIdentity); break;
		case ResourceType::eTextureView:	  retired = device->textureViews.retire(typed<TextureViewHandle>(handle), kMatchIdentity); break;
		case ResourceType::eSampler:		  retired = device->samplers.retire(typed<SamplerHandle>(handle), kMatchIdentity); break;
		case ResourceType::eHeap:			  retired = device->heaps.retire(typed<HeapHandle>(handle), kMatchIdentity); break;
		case ResourceType::eTimeline:		  retired = device->timelines.retire(typed<TimelineHandle>(handle), kMatchIdentity); break;
		case ResourceType::eBinarySemaphore:  retired = device->binarySemaphores.retire(typed<BinarySemaphoreHandle>(handle), kMatchIdentity); break;
		case ResourceType::eGraphicsPipeline: retired = device->graphicsPipelines.retire(typed<GraphicsPipelineHandle>(handle), kMatchIdentity); break;
		case ResourceType::eComputePipeline:  retired = device->computePipelines.retire(typed<ComputePipelineHandle>(handle), kMatchIdentity); break;
		case ResourceType::eQueryPool:		  retired = device->queryPools.retire(typed<QueryPoolHandle>(handle), kMatchIdentity); break;

		case ResourceType::eDescriptorSet: retired = device->descriptorSets.retire(typed<DescriptorSetHandle>(handle), kMatchIdentity); break;

		case ResourceType::eDescriptorSetLayout: retired = device->descriptorSetLayouts.retire(typed<DescriptorSetLayoutHandle>(handle), kMatchIdentity); break;
		case ResourceType::ePipelineLayout:		 retired = device->pipelineLayouts.retire(typed<PipelineLayoutHandle>(handle), kMatchIdentity); break;

		default: retired = device->tracked.retire(type, handle, kMatchIdentity); break;
		}

		if (!retired)
		{
			return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed handle");
		}

		[[maybe_unused]] const std::uint64_t pending = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
		AZO_RHI_PROFILE_PLOT("rhi.metal4.pendingRetire", static_cast<std::int64_t>(pending));
		return succeed(error);
	}

	bool metal4_collect_garbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.collectGarbage");

		if (type == ResourceType::eBuffer)
		{
			static_cast<Metal4Device *>(impl)->pendingRetire.store(0, std::memory_order_relaxed);
			AZO_RHI_PROFILE_PLOT("rhi.metal4.pendingRetire", static_cast<std::int64_t>(0));
		}
		return succeed(error);
	}

	bool metal4_collect_garbage_timeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept
	{
		return metal4_collect_garbage(impl, type, error);
	}

}

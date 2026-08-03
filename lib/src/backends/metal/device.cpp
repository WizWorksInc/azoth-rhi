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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	MappedMemory MetalMap(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.map");

		auto * device = static_cast<MetalDevice *>(impl);

		const auto * tracked = device->buffers.Resolve(buffer, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			Fail(error, ErrorCode::eInvalidHandle, "Map of a buffer this device never created");
			return {};
		}

		MTL::Buffer * raw = tracked->buffer.get();
		void * contents	  = raw->contents();
		if (contents == nullptr)
		{
			Fail(error, ErrorCode::eUnsupportedFeature, "Map of a buffer without CPU-visible storage");
			return {};
		}

		const auto length = static_cast<std::uint64_t>(raw->length());
		if (desc.offset > length)
		{
			Fail(error, ErrorCode::eInvalidArgument, "Map offset is beyond the buffer length");
			return {};
		}

		const std::uint64_t size = (desc.size == std::numeric_limits<std::uint64_t>::max()) ? (length - desc.offset) : desc.size;
		if (desc.offset + size > length)
		{
			Fail(error, ErrorCode::eInvalidArgument, "Map range extends beyond the buffer length");
			return {};
		}

		Succeed(error);
		return MappedMemory{
			.data	  = static_cast<char *>(contents) + desc.offset,
			.size	  = size,
			.coherent = true,
		};
	}

	bool MetalQueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "memory budget output pointer is null");
		}

		auto * device			   = static_cast<MetalDevice *>(impl);
		const std::uint64_t budget = device->device->recommendedMaxWorkingSetSize();
		const std::uint64_t usage  = device->device->currentAllocatedSize();

		*out = MemoryBudgetInfo{
			.heap						  = heap,
			.budgetBytes				  = budget,
			.usageBytes					  = usage,
			.availableForReservationBytes = (budget > usage) ? (budget - usage) : 0,
			.budgetIsPrecise			  = false,
		};
		return Succeed(error);
	}

	bool MetalDestroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.destroy");

		auto * device = static_cast<MetalDevice *>(impl);

		/*
		 * A Metal command buffer retains the resources its encoders reference until it completes. Dropping the last SharedPtr here cannot free something the
		 * GPU is still reading. That lets the release be immediate and DestroyDesc::policy be ignored, where the Vulkan backend queues a deferred free.
		 *
		 * The retire matches on identity whatever the mode because a slot handed back twice would go onto the free list twice.
		 */
		constexpr bool matchIdentity = true;

		switch (type)
		{
		case ResourceType::eBuffer:			  static_cast<void>(device->buffers.Retire(Typed<BufferHandle>(handle), matchIdentity)); break;
		case ResourceType::eTexture:		  static_cast<void>(device->textures.Retire(Typed<TextureHandle>(handle), matchIdentity)); break;
		case ResourceType::eTextureView:	  static_cast<void>(device->textureViews.Retire(Typed<TextureViewHandle>(handle), matchIdentity)); break;
		case ResourceType::eSampler:		  static_cast<void>(device->samplers.Retire(Typed<SamplerHandle>(handle), matchIdentity)); break;
		case ResourceType::eHeap:			  static_cast<void>(device->heaps.Retire(Typed<HeapHandle>(handle), matchIdentity)); break;
		case ResourceType::eTimeline:		  static_cast<void>(device->timelines.Retire(Typed<TimelineHandle>(handle), matchIdentity)); break;
		case ResourceType::eBinarySemaphore:  static_cast<void>(device->binarySemaphores.Retire(Typed<BinarySemaphoreHandle>(handle), matchIdentity)); break;
		case ResourceType::eGraphicsPipeline: static_cast<void>(device->graphicsPipelines.Retire(Typed<GraphicsPipelineHandle>(handle), matchIdentity)); break;
		case ResourceType::eComputePipeline:  static_cast<void>(device->computePipelines.Retire(Typed<ComputePipelineHandle>(handle), matchIdentity)); break;
		case ResourceType::eQueryPool:		  static_cast<void>(device->queryPools.Retire(Typed<QueryPoolHandle>(handle), matchIdentity)); break;

		// Retired here, not left to the arena reset so a set destroyed by hand hands its slot back instead of outliving every destroy.
		case ResourceType::eDescriptorSet: static_cast<void>(device->descriptorSets.Retire(Typed<DescriptorSetHandle>(handle), matchIdentity)); break;

		// No native object behind either, but both hold what a pipeline checks its shaders against, so both have a slot of their own to hand back.
		case ResourceType::eDescriptorSetLayout:
			static_cast<void>(device->descriptorSetLayouts.Retire(Typed<DescriptorSetLayoutHandle>(handle), matchIdentity));
			break;
		case ResourceType::ePipelineLayout: static_cast<void>(device->pipelineLayouts.Retire(Typed<PipelineLayoutHandle>(handle), matchIdentity)); break;

		// The kinds with nothing native behind them, tracked for liveness alone.
		default: static_cast<void>(device->tracked.Retire(type, handle, matchIdentity)); break;
		}

		[[maybe_unused]] const std::uint64_t pending = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
		AZO_RHI_PROFILE_PLOT("rhi.metal.pendingRetire", static_cast<std::int64_t>(pending));
		return Succeed(error);
	}

	/*
	 * A destroy here releases its object outright so no kind has anything queued and there is nothing for a collect to free.
	 *
	 * All the sweep leaves to do is clear the counter and that counts destroys across the device, not within a kind so it is cleared on the first kind
	 * the sweep reaches, not sixteen times over.
	 */
	bool MetalCollectGarbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.collectGarbage");

		if (type == ResourceType::eBuffer)
		{
			static_cast<MetalDevice *>(impl)->pendingRetire.store(0, std::memory_order_relaxed);
			AZO_RHI_PROFILE_PLOT("rhi.metal.pendingRetire", static_cast<std::int64_t>(0));
		}
		return Succeed(error);
	}

	bool MetalCollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept
	{
		return MetalCollectGarbage(impl, type, error);
	}

} // namespace azo::rhi::metal

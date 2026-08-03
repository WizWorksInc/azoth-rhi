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

		/*
		 * Private memory is not mappable in the portable model and Vulkan and Null both refuse it. A unified adapter shares one pool, so contents() hands back a
		 * pointer anyway, and writing through it to a private resource is ordered against nothing the RHI knows about. Opt in per device to get it.
		 */
		if (raw->storageMode() == MTL::StorageModePrivate && !device->allowDeviceLocalMapping)
		{
			Fail(error, ErrorCode::eInvalidArgument, "map of a buffer whose memory is not host visible, without DeviceDesc::allowDeviceLocalMapping");
			return {};
		}

		void * contents = raw->contents();
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

		/*
		 * A back buffer and the view over it belong to the swapchain, which hands the same two handles back every frame and outlives this call. Retiring
		 * either would free its slot, and the next acquire writes that frame's drawable through an unvalidated resolve into whatever took it.
		 *
		 * Refused whatever the mode, as Vulkan and Null already do. The validation layer counts on that refusal to put the handle back.
		 */
		if (type == ResourceType::eTexture)
		{
			const MetalTextureSlot * const slot = device->textures.Resolve(Typed<TextureHandle>(handle), matchIdentity);
			if (slot != nullptr && slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer texture is not allowed");
			}
		}
		else if (type == ResourceType::eTextureView)
		{
			const MetalTextureViewSlot * const slot = device->textureViews.Resolve(Typed<TextureViewHandle>(handle), matchIdentity);
			if (slot != nullptr && slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer view is not allowed");
			}
		}

		bool retired = false;
		switch (type)
		{
		case ResourceType::eBuffer:			  retired = device->buffers.Retire(Typed<BufferHandle>(handle), matchIdentity); break;
		case ResourceType::eTexture:		  retired = device->textures.Retire(Typed<TextureHandle>(handle), matchIdentity); break;
		case ResourceType::eTextureView:	  retired = device->textureViews.Retire(Typed<TextureViewHandle>(handle), matchIdentity); break;
		case ResourceType::eSampler:		  retired = device->samplers.Retire(Typed<SamplerHandle>(handle), matchIdentity); break;
		case ResourceType::eHeap:			  retired = device->heaps.Retire(Typed<HeapHandle>(handle), matchIdentity); break;
		case ResourceType::eTimeline:		  retired = device->timelines.Retire(Typed<TimelineHandle>(handle), matchIdentity); break;
		case ResourceType::eBinarySemaphore:  retired = device->binarySemaphores.Retire(Typed<BinarySemaphoreHandle>(handle), matchIdentity); break;
		case ResourceType::eGraphicsPipeline: retired = device->graphicsPipelines.Retire(Typed<GraphicsPipelineHandle>(handle), matchIdentity); break;
		case ResourceType::eComputePipeline:  retired = device->computePipelines.Retire(Typed<ComputePipelineHandle>(handle), matchIdentity); break;
		case ResourceType::eQueryPool:		  retired = device->queryPools.Retire(Typed<QueryPoolHandle>(handle), matchIdentity); break;

		// Retired here, not left to the arena reset so a set destroyed by hand hands its slot back instead of outliving every destroy.
		case ResourceType::eDescriptorSet: retired = device->descriptorSets.Retire(Typed<DescriptorSetHandle>(handle), matchIdentity); break;

		// No native object behind either, but both hold what a pipeline checks its shaders against, so both have a slot of their own to hand back.
		case ResourceType::eDescriptorSetLayout:
			retired = device->descriptorSetLayouts.Retire(Typed<DescriptorSetLayoutHandle>(handle), matchIdentity);
			break;
		case ResourceType::ePipelineLayout: retired = device->pipelineLayouts.Retire(Typed<PipelineLayoutHandle>(handle), matchIdentity); break;

		// The kinds with nothing native behind them, tracked for liveness alone.
		default: retired = device->tracked.Retire(type, handle, matchIdentity); break;
		}

		/*
		 * The retire already matched on identity, so it has the answer to whether this handle was live. Reporting success anyway would be the silent no-op the
		 * API rules out, and it is the same answer Vulkan gives, in every validation mode, since the slot has to be resolved either way.
		 */
		if (!retired)
		{
			return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed handle");
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

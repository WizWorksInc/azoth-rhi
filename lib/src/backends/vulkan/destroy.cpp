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

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	/*
	 * Releases a retired object's native handles now or queues them for CollectGarbage when the destroy defers to a valid retire point. The caller has already
	 * retired the slot so the object is unreachable either way. A default DestroyDesc names no timeline and frees immediately.
	 *
	 * The queue this appends to is the one for the kind being destroyed and the RHI holds that kind's guard for the length of this call. The append needs no
	 * lock. pendingRetire stays device wide.
	 */
	bool RetireNative(VulkanDevice * device, ResourceType type, const DestroyDesc & desc, const PendingFree & pending, Error * error) noexcept
	{
		// The kind is the index into the per-kind queues, so a value outside the enum is refused, not used to reach past them.
		const std::size_t kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "destroy names a resource kind this device has no queue for");
		}

		if (desc.policy == DestroyPolicy::eDeferUntilSafe && desc.safeAfter.timeline.IsValid())
		{
			if (!detail::TryPushBack(device->garbage[kind], pending))
			{
				return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan deferred destroy queue allocation failed");
			}

			const std::uint64_t count = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
			AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(count));
			return true;
		}
		FreePending(device->device, device->dispatch, device->allocator, pending);
		return true;
	}

	// Releases what the deferred destroys of one kind have queued. The no-timeline form assumes the caller has idled the device so it drains that kind
	// outright. The timeline form releases only the objects whose retire point that timeline has reached.
	bool VulkanCollectGarbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.collectGarbage");
		auto * device								 = static_cast<VulkanDevice *>(impl);
		// The kind is the index into the per-kind queues, so a value outside the enum is refused, not used to reach past them.
		const std::size_t kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "collectGarbage names a resource kind this device has no queue for");
		}

		detail::HostVector<PendingFree> & collecting = device->garbage[kind];

		for (const PendingFree & pending : collecting)
		{
			FreePending(device->device, device->dispatch, device->allocator, pending);
		}

		const std::uint64_t freed = collecting.size();
		collecting.clear();

		AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(device->pendingRetire.fetch_sub(freed, std::memory_order_relaxed) - freed));
		return Succeed(error);
	}

	bool VulkanCollectGarbageTimeline(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.collectGarbage");
		auto * device								 = static_cast<VulkanDevice *>(impl);
		// The kind is the index into the per-kind queues, so a value outside the enum is refused, not used to reach past them.
		const std::size_t kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "collectGarbage names a resource kind this device has no queue for");
		}

		detail::HostVector<PendingFree> & collecting = device->garbage[kind];

		const std::uint64_t freed = std::erase_if(collecting,
			[&](const PendingFree & pending)
			{
				const bool ready = pending.safeAfter.timeline == timeline && pending.safeAfter.value <= completedValue;
				if (ready)
				{
					FreePending(device->device, device->dispatch, device->allocator, pending);
				}

				return ready;
			});

		AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(device->pendingRetire.fetch_sub(freed, std::memory_order_relaxed) - freed));
		return Succeed(error);
	}

	/*
	 * The single type erased destroy entry all the typed Device::Destroy overloads route through. Not every ResourceType gets a branch. Ray tracing pipelines
	 * and acceleration structures reach the eUnsupportedFeature tail because this backend does not create them. Descriptor sets reach it because their arena
	 * frees them wholesale through vkResetDescriptorPool.
	 *
	 * Generation validation matches the Null backend and the native release is immediate or deferred per RetireNative.
	 */
	// NOLINTNEXTLINE(readability-function-cognitive-complexity)
	bool VulkanDestroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.destroy");
		auto * device = static_cast<VulkanDevice *>(impl);

		if (type == ResourceType::eBuffer)
		{
			const BufferHandle bufferHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			BufferSlot * slot = device->bufferSlots.Resolve(bufferHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed buffer");
			}

			// An adopted buffer's VkBuffer is the caller's. Retire the slot so the handle stops resolving and leave the object alone.
			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->bufferSlots.Retire(bufferHandle, true));
				return Succeed(error);
			}

			if (slot->buffer != VK_NULL_HANDLE)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter	= desc.safeAfter,
							.buffer		= slot->buffer,
							.allocation = slot->allocation,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->bufferSlots.Retire(bufferHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eTexture)
		{
			const TextureHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TextureSlot * slot = device->textureSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed texture");
			}

			/*
			 * What destroy means depends on who owns the image.
			 *
			 * A back buffer is the swapchain's and destroying it is a caller mistake. An adopted one is the caller's, so the slot is retired and the VkImage is
			 * left for whatever library made it. Only an owned one is freed here.
			 */
			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer texture is not allowed");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->textureSlots.Retire(slotHandle, true));
				return Succeed(error);
			}

			if (!RetireNative(device,
					type,
					desc,
					PendingFree{
						.safeAfter	= desc.safeAfter,
						.image		= slot->image,
						.allocation = slot->allocation,
						.view		= vk::ImageView(slot->defaultView),
					},
					error))
			{
				return false;
			}

			static_cast<void>(device->textureSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eTextureView)
		{
			const TextureViewHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TextureViewSlot * slot = device->textureViewSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed texture view");
			}

			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer view is not allowed");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->textureViewSlots.Retire(slotHandle, true));
				return Succeed(error);
			}

			if (slot->view)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.view	   = slot->view,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->textureViewSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::ePipelineLayout)
		{
			const PipelineLayoutHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			PipelineLayoutSlot * slot = device->pipelineLayoutSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed pipeline layout");
			}

			if (slot->layout)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter		= desc.safeAfter,
							.pipelineLayout = slot->layout,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->pipelineLayoutSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eGraphicsPipeline)
		{
			const GraphicsPipelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			GraphicsPipelineSlot * slot = device->graphicsPipelineSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed graphics pipeline");
			}

			if (slot->pipeline)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.pipeline  = slot->pipeline,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->graphicsPipelineSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eTimeline)
		{
			const TimelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TimelineSlot * slot = device->timelineSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed timeline");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->timelineSlots.Retire(slotHandle, true));
				return Succeed(error);
			}

			if (slot->semaphore)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.semaphore = slot->semaphore,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->timelineSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eQueryPool)
		{
			const QueryPoolHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			QueryPoolSlot * slot = device->queryPoolSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed query pool");
			}

			if (slot->pool)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.queryPool = slot->pool,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->queryPoolSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eSampler)
		{
			const SamplerHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			SamplerSlot * slot = device->samplerSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed sampler");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->samplerSlots.Retire(slotHandle, true));
				return Succeed(error);
			}

			if (slot->sampler)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.sampler   = slot->sampler,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->samplerSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eComputePipeline)
		{
			const ComputePipelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			ComputePipelineSlot * slot = device->computePipelineSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed compute pipeline");
			}

			if (slot->pipeline)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.pipeline  = slot->pipeline,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->computePipelineSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::ePipelineCache)
		{
			const PipelineCacheHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			PipelineCacheSlot * slot = device->pipelineCacheSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed pipeline cache");
			}

			if (slot->cache)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter	   = desc.safeAfter,
							.pipelineCache = slot->cache,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->pipelineCacheSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eBinarySemaphore)
		{
			const std::uint32_t index = handle.index & ~kDeviceBinarySemaphoreBit; // strip the device-semaphore tag bit
			const BinarySemaphoreHandle slotHandle{
				.index		= index,
				.generation = handle.generation,
			};
			BinarySemaphoreSlot * slot = device->binarySemaphoreSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed binary semaphore");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->binarySemaphoreSlots.Retire(slotHandle, true));
				return Succeed(error);
			}

			if (slot->semaphore)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter		 = desc.safeAfter,
							.binarySemaphore = slot->semaphore,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->binarySemaphoreSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eDescriptorSetLayout)
		{
			const DescriptorSetLayoutHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			DescriptorSetLayoutSlot * slot = device->descriptorSetLayoutSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed descriptor set layout");
			}

			if (slot->layout)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter			 = desc.safeAfter,
							.descriptorSetLayout = slot->layout,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->descriptorSetLayoutSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		if (type == ResourceType::eHeap)
		{
			const HeapHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			HeapSlot * slot = device->heapSlots.Resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed heap");
			}

			if (slot->memory)
			{
				if (!RetireNative(device,
						type,
						desc,
						PendingFree{
							.safeAfter	  = desc.safeAfter,
							.deviceMemory = slot->memory,
						},
						error))
				{
					return false;
				}
			}

			static_cast<void>(device->heapSlots.Retire(slotHandle, true));
			return Succeed(error);
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: destroy of this resource type not implemented yet");
	}

} // namespace azo::rhi::vulkan

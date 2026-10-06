// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	bool RetireNative(VulkanDevice * device, ResourceType type, const DestroyDesc & desc, const PendingFree & pending, Error * error) noexcept
	{
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

	bool VulkanCollectGarbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.collectGarbage");
		auto * device		   = static_cast<VulkanDevice *>(impl);
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
		auto * device		   = static_cast<VulkanDevice *>(impl);
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
			const std::uint32_t index = handle.index & ~kDeviceBinarySemaphoreBit;
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

		if (type == ResourceType::eDescriptorSet)
		{
			const DescriptorSetHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (device->descriptorSetSlots.Resolve(slotHandle, true) == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed descriptor set");
			}

			static_cast<void>(device->descriptorSetSlots.Retire(slotHandle, true));
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

}

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

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

#include "backends/vulkan/internal.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint> // NOLINT

namespace azo::rhi::vulkan
{
	bool retire_native(VulkanDevice * device, ResourceType type, const DestroyDesc & desc, const PendingFree & pending, Error * error) noexcept
	{
		const auto kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return fail(error, ErrorCode::eInvalidArgument, "destroy names a resource kind this device has no queue for");
		}

		if (desc.policy == DestroyPolicy::eDeferUntilSafe && desc.safeAfter.timeline.is_valid())
		{
			if (!detail::try_push_back(azo::rhi::detail::at(device->garbage, kind), pending))
			{
				return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan deferred destroy queue allocation failed");
			}

			const std::uint64_t count = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
			AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(count));
			return true;
		}
		free_pending(device->device, device->dispatch, device->allocator, pending);
		return true;
	}

	bool vulkan_collect_garbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.collectGarbage");
		auto * device	= static_cast<VulkanDevice *>(impl);
		const auto kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return fail(error, ErrorCode::eInvalidArgument, "collectGarbage names a resource kind this device has no queue for");
		}

		detail::HostVector<PendingFree> & collecting = azo::rhi::detail::at(device->garbage, kind);

		for (const PendingFree & pending : collecting)
		{
			free_pending(device->device, device->dispatch, device->allocator, pending);
		}

		const std::uint64_t freed = collecting.size();
		collecting.clear();

		AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(device->pendingRetire.fetch_sub(freed, std::memory_order_relaxed) - freed));
		return succeed(error);
	}

	bool vulkan_collect_garbage_timeline(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.collectGarbage");
		auto * device	= static_cast<VulkanDevice *>(impl);
		const auto kind = static_cast<std::size_t>(type);
		if (kind >= device->garbage.size())
		{
			return fail(error, ErrorCode::eInvalidArgument, "collectGarbage names a resource kind this device has no queue for");
		}

		detail::HostVector<PendingFree> & collecting = azo::rhi::detail::at(device->garbage, kind);

		const std::uint64_t freed = std::erase_if(
			collecting,
			[&](const PendingFree & pending)
			{
				const bool ready = pending.safeAfter.timeline == timeline && pending.safeAfter.value <= completedValue;
				if (ready)
				{
					free_pending(device->device, device->dispatch, device->allocator, pending);
				}

				return ready;
			}
		);

		AZO_RHI_PROFILE_PLOT("rhi.vulkan.pendingRetire", static_cast<std::int64_t>(device->pendingRetire.fetch_sub(freed, std::memory_order_relaxed) - freed));
		return succeed(error);
	}

	// NOLINTNEXTLINE(readability-function-cognitive-complexity)
	bool vulkan_destroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.destroy");
		auto * device = static_cast<VulkanDevice *>(impl);

		if (type == ResourceType::eBuffer)
		{
			const BufferHandle bufferHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			BufferSlot * slot = device->bufferSlots.resolve(bufferHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed buffer");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->bufferSlots.retire(bufferHandle, true));
				return succeed(error);
			}

			if (slot->buffer != VK_NULL_HANDLE)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter	= desc.safeAfter,
							.buffer		= slot->buffer,
							.allocation = slot->allocation,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->bufferSlots.retire(bufferHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eTexture)
		{
			const TextureHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TextureSlot * slot = device->textureSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed texture");
			}

			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer texture is not allowed");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->textureSlots.retire(slotHandle, true));
				return succeed(error);
			}

			if (!retire_native(
					device,
					type,
					desc,
					PendingFree{
						.safeAfter	= desc.safeAfter,
						.image		= slot->image,
						.allocation = slot->allocation,
						.view		= vk::ImageView(slot->defaultView),
					},
					error
				))
			{
				return false;
			}

			static_cast<void>(device->textureSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eTextureView)
		{
			const TextureViewHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TextureViewSlot * slot = device->textureViewSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed texture view");
			}

			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a borrowed swapchain back buffer view is not allowed");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->textureViewSlots.retire(slotHandle, true));
				return succeed(error);
			}

			if (slot->view)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.view	   = slot->view,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->textureViewSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::ePipelineLayout)
		{
			const PipelineLayoutHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			PipelineLayoutSlot * slot = device->pipelineLayoutSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed pipeline layout");
			}

			if (slot->layout)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter		= desc.safeAfter,
							.pipelineLayout = slot->layout,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->pipelineLayoutSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eGraphicsPipeline)
		{
			const GraphicsPipelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			GraphicsPipelineSlot * slot = device->graphicsPipelineSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed graphics pipeline");
			}

			if (slot->pipeline)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.pipeline  = slot->pipeline,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->graphicsPipelineSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eTimeline)
		{
			const TimelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			TimelineSlot * slot = device->timelineSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed timeline");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->timelineSlots.retire(slotHandle, true));
				return succeed(error);
			}

			if (slot->semaphore)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.semaphore = slot->semaphore,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->timelineSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eQueryPool)
		{
			const QueryPoolHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			QueryPoolSlot * slot = device->queryPoolSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed query pool");
			}

			if (slot->pool)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.queryPool = slot->pool,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->queryPoolSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eSampler)
		{
			const SamplerHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			SamplerSlot * slot = device->samplerSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed sampler");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->samplerSlots.retire(slotHandle, true));
				return succeed(error);
			}

			if (slot->sampler)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.sampler   = slot->sampler,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->samplerSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eComputePipeline)
		{
			const ComputePipelineHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			ComputePipelineSlot * slot = device->computePipelineSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed compute pipeline");
			}

			if (slot->pipeline)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter = desc.safeAfter,
							.pipeline  = slot->pipeline,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->computePipelineSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::ePipelineCache)
		{
			const PipelineCacheHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			PipelineCacheSlot * slot = device->pipelineCacheSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed pipeline cache");
			}

			if (slot->cache)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter	   = desc.safeAfter,
							.pipelineCache = slot->cache,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->pipelineCacheSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eBinarySemaphore)
		{
			const std::uint32_t index = handle.index & ~kDeviceBinarySemaphoreBit;
			const BinarySemaphoreHandle slotHandle{
				.index		= index,
				.generation = handle.generation,
			};
			BinarySemaphoreSlot * slot = device->binarySemaphoreSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed binary semaphore");
			}

			if (slot->lifetime == SlotLifetime::eAdopted)
			{
				static_cast<void>(device->binarySemaphoreSlots.retire(slotHandle, true));
				return succeed(error);
			}

			if (slot->semaphore)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter		 = desc.safeAfter,
							.binarySemaphore = slot->semaphore,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->binarySemaphoreSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eDescriptorSet)
		{
			const DescriptorSetHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (device->descriptorSetSlots.resolve(slotHandle, true) == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed descriptor set");
			}

			static_cast<void>(device->descriptorSetSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eDescriptorSetLayout)
		{
			const DescriptorSetLayoutHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			DescriptorSetLayoutSlot * slot = device->descriptorSetLayoutSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed descriptor set layout");
			}

			if (slot->layout)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter			 = desc.safeAfter,
							.descriptorSetLayout = slot->layout,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->descriptorSetLayoutSlots.retire(slotHandle, true));
			return succeed(error);
		}

		if (type == ResourceType::eHeap)
		{
			const HeapHandle slotHandle{
				.index		= handle.index,
				.generation = handle.generation,
			};
			HeapSlot * slot = device->heapSlots.resolve(slotHandle, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed heap");
			}

			if (slot->memory)
			{
				if (!retire_native(
						device,
						type,
						desc,
						PendingFree{
							.safeAfter	  = desc.safeAfter,
							.deviceMemory = slot->memory,
						},
						error
					))
				{
					return false;
				}
			}

			static_cast<void>(device->heapSlots.retire(slotHandle, true));
			return succeed(error);
		}

		return fail(error, ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: destroy of this resource type not implemented yet");
	}

} // namespace azo::rhi::vulkan

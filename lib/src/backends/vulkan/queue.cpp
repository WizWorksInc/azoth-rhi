// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/queue.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include "backends/vulkan/barrier_tables.hpp"
#include "backends/vulkan/internal.hpp"
#include "vulkan/vulkan.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi::vulkan
{
	// A pure lookup over a vector built at device create, which is what lets Submit hold an entry while GetQueue runs on another thread.
	std::uint32_t acquire_submit_timeline(VulkanDevice * device, const QueueType type, const std::uint32_t index) noexcept
	{
		for (std::size_t at = 0; at < device->submitTimelines.size(); ++at)
		{
			const SubmitTimeline & existing = *azo::rhi::detail::at(device->submitTimelines, at);
			if (existing.type == type && existing.index == index)
			{
				return static_cast<std::uint32_t>(at);
			}
		}

		return kNoSubmitTimeline;
	}

	bool build_submit_timelines(VulkanDevice * device, Error * error) noexcept
	{
		constexpr std::array kTypes{ QueueType::eGraphics, QueueType::eCompute, QueueType::eCopy };

		std::size_t wanted = 0;
		for (const QueueType type : kTypes)
		{
			wanted += device->queues_for_type(type).size();
		}

		if (!detail::try_reserve(device->submitTimelines, wanted))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline storage allocation failed");
		}

		for (const QueueType type : kTypes)
		{
			const std::size_t count = device->queues_for_type(type).size();
			for (std::size_t index = 0; index < count; ++index)
			{
				const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 0);
				const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return fail_native(error, "Vulkan submit timeline creation failed", created.result);
				}

				auto tracked = host_new<SubmitTimeline>();
				if (tracked == nullptr)
				{
					device->device.destroySemaphore(created.value, nullptr, device->dispatch);
					return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline allocation failed");
				}

				tracked->semaphore = created.value;
				tracked->type	   = type;
				tracked->index	   = static_cast<std::uint32_t>(index);

				if (!detail::try_push_back(device->submitTimelines, std::move(tracked)))
				{
					device->device.destroySemaphore(created.value, nullptr, device->dispatch);
					return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline allocation failed");
				}
			}
		}

		return true;
	}

	bool caller_signal_reached(VulkanDevice * device, const std::span<const TimelinePoint> callerSignals) noexcept
	{
		for (const TimelinePoint & signal : callerSignals)
		{
			// Validate just in case the caller possibly destroyed the timeline since the submit.
			const TimelineSlot * slot = device->timelineSlots.resolve(signal.timeline, true);
			if (slot == nullptr)
			{
				continue;
			}

			const auto reached = device->device.getSemaphoreCounterValue(slot->semaphore, device->dispatch);
			if (reached.result == vk::Result::eSuccess && reached.value >= signal.value)
			{
				return true;
			}
		}

		return false;
	}

	bool submission_still_running(
		VulkanDevice * device,
		const std::uint32_t submitTimeline,
		const std::uint64_t submitValue,
		const std::span<const TimelinePoint> callerSignals
	) noexcept
	{
		if (submitTimeline == kNoSubmitTimeline || submitValue == 0 || submitTimeline >= device->submitTimelines.size())
		{
			return false;
		}

		const auto reached =
			device->device.getSemaphoreCounterValue(azo::rhi::detail::at(device->submitTimelines, submitTimeline)->semaphore, device->dispatch);
		if (reached.result != vk::Result::eSuccess)
		{
			// A counter that cannot be read is treated as still running, because freeing or reusing a live buffer is the worse mistake.
			return true;
		}

		// Per vkQueueSubmit2 every signal in a batch covers all its commands, and none is ordered before another, so the caller's may land first.
		return reached.value < submitValue && !caller_signal_reached(device, callerSignals);
	}

	bool list_still_running(const VulkanCommandList * list) noexcept
	{
		if (list == nullptr)
		{
			return false;
		}

		return submission_still_running(list->owner, list->submitTimeline, list->submitValue, list->callerSignals);
	}

	void sweep_retired_command_buffers(VulkanCommandPool * pool) noexcept
	{
		if (pool == nullptr || pool->retired.empty())
		{
			return;
		}

		VulkanDevice * device = pool->owner;
		std::size_t kept	  = 0;
		for (std::size_t at = 0; at < pool->retired.size(); ++at)
		{
			RetiredCommandBuffer & entry = azo::rhi::detail::at(pool->retired, at);
			if (submission_still_running(device, entry.submitTimeline, entry.submitValue, entry.callerSignals))
			{
				if (kept != at)
				{
					azo::rhi::detail::at(pool->retired, kept) = std::move(entry);
				}

				++kept;
				continue;
			}

			device->device.freeCommandBuffers(pool->pool, 1, &entry.buffer, device->dispatch);
		}

		pool->retired.resize(kept);
	}

	void * vulkan_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device							   = static_cast<VulkanDevice *>(impl);
		const detail::HostVector<vk::Queue> & pool = device->queues_for_type(type);
		if (index >= pool.size())
		{
			return fail_value<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
		}

		auto queue = host_new<VulkanQueue>();
		if (queue == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan queue allocation failed");
		}

		const bool bindsSparse = type == QueueType::eGraphics && device->caps.sparseTier > SparseTier::eNone;

		queue->object	   = bindsSparse ? publishing_object<Published<QueueApi, &queue_block>, Published<SparseApi, &sparse_block>>()
										 : publishing_object<Published<QueueApi, &queue_block>>();
		queue->owner	   = device;
		queue->type		   = type;
		queue->queue	   = azo::rhi::detail::at(pool, index);
		queue->familyIndex = device->family_for_type(type);

		queue->submitTimeline = acquire_submit_timeline(device, type, index);

		VulkanQueue * raw = queue.get();
		if (!detail::try_push_back(device->queues, std::move(queue)))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan queue allocation failed");
		}

		return return_value(raw, error);
	}

	bool vulkan_queue_bind_sparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.bindSparse");

		auto * queue		  = static_cast<VulkanQueue *>(impl);
		VulkanDevice * device = queue->owner;

		detail::HostVector<vk::SparseMemoryBind> bufferBinds;
		detail::HostVector<vk::SparseBufferMemoryBindInfo> bufferInfos;
		detail::HostVector<vk::SparseImageMemoryBind> imageBinds;
		detail::HostVector<vk::SparseImageMemoryBindInfo> imageInfos;
		detail::HostVector<vk::Semaphore> waits;
		detail::HostVector<vk::Semaphore> signals;
		detail::HostVector<std::uint64_t> waitValues;
		detail::HostVector<std::uint64_t> signalValues;

		if (!detail::try_reserve(bufferBinds, desc.buffers.size()) || !detail::try_reserve(bufferInfos, desc.buffers.size()) ||
			!detail::try_reserve(imageBinds, desc.textures.size()) || !detail::try_reserve(imageInfos, desc.textures.size()) ||
			!detail::try_reserve(waits, desc.timelineWaits.size()) || !detail::try_reserve(signals, desc.timelineSignals.size() + 1) ||
			!detail::try_reserve(waitValues, desc.timelineWaits.size()) || !detail::try_reserve(signalValues, desc.timelineSignals.size() + 1))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan sparse bind storage allocation failed");
		}

		for (const TimelinePoint & wait : desc.timelineWaits)
		{
			const vk::Semaphore semaphore = resolve_timeline(device, wait.timeline);
			if (!semaphore)
			{
				return fail(error, ErrorCode::eInvalidHandle, "sparse bind waits on an invalid timeline");
			}

			waits.push_back(semaphore);
			waitValues.push_back(wait.value);
		}

		for (const TimelinePoint & signal : desc.timelineSignals)
		{
			const vk::Semaphore semaphore = resolve_timeline(device, signal.timeline);
			if (!semaphore)
			{
				return fail(error, ErrorCode::eInvalidHandle, "sparse bind signals an invalid timeline");
			}

			signals.push_back(semaphore);
			signalValues.push_back(signal.value);
		}

		for (const SparseBufferBind & bind : desc.buffers)
		{
			const BufferSlot * slot = resolve_buffer(device, bind.buffer);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid buffer handle");
			}
			if (!slot->sparse)
			{
				return fail(error, ErrorCode::eValidationFailed, "sparse bind targets a buffer not created with allowSparseBinding");
			}

			vk::DeviceMemory memory{};
			if (bind.page.heap.is_valid())
			{
				const HeapSlot * heap = resolve_heap(device, bind.page.heap);
				if (heap == nullptr)
				{
					return fail(error, ErrorCode::eInvalidHandle, "sparse bind names an invalid heap");
				}

				memory = heap->memory;
			}

			bufferBinds.push_back(vk::SparseMemoryBind{ bind.resourceOffset, bind.page.size, memory, bind.page.heapOffset, {} });
			bufferInfos.emplace_back(vk::Buffer(slot->buffer), 1, nullptr);
		}

		for (const SparseTextureBind & bind : desc.textures)
		{
			const TextureSlot * slot = device->textureSlots.resolve(bind.texture, kHandleAlreadyChecked);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid texture handle");
			}
			if (!slot->sparse)
			{
				return fail(error, ErrorCode::eValidationFailed, "sparse bind targets a texture not created with allowSparseBinding");
			}

			vk::DeviceMemory memory{};
			if (bind.page.heap.is_valid())
			{
				const HeapSlot * heap = resolve_heap(device, bind.page.heap);
				if (heap == nullptr)
				{
					return fail(error, ErrorCode::eInvalidHandle, "sparse bind names an invalid heap");
				}

				memory = heap->memory;
			}

			vk::SparseImageMemoryBind imageBind{};
			imageBind.subresource  = vk::ImageSubresource{ map_aspect(bind.subresource.aspects), bind.subresource.mip, bind.subresource.layer };
			imageBind.offset	   = vk::Offset3D{ bind.offset.x, bind.offset.y, bind.offset.z };
			imageBind.extent	   = vk::Extent3D{ bind.extent.width, bind.extent.height, bind.extent.depth };
			imageBind.memory	   = memory;
			imageBind.memoryOffset = bind.page.heapOffset;

			imageBinds.push_back(imageBind);
			imageInfos.emplace_back(vk::Image(slot->image), 1, nullptr);
		}

		// Every bind is in place before the first pointer into these vectors is taken, so a bind that ever pushes more than one cannot leave one dangling.
		std::uint32_t boundBuffers = 0;
		for (vk::SparseBufferMemoryBindInfo & bufferInfo : bufferInfos)
		{
			bufferInfo.pBinds = bufferBinds.data() + boundBuffers;
			boundBuffers += bufferInfo.bindCount;
		}

		std::uint32_t boundImages = 0;
		for (vk::SparseImageMemoryBindInfo & imageInfo : imageInfos)
		{
			imageInfo.pBinds = imageBinds.data() + boundImages;
			boundImages += imageInfo.bindCount;
		}

		// A bind is queue work like a submit, so it signals the same timeline and one drain at teardown covers both.
		SubmitTimeline & tracked	= *azo::rhi::detail::at(device->submitTimelines, queue->submitTimeline);
		const std::uint64_t boundAt = tracked.counter.fetch_add(1, std::memory_order_acq_rel) + 1;
		signals.push_back(tracked.semaphore);
		signalValues.push_back(boundAt);

		vk::TimelineSemaphoreSubmitInfo timelineInfo{};
		timelineInfo.setWaitSemaphoreValues(waitValues);
		timelineInfo.setSignalSemaphoreValues(signalValues);

		vk::BindSparseInfo info{};
		info.pNext = &timelineInfo;
		info.setWaitSemaphores(waits);
		info.setSignalSemaphores(signals);
		info.setBufferBinds(bufferInfos);
		info.setImageBinds(imageInfos);

		if (const vk::Result bound = queue->queue.bindSparse(1, &info, vk::Fence{}, device->dispatch); bound != vk::Result::eSuccess)
		{
			return fail_native(error, "vkQueueBindSparse failed", bound);
		}

		tracked.submitted.store(boundAt, std::memory_order_release);

		return succeed(error);
	}

	QueueType vulkan_queue_type(void * impl) noexcept
	{
		return static_cast<VulkanQueue *>(impl)->type;
	}

	bool vulkan_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.submit");
		auto * queue		  = static_cast<VulkanQueue *>(impl);
		VulkanDevice * device = queue->owner;

		if (const char * refusal = submit_refusal_for_lists(
				desc.commandLists,
				device->caps.supportsCommandListResubmit,
				[](const CommandList & list)
				{
					return static_cast<const VulkanCommandList *>(detail::unwrapped_impl_of(list));
				},
				[](const VulkanCommandList & record)
				{
					return list_still_running(&record);
				}
			);
			refusal != nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, refusal);
		}

		detail::HostVector<vk::SemaphoreSubmitInfo> waits;
		detail::HostVector<vk::SemaphoreSubmitInfo> signals;
		detail::HostVector<vk::CommandBufferSubmitInfo> commandBuffers;
		if (!detail::try_reserve(waits, desc.waits.size() + desc.swapchains.size()) ||
			!detail::try_reserve(signals, desc.signals.size() + desc.swapchains.size() + 1) || !detail::try_reserve(commandBuffers, desc.commandLists.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit storage allocation failed");
		}

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.acquired.is_valid())
			{
				continue;
			}

			const vk::Semaphore sem = resolve_binary_semaphore(device, sync.acquired);
			if (!sem)
			{
				return fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid acquire semaphore");
			}

			waits.emplace_back(sem, 0, map_stages(sync.waitStages));
		}

		for (const TimelinePoint & tw : desc.waits)
		{
			const vk::Semaphore sem = resolve_timeline(device, tw.timeline);
			if (!sem)
			{
				return fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid timeline");
			}
			waits.emplace_back(sem, tw.value, map_stages(tw.waitStages));
		}

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.renderFinished.is_valid())
			{
				continue;
			}

			const vk::Semaphore sem = resolve_binary_semaphore(device, sync.renderFinished);
			if (!sem)
			{
				return fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid present semaphore");
			}
			signals.emplace_back(sem, 0, vk::PipelineStageFlagBits2::eAllCommands);
		}
		for (const TimelinePoint & ts : desc.signals)
		{
			const vk::Semaphore sem = resolve_timeline(device, ts.timeline);
			if (!sem)
			{
				return fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid timeline");
			}
			signals.emplace_back(sem, ts.value, vk::PipelineStageFlagBits2::eAllCommands);
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}

			// The check above refused everything that was not ended, so every list still here carries work.
			auto * record = static_cast<VulkanCommandList *>(detail::unwrapped_impl_of(*list));
			commandBuffers.emplace_back(record->buffer);
		}

		// One extra signal per submit is what later tells a resubmit or a re-Begin whether this submission has finished.
		SubmitTimeline & tracked		= *azo::rhi::detail::at(device->submitTimelines, queue->submitTimeline);
		const std::uint64_t submittedAt = tracked.counter.fetch_add(1, std::memory_order_acq_rel) + 1;
		signals.emplace_back(tracked.semaphore, submittedAt, vk::PipelineStageFlagBits2::eAllCommands);

		const vk::SubmitInfo2 submitInfo({}, waits, commandBuffers, signals);
		const vk::Result submitted =
			device->coreVk13 ? queue->queue.submit2(submitInfo, nullptr, device->dispatch) : queue->queue.submit2KHR(submitInfo, nullptr, device->dispatch);
		if (submitted != vk::Result::eSuccess)
		{
			return fail_native(error, "vkQueueSubmit2 failed", submitted);
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}

			auto * record		   = static_cast<VulkanCommandList *>(detail::unwrapped_impl_of(*list));
			record->lifecycle	   = ListLifecycle::eSubmitted;
			record->submitTimeline = queue->submitTimeline;
			record->submitValue	   = submittedAt;

			record->callerSignals.clear();
			if (detail::try_reserve(record->callerSignals, desc.signals.size()))
			{
				record->callerSignals.assign(desc.signals.begin(), desc.signals.end());
			}
		}

		tracked.submitted.store(submittedAt, std::memory_order_release);

		return succeed(error);
	}

	bool vulkan_queue_wait_idle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.waitIdle");
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (const vk::Result waited = queue->queue.waitIdle(queue->owner->dispatch); waited != vk::Result::eSuccess)
		{
			return fail_native(error, "vkQueueWaitIdle failed", waited);
		}

		return succeed(error);
	}

	bool vulkan_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		*out					= 0;
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = resolve_timeline(device, timeline);
		if (!sem)
		{
			return fail(error, ErrorCode::eInvalidHandle, "getCompletedValue on an invalid timeline");
		}

		const auto counter = device->device.getSemaphoreCounterValue(sem, device->dispatch);
		if (counter.result != vk::Result::eSuccess)
		{
			return fail_native(error, "vkGetSemaphoreCounterValue failed", counter.result);
		}

		return store(out, counter.value, error);
	}

	bool vulkan_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = resolve_timeline(device, timeline);
		if (!sem)
		{
			return fail(error, ErrorCode::eInvalidHandle, "wait on an invalid timeline");
		}

		const vk::SemaphoreWaitInfo waitInfo({}, sem, value);
		const vk::Result waited = device->device.waitSemaphores(waitInfo, timeoutNanoseconds, device->dispatch);
		if (waited == vk::Result::eTimeout)
		{
			return fail(error, ErrorCode::eTimeout, "timeline wait timed out");
		}

		if (waited != vk::Result::eSuccess)
		{
			return fail_native(error, "vkWaitSemaphores failed", waited);
		}

		return succeed(error);
	}

	bool vulkan_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = resolve_timeline(device, timeline);

		if (!sem)
		{
			return fail(error, ErrorCode::eInvalidHandle, "signal on an invalid timeline");
		}

		if (const vk::Result signaled = device->device.signalSemaphore(vk::SemaphoreSignalInfo(sem, value), device->dispatch); signaled != vk::Result::eSuccess)
		{
			return fail_native(error, "vkSignalSemaphore failed", signaled);
		}

		return succeed(error);
	}

	std::uint32_t queue_family_for_type(const VulkanDevice * device, QueueType type) noexcept
	{
		switch (type)
		{
		case QueueType::eCompute:  return device->computeFamily;
		case QueueType::eCopy:	   return device->copyFamily;
		case QueueType::eGraphics: return device->graphicsFamily;
		}
		return device->graphicsFamily;
	}

}

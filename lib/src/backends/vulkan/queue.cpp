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
	// A pure lookup over a vector built at device create, which is what lets Submit hold an entry while GetQueue runs on another thread.
	std::uint32_t AcquireSubmitTimeline(VulkanDevice * device, const QueueType type, const std::uint32_t index) noexcept
	{
		for (std::size_t at = 0; at < device->submitTimelines.size(); ++at)
		{
			const SubmitTimeline & existing = *device->submitTimelines[at];
			if (existing.type == type && existing.index == index)
			{
				return static_cast<std::uint32_t>(at);
			}
		}

		return kNoSubmitTimeline;
	}

	bool BuildSubmitTimelines(VulkanDevice * device, Error * error) noexcept
	{
		constexpr std::array kTypes{ QueueType::eGraphics, QueueType::eCompute, QueueType::eCopy };

		std::size_t wanted = 0;
		for (const QueueType type : kTypes)
		{
			wanted += device->QueuesForType(type).size();
		}

		if (!detail::TryReserve(device->submitTimelines, wanted))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline storage allocation failed");
		}

		for (const QueueType type : kTypes)
		{
			const std::size_t count = device->QueuesForType(type).size();
			for (std::size_t index = 0; index < count; ++index)
			{
				const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 0);
				const auto created = device->device.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return FailNative(error, "Vulkan submit timeline creation failed", created.result);
				}

				auto tracked = HostNew<SubmitTimeline>();
				if (tracked == nullptr)
				{
					device->device.destroySemaphore(created.value, nullptr, device->dispatch);
					return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline allocation failed");
				}

				tracked->semaphore = created.value;
				tracked->type	   = type;
				tracked->index	   = static_cast<std::uint32_t>(index);

				if (!detail::TryPushBack(device->submitTimelines, std::move(tracked)))
				{
					device->device.destroySemaphore(created.value, nullptr, device->dispatch);
					return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit timeline allocation failed");
				}
			}
		}

		return true;
	}

	bool SubmissionStillRunning(VulkanDevice * device, const std::uint32_t submitTimeline, const std::uint64_t submitValue) noexcept
	{
		if (submitTimeline == kNoSubmitTimeline || submitValue == 0 || submitTimeline >= device->submitTimelines.size())
		{
			return false;
		}

		const auto reached = device->device.getSemaphoreCounterValue(device->submitTimelines[submitTimeline]->semaphore, device->dispatch);
		if (reached.result != vk::Result::eSuccess)
		{
			// A counter that cannot be read is treated as still running, because freeing or reusing a live buffer is the worse mistake.
			return true;
		}

		return reached.value < submitValue;
	}

	bool ListStillRunning(const VulkanCommandList * list) noexcept
	{
		if (list == nullptr)
		{
			return false;
		}

		return SubmissionStillRunning(list->owner, list->submitTimeline, list->submitValue);
	}

	void SweepRetiredCommandBuffers(VulkanCommandPool * pool) noexcept
	{
		if (pool == nullptr || pool->retired.empty())
		{
			return;
		}

		VulkanDevice * device = pool->owner;
		std::size_t kept	  = 0;
		for (std::size_t at = 0; at < pool->retired.size(); ++at)
		{
			const RetiredCommandBuffer & entry = pool->retired[at];
			if (SubmissionStillRunning(device, entry.submitTimeline, entry.submitValue))
			{
				pool->retired[kept] = entry;
				++kept;
				continue;
			}

			device->device.freeCommandBuffers(pool->pool, 1, &entry.buffer, device->dispatch);
		}

		pool->retired.resize(kept);
	}

	void * VulkanGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device							   = static_cast<VulkanDevice *>(impl);
		const detail::HostVector<vk::Queue> & pool = device->QueuesForType(type);
		if (index >= pool.size())
		{
			return FailValue<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
		}

		auto queue = HostNew<VulkanQueue>();
		if (queue == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan queue allocation failed");
		}

		const bool bindsSparse = type == QueueType::eGraphics && device->caps.sparseTier > SparseTier::eNone;

		queue->object	   = bindsSparse ? PublishingObject<Published<QueueApi, &QueueBlock>, Published<SparseApi, &SparseBlock>>()
										 : PublishingObject<Published<QueueApi, &QueueBlock>>();
		queue->owner	   = device;
		queue->type		   = type;
		queue->queue	   = pool[index];
		queue->familyIndex = device->FamilyForType(type);

		queue->submitTimeline = AcquireSubmitTimeline(device, type, index);

		VulkanQueue * raw = queue.get();
		if (!detail::TryPushBack(device->queues, std::move(queue)))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan queue allocation failed");
		}

		return ReturnValue(raw, error);
	}

	bool VulkanQueueBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
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

		if (!detail::TryReserve(bufferBinds, desc.buffers.size()) || !detail::TryReserve(bufferInfos, desc.buffers.size()) ||
			!detail::TryReserve(imageBinds, desc.textures.size()) || !detail::TryReserve(imageInfos, desc.textures.size()) ||
			!detail::TryReserve(waits, desc.timelineWaits.size()) || !detail::TryReserve(signals, desc.timelineSignals.size() + 1) ||
			!detail::TryReserve(waitValues, desc.timelineWaits.size()) || !detail::TryReserve(signalValues, desc.timelineSignals.size() + 1))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan sparse bind storage allocation failed");
		}

		for (const TimelinePoint & wait : desc.timelineWaits)
		{
			const vk::Semaphore semaphore = ResolveTimeline(device, wait.timeline);
			if (!semaphore)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind waits on an invalid timeline");
			}

			waits.push_back(semaphore);
			waitValues.push_back(wait.value);
		}

		for (const TimelinePoint & signal : desc.timelineSignals)
		{
			const vk::Semaphore semaphore = ResolveTimeline(device, signal.timeline);
			if (!semaphore)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind signals an invalid timeline");
			}

			signals.push_back(semaphore);
			signalValues.push_back(signal.value);
		}

		for (const SparseBufferBind & bind : desc.buffers)
		{
			const BufferSlot * slot = ResolveBuffer(device, bind.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid buffer handle");
			}
			if (!slot->sparse)
			{
				return Fail(error, ErrorCode::eValidationFailed, "sparse bind targets a buffer not created with allowSparseBinding");
			}

			vk::DeviceMemory memory{};
			if (bind.page.heap.IsValid())
			{
				const HeapSlot * heap = ResolveHeap(device, bind.page.heap);
				if (heap == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "sparse bind names an invalid heap");
				}

				memory = heap->memory;
			}

			bufferBinds.push_back(vk::SparseMemoryBind{ bind.resourceOffset, bind.page.size, memory, bind.page.heapOffset, {} });
			bufferInfos.emplace_back(vk::Buffer(slot->buffer), 1, nullptr);
		}

		for (const SparseTextureBind & bind : desc.textures)
		{
			const TextureSlot * slot = device->textureSlots.Resolve(bind.texture, kHandleAlreadyChecked);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "sparse bind of an invalid texture handle");
			}
			if (!slot->sparse)
			{
				return Fail(error, ErrorCode::eValidationFailed, "sparse bind targets a texture not created with allowSparseBinding");
			}

			vk::DeviceMemory memory{};
			if (bind.page.heap.IsValid())
			{
				const HeapSlot * heap = ResolveHeap(device, bind.page.heap);
				if (heap == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "sparse bind names an invalid heap");
				}

				memory = heap->memory;
			}

			vk::SparseImageMemoryBind imageBind{};
			imageBind.subresource  = vk::ImageSubresource{ MapAspect(bind.subresource.aspects), bind.subresource.mip, bind.subresource.layer };
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
		SubmitTimeline & tracked	= *device->submitTimelines[queue->submitTimeline];
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
			return FailNative(error, "vkQueueBindSparse failed", bound);
		}

		tracked.submitted.store(boundAt, std::memory_order_release);

		return Succeed(error);
	}

	QueueType VulkanQueueType(void * impl) noexcept
	{
		return static_cast<VulkanQueue *>(impl)->type;
	}

	bool VulkanQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.submit");
		auto * queue		  = static_cast<VulkanQueue *>(impl);
		VulkanDevice * device = queue->owner;

		if (const char * refusal = SubmitRefusalForLists(
				desc.commandLists,
				device->caps.supportsCommandListResubmit,
				[](const CommandList & list)
				{
					return static_cast<const VulkanCommandList *>(detail::UnwrappedImplOf(list));
				},
				[](const VulkanCommandList & record)
				{
					return ListStillRunning(&record);
				});
			refusal != nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, refusal);
		}

		detail::HostVector<vk::SemaphoreSubmitInfo> waits;
		detail::HostVector<vk::SemaphoreSubmitInfo> signals;
		detail::HostVector<vk::CommandBufferSubmitInfo> commandBuffers;
		if (!detail::TryReserve(waits, desc.waits.size() + desc.swapchains.size()) ||
			!detail::TryReserve(signals, desc.signals.size() + desc.swapchains.size() + 1) || !detail::TryReserve(commandBuffers, desc.commandLists.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit storage allocation failed");
		}

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.acquired.IsValid())
			{
				continue;
			}

			const vk::Semaphore sem = ResolveBinarySemaphore(device, sync.acquired);
			if (!sem)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid acquire semaphore");
			}

			waits.emplace_back(sem, 0, MapStages(sync.waitStages));
		}

		for (const TimelinePoint & tw : desc.waits)
		{
			const vk::Semaphore sem = ResolveTimeline(device, tw.timeline);
			if (!sem)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid timeline");
			}
			waits.emplace_back(sem, tw.value, MapStages(tw.waitStages));
		}

		for (const SwapchainSync & sync : desc.swapchains)
		{
			if (!sync.renderFinished.IsValid())
			{
				continue;
			}

			const vk::Semaphore sem = ResolveBinarySemaphore(device, sync.renderFinished);
			if (!sem)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid present semaphore");
			}
			signals.emplace_back(sem, 0, vk::PipelineStageFlagBits2::eAllCommands);
		}
		for (const TimelinePoint & ts : desc.signals)
		{
			const vk::Semaphore sem = ResolveTimeline(device, ts.timeline);
			if (!sem)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit signals an invalid timeline");
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
			auto * record = static_cast<VulkanCommandList *>(detail::UnwrappedImplOf(*list));
			commandBuffers.emplace_back(record->buffer);
		}

		// One extra signal per submit is what later tells a resubmit or a re-Begin whether this submission has finished.
		SubmitTimeline & tracked		= *device->submitTimelines[queue->submitTimeline];
		const std::uint64_t submittedAt = tracked.counter.fetch_add(1, std::memory_order_acq_rel) + 1;
		signals.emplace_back(tracked.semaphore, submittedAt, vk::PipelineStageFlagBits2::eAllCommands);

		const vk::SubmitInfo2 submitInfo({}, waits, commandBuffers, signals);
		const vk::Result submitted =
			device->coreVk13 ? queue->queue.submit2(submitInfo, nullptr, device->dispatch) : queue->queue.submit2KHR(submitInfo, nullptr, device->dispatch);
		if (submitted != vk::Result::eSuccess)
		{
			return FailNative(error, "vkQueueSubmit2 failed", submitted);
		}

		for (const CommandList * list : desc.commandLists)
		{
			if (list == nullptr)
			{
				continue;
			}

			auto * record		   = static_cast<VulkanCommandList *>(detail::UnwrappedImplOf(*list));
			record->lifecycle	   = ListLifecycle::eSubmitted;
			record->submitTimeline = queue->submitTimeline;
			record->submitValue	   = submittedAt;
		}

		tracked.submitted.store(submittedAt, std::memory_order_release);

		return Succeed(error);
	}

	bool VulkanQueueWaitIdle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.waitIdle");
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (const vk::Result waited = queue->queue.waitIdle(queue->owner->dispatch); waited != vk::Result::eSuccess)
		{
			return FailNative(error, "vkQueueWaitIdle failed", waited);
		}

		return Succeed(error);
	}

	bool VulkanQueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "completed value output pointer is null");
		}

		*out					= 0;
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = ResolveTimeline(device, timeline);
		if (!sem)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getCompletedValue on an invalid timeline");
		}

		const auto counter = device->device.getSemaphoreCounterValue(sem, device->dispatch);
		if (counter.result != vk::Result::eSuccess)
		{
			return FailNative(error, "vkGetSemaphoreCounterValue failed", counter.result);
		}

		return Store(out, counter.value, error);
	}

	bool VulkanQueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = ResolveTimeline(device, timeline);
		if (!sem)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "wait on an invalid timeline");
		}

		const vk::SemaphoreWaitInfo waitInfo({}, sem, value);
		const vk::Result waited = device->device.waitSemaphores(waitInfo, timeoutNanoseconds, device->dispatch);
		if (waited == vk::Result::eTimeout)
		{
			return Fail(error, ErrorCode::eTimeout, "timeline wait timed out");
		}

		if (waited != vk::Result::eSuccess)
		{
			return FailNative(error, "vkWaitSemaphores failed", waited);
		}

		return Succeed(error);
	}

	bool VulkanQueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept
	{
		VulkanDevice * device	= static_cast<VulkanQueue *>(impl)->owner;
		const vk::Semaphore sem = ResolveTimeline(device, timeline);

		if (!sem)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "signal on an invalid timeline");
		}

		if (const vk::Result signaled = device->device.signalSemaphore(vk::SemaphoreSignalInfo(sem, value), device->dispatch); signaled != vk::Result::eSuccess)
		{
			return FailNative(error, "vkSignalSemaphore failed", signaled);
		}

		return Succeed(error);
	}

	std::uint32_t QueueFamilyForType(const VulkanDevice * device, QueueType type) noexcept
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

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
	 * Queue entries. The device hands out a queue record for the requested type and index, backed by the queue from the family selected for that type at
	 * device creation (compute and copy fall back to the graphics family when the adapter has no dedicated one). index selects among the queues created
	 * for the type and must be below the count the device exposes for it.
	 */
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

		/*
		 * SparseApi is published only where the queue can actually bind, which is the graphics family on an adapter whose features are enabled for it.
		 * A queue that published the block and refused every call would report sparse support through DeviceCaps, since the tier is derived from block
		 * presence, and that is exactly the reportable-but-unusable shape the surface exists to avoid.
		 */
		const bool bindsSparse = type == QueueType::eGraphics && device->caps.sparseTier > SparseTier::eNone;

		queue->object	   = bindsSparse ? PublishingObject<Published<QueueApi, &QueueBlock>, Published<SparseApi, &SparseBlock>>()
										 : PublishingObject<Published<QueueApi, &QueueBlock>>();
		queue->owner	   = device;
		queue->type		   = type;
		queue->queue	   = pool[index];
		queue->familyIndex = device->FamilyForType(type);

		VulkanQueue * raw = queue.get();
		if (!detail::TryPushBack(device->queues, std::move(queue)))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan queue allocation failed");
		}

		return ReturnValue(raw, error);
	}

	/*
	 * Binds heap memory to the virtual pages of sparse resources. One vkQueueBindSparse for the whole batch and not a call per bind, which is what makes
	 * the ordering right: waits apply before every bind and signals fire after all of them. A caller cannot observe a half-bound resource.
	 *
	 * A texture bind goes through imageBinds, not imageOpaqueBinds, since the RHI's SparseTextureBind names a subresource and a texel region, which the
	 * opaque form cannot express.
	 */
	bool VulkanQueueBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.bindSparse");

		auto * queue		  = static_cast<VulkanQueue *>(impl);
		VulkanDevice * device = queue->owner;

		// One VkSparseMemoryBind per buffer bind, each wrapped in its own VkSparseBufferMemoryBindInfo. Vulkan groups binds by resource and the RHI does not,
		// so grouping would mean sorting the caller's batch and changing the order their binds apply in.
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
			!detail::TryReserve(waits, desc.timelineWaits.size()) || !detail::TryReserve(signals, desc.timelineSignals.size()) ||
			!detail::TryReserve(waitValues, desc.timelineWaits.size()) || !detail::TryReserve(signalValues, desc.timelineSignals.size()))
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

			// An invalid heap unbinds the range, which Vulkan spells as a null memory handle and not as a flag.
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
			bufferInfos.emplace_back( vk::Buffer(slot->buffer), 1, &bufferBinds.back() );
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

			// The caller's own aspect and not one derived from the format, since a depth-stencil texture has two and only they know which is meant.
			vk::SparseImageMemoryBind imageBind{};
			imageBind.subresource  = vk::ImageSubresource{ MapAspect(bind.subresource.aspects), bind.subresource.mip, bind.subresource.layer };
			imageBind.offset	   = vk::Offset3D{ bind.offset.x, bind.offset.y, bind.offset.z };
			imageBind.extent	   = vk::Extent3D{ bind.extent.width, bind.extent.height, bind.extent.depth };
			imageBind.memory	   = memory;
			imageBind.memoryOffset = bind.page.heapOffset;

			imageBinds.push_back(imageBind);
			imageInfos.emplace_back( vk::Image(slot->image), 1, &imageBinds.back() );
		}

		vk::TimelineSemaphoreSubmitInfo timelineInfo{};
		timelineInfo.setWaitSemaphoreValues(waitValues);
		timelineInfo.setSignalSemaphoreValues(signalValues);

		vk::BindSparseInfo info{};
		info.pNext = &timelineInfo;
		info.setWaitSemaphores(waits);
		info.setSignalSemaphores(signals);
		info.setBufferBinds(bufferInfos);
		info.setImageBinds(imageInfos);

		if (queue->queue.bindSparse(1, &info, vk::Fence{}, device->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkQueueBindSparse failed");
		}

		return Succeed(error);
	}

	QueueType VulkanQueueType(void * impl) noexcept
	{
		return static_cast<VulkanQueue *>(impl)->type;
	}

	std::uint32_t VulkanQueueFamilyIndex(void * impl) noexcept
	{
		return static_cast<VulkanQueue *>(impl)->familyIndex;
	}

	/*
	 * Submits recorded command lists to the queue through vkQueueSubmit2 (synchronization2). Timeline points and the swapchain's binary pair lower uniformly to
	 * semaphore submit infos so a non-empty sync span is carried on the submit, not dropped. An unresolvable timeline, or a swapchain semaphore that was
	 * given but does not resolve, is rejected with eInvalidHandle.
	 */
	bool VulkanQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.submit");
		auto * queue		  = static_cast<VulkanQueue *>(impl);
		VulkanDevice * device = queue->owner;

		// vkQueueSubmit2 (synchronization2) carries binary and timeline waits and signals uniformly as semaphore submit infos (the value is ignored for a
		// binary semaphore).
		detail::HostVector<vk::SemaphoreSubmitInfo> waits;
		detail::HostVector<vk::SemaphoreSubmitInfo> signals;
		detail::HostVector<vk::CommandBufferSubmitInfo> commandBuffers;
		if (!detail::TryReserve(waits, desc.waits.size() + desc.swapchains.size()) ||
			!detail::TryReserve(signals, desc.signals.size() + desc.swapchains.size()) || !detail::TryReserve(commandBuffers, desc.commandLists.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan submit storage allocation failed");
		}

		// Each of the three was reserved for exactly what the loops below put in it, so none of the appends can grow.
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

			waits.emplace_back(sem, 0, MapStages2(sync.waitStages));
		}

		for (const TimelinePoint & tw : desc.waits)
		{
			const vk::Semaphore sem = ResolveTimeline(device, tw.timeline);
			if (!sem)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "submit waits on an invalid timeline");
			}
			waits.emplace_back(sem, tw.value, MapStages2(tw.waitStages));
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
			commandBuffers.emplace_back(static_cast<VulkanCommandList *>(detail::UnwrappedImplOf(*list))->buffer);
		}

		const vk::SubmitInfo2 submitInfo({}, waits, commandBuffers, signals);
		const vk::Result submitted =
			device->coreVk13 ? queue->queue.submit2(submitInfo, nullptr, device->dispatch) : queue->queue.submit2KHR(submitInfo, nullptr, device->dispatch);
		if (submitted != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkQueueSubmit2 failed");
		}

		return Succeed(error);
	}

	bool VulkanQueueWaitIdle(void * impl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.queue.waitIdle");
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (queue->queue.waitIdle(queue->owner->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkQueueWaitIdle failed");
		}

		return Succeed(error);
	}

	// Timeline queue operations. GetCompletedValue reads the current value, Wait blocks the host until the timeline reaches a value (frame pacing uses
	// this) and Signal raises it from the host.
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
			return Fail(error, ErrorCode::eNativeApiError, "vkGetSemaphoreCounterValue failed");
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
			return Fail(error, ErrorCode::eNativeApiError, "vkWaitSemaphores failed");
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

		if (device->device.signalSemaphore(vk::SemaphoreSignalInfo(sem, value), device->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkSignalSemaphore failed");
		}

		return Succeed(error);
	}

	// Resolves the abstract queue type to the concrete Vulkan queue family the device selected for it.
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

} // namespace azo::rhi::vulkan

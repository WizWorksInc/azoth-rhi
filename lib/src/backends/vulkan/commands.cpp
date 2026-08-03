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

#include "azoth/rhi/core/build_config.hpp"

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	namespace
	{
		const BackendObject * CommandListObject() noexcept;
	} // namespace

	/*
	 * Command pool and command list entries. The pool wraps a VkCommandPool on the family selected by desc.queueType. Allocate hands back a command list wrapping
	 * a primary VkCommandBuffer for that family. Ray tracing is the one part of the recording surface not lowered here and reports eUnsupportedFeature. For
	 * anything the RHI cannot express, GetVulkanCommandBuffer hands back the VkCommandBuffer between BeginNativeMutation and EndNativeMutation.
	 */
	void * VulkanCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createCommandPool");
		auto * device = static_cast<VulkanDevice *>(impl);

		vk::CommandPoolCreateFlags flags{};
		if (desc.transient)
		{
			flags |= vk::CommandPoolCreateFlagBits::eTransient;
		}

		if (desc.individualReset)
		{
			flags |= vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		}

		auto commandPool = HostNew<VulkanCommandPool>();
		if (commandPool == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command pool allocation failed");
		}

		commandPool->object = PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>();
		commandPool->owner	= device;
		commandPool->family = QueueFamilyForType(device, desc.queueType);

		const auto created = device->device.createCommandPool(vk::CommandPoolCreateInfo(flags, commandPool->family), nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Vulkan command pool creation failed");
		}

		commandPool->pool = created.value;

		VulkanCommandPool * raw = commandPool.get();
		if (!detail::TryPushBack(device->commandPools, std::move(commandPool)))
		{
			device->device.destroyCommandPool(created.value, nullptr, device->dispatch);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command pool allocation failed");
		}

		return ReturnValue(raw, error);
	}

	void * VulkanCommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.commandPool.allocate");
		auto * commandPool	  = static_cast<VulkanCommandPool *>(impl);
		VulkanDevice * device = commandPool->owner;

		// A buffer this pool allocated before and has since taken back in a reset. It is in the initial state, which is where Begin needs it, so nothing about it
		// needs remaking.
		if (commandPool->handedOut < commandPool->lists.size())
		{
			VulkanCommandList * recycled = commandPool->lists[commandPool->handedOut];
			++commandPool->handedOut;
			return ReturnValue(recycled, error);
		}

		const auto buffers = device->device.allocateCommandBuffers<HostAllocatorAdapter<vk::CommandBuffer>>(
			vk::CommandBufferAllocateInfo(commandPool->pool, vk::CommandBufferLevel::ePrimary, 1), device->dispatch);

		// One buffer was asked for, so a success that handed back none would leave front() reading an empty vector.
		if (buffers.result != vk::Result::eSuccess || buffers.value.empty())
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Vulkan command buffer allocation failed");
		}

		auto list = HostNew<VulkanCommandList>();
		if (list == nullptr)
		{
			device->device.freeCommandBuffers(commandPool->pool, buffers.value.front(), device->dispatch);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}

		list->object = CommandListObject();
		list->owner	 = device;
		list->pool	 = commandPool;
		list->buffer = buffers.value.front();
		list->family = commandPool->family;

		VulkanCommandList * raw = list.get();
		if (!detail::TryPushBack(device->commandLists, std::move(list)))
		{
			device->device.freeCommandBuffers(commandPool->pool, buffers.value.front(), device->dispatch);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}

		// The device owns the record from here, so a pool that cannot remember it still has to refuse: handing it out unrecorded would allocate a second buffer for
		// it on the next frame and neither would ever be recycled.
		if (!detail::TryPushBack(commandPool->lists, raw))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}
		++commandPool->handedOut;

		return ReturnValue(raw, error);
	}

	/*
	 * What a command list from this device publishes, which is not the same on every device.
	 *
	 * Timestamps and the indirect count entries are driver conditional so a list from a device without them declines the block without publishing entries that
	 * would refuse at the call. The block is then the only place the answer lives and DeviceCaps reads it back off exactly this.
	 */
	namespace
	{
		const void * VulkanCommandListQueryInterface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			const auto * list = static_cast<const VulkanCommandList *>(object);

			if (id == InterfaceTraits<QueryCommandApi>::kId && !list->owner->caps.supportsTimestampQueries)
			{
				return nullptr;
			}

			if (id == InterfaceTraits<IndirectCountApi>::kId && !list->owner->caps.supportsIndirectCount)
			{
				return nullptr;
			}

			return QueryPublished<Published<RenderCommandApi, &RenderCommandBlock>,
				Published<AliasingCommandApi, &AliasingCommandBlock>,
				Published<QueryCommandApi, &QueryCommandBlock>,
				Published<IndirectApi, &IndirectBlock>,
				Published<IndirectCountApi, &IndirectCountBlock>,
				Published<NativeEscapeApi, &NativeEscapeBlock>>(object, id, minVersion);
		}

		const BackendObject * CommandListObject() noexcept
		{
			static constexpr BackendObject object{ .queryInterface = &VulkanCommandListQueryInterface };
			return &object;
		}
	} // namespace

	bool VulkanCommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.commandPool.reset");
		auto * commandPool = static_cast<VulkanCommandPool *>(impl);

		// safeAfter means the GPU has finished with this pool's command buffers so the framebuffers they referenced (no dynamic rendering) can be destroyed.
		for (const vk::Framebuffer framebuffer : commandPool->framebuffers)
		{
			if (framebuffer)
			{
				commandPool->owner->device.destroyFramebuffer(framebuffer, nullptr, commandPool->owner->dispatch);
			}
		}
		commandPool->framebuffers.clear();

		if (commandPool->owner->device.resetCommandPool(commandPool->pool, {}, commandPool->owner->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan command pool reset failed");
		}

		// Every buffer is back in the initial state, so the lists standing in front of them are the ones the next frame is handed.
		commandPool->handedOut = 0;

		return Succeed(error);
	}

	bool VulkanCommandListBegin(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		if (list->buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit), list->owner->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkBeginCommandBuffer failed");
		}

		return Succeed(error);
	}

	bool VulkanCommandListEnd(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		if (list->buffer.end(list->owner->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "vkEndCommandBuffer failed");
		}

		return Succeed(error);
	}

	// Dynamic state recording. These take plain values, not resource handles so none of them touch the device registry.
	bool VulkanCmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		/*
		 * Vulkan's own NDC runs Y down so eYDown needs nothing done. Presenting eYUp means flipping: move the origin to the bottom edge and negate the height. A
		 * negative viewport height is core from Vulkan 1.1 and the backend floor is 1.2 so it is always available. Winding is left as authored either way, matching
		 * what the other two backends do.
		 */
		float originY = viewport.y;
		float height  = viewport.height;
		if (GetClipSpace() == ClipSpaceConvention::eYUp)
		{
			originY = viewport.y + viewport.height;
			height	= -viewport.height;
		}

		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setViewport(0, vk::Viewport(viewport.x, originY, viewport.width, height, viewport.minDepth, viewport.maxDepth), list->owner->dispatch);

		return Succeed(error);
	}

	bool VulkanCmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setScissor(0, vk::Rect2D(vk::Offset2D(scissor.x, scissor.y), vk::Extent2D(scissor.width, scissor.height)), list->owner->dispatch);
		return Succeed(error);
	}

	// Copies a range between two buffers this device created. Resolves both handles to their VkBuffer through the device's buffer registry (the developer and
	// capture modes catch a stale handle), then records the copy onto the list's native buffer. This is the device-local upload primitive.
	bool VulkanCmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		BufferSlot * srcSlot = ResolveBuffer(device, src);
		BufferSlot * dstSlot = ResolveBuffer(device, dst);
		if (srcSlot == nullptr || dstSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBuffer with an invalid buffer handle");
		}

		list->buffer.copyBuffer(vk::Buffer(srcSlot->buffer), vk::Buffer(dstSlot->buffer), vk::BufferCopy(srcOffset, dstOffset, size), device->dispatch);
		return Succeed(error);
	}

	/*
	 * Query and timestamp recording. Each resolves its query pool locklessly through the device registry (developer and capture modes catch a stale handle) and
	 * records onto the list's native buffer. A query pool must be reset before reuse so a caller records resetQueryPool ahead of the writes.
	 */
	bool VulkanCmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resetQueryPool with an invalid query pool handle");
		}
		// Subtracted and not added, the sum of two counts a caller chooses being free to wrap and let an out-of-range range through.
		if (firstQuery > slot->queryCount || queryCount > slot->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}

		list->buffer.resetQueryPool(slot->pool, firstQuery, queryCount, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "writeTimestamp with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}

		if (list->owner->coreVk13)
		{
			list->buffer.writeTimestamp2(MapStages2(stage), slot->pool, query, list->owner->dispatch);
		}
		else
		{
			list->buffer.writeTimestamp2KHR(MapStages2(stage), slot->pool, query, list->owner->dispatch);
		}
		return Succeed(error);
	}

	bool VulkanCmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "beginQuery with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "beginQuery names a query past the end of the pool");
		}

		list->buffer.beginQuery(slot->pool, query, vk::QueryControlFlags{}, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "endQuery with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "endQuery names a query past the end of the pool");
		}

		list->buffer.endQuery(slot->pool, query, list->owner->dispatch);
		return Succeed(error);
	}

	// Copies query results into a buffer this device created. The eWait flag makes the copy wait for the queries to finish so the readback is valid once the
	// submission completes. Results are 64-bit, packed one per query at the given destination offset.
	bool VulkanCmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * poolSlot = ResolveQueryPool(list->owner, pool);
		BufferSlot * dstSlot	 = ResolveBuffer(list->owner, dst);
		if (poolSlot == nullptr || dstSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveQueryData with an invalid query pool or buffer handle");
		}
		// Subtracted and not added, as in resetQueryPool above. Here the wrapped range drives a GPU write into the destination buffer.
		if (firstQuery > poolSlot->queryCount || queryCount > poolSlot->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}

		list->buffer.copyQueryPoolResults(poolSlot->pool,
			firstQuery,
			queryCount,
			vk::Buffer(dstSlot->buffer),
			dstOffset,
			sizeof(std::uint64_t),
			vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait,
			list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * Debug labels bracket a span of commands for GPU debuggers and profilers (RenderDoc, Tracy). They are a no-op success when the device did not enable
	 * VK_EXT_debug_utils, or when DeviceDesc turned labels off, so a caller records them unconditionally. Unpacks a 0xRRGGBBAA RHI debug-label color into the
	 * normalized float color Vulkan labels carry.
	 */
	[[nodiscard]] std::array<float, 4> UnpackLabelColor(std::uint32_t color) noexcept
	{
		return { static_cast<float>((color >> 24) & 0xFFu) / 255.0f,
			static_cast<float>((color >> 16) & 0xFFu) / 255.0f,
			static_cast<float>((color >> 8) & 0xFFu) / 255.0f,
			static_cast<float>(color & 0xFFu) / 255.0f };
	}

	bool VulkanCmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (list->owner->debugUtils && list->owner->debugLabels)
		{
			list->buffer.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT(name != nullptr ? name : "", UnpackLabelColor(color)), list->owner->dispatch);
		}
		return Succeed(error);
	}

	bool VulkanCmdEndDebugLabel(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (list->owner->debugUtils && list->owner->debugLabels)
		{
			list->buffer.endDebugUtilsLabelEXT(list->owner->dispatch);
		}
		return Succeed(error);
	}

	/*
	 * Native mutation is the escape hatch: between begin and end the caller records raw Vulkan onto the list's native command buffer (reached through
	 * GetVulkanCommandBuffer). Begin only checks the API matches this backend. The touched-resource lists in the desc are reconciled by the validation layer,
	 * against tracking only that layer keeps, so they need no action here.
	 */
	bool VulkanCmdBeginNativeMutation(void * impl, GraphicsApiId api, const NativeMutationDesc & /*desc*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
		}
		return Succeed(error);
	}

	bool VulkanCmdEndNativeMutation(void * impl, const NativeMutationDesc & /*desc*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		return Succeed(error);
	}

	bool VulkanQueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept
	{
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (queue->owner->debugUtils && queue->owner->debugLabels)
		{
			queue->queue.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT(name != nullptr ? name : "", UnpackLabelColor(color)), queue->owner->dispatch);
		}
		return Succeed(error);
	}

	bool VulkanQueueEndDebugLabel(void * impl, Error * error) noexcept
	{
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (queue->owner->debugUtils && queue->owner->debugLabels)
		{
			queue->queue.endDebugUtilsLabelEXT(queue->owner->dispatch);
		}
		return Succeed(error);
	}

	/*
	 * Scene-pass recording commands. They resolve their resource handles locklessly through the device registries (the recording contract rules out concurrent
	 * resource creation) and record onto the list's native buffer. Barriers lower to vkCmdPipelineBarrier2 so each barrier carries its own stage and access masks,
	 * the natural shape of the RHI BarrierBatch.
	 */
	/*
	 * The queue family sentinels are passed through, not mapped, which is only correct because the RHI picked the same values Vulkan uses. That was true of
	 * kIgnoreQueueFamily before kExternalQueueFamily existed and was never stated, so both are pinned here: a change to either constant fails the build instead of
	 * silently turning an ownership transfer into a transfer to family 4294967294.
	 */
	static_assert(kIgnoreQueueFamily == VK_QUEUE_FAMILY_IGNORED, "kIgnoreQueueFamily is passed to Vulkan unmapped, so it has to be VK_QUEUE_FAMILY_IGNORED");
	static_assert(
		kExternalQueueFamily == VK_QUEUE_FAMILY_EXTERNAL, "kExternalQueueFamily is passed to Vulkan unmapped, so it has to be VK_QUEUE_FAMILY_EXTERNAL");

	bool VulkanCmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		detail::HostVector<vk::MemoryBarrier2> memoryBarriers;
		detail::HostVector<vk::BufferMemoryBarrier2> bufferBarriers;
		detail::HostVector<vk::ImageMemoryBarrier2> imageBarriers;
		if (!detail::TryReserve(memoryBarriers, barriers.memory.size()) || !detail::TryReserve(bufferBarriers, barriers.buffers.size()) ||
			!detail::TryReserve(imageBarriers, barriers.textures.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan barrier recording allocation failed");
		}

		// Each of the three was reserved for exactly what the loops below put in it, so none of the appends can grow.
		for (const MemoryBarrier & b : barriers.memory)
		{
			memoryBarriers.emplace_back(MapStages2(b.before.stages), MapAccess2(b.before.access), MapStages2(b.after.stages), MapAccess2(b.after.access));
		}

		for (const BufferBarrier & b : barriers.buffers)
		{
			const BufferSlot * slot = ResolveBuffer(device, b.buffer);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "buffer barrier with an invalid buffer handle");
			}

			bufferBarriers.emplace_back(MapStages2(b.before.stages),
				MapAccess2(b.before.access),
				MapStages2(b.after.stages),
				MapAccess2(b.after.access),
				b.ownership.src,
				b.ownership.dst,
				vk::Buffer(slot->buffer),
				b.offset,
				b.size);
		}

		for (const TextureBarrier & b : barriers.textures)
		{
			const vk::Image image = ResolveTexture(device, b.texture);
			if (!image)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "texture barrier with an invalid texture handle");
			}

			imageBarriers.emplace_back(MapStages2(b.before.stages),
				MapAccess2(b.before.access),
				MapStages2(b.after.stages),
				MapAccess2(b.after.access),
				MapTextureLayout(b.before.layout),
				MapTextureLayout(b.after.layout),
				b.ownership.src,
				b.ownership.dst,
				image,
				MapSubresourceRange(b.range));
		}

		vk::DependencyInfo depInfo;
		depInfo.setMemoryBarriers(memoryBarriers);
		depInfo.setBufferMemoryBarriers(bufferBarriers);
		depInfo.setImageMemoryBarriers(imageBarriers);
		if (list->owner->coreVk13)
		{
			list->buffer.pipelineBarrier2(depInfo, list->owner->dispatch);
		}
		else
		{
			list->buffer.pipelineBarrier2KHR(depInfo, list->owner->dispatch);
		}

		return Succeed(error);
	}

	// Aliasing transition for placed resources that share heap memory: a conservative global memory barrier orders writes to the 'before' resources ahead of any
	// use of the 'after' resources. The caller still transitions an after-texture's layout from undefined with a normal barrier.
	bool VulkanCmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (barriers.empty())
		{
			return Succeed(error);
		}

		for (const AliasBarrier & barrier : barriers)
		{
			if ((barrier.beforeBuffer.IsValid() && ResolveBuffer(list->owner, barrier.beforeBuffer) == nullptr) ||
				(barrier.afterBuffer.IsValid() && ResolveBuffer(list->owner, barrier.afterBuffer) == nullptr) ||
				(barrier.beforeTexture.IsValid() && !ResolveTexture(list->owner, barrier.beforeTexture)) ||
				(barrier.afterTexture.IsValid() && !ResolveTexture(list->owner, barrier.afterTexture)))
			{
				return Fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
			}
		}

		const vk::MemoryBarrier2 memory(vk::PipelineStageFlagBits2::eAllCommands,
			vk::AccessFlagBits2::eMemoryWrite,
			vk::PipelineStageFlagBits2::eAllCommands,
			vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
		vk::DependencyInfo depInfo;
		depInfo.setMemoryBarriers(memory);
		if (list->owner->coreVk13)
		{
			list->buffer.pipelineBarrier2(depInfo, list->owner->dispatch);
		}
		else
		{
			list->buffer.pipelineBarrier2KHR(depInfo, list->owner->dispatch);
		}
		return Succeed(error);
	}

	/*
	 * BeginRendering without dynamic rendering: lower the attachments onto a cached VkRenderPass plus a fresh VkFramebuffer over the bound views, then
	 * vkCmdBeginRenderPass. The framebuffer is handed to the pool, which frees it at reset once the GPU is done. Graphics pipelines were created against a
	 * compatible pass (matching formats and sample counts), which is all Vulkan requires for the draws.
	 */
	bool VulkanCmdBeginRenderPassScope(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		VulkanDevice * device = list->owner;

		RenderPassKey key;
		if (desc.colors.size() > key.colors.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "rendering scope exceeds the maximum color attachment count");
		}
		key.colorCount = static_cast<std::uint32_t>(desc.colors.size());

		detail::HostVector<vk::ImageView> views;
		detail::HostVector<vk::ClearValue> clears;
		if (!detail::TryReserve(views, desc.colors.size() + 1) || !detail::TryReserve(clears, desc.colors.size() + 1))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		// Both were reserved for every color attachment plus the depth one, so none of the appends below can grow.
		for (std::size_t i = 0; i < desc.colors.size(); ++i)
		{
			const RenderingAttachment & a = desc.colors[i];
			const TextureViewSlot * slot  = ResolveTextureViewSlot(device, a.view);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering color attachment with an invalid view handle");
			}
			// An attachment count past this array is refused above. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			key.colors[i] = RenderPassAttachmentKey{ .format = slot->format,
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				.samples										= slot->samples,
				.loadOp											= MapLoadOp(a.load),
				.storeOp										= MapStoreOp(a.store),
				.layout											= MapTextureLayout(a.state.layout) };
			views.push_back(slot->view);
			clears.emplace_back(vk::ClearColorValue(std::array<float, 4>{ a.clearColor.r, a.clearColor.g, a.clearColor.b, a.clearColor.a }));
		}

		if (desc.depthStencil != nullptr)
		{
			const TextureViewSlot * slot = ResolveTextureViewSlot(device, desc.depthStencil->view);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment with an invalid view handle");
			}
			key.hasDepth = true;
			key.depth	 = RenderPassAttachmentKey{ .format = slot->format,
				.samples								 = slot->samples,
				.loadOp									 = MapLoadOp(desc.depthStencil->load),
				.storeOp								 = MapStoreOp(desc.depthStencil->store),
				.layout									 = MapTextureLayout(desc.depthStencil->state.layout) };
			views.push_back(slot->view);
			clears.emplace_back(vk::ClearDepthStencilValue(desc.depthStencil->clearDepthStencil.depth, desc.depthStencil->clearDepthStencil.stencil));
		}

		const vk::RenderPass renderPass = GetOrCreateRenderPass(device, list->pool->renderPasses, key);
		if (!renderPass)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan render pass creation failed");
		}

		// The framebuffer must cover the render area. The attachment images are at least that large so x + width and y + height stay within them.
		vk::FramebufferCreateInfo fbInfo;
		fbInfo.renderPass = renderPass;
		fbInfo.setAttachments(views);
		fbInfo.width  = desc.x + desc.width;
		fbInfo.height = desc.y + desc.height;
		fbInfo.layers = desc.layers;

		const auto created = device->device.createFramebuffer(fbInfo, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan framebuffer creation failed");
		}

		// The pool frees this at its next reset, so a framebuffer it cannot take on is one nothing would ever free.
		if (!detail::TryPushBack(list->pool->framebuffers, created.value))
		{
			device->device.destroyFramebuffer(created.value, nullptr, device->dispatch);
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		vk::RenderPassBeginInfo beginInfo;
		beginInfo.renderPass  = renderPass;
		beginInfo.framebuffer = created.value;
		beginInfo.renderArea =
			vk::Rect2D(vk::Offset2D(static_cast<std::int32_t>(desc.x), static_cast<std::int32_t>(desc.y)), vk::Extent2D(desc.width, desc.height));
		beginInfo.setClearValues(clears);

		list->buffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline, list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * The scope's opening timestamp, and the end recorded for VulkanCmdEndRendering to issue.
	 *
	 * Ahead of the render-pass instance, not inside it. Vulkan takes a timestamp either way, but outside is where the other two backends put theirs and it keeps
	 * the write clear of the render-pass restrictions a later query type would meet.
	 */
	namespace
	{
		bool BeginRenderingTimestamps(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept
		{
			list->pendingEndTimestamp = vk::QueryPool{};
			if (desc.timestamps == nullptr)
			{
				return Succeed(error);
			}

			QueryPoolSlot * slot = ResolveQueryPool(list->owner, desc.timestamps->pool);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering timestamp writes name a query pool this device never created");
			}
			if ((desc.timestamps->beginQuery != kInvalidIndex && desc.timestamps->beginQuery >= slot->queryCount) ||
				(desc.timestamps->endQuery != kInvalidIndex && desc.timestamps->endQuery >= slot->queryCount))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a query past the end of the pool");
			}

			if (desc.timestamps->beginQuery != kInvalidIndex)
			{
				const vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eTopOfPipe;
				if (list->owner->coreVk13)
				{
					list->buffer.writeTimestamp2(stage, slot->pool, desc.timestamps->beginQuery, list->owner->dispatch);
				}
				else
				{
					list->buffer.writeTimestamp2KHR(stage, slot->pool, desc.timestamps->beginQuery, list->owner->dispatch);
				}
			}
			if (desc.timestamps->endQuery != kInvalidIndex)
			{
				list->pendingEndTimestamp	   = slot->pool;
				list->pendingEndTimestampQuery = desc.timestamps->endQuery;
			}
			return Succeed(error);
		}
	} // namespace

	bool VulkanCmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		if (!BeginRenderingTimestamps(list, desc, error))
		{
			return false;
		}

		if (!device->dynamicRendering)
		{
			return VulkanCmdBeginRenderPassScope(list, desc, error);
		}

		detail::HostVector<vk::RenderingAttachmentInfo> colorAttachments;
		if (!detail::TryReserve(colorAttachments, desc.colors.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		// Reserved for exactly one entry per color attachment, so the append below cannot grow.
		for (const RenderingAttachment & a : desc.colors)
		{
			const vk::ImageView view = ResolveTextureView(device, a.view);
			if (!view)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering color attachment with an invalid view handle");
			}

			vk::RenderingAttachmentInfo info;
			info.imageView	 = view;
			info.imageLayout = MapTextureLayout(a.state.layout);
			info.loadOp		 = MapLoadOp(a.load);
			info.storeOp	 = MapStoreOp(a.store);
			info.clearValue	 = vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{ a.clearColor.r, a.clearColor.g, a.clearColor.b, a.clearColor.a }));
			colorAttachments.push_back(info);
		}

		vk::RenderingAttachmentInfo depthAttachment;
		vk::RenderingAttachmentInfo stencilAttachment;
		const bool hasDepth = desc.depthStencil != nullptr;
		bool hasStencil		= false;
		if (hasDepth)
		{
			const TextureViewSlot * slot = ResolveTextureViewSlot(device, desc.depthStencil->view);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment with an invalid view handle");
			}
			const vk::ImageView view = slot->view;

			depthAttachment.imageView	= view;
			depthAttachment.imageLayout = MapTextureLayout(desc.depthStencil->state.layout);
			depthAttachment.loadOp		= MapLoadOp(desc.depthStencil->load);
			depthAttachment.storeOp		= MapStoreOp(desc.depthStencil->store);
			depthAttachment.clearValue =
				vk::ClearValue(vk::ClearDepthStencilValue(desc.depthStencil->clearDepthStencil.depth, desc.depthStencil->clearDepthStencil.stencil));

			/*
			 * A depth-stencil view also drives the stencil attachment so the render-pass instance matches the pipeline's stencilAttachmentFormat
			 * (VUID-vkCmdDraw-pStencilAttachment-06182). The RHI treats depth and stencil as one attachment so the stencil aspect shares the view, layout and ops.
			 */
			hasStencil = slot->format == vk::Format::eD24UnormS8Uint || slot->format == vk::Format::eD32SfloatS8Uint;
			if (hasStencil)
			{
				stencilAttachment = depthAttachment;
			}
		}

		vk::RenderingInfo renderingInfo;
		renderingInfo.renderArea =
			vk::Rect2D(vk::Offset2D(static_cast<std::int32_t>(desc.x), static_cast<std::int32_t>(desc.y)), vk::Extent2D(desc.width, desc.height));
		renderingInfo.layerCount = desc.layers;
		renderingInfo.setColorAttachments(colorAttachments);

		if (hasDepth)
		{
			renderingInfo.setPDepthAttachment(&depthAttachment);
		}
		if (hasStencil)
		{
			renderingInfo.setPStencilAttachment(&stencilAttachment);
		}

		if (list->owner->coreVk13)
		{
			list->buffer.beginRendering(renderingInfo, list->owner->dispatch);
		}
		else
		{
			list->buffer.beginRenderingKHR(renderingInfo, list->owner->dispatch);
		}

		return Succeed(error);
	}

	bool VulkanCmdEndRendering(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (!list->owner->dynamicRendering)
		{
			list->buffer.endRenderPass(list->owner->dispatch);
		}
		else if (list->owner->coreVk13)
		{
			list->buffer.endRendering(list->owner->dispatch);
		}
		else
		{
			list->buffer.endRenderingKHR(list->owner->dispatch);
		}

		// The scope's closing timestamp, left by the begin. After the render-pass instance closes, matching where the opening one was written.
		if (list->pendingEndTimestamp)
		{
			const vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eBottomOfPipe;
			if (list->owner->coreVk13)
			{
				list->buffer.writeTimestamp2(stage, list->pendingEndTimestamp, list->pendingEndTimestampQuery, list->owner->dispatch);
			}
			else
			{
				list->buffer.writeTimestamp2KHR(stage, list->pendingEndTimestamp, list->pendingEndTimestampQuery, list->owner->dispatch);
			}
			list->pendingEndTimestamp = vk::QueryPool{};
		}
		return Succeed(error);
	}

	bool VulkanCmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const vk::Pipeline vkPipeline = ResolveGraphicsPipeline(list->owner, pipeline);
		if (!vkPipeline)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline with an invalid pipeline handle");
		}

		list->buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vkPipeline, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept
	{
		auto * list						  = static_cast<VulkanCommandList *>(impl);
		const vk::PipelineLayout vkLayout = ResolvePipelineLayout(list->owner, layout);
		if (!vkLayout)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "pushConstants with an invalid pipeline layout handle");
		}

		list->buffer.pushConstants(vkLayout, MapShaderStages(stages), offset, size, data, list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * bindVertexBuffers below throws Vulkan-Hpp's LogicError when the buffer and offset counts disagree. One of each is passed, so the counts are equal by
	 * construction and the throw is unreachable, which the check cannot see through the ArrayProxy that overload builds.
	 */
	// NOLINTNEXTLINE(bugprone-exception-escape)
	bool VulkanCmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const BufferSlot * bufferSlot = ResolveBuffer(list->owner, buffer);
		if (bufferSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer with an invalid buffer handle");
		}

		list->buffer.bindVertexBuffers(slot, vk::Buffer(bufferSlot->buffer), static_cast<vk::DeviceSize>(offset), list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const BufferSlot * bufferSlot = ResolveBuffer(list->owner, buffer);
		if (bufferSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer with an invalid buffer handle");
		}

		list->buffer.bindIndexBuffer(vk::Buffer(bufferSlot->buffer), offset, index32 ? vk::IndexType::eUint32 : vk::IndexType::eUint16, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.draw(vertexCount, instanceCount, firstVertex, firstInstance, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * vkCmdDraw*Indirect issues more than one draw only with the multiDrawIndirect feature enabled so a drawCount above one is a spec violation without it. Lower
	 * it to one single-draw command per entry instead. Each still reads its own args from the buffer so only the recorded command count differs. Check
	 * caps.supportsMultiDrawIndirect for the quicker form.
	 *
	 * Multi-draw also needs a stride of at least one command. Without it the loop replays one command drawCount times so refuse instead.
	 */
	namespace
	{
		template <typename RecordFn>
		[[nodiscard]] bool LowerMultiDraw(const VulkanCommandList & list, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride,
			std::size_t commandSize, Error * error, const RecordFn & record) noexcept
		{
			if (drawCount > 1 && !list.owner->caps.supportsMultiDrawIndirect)
			{
				if (stride < commandSize)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "an indirect multi-draw needs a stride of at least one command");
				}

				for (std::uint32_t draw = 0; draw < drawCount; ++draw)
				{
					record(offset + (std::uint64_t{ draw } * stride), 1u);
				}

				return Succeed(error);
			}

			record(offset, drawCount);
			return Succeed(error);
		}
	} // namespace

	bool VulkanCmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = ResolveBuffer(list->owner, args);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndirect with an invalid buffer handle");
		}

		return LowerMultiDraw(*list,
			offset,
			drawCount,
			stride,
			sizeof(vk::DrawIndirectCommand),
			error,
			[list, slot, stride](std::uint64_t commandOffset, std::uint32_t count) noexcept
			{
				list->buffer.drawIndirect(vk::Buffer(slot->buffer), commandOffset, count, stride, list->owner->dispatch);
			});
	}

	bool VulkanCmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = ResolveBuffer(list->owner, args);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirect with an invalid buffer handle");
		}

		return LowerMultiDraw(*list,
			offset,
			drawCount,
			stride,
			sizeof(vk::DrawIndexedIndirectCommand),
			error,
			[list, slot, stride](std::uint64_t commandOffset, std::uint32_t count) noexcept
			{
				list->buffer.drawIndexedIndirect(vk::Buffer(slot->buffer), commandOffset, count, stride, list->owner->dispatch);
			});
	}

	// Count-buffer indirect draws. The GPU reads the draw count from a buffer so culling compute can decide how many draws to issue. Core in Vulkan 1.2, gated on
	// caps.supportsIndirectCount.
	bool VulkanCmdDrawIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (!list->owner->caps.supportsIndirectCount)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "drawIndirectCount requires Vulkan 1.2 (vkCmdDrawIndirectCount)");
		}

		BufferSlot * argsSlot  = ResolveBuffer(list->owner, args);
		BufferSlot * countSlot = ResolveBuffer(list->owner, count);
		if (argsSlot == nullptr || countSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndirectCount with an invalid buffer handle");
		}

		list->buffer.drawIndirectCount(
			vk::Buffer(argsSlot->buffer), argsOffset, vk::Buffer(countSlot->buffer), countOffset, maxDrawCount, stride, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdDrawIndexedIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (!list->owner->caps.supportsIndirectCount)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "drawIndexedIndirectCount requires Vulkan 1.2 (vkCmdDrawIndexedIndirectCount)");
		}

		BufferSlot * argsSlot  = ResolveBuffer(list->owner, args);
		BufferSlot * countSlot = ResolveBuffer(list->owner, count);
		if (argsSlot == nullptr || countSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirectCount with an invalid buffer handle");
		}

		list->buffer.drawIndexedIndirectCount(
			vk::Buffer(argsSlot->buffer), argsOffset, vk::Buffer(countSlot->buffer), countOffset, maxDrawCount, stride, list->owner->dispatch);
		return Succeed(error);
	}

	// Maps a single-subresource selector (one mip, one layer) to the layers form the copy commands take.
	[[nodiscard]] vk::ImageSubresourceLayers MapSubresourceLayers(const TextureSubresource & sub) noexcept
	{
		return {MapAspect(sub.aspects), sub.mip, sub.layer, 1};
	}

	// Transfer commands. The RHI is explicit so the caller has already barriered the resources into the transfer layouts (eTransferSrcOptimal /
	// eTransferDstOptimal). These record the copy and resolve their handles locklessly like the other recording commands.
	bool VulkanCmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = ResolveTexture(list->owner, dst);
		BufferSlot * srcSlot	 = ResolveBuffer(list->owner, src);
		if (!dstImage || srcSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture with an invalid texture or buffer handle");
		}

		detail::HostVector<vk::BufferImageCopy> copies;
		if (!detail::TryReserve(copies, regions.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "copyBufferToTexture ran out of host memory building its region list");
		}

		for (const BufferTextureCopy & r : regions)
		{
			copies.emplace_back(r.bufferOffset,
				r.bufferRowLength,
				r.bufferImageHeight,
				MapSubresourceLayers(r.subresource),
				vk::Offset3D(r.textureOffset.x, r.textureOffset.y, r.textureOffset.z),
				vk::Extent3D(r.textureExtent.width, r.textureExtent.height, r.textureExtent.depth));
		}

		list->buffer.copyBufferToImage(vk::Buffer(srcSlot->buffer), dstImage, vk::ImageLayout::eTransferDstOptimal, copies, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		BufferSlot * dstSlot	 = ResolveBuffer(list->owner, dst);
		const vk::Image srcImage = ResolveTexture(list->owner, src);
		if (dstSlot == nullptr || !srcImage)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer with an invalid buffer or texture handle");
		}

		detail::HostVector<vk::BufferImageCopy> copies;
		if (!detail::TryReserve(copies, regions.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "copyTextureToBuffer ran out of host memory building its region list");
		}

		for (const BufferTextureCopy & r : regions)
		{
			copies.emplace_back(r.bufferOffset,
				r.bufferRowLength,
				r.bufferImageHeight,
				MapSubresourceLayers(r.subresource),
				vk::Offset3D(r.textureOffset.x, r.textureOffset.y, r.textureOffset.z),
				vk::Extent3D(r.textureExtent.width, r.textureExtent.height, r.textureExtent.depth));
		}

		list->buffer.copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, vk::Buffer(dstSlot->buffer), copies, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = ResolveTexture(list->owner, dst);
		const vk::Image srcImage = ResolveTexture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageCopy> copies;
		if (!detail::TryReserve(copies, regions.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "copyTexture ran out of host memory building its region list");
		}

		for (const TextureCopy & r : regions)
		{
			copies.emplace_back(MapSubresourceLayers(r.srcSubresource),
				vk::Offset3D(r.srcOffset.x, r.srcOffset.y, r.srcOffset.z),
				MapSubresourceLayers(r.dstSubresource),
				vk::Offset3D(r.dstOffset.x, r.dstOffset.y, r.dstOffset.z),
				vk::Extent3D(r.extent.width, r.extent.height, r.extent.depth));
		}

		list->buffer.copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, copies, list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * Optimal-tiling blit support for a format. A blit source needs eBlitSrc (plus eSampledImageFilterLinear when the blit filters linearly), a destination needs
	 * eBlitDst. Block-compressed and integer formats commonly lack these so a linear blit and generateMips reject them under full validation, not recording a
	 * command the driver would refuse.
	 */
	[[nodiscard]] bool FormatSupportsBlit(const VulkanDevice * device, vk::Format format, bool asSource, bool linearFilter) noexcept
	{
		const vk::FormatFeatureFlags features = device->phys.getFormatProperties(format, device->dispatch).optimalTilingFeatures;
		const vk::FormatFeatureFlags required = asSource ? vk::FormatFeatureFlagBits::eBlitSrc : vk::FormatFeatureFlagBits::eBlitDst;
		if ((features & required) != required)
		{
			return false;
		}

		return !(asSource && linearFilter) || static_cast<bool>(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
	}

	bool VulkanCmdBlit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = ResolveTexture(list->owner, dst);
		const vk::Image srcImage = ResolveTexture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "blit with an invalid texture handle");
		}

		/*
		 * ResolveTexture validated both handles above so their slots can be read unvalidated here. What the format can do is Vulkan's own answer, not a rule the RHI
		 * imposes so it is asked in every mode.
		 */
		const bool linear		   = filter == Filter::eLinear;
		const vk::Format srcFormat = list->owner->textureSlots.Resolve(src, false)->format;
		const vk::Format dstFormat = list->owner->textureSlots.Resolve(dst, false)->format;
		if (!FormatSupportsBlit(list->owner, srcFormat, true, linear))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "blit source format does not support blit, or linear filtering when requested");
		}

		if (!FormatSupportsBlit(list->owner, dstFormat, false, false))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "blit destination format does not support being a blit target");
		}

		const vk::Filter vkFilter = filter == Filter::eLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
		detail::HostVector<vk::ImageBlit> blits;
		if (!detail::TryReserve(blits, regions.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "blit ran out of host memory building its region list");
		}

		for (const TextureBlit & r : regions)
		{
			const std::array<vk::Offset3D, 2> srcBox{ vk::Offset3D(r.srcOffsets[0].x, r.srcOffsets[0].y, r.srcOffsets[0].z),
				vk::Offset3D(r.srcOffsets[1].x, r.srcOffsets[1].y, r.srcOffsets[1].z) };
			const std::array<vk::Offset3D, 2> dstBox{ vk::Offset3D(r.dstOffsets[0].x, r.dstOffsets[0].y, r.dstOffsets[0].z),
				vk::Offset3D(r.dstOffsets[1].x, r.dstOffsets[1].y, r.dstOffsets[1].z) };
			blits.emplace_back(MapSubresourceLayers(r.srcSubresource), srcBox, MapSubresourceLayers(r.dstSubresource), dstBox);
		}

		list->buffer.blitImage(
			srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, blits, vkFilter, list->owner->dispatch);
		return Succeed(error);
	}

	/*
	 * Generates the mip chain by halving blits down the levels. Contract: every mip is in eTransferDstOptimal on entry (the natural state right after uploading
	 * mip 0) and every mip is left in eTransferSrcOptimal on exit so the caller follows with one barrier to the layout it needs. A texture with one mip has
	 * nothing to generate and returns having recorded nothing so it keeps the layout it arrived in and the exit half does not apply.
	 */
	bool VulkanCmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		const TextureSlot * const resolved = device->textureSlots.Resolve(texture, true);
		if (resolved == nullptr || resolved->image == VK_NULL_HANDLE)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "generateMips with an invalid or stale texture handle");
		}
		const TextureSlot & slot = *resolved;

		if (slot.mipLevels <= 1)
		{
			return Succeed(error);
		}

		// generateMips blits each level into the next with a linear filter so the format must support being a blit source and destination and filtering linearly.
		// Block-compressed and integer formats do not.
		if (!FormatSupportsBlit(device, slot.format, true, true) || !FormatSupportsBlit(device, slot.format, false, false))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "generateMips needs a linear-filterable, blit-capable format (not block-compressed or integer)");
		}

		const vk::Image image	   = vk::Image(slot.image);
		const std::uint32_t layers = slot.arrayLayers;
		const auto transition	   = [&](std::uint32_t mip, vk::ImageLayout oldLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess)
		{
			const vk::ImageMemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eTransfer,
				srcAccess,
				vk::PipelineStageFlagBits2::eTransfer,
				dstAccess,
				oldLayout,
				vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, mip, 1, 0, layers));
			vk::DependencyInfo dep;
			dep.setImageMemoryBarriers(barrier);
			device->coreVk13 ? list->buffer.pipelineBarrier2(dep, device->dispatch) : list->buffer.pipelineBarrier2KHR(dep, device->dispatch);
		};

		/*
		 * Makes the whole chain available to a blit before the first one runs. The caller got mip zero to eCopyDst through their own barrier, and the widest transfer
		 * stage the public PipelineStage offers is eCopy, which lowers to VK_PIPELINE_STAGE_2_COPY_BIT. The blits below write at VK_PIPELINE_STAGE_2_BLIT_BIT so the
		 * caller's barrier does not order against them. Nothing in the public surface can name the blit stage, so closing that gap is this entry point's job.
		 * eTransfer covers copy and blit both.
		 */
		{
			const vk::ImageMemoryBarrier2 entry(vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eTransferRead,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eTransferDstOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, slot.mipLevels, 0, layers));
			vk::DependencyInfo dep;
			dep.setImageMemoryBarriers(entry);
			device->coreVk13 ? list->buffer.pipelineBarrier2(dep, device->dispatch) : list->buffer.pipelineBarrier2KHR(dep, device->dispatch);
		}

		// Depth halves alongside width and height so a 3D texture downsamples through its volume. A 2D or array texture has depth 1, leaving the z extent at 1 and
		// the per-level slice count untouched.
		std::int32_t mipWidth  = static_cast<std::int32_t>(slot.width);
		std::int32_t mipHeight = static_cast<std::int32_t>(slot.height);
		std::int32_t mipDepth  = static_cast<std::int32_t>(slot.depth);
		for (std::uint32_t i = 1; i < slot.mipLevels; ++i)
		{
			transition(i - 1, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eTransferRead);

			const std::int32_t nextWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
			const std::int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
			const std::int32_t nextDepth  = mipDepth > 1 ? mipDepth / 2 : 1;
			const std::array<vk::Offset3D, 2> srcBox{ vk::Offset3D(0, 0, 0), vk::Offset3D(mipWidth, mipHeight, mipDepth) };
			const std::array<vk::Offset3D, 2> dstBox{ vk::Offset3D(0, 0, 0), vk::Offset3D(nextWidth, nextHeight, nextDepth) };
			const vk::ImageBlit blit(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, layers),
				srcBox,
				vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, layers),
				dstBox);
			list->buffer.blitImage(
				image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear, device->dispatch);

			mipWidth  = nextWidth;
			mipHeight = nextHeight;
			mipDepth  = nextDepth;
		}

		// The last level was written as a blit destination so leave it in eTransferSrcOptimal like the rest.
		transition(slot.mipLevels - 1, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eTransferRead);
		return Succeed(error);
	}

	bool VulkanCmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		BufferSlot * slot = ResolveBuffer(list->owner, buffer);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearBuffer with an invalid buffer handle");
		}

		list->buffer.fillBuffer(vk::Buffer(slot->buffer), offset, size, value, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image image = ResolveTexture(list->owner, texture);
		if (!image)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageSubresourceRange> subranges;
		if (!detail::TryReserve(subranges, ranges.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "clearTexture ran out of host memory building its subresource list");
		}

		for (const TextureSubresourceRange & r : ranges)
		{
			subranges.push_back(MapSubresourceRange(r));
		}

		const vk::ClearColorValue clear(std::array<float, 4>{ color.r, color.g, color.b, color.a });
		list->buffer.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, clear, subranges, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = ResolveTexture(list->owner, dst);
		const vk::Image srcImage = ResolveTexture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageResolve> resolves;
		if (!detail::TryReserve(resolves, regions.size()))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "resolveTexture ran out of host memory building its region list");
		}

		for (const TextureResolve & r : regions)
		{
			resolves.emplace_back(MapSubresourceLayers(r.srcSubresource),
				vk::Offset3D(r.srcOffset.x, r.srcOffset.y, r.srcOffset.z),
				MapSubresourceLayers(r.dstSubresource),
				vk::Offset3D(r.dstOffset.x, r.dstOffset.y, r.dstOffset.z),
				vk::Extent3D(r.extent.width, r.extent.height, r.extent.depth));
		}

		list->buffer.resolveImage(
			srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, resolves, list->owner->dispatch);
		return Succeed(error);
	}

	// Dynamic state. These record the value the matching dynamic-state pipeline reads, no handle to resolve.
	bool VulkanCmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept
	{
		const std::array<float, 4> constants{ r, g, b, a };
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setBlendConstants(constants.data(), list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, reference, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setDepthBias(constantFactor, clamp, slopeFactor, list->owner->dispatch);
		return Succeed(error);
	}

	// Compute commands. setComputePipeline resolves the pipeline locklessly like the graphics one. Dispatch records directly and dispatchIndirect resolves the
	// indirect-args buffer.
	bool VulkanCmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const vk::Pipeline vkPipeline = ResolveComputePipeline(list->owner, pipeline);
		if (!vkPipeline)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setComputePipeline with an invalid pipeline handle");
		}

		list->buffer.bindPipeline(vk::PipelineBindPoint::eCompute, vkPipeline, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.dispatch(groupCountX, groupCountY, groupCountZ, list->owner->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = ResolveBuffer(list->owner, args);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect with an invalid buffer handle");
		}

		list->buffer.dispatchIndirect(vk::Buffer(slot->buffer), offset, list->owner->dispatch);
		return Succeed(error);
	}

} // namespace azo::rhi::vulkan

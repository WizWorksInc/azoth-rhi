// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/command_list.hpp"
#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/build_config.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "backends/vulkan/barrier_tables.hpp"
#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/layouts.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi::vulkan
{
	namespace
	{
		const BackendObject * command_list_object() noexcept;
	}

	void * vulkan_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createCommandPool");
		auto * device = static_cast<VulkanDevice *>(impl);

		vk::CommandPoolCreateFlags flags{};
		if (desc.transient)
		{
			flags |= vk::CommandPoolCreateFlagBits::eTransient;
		}

		const bool perListReset = desc.reuse == ListReuse::ePerListReset;
		if (perListReset)
		{
			flags |= vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		}

		auto commandPool = host_new<VulkanCommandPool>();
		if (commandPool == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command pool allocation failed");
		}

		commandPool->object				   = publishing_object<Published<CommandPoolApi, &command_pool_block>>();
		commandPool->owner				   = device;
		commandPool->family				   = queue_family_for_type(device, desc.queueType);
		commandPool->resetsIndividualLists = perListReset;

		const auto created = device->device.createCommandPool(vk::CommandPoolCreateInfo(flags, commandPool->family), nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return fail_native_value<void *>(error, "Vulkan command pool creation failed", created.result);
		}

		commandPool->pool = created.value;

		VulkanCommandPool * raw = commandPool.get();
		if (!detail::try_push_back(device->commandPools, std::move(commandPool)))
		{
			device->device.destroyCommandPool(created.value, nullptr, device->dispatch);
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command pool allocation failed");
		}

		return return_value(raw, error);
	}

	void * vulkan_command_pool_allocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.commandPool.allocate");
		auto * commandPool	  = static_cast<VulkanCommandPool *>(impl);
		VulkanDevice * device = commandPool->owner;

		sweep_retired_command_buffers(commandPool);

		if (commandPool->handedOut < commandPool->lists.size())
		{
			VulkanCommandList * recycled = azo::rhi::detail::at(commandPool->lists, commandPool->handedOut);
			++commandPool->handedOut;
			return return_value(recycled, error);
		}

		const auto buffers = device->device.allocateCommandBuffers<HostAllocatorAdapter<vk::CommandBuffer>>(
			vk::CommandBufferAllocateInfo(commandPool->pool, vk::CommandBufferLevel::ePrimary, 1),
			device->dispatch
		);

		if (buffers.result != vk::Result::eSuccess || buffers.value.empty())
		{
			return fail_native_value<void *>(error, "Vulkan command buffer allocation failed", buffers.result);
		}

		auto list = host_new<VulkanCommandList>();
		if (list == nullptr)
		{
			device->device.freeCommandBuffers(commandPool->pool, buffers.value.front(), device->dispatch);
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}

		list->object = command_list_object();
		list->owner	 = device;
		list->pool	 = commandPool;
		list->buffer = buffers.value.front();
		list->family = commandPool->family;

		VulkanCommandList * raw = list.get();
		if (!detail::try_push_back(device->commandLists, std::move(list)))
		{
			device->device.freeCommandBuffers(commandPool->pool, buffers.value.front(), device->dispatch);
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}

		if (!detail::try_push_back(commandPool->lists, raw))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan command list allocation failed");
		}
		++commandPool->handedOut;

		return return_value(raw, error);
	}

	namespace
	{
		const void * vulkan_command_list_query_interface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
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

			return query_published<
				Published<RenderCommandApi, &render_command_block>,
				Published<AliasingCommandApi, &aliasing_command_block>,
				Published<QueryCommandApi, &query_command_block>,
				Published<IndirectApi, &indirect_block>,
				Published<IndirectCountApi, &indirect_count_block>,
				Published<NativeEscapeApi, &native_escape_block>>(object, id, minVersion);
		}

		const BackendObject * command_list_object() noexcept
		{
			static constexpr BackendObject kObject{ .queryInterface = &vulkan_command_list_query_interface };
			return &kObject;
		}
	}

	bool vulkan_command_pool_reset(void * impl, RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.commandPool.reset");
		auto * commandPool = static_cast<VulkanCommandPool *>(impl);

		const std::array retirePoint{ TimelinePoint{ .timeline = safeAfter.timeline, .value = safeAfter.value } };
		// The caller names safeAfter as the point this pool's work is done, so once it is reached a lagging backend signal does not block the reset.
		const bool callerProvedIdle = safeAfter.value != 0 && caller_signal_reached(commandPool->owner, retirePoint);

		// VUID-vkResetCommandPool-commandPool-00040: no buffer allocated from the pool may be pending, and a retired one still counts as allocated.
		for (const VulkanCommandList * list : commandPool->lists)
		{
			if (!callerProvedIdle && list_still_running(list))
			{
				return fail(error, ErrorCode::eInvalidState, kResetOfPoolWithRunningList);
			}
		}
		for (const RetiredCommandBuffer & entry : commandPool->retired)
		{
			if (!callerProvedIdle && submission_still_running(commandPool->owner, entry.submitTimeline, entry.submitValue, entry.callerSignals))
			{
				return fail(error, ErrorCode::eInvalidState, kResetOfPoolWithRunningList);
			}
		}

		for (const vk::Framebuffer framebuffer : commandPool->framebuffers)
		{
			if (framebuffer)
			{
				commandPool->owner->device.destroyFramebuffer(framebuffer, nullptr, commandPool->owner->dispatch);
			}
		}
		commandPool->framebuffers.clear();

		sweep_retired_command_buffers(commandPool);

		// Marked before the native reset rather than after, so a reset that fails cannot leave a list looking submittable.
		for (VulkanCommandList * list : commandPool->lists)
		{
			list->lifecycle		 = ListLifecycle::eFresh;
			list->submitTimeline = kNoSubmitTimeline;
			list->submitValue	 = 0;
			list->callerSignals.clear();
		}

		if (const vk::Result reset = commandPool->owner->device.resetCommandPool(commandPool->pool, {}, commandPool->owner->dispatch);
			reset != vk::Result::eSuccess)
		{
			return fail_native(error, "Vulkan command pool reset failed", reset);
		}

		commandPool->handedOut = 0;

		return succeed(error);
	}

	// Freeing needs no pool flag and only forbids a pending buffer, per VUID-vkFreeCommandBuffers-pCommandBuffers-00047, so a live one waits on the pool.
	static bool RetireAndReplaceBuffer(VulkanCommandList * list, const bool running, Error * error) noexcept
	{
		VulkanCommandPool * pool = list->pool;
		VulkanDevice * device	 = list->owner;
		if (pool == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no pool to take a fresh buffer from");
		}

		const vk::CommandBufferAllocateInfo info(pool->pool, vk::CommandBufferLevel::ePrimary, 1);
		vk::CommandBuffer replacement;
		if (const vk::Result allocated = device->device.allocateCommandBuffers(&info, &replacement, device->dispatch); allocated != vk::Result::eSuccess)
		{
			return fail_native(error, "vkAllocateCommandBuffers failed replacing a used command buffer", allocated);
		}

		const vk::CommandBuffer retiring = list->buffer;
		if (running)
		{
			RetiredCommandBuffer entry{};
			entry.buffer		 = retiring;
			entry.submitTimeline = list->submitTimeline;
			entry.submitValue	 = list->submitValue;

			if (!detail::try_push_back(pool->retired, std::move(entry)))
			{
				device->device.freeCommandBuffers(pool->pool, 1, &replacement, device->dispatch);
				return fail(error, ErrorCode::eOutOfHostMemory, "could not park a command buffer that is still executing");
			}
			pool->retired.back().callerSignals = std::move(list->callerSignals);
		}
		else
		{
			device->device.freeCommandBuffers(pool->pool, 1, &retiring, device->dispatch);
		}

		list->buffer = replacement;
		return true;
	}

	bool vulkan_command_list_begin(void * impl, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		sweep_retired_command_buffers(list->pool);

		// A buffer that has been begun before is no longer in the initial state, and only a pool carrying eResetCommandBuffer may reset it in place.
		const bool used	   = list->lifecycle != ListLifecycle::eFresh;
		const bool running = list_still_running(list);
		// A recording buffer is excluded because vkBeginCommandBuffer forbids it even with the reset bit, per VUID 00049, so it takes the retire path.
		const bool resetHere = list->pool != nullptr && list->pool->resetsIndividualLists && !running && list->lifecycle != ListLifecycle::eRecording;
		if ((used && !resetHere) && (!RetireAndReplaceBuffer(list, running, error)))

		{
			return false;
		}

		if (const vk::Result began = list->buffer.begin(vk::CommandBufferBeginInfo(), device->dispatch); began != vk::Result::eSuccess)
		{
			return fail_native(error, "vkBeginCommandBuffer failed", began);
		}

		list->lifecycle		 = ListLifecycle::eRecording;
		list->submitTimeline = kNoSubmitTimeline;
		list->submitValue	 = 0;
		list->callerSignals.clear();
		return succeed(error);
	}

	bool vulkan_command_list_end(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		if (const vk::Result ended = list->buffer.end(list->owner->dispatch); ended != vk::Result::eSuccess)
		{
			return fail_native(error, "vkEndCommandBuffer failed", ended);
		}

		list->lifecycle = ListLifecycle::eEnded;
		return succeed(error);
	}

	bool vulkan_cmd_set_viewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		float originY = viewport.y;
		float height  = viewport.height;
		if (get_clip_space() == ClipSpaceConvention::eYUp)
		{
			originY = viewport.y + viewport.height;
			height	= -viewport.height;
		}

		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setViewport(0, vk::Viewport(viewport.x, originY, viewport.width, height, viewport.minDepth, viewport.maxDepth), list->owner->dispatch);

		return succeed(error);
	}

	bool vulkan_cmd_set_scissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setScissor(0, vk::Rect2D(vk::Offset2D(scissor.x, scissor.y), vk::Extent2D(scissor.width, scissor.height)), list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_copy_buffer(
		void * impl,
		BufferHandle dst,
		std::uint64_t dstOffset,
		BufferHandle src,
		std::uint64_t srcOffset,
		std::uint64_t size,
		Error * error
	) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		BufferSlot * srcSlot = resolve_buffer(device, src);
		BufferSlot * dstSlot = resolve_buffer(device, dst);
		if (srcSlot == nullptr || dstSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBuffer with an invalid buffer handle");
		}

		list->buffer.copyBuffer(vk::Buffer(srcSlot->buffer), vk::Buffer(dstSlot->buffer), vk::BufferCopy(srcOffset, dstOffset, size), device->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = resolve_query_pool(list->owner, pool);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resetQueryPool with an invalid query pool handle");
		}
		if (firstQuery > slot->queryCount || queryCount > slot->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}

		list->buffer.resetQueryPool(slot->pool, firstQuery, queryCount, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = resolve_query_pool(list->owner, pool);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "writeTimestamp with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}

		if (!is_one_timestamp_stage(stage))
		{
			return fail(error, ErrorCode::eInvalidArgument, "writeTimestamp takes a single stage and this mask names more than one");
		}

		const vk::PipelineStageFlagBits2 stageBit = timestamp_stage(stage);
		if (list->owner->coreVk13)
		{
			list->buffer.writeTimestamp2(stageBit, slot->pool, query, list->owner->dispatch);
		}
		else
		{
			list->buffer.writeTimestamp2KHR(stageBit, slot->pool, query, list->owner->dispatch);
		}
		return succeed(error);
	}

	bool vulkan_cmd_begin_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = resolve_query_pool(list->owner, pool);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "beginQuery with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return fail(error, ErrorCode::eInvalidArgument, "beginQuery names a query past the end of the pool");
		}

		list->buffer.beginQuery(slot->pool, query, vk::QueryControlFlags{}, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_end_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * slot = resolve_query_pool(list->owner, pool);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "endQuery with an invalid query pool handle");
		}
		if (query >= slot->queryCount)
		{
			return fail(error, ErrorCode::eInvalidArgument, "endQuery names a query past the end of the pool");
		}

		list->buffer.endQuery(slot->pool, query, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_resolve_query_data(
		void * impl,
		QueryPoolHandle pool,
		std::uint32_t firstQuery,
		std::uint32_t queryCount,
		BufferHandle dst,
		std::uint64_t dstOffset,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		QueryPoolSlot * poolSlot = resolve_query_pool(list->owner, pool);
		BufferSlot * dstSlot	 = resolve_buffer(list->owner, dst);
		if (poolSlot == nullptr || dstSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveQueryData with an invalid query pool or buffer handle");
		}
		if (firstQuery > poolSlot->queryCount || queryCount > poolSlot->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}

		list->buffer.copyQueryPoolResults(
			poolSlot->pool,
			firstQuery,
			queryCount,
			vk::Buffer(dstSlot->buffer),
			dstOffset,
			sizeof(std::uint64_t),
			vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait,
			list->owner->dispatch
		);
		return succeed(error);
	}

	[[nodiscard]] std::array<float, 4> unpack_label_color(std::uint32_t color) noexcept
	{
		return {
			static_cast<float>((color >> 24) & 0xFFu) / 255.0f,
			static_cast<float>((color >> 16) & 0xFFu) / 255.0f,
			static_cast<float>((color >> 8) & 0xFFu) / 255.0f,
			static_cast<float>(color & 0xFFu) / 255.0f,
		};
	}

	bool vulkan_cmd_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (list->owner->debugUtils && list->owner->debugLabels)
		{
			list->buffer.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT(name != nullptr ? name : "", unpack_label_color(color)), list->owner->dispatch);
		}
		return succeed(error);
	}

	bool vulkan_cmd_end_debug_label(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (list->owner->debugUtils && list->owner->debugLabels)
		{
			list->buffer.endDebugUtilsLabelEXT(list->owner->dispatch);
		}
		return succeed(error);
	}

	bool vulkan_cmd_begin_native_mutation(void * impl, GraphicsApiId api, const NativeMutationDesc & /*unused*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
		}
		return succeed(error);
	}

	bool vulkan_cmd_end_native_mutation(void * impl, const NativeMutationDesc & /*unused*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		return succeed(error);
	}

	bool vulkan_queue_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept
	{
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (queue->owner->debugUtils && queue->owner->debugLabels)
		{
			queue->queue.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT(name != nullptr ? name : "", unpack_label_color(color)), queue->owner->dispatch);
		}
		return succeed(error);
	}

	bool vulkan_queue_end_debug_label(void * impl, Error * error) noexcept
	{
		auto * queue = static_cast<VulkanQueue *>(impl);
		if (queue->owner->debugUtils && queue->owner->debugLabels)
		{
			queue->queue.endDebugUtilsLabelEXT(queue->owner->dispatch);
		}
		return succeed(error);
	}

	namespace
	{
		struct OwnershipFamilies final
		{
			std::uint32_t src = VK_QUEUE_FAMILY_IGNORED;
			std::uint32_t dst = VK_QUEUE_FAMILY_IGNORED;
		};

		[[nodiscard]] OwnershipFamilies families_for(const VulkanCommandList * list, const QueueOwnership & ownership) noexcept
		{
			const std::uint32_t here = list->family;

			switch (ownership.op)
			{
			case OwnershipOp::eRelease:
			{
				const std::uint32_t there = list->owner->family_for_type(ownership.counterpart);
				return here == there ? OwnershipFamilies{} : OwnershipFamilies{ .src = here, .dst = there };
			}
			case OwnershipOp::eAcquire:
			{
				const std::uint32_t there = list->owner->family_for_type(ownership.counterpart);
				return here == there ? OwnershipFamilies{} : OwnershipFamilies{ .src = there, .dst = here };
			}
			case OwnershipOp::eReleaseToExternal:	return { .src = here, .dst = VK_QUEUE_FAMILY_EXTERNAL };
			case OwnershipOp::eAcquireFromExternal: return { .src = VK_QUEUE_FAMILY_EXTERNAL, .dst = here };
			case OwnershipOp::eNone:				break;
			}

			return {};
		}
	}

	bool vulkan_cmd_barriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		detail::HostVector<vk::MemoryBarrier2> memoryBarriers;
		detail::HostVector<vk::BufferMemoryBarrier2> bufferBarriers;
		detail::HostVector<vk::ImageMemoryBarrier2> imageBarriers;
		if (!detail::try_reserve(memoryBarriers, barriers.memory.size()) || !detail::try_reserve(bufferBarriers, barriers.buffers.size()) ||
			!detail::try_reserve(imageBarriers, barriers.textures.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan barrier recording allocation failed");
		}

		for (const MemoryBarrier & b : barriers.memory)
		{
			memoryBarriers.emplace_back(
				map_barrier_stages(b.before.stages, b.before.use),
				map_barrier_access(b.before.use),
				map_barrier_stages(b.after.stages, b.after.use),
				map_barrier_access(b.after.use)
			);
		}

		for (const BufferBarrier & b : barriers.buffers)
		{
			const BufferSlot * slot = resolve_buffer(device, b.buffer);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "buffer barrier with an invalid buffer handle");
			}

			const OwnershipFamilies families = families_for(list, b.ownership);

			bufferBarriers.emplace_back(
				map_barrier_stages(b.before.stages, b.before.use),
				map_barrier_access(b.before.use),
				map_barrier_stages(b.after.stages, b.after.use),
				map_barrier_access(b.after.use),
				families.src,
				families.dst,
				vk::Buffer(slot->buffer),
				b.offset,
				b.size
			);
		}

		for (const TextureBarrier & b : barriers.textures)
		{
			const vk::Image image = resolve_texture(device, b.texture);
			if (!image)
			{
				return fail(error, ErrorCode::eInvalidHandle, "texture barrier with an invalid texture handle");
			}

			const OwnershipFamilies families = families_for(list, b.ownership);

			imageBarriers.emplace_back(
				map_barrier_stages(b.before.stages, b.before.use),
				map_barrier_access(b.before.use),
				map_barrier_stages(b.after.stages, b.after.use),
				map_barrier_access(b.after.use),
				layout_for_use(b.before.use, device->unifiedImageLayouts),
				layout_for_use(b.after.use, device->unifiedImageLayouts),
				families.src,
				families.dst,
				image,
				map_subresource_range(b.range)
			);
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

		return succeed(error);
	}

	bool vulkan_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (barriers.empty())
		{
			return succeed(error);
		}

		VulkanDevice * device = list->owner;
		const auto liveBuffer = [device](const BufferHandle handle) noexcept
		{
			return !handle.is_valid() || device->bufferSlots.resolve(handle, true) != nullptr;
		};
		const auto liveTexture = [device](const TextureHandle handle) noexcept
		{
			return !handle.is_valid() || device->textureSlots.resolve(handle, true) != nullptr;
		};

		for (const AliasBarrier & barrier : barriers)
		{
			if (!liveBuffer(barrier.beforeBuffer) || !liveBuffer(barrier.afterBuffer) || !liveTexture(barrier.beforeTexture) ||
				!liveTexture(barrier.afterTexture))
			{
				return fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
			}
		}

		const vk::MemoryBarrier2 memory(
			vk::PipelineStageFlagBits2::eAllCommands,
			vk::AccessFlagBits2::eMemoryWrite,
			vk::PipelineStageFlagBits2::eAllCommands,
			vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
		);
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
		return succeed(error);
	}

	bool vulkan_cmd_begin_render_pass_scope(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		VulkanDevice * device = list->owner;

		RenderPassKey key;
		if (desc.colors.size() > key.colors.size())
		{
			return fail(error, ErrorCode::eInvalidArgument, "rendering scope exceeds the maximum color attachment count");
		}
		key.colorCount = static_cast<std::uint32_t>(desc.colors.size());

		detail::HostVector<vk::ImageView> views;
		detail::HostVector<vk::ClearValue> clears;
		if (!detail::try_reserve(views, desc.colors.size() + 1) || !detail::try_reserve(clears, desc.colors.size() + 1))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		for (std::size_t i = 0; i < desc.colors.size(); ++i)
		{
			const RenderingAttachment & a = azo::rhi::detail::at(desc.colors, i);
			const TextureViewSlot * slot  = resolve_texture_view_slot(device, a.view);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering color attachment with an invalid view handle");
			}
			// An attachment count past this array is refused above. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			key.colors[i] = RenderPassAttachmentKey{
				.format = slot->format,
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				.samples = slot->samples,
				.loadOp	 = map_load_op(a.load),
				.storeOp = map_store_op(a.store),
				.layout	 = layout_for_use(a.state.use, device->unifiedImageLayouts),
			};
			views.push_back(slot->view);
			clears.emplace_back(vk::ClearColorValue(std::array<float, 4>{ a.clearColor.r, a.clearColor.g, a.clearColor.b, a.clearColor.a }));
		}

		if (desc.depthStencil != nullptr)
		{
			const TextureViewSlot * slot = resolve_texture_view_slot(device, desc.depthStencil->view);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment with an invalid view handle");
			}
			key.hasDepth = true;
			key.depth	 = RenderPassAttachmentKey{
				.format	 = slot->format,
				.samples = slot->samples,
				.loadOp	 = map_load_op(desc.depthStencil->load),
				.storeOp = map_store_op(desc.depthStencil->store),
				.layout	 = layout_for_use(desc.depthStencil->state.use, device->unifiedImageLayouts),
			};
			views.push_back(slot->view);
			clears.emplace_back(vk::ClearDepthStencilValue(desc.depthStencil->clearDepthStencil.depth, desc.depthStencil->clearDepthStencil.stencil));
		}

		vk::Result renderPassResult		= vk::Result::eSuccess;
		const vk::RenderPass renderPass = get_or_create_render_pass(device, list->pool->renderPasses, key, renderPassResult);
		if (!renderPass)
		{
			return fail_native(error, "Vulkan render pass creation failed", renderPassResult);
		}

		vk::FramebufferCreateInfo fbInfo;
		fbInfo.renderPass = renderPass;
		fbInfo.setAttachments(views);
		fbInfo.width  = desc.x + desc.width;
		fbInfo.height = desc.y + desc.height;
		fbInfo.layers = desc.layers;

		const auto created = device->device.createFramebuffer(fbInfo, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return fail_native(error, "Vulkan framebuffer creation failed", created.result);
		}

		if (!detail::try_push_back(list->pool->framebuffers, created.value))
		{
			device->device.destroyFramebuffer(created.value, nullptr, device->dispatch);
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		vk::RenderPassBeginInfo beginInfo;
		beginInfo.renderPass  = renderPass;
		beginInfo.framebuffer = created.value;
		beginInfo.renderArea =
			vk::Rect2D(vk::Offset2D(static_cast<std::int32_t>(desc.x), static_cast<std::int32_t>(desc.y)), vk::Extent2D(desc.width, desc.height));
		beginInfo.setClearValues(clears);

		list->buffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline, list->owner->dispatch);
		return succeed(error);
	}

	namespace
	{
		bool begin_rendering_timestamps(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept
		{
			list->pendingEndTimestamp = vk::QueryPool{};
			if (desc.timestamps == nullptr)
			{
				return succeed(error);
			}

			QueryPoolSlot * slot = resolve_query_pool(list->owner, desc.timestamps->pool);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering timestamp writes name a query pool this device never created");
			}
			if ((desc.timestamps->beginQuery != kInvalidIndex && desc.timestamps->beginQuery >= slot->queryCount) ||
				(desc.timestamps->endQuery != kInvalidIndex && desc.timestamps->endQuery >= slot->queryCount))
			{
				return fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a query past the end of the pool");
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
			return succeed(error);
		}
	}

	bool vulkan_cmd_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		if (!begin_rendering_timestamps(list, desc, error))
		{
			return false;
		}

		if (!device->dynamicRendering)
		{
			return vulkan_cmd_begin_render_pass_scope(list, desc, error);
		}

		detail::HostVector<vk::RenderingAttachmentInfo> colorAttachments;
		if (!detail::try_reserve(colorAttachments, desc.colors.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan rendering attachment allocation failed");
		}

		for (const RenderingAttachment & a : desc.colors)
		{
			const vk::ImageView view = resolve_texture_view(device, a.view);
			if (!view)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering color attachment with an invalid view handle");
			}

			vk::RenderingAttachmentInfo info;
			info.imageView	 = view;
			info.imageLayout = layout_for_use(a.state.use, device->unifiedImageLayouts);
			info.loadOp		 = map_load_op(a.load);
			info.storeOp	 = map_store_op(a.store);
			info.clearValue	 = vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{ a.clearColor.r, a.clearColor.g, a.clearColor.b, a.clearColor.a }));
			colorAttachments.push_back(info);
		}

		vk::RenderingAttachmentInfo depthAttachment;
		vk::RenderingAttachmentInfo stencilAttachment;
		const bool hasDepth = desc.depthStencil != nullptr;
		bool hasStencil		= false;
		if (hasDepth)
		{
			const TextureViewSlot * slot = resolve_texture_view_slot(device, desc.depthStencil->view);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment with an invalid view handle");
			}
			const vk::ImageView view = slot->view;

			depthAttachment.imageView	= view;
			depthAttachment.imageLayout = layout_for_use(desc.depthStencil->state.use, device->unifiedImageLayouts);
			depthAttachment.loadOp		= map_load_op(desc.depthStencil->load);
			depthAttachment.storeOp		= map_store_op(desc.depthStencil->store);
			depthAttachment.clearValue =
				vk::ClearValue(vk::ClearDepthStencilValue(desc.depthStencil->clearDepthStencil.depth, desc.depthStencil->clearDepthStencil.stencil));

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

		return succeed(error);
	}

	bool vulkan_cmd_end_rendering(void * impl, Error * error) noexcept
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
		return succeed(error);
	}

	bool vulkan_cmd_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const vk::Pipeline vkPipeline = resolve_graphics_pipeline(list->owner, pipeline);
		if (!vkPipeline)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline with an invalid pipeline handle");
		}

		list->buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vkPipeline, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_push_constants(
		void * impl,
		PipelineLayoutHandle layout,
		Flags<ShaderStage> stages,
		std::uint32_t offset,
		std::uint32_t size,
		const void * data,
		Error * error
	) noexcept
	{
		auto * list						  = static_cast<VulkanCommandList *>(impl);
		const vk::PipelineLayout vkLayout = resolve_pipeline_layout(list->owner, layout);
		if (!vkLayout)
		{
			return fail(error, ErrorCode::eInvalidHandle, "pushConstants with an invalid pipeline layout handle");
		}

		list->buffer.pushConstants(vkLayout, map_shader_stages(stages), offset, size, data, list->owner->dispatch);
		return succeed(error);
	}

	// NOLINTNEXTLINE(bugprone-exception-escape)
	bool vulkan_cmd_set_vertex_buffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const BufferSlot * bufferSlot = resolve_buffer(list->owner, buffer);
		if (bufferSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer with an invalid buffer handle");
		}

		list->buffer.bindVertexBuffers(slot, vk::Buffer(bufferSlot->buffer), static_cast<vk::DeviceSize>(offset), list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_set_index_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const BufferSlot * bufferSlot = resolve_buffer(list->owner, buffer);
		if (bufferSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer with an invalid buffer handle");
		}

		list->buffer.bindIndexBuffer(vk::Buffer(bufferSlot->buffer), offset, index32 ? vk::IndexType::eUint32 : vk::IndexType::eUint16, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_draw(
		void * impl,
		std::uint32_t vertexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstVertex,
		std::uint32_t firstInstance,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.draw(vertexCount, instanceCount, firstVertex, firstInstance, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_draw_indexed(
		void * impl,
		std::uint32_t indexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstIndex,
		std::int32_t vertexOffset,
		std::uint32_t firstInstance,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, list->owner->dispatch);
		return succeed(error);
	}

	namespace
	{
		template <typename RecordFn>
		[[nodiscard]] bool lower_multi_draw(
			const VulkanCommandList & list,
			std::uint64_t offset,
			std::uint32_t drawCount,
			std::uint32_t stride,
			std::size_t commandSize,
			Error * error,
			const RecordFn & record
		) noexcept
		{
			if (drawCount > 1 && !list.owner->caps.supportsMultiDrawIndirect)
			{
				if (stride < commandSize)
				{
					return fail(error, ErrorCode::eInvalidArgument, "an indirect multi-draw needs a stride of at least one command");
				}

				for (std::uint32_t draw = 0; draw < drawCount; ++draw)
				{
					record(offset + (std::uint64_t{ draw } * stride), 1u);
				}

				return succeed(error);
			}

			record(offset, drawCount);
			return succeed(error);
		}
	}

	bool vulkan_cmd_draw_indirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = resolve_buffer(list->owner, args);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndirect with an invalid buffer handle");
		}

		return lower_multi_draw(
			*list,
			offset,
			drawCount,
			stride,
			sizeof(vk::DrawIndirectCommand),
			error,
			[list, slot, stride](std::uint64_t commandOffset, std::uint32_t count) noexcept
			{
				list->buffer.drawIndirect(vk::Buffer(slot->buffer), commandOffset, count, stride, list->owner->dispatch);
			}
		);
	}

	bool vulkan_cmd_draw_indexed_indirect(
		void * impl,
		BufferHandle args,
		std::uint64_t offset,
		std::uint32_t drawCount,
		std::uint32_t stride,
		Error * error
	) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = resolve_buffer(list->owner, args);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirect with an invalid buffer handle");
		}

		return lower_multi_draw(
			*list,
			offset,
			drawCount,
			stride,
			sizeof(vk::DrawIndexedIndirectCommand),
			error,
			[list, slot, stride](std::uint64_t commandOffset, std::uint32_t count) noexcept
			{
				list->buffer.drawIndexedIndirect(vk::Buffer(slot->buffer), commandOffset, count, stride, list->owner->dispatch);
			}
		);
	}

	bool vulkan_cmd_draw_indirect_count(
		void * impl,
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (!list->owner->caps.supportsIndirectCount)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "drawIndirectCount requires Vulkan 1.2 (vkCmdDrawIndirectCount)");
		}

		BufferSlot * argsSlot  = resolve_buffer(list->owner, args);
		BufferSlot * countSlot = resolve_buffer(list->owner, count);
		if (argsSlot == nullptr || countSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndirectCount with an invalid buffer handle");
		}

		list->buffer.drawIndirectCount(
			vk::Buffer(argsSlot->buffer),
			argsOffset,
			vk::Buffer(countSlot->buffer),
			countOffset,
			maxDrawCount,
			stride,
			list->owner->dispatch
		);
		return succeed(error);
	}

	bool vulkan_cmd_draw_indexed_indirect_count(
		void * impl,
		BufferHandle args,
		std::uint64_t argsOffset,
		BufferHandle count,
		std::uint64_t countOffset,
		std::uint32_t maxDrawCount,
		std::uint32_t stride,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		if (!list->owner->caps.supportsIndirectCount)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "drawIndexedIndirectCount requires Vulkan 1.2 (vkCmdDrawIndexedIndirectCount)");
		}

		BufferSlot * argsSlot  = resolve_buffer(list->owner, args);
		BufferSlot * countSlot = resolve_buffer(list->owner, count);
		if (argsSlot == nullptr || countSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirectCount with an invalid buffer handle");
		}

		list->buffer.drawIndexedIndirectCount(
			vk::Buffer(argsSlot->buffer),
			argsOffset,
			vk::Buffer(countSlot->buffer),
			countOffset,
			maxDrawCount,
			stride,
			list->owner->dispatch
		);
		return succeed(error);
	}

	[[nodiscard]] vk::ImageSubresourceLayers map_subresource_layers(const TextureSubresource & sub) noexcept
	{
		return { map_aspect(sub.aspects), sub.mip, sub.layer, 1 };
	}

	bool vulkan_cmd_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = resolve_texture(list->owner, dst);
		BufferSlot * srcSlot	 = resolve_buffer(list->owner, src);
		if (!dstImage || srcSlot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture with an invalid texture or buffer handle");
		}

		detail::HostVector<vk::BufferImageCopy> copies;
		if (!detail::try_reserve(copies, regions.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "copyBufferToTexture ran out of host memory building its region list");
		}

		for (const BufferTextureCopy & r : regions)
		{
			copies.emplace_back(
				r.bufferOffset,
				r.bufferRowLength,
				r.bufferImageHeight,
				map_subresource_layers(r.subresource),
				vk::Offset3D(r.textureOffset.x, r.textureOffset.y, r.textureOffset.z),
				vk::Extent3D(r.textureExtent.width, r.textureExtent.height, r.textureExtent.depth)
			);
		}

		const vk::ImageLayout dstLayout = layout_for_use(ResourceUse::eCopyDst, list->owner->unifiedImageLayouts);
		list->buffer.copyBufferToImage(vk::Buffer(srcSlot->buffer), dstImage, dstLayout, copies, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		BufferSlot * dstSlot	 = resolve_buffer(list->owner, dst);
		const vk::Image srcImage = resolve_texture(list->owner, src);
		if (dstSlot == nullptr || !srcImage)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer with an invalid buffer or texture handle");
		}

		detail::HostVector<vk::BufferImageCopy> copies;
		if (!detail::try_reserve(copies, regions.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "copyTextureToBuffer ran out of host memory building its region list");
		}

		for (const BufferTextureCopy & r : regions)
		{
			copies.emplace_back(
				r.bufferOffset,
				r.bufferRowLength,
				r.bufferImageHeight,
				map_subresource_layers(r.subresource),
				vk::Offset3D(r.textureOffset.x, r.textureOffset.y, r.textureOffset.z),
				vk::Extent3D(r.textureExtent.width, r.textureExtent.height, r.textureExtent.depth)
			);
		}

		const vk::ImageLayout srcLayout = layout_for_use(ResourceUse::eCopySrc, list->owner->unifiedImageLayouts);
		list->buffer.copyImageToBuffer(srcImage, srcLayout, vk::Buffer(dstSlot->buffer), copies, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = resolve_texture(list->owner, dst);
		const vk::Image srcImage = resolve_texture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageCopy> copies;
		if (!detail::try_reserve(copies, regions.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "copyTexture ran out of host memory building its region list");
		}

		for (const TextureCopy & r : regions)
		{
			copies.emplace_back(
				map_subresource_layers(r.srcSubresource),
				vk::Offset3D(r.srcOffset.x, r.srcOffset.y, r.srcOffset.z),
				map_subresource_layers(r.dstSubresource),
				vk::Offset3D(r.dstOffset.x, r.dstOffset.y, r.dstOffset.z),
				vk::Extent3D(r.extent.width, r.extent.height, r.extent.depth)
			);
		}

		const vk::ImageLayout srcLayout = layout_for_use(ResourceUse::eCopySrc, list->owner->unifiedImageLayouts);
		const vk::ImageLayout dstLayout = layout_for_use(ResourceUse::eCopyDst, list->owner->unifiedImageLayouts);
		list->buffer.copyImage(srcImage, srcLayout, dstImage, dstLayout, copies, list->owner->dispatch);
		return succeed(error);
	}

	[[nodiscard]] bool format_supports_blit(const VulkanDevice * device, vk::Format format, bool asSource, bool linearFilter) noexcept
	{
		const vk::FormatFeatureFlags features = device->phys.getFormatProperties(format, device->dispatch).optimalTilingFeatures;
		const vk::FormatFeatureFlags required = asSource ? vk::FormatFeatureFlagBits::eBlitSrc : vk::FormatFeatureFlagBits::eBlitDst;
		if ((features & required) != required)
		{
			return false;
		}

		return !(asSource && linearFilter) || static_cast<bool>(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
	}

	bool vulkan_cmd_blit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = resolve_texture(list->owner, dst);
		const vk::Image srcImage = resolve_texture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return fail(error, ErrorCode::eInvalidHandle, "blit with an invalid texture handle");
		}

		const bool linear		   = filter == Filter::eLinear;
		const vk::Format srcFormat = list->owner->textureSlots.resolve(src, false)->format;
		const vk::Format dstFormat = list->owner->textureSlots.resolve(dst, false)->format;
		if (!format_supports_blit(list->owner, srcFormat, true, linear))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "blit source format does not support blit, or linear filtering when requested");
		}

		if (!format_supports_blit(list->owner, dstFormat, false, false))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "blit destination format does not support being a blit target");
		}

		const vk::Filter vkFilter = filter == Filter::eLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
		detail::HostVector<vk::ImageBlit> blits;
		if (!detail::try_reserve(blits, regions.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "blit ran out of host memory building its region list");
		}

		for (const TextureBlit & r : regions)
		{
			const std::array<vk::Offset3D, 2> srcBox{
				vk::Offset3D(azo::rhi::detail::at(r.srcOffsets, 0).x, azo::rhi::detail::at(r.srcOffsets, 0).y, azo::rhi::detail::at(r.srcOffsets, 0).z),
				vk::Offset3D(azo::rhi::detail::at(r.srcOffsets, 1).x, azo::rhi::detail::at(r.srcOffsets, 1).y, azo::rhi::detail::at(r.srcOffsets, 1).z),
			};
			const std::array<vk::Offset3D, 2> dstBox{
				vk::Offset3D(azo::rhi::detail::at(r.dstOffsets, 0).x, azo::rhi::detail::at(r.dstOffsets, 0).y, azo::rhi::detail::at(r.dstOffsets, 0).z),
				vk::Offset3D(azo::rhi::detail::at(r.dstOffsets, 1).x, azo::rhi::detail::at(r.dstOffsets, 1).y, azo::rhi::detail::at(r.dstOffsets, 1).z),
			};
			blits.emplace_back(map_subresource_layers(r.srcSubresource), srcBox, map_subresource_layers(r.dstSubresource), dstBox);
		}

		const vk::ImageLayout srcLayout = layout_for_use(ResourceUse::eCopySrc, list->owner->unifiedImageLayouts);
		const vk::ImageLayout dstLayout = layout_for_use(ResourceUse::eCopyDst, list->owner->unifiedImageLayouts);
		list->buffer.blitImage(srcImage, srcLayout, dstImage, dstLayout, blits, vkFilter, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		auto * list			  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device = list->owner;

		const TextureSlot * const resolved = device->textureSlots.resolve(texture, true);
		if (resolved == nullptr || resolved->image == VK_NULL_HANDLE)
		{
			return fail(error, ErrorCode::eInvalidHandle, "generateMips with an invalid or stale texture handle");
		}
		const TextureSlot & slot = *resolved;

		if (slot.mipLevels <= 1)
		{
			return succeed(error);
		}

		if (!format_supports_blit(device, slot.format, true, true) || !format_supports_blit(device, slot.format, false, false))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "generateMips needs a linear-filterable, blit-capable format (not block-compressed or integer)");
		}

		const vk::Image image	   = vk::Image(slot.image);
		const std::uint32_t layers = slot.arrayLayers;

		const vk::ImageLayout srcLayout = layout_for_use(ResourceUse::eCopySrc, device->unifiedImageLayouts);
		const vk::ImageLayout dstLayout = layout_for_use(ResourceUse::eCopyDst, device->unifiedImageLayouts);
		const auto transition			= [&](std::uint32_t mip)
		{
			const vk::ImageMemoryBarrier2 barrier(
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferRead,
				dstLayout,
				srcLayout,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, mip, 1, 0, layers)
			);
			vk::DependencyInfo dep;
			dep.setImageMemoryBarriers(barrier);
			device->coreVk13 ? list->buffer.pipelineBarrier2(dep, device->dispatch) : list->buffer.pipelineBarrier2KHR(dep, device->dispatch);
		};

		auto mipWidth  = static_cast<std::int32_t>(slot.width);
		auto mipHeight = static_cast<std::int32_t>(slot.height);
		auto mipDepth  = static_cast<std::int32_t>(slot.depth);
		for (std::uint32_t i = 1; i < slot.mipLevels; ++i)
		{
			const std::int32_t nextWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
			const std::int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
			const std::int32_t nextDepth  = mipDepth > 1 ? mipDepth / 2 : 1;
			const std::array<vk::Offset3D, 2> srcBox{ vk::Offset3D(0, 0, 0), vk::Offset3D(mipWidth, mipHeight, mipDepth) };
			const std::array<vk::Offset3D, 2> dstBox{ vk::Offset3D(0, 0, 0), vk::Offset3D(nextWidth, nextHeight, nextDepth) };
			const vk::ImageBlit blit(
				vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, layers),
				srcBox,
				vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, layers),
				dstBox
			);
			list->buffer.blitImage(image, srcLayout, image, dstLayout, blit, vk::Filter::eLinear, device->dispatch);

			transition(i);

			mipWidth  = nextWidth;
			mipHeight = nextHeight;
			mipDepth  = nextDepth;
		}

		return succeed(error);
	}

	bool vulkan_cmd_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		BufferSlot * slot = resolve_buffer(list->owner, buffer);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearBuffer with an invalid buffer handle");
		}

		list->buffer.fillBuffer(vk::Buffer(slot->buffer), offset, size, value, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_clear_texture(
		void * impl,
		TextureHandle texture,
		const ClearColor & color,
		std::span<const TextureSubresourceRange> ranges,
		Error * error
	) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image image = resolve_texture(list->owner, texture);
		if (!image)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageSubresourceRange> subranges;
		if (!detail::try_reserve(subranges, ranges.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "clearTexture ran out of host memory building its subresource list");
		}

		for (const TextureSubresourceRange & r : ranges)
		{
			subranges.push_back(map_subresource_range(r));
		}

		const vk::ClearColorValue clear(std::array<float, 4>{ color.r, color.g, color.b, color.a });
		const vk::ImageLayout clearLayout = layout_for_use(ResourceUse::eCopyDst, list->owner->unifiedImageLayouts);
		list->buffer.clearColorImage(image, clearLayout, clear, subranges, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);

		const vk::Image dstImage = resolve_texture(list->owner, dst);
		const vk::Image srcImage = resolve_texture(list->owner, src);
		if (!dstImage || !srcImage)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveTexture with an invalid texture handle");
		}

		detail::HostVector<vk::ImageResolve> resolves;
		if (!detail::try_reserve(resolves, regions.size()))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "resolveTexture ran out of host memory building its region list");
		}

		for (const TextureResolve & r : regions)
		{
			resolves.emplace_back(
				map_subresource_layers(r.srcSubresource),
				vk::Offset3D(r.srcOffset.x, r.srcOffset.y, r.srcOffset.z),
				map_subresource_layers(r.dstSubresource),
				vk::Offset3D(r.dstOffset.x, r.dstOffset.y, r.dstOffset.z),
				vk::Extent3D(r.extent.width, r.extent.height, r.extent.depth)
			);
		}

		const vk::ImageLayout srcLayout = layout_for_use(ResourceUse::eResolveSrc, list->owner->unifiedImageLayouts);
		const vk::ImageLayout dstLayout = layout_for_use(ResourceUse::eResolveDst, list->owner->unifiedImageLayouts);
		list->buffer.resolveImage(srcImage, srcLayout, dstImage, dstLayout, resolves, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_set_blend_constants(void * impl, float r, float g, float b, float a, Error * error) noexcept
	{
		const std::array<float, 4> constants{ r, g, b, a };
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setBlendConstants(constants.data(), list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_set_stencil_reference(void * impl, std::uint32_t reference, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, reference, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_set_depth_bias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.setDepthBias(constantFactor, clamp, slopeFactor, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * list					  = static_cast<VulkanCommandList *>(impl);
		const vk::Pipeline vkPipeline = resolve_compute_pipeline(list->owner, pipeline);
		if (!vkPipeline)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setComputePipeline with an invalid pipeline handle");
		}

		list->buffer.bindPipeline(vk::PipelineBindPoint::eCompute, vkPipeline, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_dispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept
	{
		auto * list = static_cast<VulkanCommandList *>(impl);
		list->buffer.dispatch(groupCountX, groupCountY, groupCountZ, list->owner->dispatch);
		return succeed(error);
	}

	bool vulkan_cmd_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * list		  = static_cast<VulkanCommandList *>(impl);
		BufferSlot * slot = resolve_buffer(list->owner, args);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect with an invalid buffer handle");
		}

		list->buffer.dispatchIndirect(vk::Buffer(slot->buffer), offset, list->owner->dispatch);
		return succeed(error);
	}

}

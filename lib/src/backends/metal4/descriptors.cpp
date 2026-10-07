// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/resources/descriptors.hpp"

#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/blocks/descriptor_arena.hpp"
#include "azoth/rhi/backend/blocks/queue.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLGPUAddress.hpp>
#include <Metal/MTLResource.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace azo::rhi::metal4
{
	namespace
	{
		[[nodiscard]] constexpr std::uint32_t members_for(const DescriptorBinding & entry) noexcept
		{
			return (entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u) * std::max(entry.count, 1u);
		}

		[[nodiscard]] constexpr std::uint64_t descriptor_key(const std::uint32_t binding, const std::uint32_t arrayIndex) noexcept
		{
			return (static_cast<std::uint64_t>(binding) << 32u) | arrayIndex;
		}

		[[nodiscard]] constexpr std::uint32_t binding_of(const std::uint64_t key) noexcept
		{
			return static_cast<std::uint32_t>(key >> 32u);
		}

		[[nodiscard]] bool metal_argument_member_index(
			const Metal4DescriptorSetLayout & layout,
			const std::uint32_t binding,
			std::uint32_t & outMember,
			std::uint32_t & outCount
		) noexcept
		{
			std::uint32_t member = 0;
			bool found			 = false;
			for (const DescriptorBinding & entry : layout.bindings)
			{
				if (entry.binding == binding && !found)
				{
					outMember = member;
					found	  = true;
				}

				member += members_for(entry);
			}

			outCount = member;
			return found;
		}

		[[nodiscard]] constexpr std::uint64_t metal_argument_buffer_bytes(const std::uint32_t memberCount) noexcept
		{
			return static_cast<std::uint64_t>(memberCount) * sizeof(std::uint64_t);
		}

		void write_argument_member(const Metal4DescriptorSet & set, const std::uint32_t member, const std::uint64_t value) noexcept
		{
			if (set.argumentBuffer.get() == nullptr)
			{
				return;
			}

			auto * words = static_cast<std::uint64_t *>(set.argumentBuffer->contents());
			if (words != nullptr && metal_argument_buffer_bytes(member + 1) <= set.argumentBuffer->length())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): an argument buffer is a flat run of words by construction.
				words[member] = value;
			}
		}

		void encode_argument(Metal4Device * device, Metal4DescriptorSet & set, const std::uint32_t binding, const std::uint32_t element) noexcept
		{
			if (set.argumentBuffer.get() == nullptr)
			{
				return;
			}

			const auto * layout = device->descriptorSetLayouts.resolve(set.layout, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return;
			}

			std::uint32_t member = 0;
			for (const DescriptorBinding & entry : layout->bindings)
			{
				const std::uint32_t stride = entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u;
				if (entry.binding == binding && element < std::max(entry.count, 1u))
				{
					const auto found = set.bindings.find(descriptor_key(entry.binding, element));
					if (found == set.bindings.end())
					{
						return;
					}

					const Metal4Descriptor & descriptor = found->second;
					const std::uint32_t at				= member + (element * stride);
					if (descriptor.buffer != nullptr)
					{
						write_argument_member(set, at, descriptor.buffer->gpuAddress() + descriptor.offset);
					}
					else if (descriptor.texture != nullptr)
					{
						write_argument_member(set, at, descriptor.texture->gpuResourceID()._impl);
					}
					else if (descriptor.sampler != nullptr)
					{
						write_argument_member(set, at, descriptor.sampler->gpuResourceID()._impl);
					}

					if (entry.type == DescriptorType::eCombinedImageSampler && descriptor.sampler != nullptr)
					{
						write_argument_member(set, at + 1, descriptor.sampler->gpuResourceID()._impl);
					}
					return;
				}

				member += members_for(entry);
			}
		}
	}

	bool metal4_cmd_bind_descriptor_set(
		void * impl,
		PipelineLayoutHandle /*unused*/,
		const std::uint32_t setIndex,
		DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.bindDescriptorSet");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no argument table");
		}

		const auto * tracked = device->descriptorSets.resolve(set, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet names a set this device never created");
		}

		if (tracked->argumentBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "this device has no argument buffers, which the Metal 4 backend binds sets through");
		}

		static_cast<void>(dynamicOffsets);

		list->argumentTable->setAddress(tracked->argumentBuffer->gpuAddress(), metal_argument_buffer_index_for_set(setIndex));
		return succeed(error);
	}

	bool metal4_cmd_push_constants(
		void * impl,
		PipelineLayoutHandle /*unused*/,
		Flags<ShaderStage> /*unused*/,
		const std::uint32_t offset,
		const std::uint32_t size,
		const void * data,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.pushConstants");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no argument table");
		}
		if (data == nullptr || size == 0)
		{
			return succeed(error);
		}

		if (offset != 0)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "the Metal 4 backend writes a push constant range whole, so a non-zero offset is refused");
		}

		const MTL::GPUAddress address = write_push_constants(device, list, data, size);
		if (address == 0)
		{
			return fail(error, ErrorCode::eOutOfDeviceMemory, "push constant storage could not be allocated");
		}

		list->argumentTable->setAddress(address, kMetalPushConstantIndex);
		return succeed(error);
	}

	bool metal4_update_descriptors_buffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsBuffer");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorWriteBuffer & write : writes)
		{
			auto * set = device->descriptorSets.resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			MTL::Buffer * buffer = resolve_buffer(device, write.buffer);
			if (buffer == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a buffer this device never created");
			}
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = Metal4Descriptor{
				.type	= write.type,
				.buffer = buffer,
				.offset = write.offset,
			};
			encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	bool metal4_update_descriptors_texture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsTexture");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorWriteTexture & write : writes)
		{
			auto * set = device->descriptorSets.resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			Metal4Descriptor descriptor{ .type = write.type };
			descriptor.texture = resolve_texture_view(device, write.view);
			if (write.sampler.is_valid())
			{
				const auto * sampler = device->samplers.resolve(write.sampler, kHandleAlreadyChecked);
				descriptor.sampler	 = sampler != nullptr ? sampler->get() : nullptr;
			}
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = descriptor;
			encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	bool metal4_update_descriptors_sampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsSampler");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorWriteSampler & write : writes)
		{
			auto * set = device->descriptorSets.resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			const auto * sampler										   = device->samplers.resolve(write.sampler, kHandleAlreadyChecked);
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = Metal4Descriptor{
				.type	 = DescriptorType::eSampler,
				.sampler = sampler != nullptr ? sampler->get() : nullptr,
			};
			encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	void * metal4_create_descriptor_arena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorArena");

		void * arena = alloc_object(static_cast<Metal4Device *>(impl), publishing_object<Published<DescriptorArenaApi, &descriptor_arena_block>>());
		if (arena == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor arena allocation failed");
		}

		return return_value(arena, error);
	}

	void * metal4_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createCommandPool");

		auto * device = static_cast<Metal4Device *>(impl);

		void * pool = alloc_object(device, publishing_object<Published<CommandPoolApi, &command_pool_block>>(), desc.queueType);
		if (pool == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		auto record = host_new<CmdPool>();
		if (record == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		CmdPool * raw = record.get();
		if (!detail::try_push_back(device->cmdPools, std::move(record)))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool tracking failed");
		}

		static_cast<Metal4Object *>(pool)->pool = raw;
		return return_value(pool, error);
	}

	void * metal4_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);
		if (index >= queue_count_for_type(device->caps, type))
		{
			return fail_value<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
		}

		void * queue = alloc_object(device, publishing_object<Published<QueueApi, &queue_block>>(), type);
		if (queue == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal queue allocation failed");
		}

		return return_value(queue, error);
	}

	DescriptorSetHandle metal4_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.allocate");

		auto * arena		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;
		if (!resolves(device, desc.layout))
		{
			return fail_value<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocated from an invalid or stale layout handle");
		}

		NS::SharedPtr<MTL::Buffer> argumentBuffer;
		if (device->caps.bindingTier >= BindingTier::eUnbounded)
		{
			const auto * setLayout	   = device->descriptorSetLayouts.resolve(desc.layout, kHandleAlreadyChecked);
			std::uint32_t memberCount  = 0;
			std::uint32_t ignoredIndex = 0;
			if (setLayout != nullptr)
			{
				static_cast<void>(metal_argument_member_index(*setLayout, ~0u, ignoredIndex, memberCount));
			}

			if (memberCount > 0)
			{
				MTL::Buffer * raw = device->device->newBuffer(metal_argument_buffer_bytes(memberCount), MTL::ResourceStorageModeShared);
				if (raw == nullptr)
				{
					return fail_value<DescriptorSetHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal descriptor set argument buffer allocation failed");
				}

				set_metal_label(raw, desc.debugName);
				argumentBuffer = NS::TransferPtr(raw);
				std::memset(argumentBuffer->contents(), 0, argumentBuffer->length());

				device->note_allocation(Metal4Device::Residency::eDescriptorSets, argumentBuffer.get());
			}
		}

		const DescriptorSetHandle handle = device->descriptorSets.store(
			Metal4DescriptorSet{
				.bindings		= {},
				.arena			= arena,
				.epoch			= arena->arenaEpoch.load(std::memory_order_acquire),
				.layout			= desc.layout,
				.argumentBuffer = std::move(argumentBuffer),
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set tracking failed");
		}

		return return_value(handle, error);
	}

	bool metal4_arena_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.reset");

		auto * arena		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;

		const std::uint64_t bumped = arena->arenaEpoch.fetch_add(1, std::memory_order_release) + 1;

		device->descriptorSets.retire_if(
			[arena, bumped](const Metal4DescriptorSet & set)
			{
				return set.arena == arena && set.epoch < bumped;
			}
		);

		return succeed(error);
	}

}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/blocks/descriptor_arena.hpp"
#include "azoth/rhi/backend/blocks/queue.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"
#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"
#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLResource.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace azo::rhi::metal
{
	bool ensure_compute_encoder(MetalObject * object, Error * error) noexcept
	{
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}
		if (object->list->renderEncoder.get() != nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "a compute command cannot be recorded inside a rendering scope, so record it between passes");
		}
		if (object->list->computeEncoder.get() == nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			object->list->computeEncoder				  = NS::RetainPtr(object->list->commandBuffer->computeCommandEncoder());
			++object->list->encoderEpoch;
			consume_alias_wait(object->list, object->list->computeEncoder.get());
		}

		return succeed(error);
	}

	bool metal_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = cmd_buffer_of(object);
		if (commandBuffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const auto * tracked = device->computePipelines.resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setComputePipeline names a pipeline this device never created");
		}

		if (!ensure_compute_encoder(object, error))
		{
			return false;
		}

		object->list->computeEncoder->setComputePipelineState(tracked->state.get());
		object->list->boundThreadGroup = tracked->threadsPerThreadgroup;
		return succeed(error);
	}

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
			const MetalDescriptorSetLayout & layout, const std::uint32_t binding, std::uint32_t & outMember, std::uint32_t & outCount) noexcept
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

		void metal_write_argument_member(const MetalDescriptorSet & set, const std::uint32_t member, const std::uint64_t value) noexcept
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

		void metal_encode_argument(MetalDevice * device, MetalDescriptorSet & set, const std::uint32_t binding, const std::uint32_t element) noexcept
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

					const MetalDescriptor & descriptor = found->second;
					const std::uint32_t at			   = member + (element * stride);
					if (descriptor.buffer != nullptr)
					{
						metal_write_argument_member(set, at, descriptor.buffer->gpuAddress() + descriptor.offset);
					}
					else if (descriptor.texture != nullptr)
					{
						metal_write_argument_member(set, at, descriptor.texture->gpuResourceID()._impl);
					}
					else if (descriptor.sampler != nullptr)
					{
						metal_write_argument_member(set, at, descriptor.sampler->gpuResourceID()._impl);
					}

					if (entry.type == DescriptorType::eCombinedImageSampler && descriptor.sampler != nullptr)
					{
						metal_write_argument_member(set, at + 1, descriptor.sampler->gpuResourceID()._impl);
					}
					return;
				}

				member += members_for(entry);
			}
		}
	}

	bool metal_bind_descriptor_set(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, const std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.bindDescriptorSet");

		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;

		const auto * tracked = device->descriptorSets.resolve(set, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet names a set this device never created");
		}

		const bool graphics = object->list != nullptr && object->list->renderEncoder.get() != nullptr;
		if (!graphics && !ensure_compute_encoder(object, error))
		{
			return false;
		}
		MTL::RenderCommandEncoder * render	 = graphics ? object->list->renderEncoder.get() : nullptr;
		MTL::ComputeCommandEncoder * compute = graphics ? nullptr : object->list->computeEncoder.get();
		if (render == nullptr && compute == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "bindDescriptorSet outside a render or compute scope");
		}

		if (tracked->argumentBuffer.get() != nullptr)
		{
			if (render != nullptr)
			{
				render->setVertexBuffer(tracked->argumentBuffer.get(), 0, metal_argument_buffer_index_for_set(setIndex));
				render->setFragmentBuffer(tracked->argumentBuffer.get(), 0, metal_argument_buffer_index_for_set(setIndex));
			}
			else
			{
				compute->setBuffer(tracked->argumentBuffer.get(), 0, metal_argument_buffer_index_for_set(setIndex));
			}

			for (const auto & [key, descriptor] : tracked->bindings)
			{
				const MTL::Resource * resource = descriptor.buffer != nullptr ? static_cast<const MTL::Resource *>(descriptor.buffer)
																			  : static_cast<const MTL::Resource *>(descriptor.texture);
				if (resource == nullptr)
				{
					continue;
				}

				const bool writes = descriptor.type == DescriptorType::eTextureUAV || descriptor.type == DescriptorType::eBufferUAV ||
									descriptor.type == DescriptorType::eStorageBuffer || descriptor.type == DescriptorType::eDynamicStorageBuffer ||
									descriptor.type == DescriptorType::eTexelBufferUAV;

				const MTL::ResourceUsage usage = writes ? MTL::ResourceUsageRead | MTL::ResourceUsageWrite : MTL::ResourceUsageRead;
				if (render != nullptr)
				{
					render->useResource(resource, usage, MTL::RenderStageVertex | MTL::RenderStageFragment);
				}
				else
				{
					compute->useResource(resource, usage);
				}
			}

			return succeed(error);
		}

		for (const auto & [key, descriptor] : tracked->bindings)
		{
			const std::uint32_t binding = binding_of(key);

			std::uint64_t bufferOffset = descriptor.offset;
			for (const DynamicDescriptorOffset & dynamic : dynamicOffsets)
			{
				if (dynamic.binding == binding)
				{
					bufferOffset += dynamic.offset;
				}
			}

			if (descriptor.buffer != nullptr)
			{
				if (graphics)
				{
					render->setVertexBuffer(descriptor.buffer, bufferOffset, binding);
					render->setFragmentBuffer(descriptor.buffer, bufferOffset, binding);
				}
				else
				{
					compute->setBuffer(descriptor.buffer, bufferOffset, binding);
				}
			}
			if (descriptor.texture != nullptr)
			{
				if (graphics)
				{
					render->setVertexTexture(descriptor.texture, binding);
					render->setFragmentTexture(descriptor.texture, binding);
				}
				else
				{
					compute->setTexture(descriptor.texture, binding);
				}
			}
			if (descriptor.sampler != nullptr)
			{
				if (graphics)
				{
					render->setVertexSamplerState(descriptor.sampler, binding);
					render->setFragmentSamplerState(descriptor.sampler, binding);
				}
				else
				{
					compute->setSamplerState(descriptor.sampler, binding);
				}
			}
		}
		return succeed(error);
	}

	bool metal_update_descriptors_buffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsBuffer");

		auto * device = static_cast<MetalDevice *>(impl);
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
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = MetalDescriptor{
				.type	= write.type,
				.buffer = buffer,
				.offset = write.offset,
			};
			metal_encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	bool metal_update_descriptors_texture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsTexture");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorWriteTexture & write : writes)
		{
			auto * set = device->descriptorSets.resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			MetalDescriptor descriptor{ .type = write.type };
			descriptor.texture = resolve_texture_view(device, write.view);
			if (write.sampler.is_valid())
			{
				const auto * sampler = device->samplers.resolve(write.sampler, kHandleAlreadyChecked);
				descriptor.sampler	 = sampler != nullptr ? sampler->get() : nullptr;
			}
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = descriptor;
			metal_encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	bool metal_update_descriptors_sampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsSampler");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorWriteSampler & write : writes)
		{
			auto * set = device->descriptorSets.resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			const auto * sampler										  = device->samplers.resolve(write.sampler, kHandleAlreadyChecked);
			set->bindings[descriptor_key(write.binding, write.arrayIndex)] = MetalDescriptor{
				.type	 = DescriptorType::eSampler,
				.sampler = sampler != nullptr ? sampler->get() : nullptr,
			};
			metal_encode_argument(device, *set, write.binding, write.arrayIndex);
		}
		return succeed(error);
	}

	bool metal_dispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->computeEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "dispatch without a bound compute pipeline");
		}
		if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
		{
			return succeed(error);
		}
		object->list->computeEncoder->dispatchThreadgroups(MTL::Size::Make(groupCountX, groupCountY, groupCountZ), object->list->boundThreadGroup);
		return succeed(error);
	}

	bool metal_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		if (object->list == nullptr || object->list->computeEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "dispatchIndirect without a bound compute pipeline");
		}

		MTL::Buffer * indirect = resolve_buffer(device, args);
		if (indirect == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect names a buffer this device never created");
		}

		object->list->computeEncoder->dispatchThreadgroups(indirect, offset, object->list->boundThreadGroup);
		return succeed(error);
	}

	void * metal_create_descriptor_arena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createDescriptorArena");

		void * arena = alloc_object(static_cast<MetalDevice *>(impl), publishing_object<Published<DescriptorArenaApi, &descriptor_arena_block>>());
		if (arena == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor arena allocation failed");
		}

		return return_value(arena, error);
	}

	void * metal_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createCommandPool");

		auto * device = static_cast<MetalDevice *>(impl);

		void * pool = alloc_object(device, publishing_object<Published<CommandPoolApi, &command_pool_block>>(), desc.queueType);
		if (pool == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		auto record = host_new<MetalCmdPool>();
		if (record == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		MetalCmdPool * raw = record.get();
		if (!detail::try_push_back(device->cmdPools, std::move(record)))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool tracking failed");
		}

		static_cast<MetalObject *>(pool)->pool = raw;
		return return_value(pool, error);
	}

	void * metal_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device = static_cast<MetalDevice *>(impl);
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

	DescriptorSetHandle metal_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.descriptorArena.allocate");

		auto * arena		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = arena->owner;
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

				device->note_allocation(MetalDevice::Residency::eDescriptorSets, argumentBuffer.get());
			}
		}

		const DescriptorSetHandle handle = device->descriptorSets.store(MetalDescriptorSet{
			.bindings		= {},
			.arena			= arena,
			.epoch			= arena->arenaEpoch.load(std::memory_order_acquire),
			.layout			= desc.layout,
			.argumentBuffer = std::move(argumentBuffer),
		});
		if (!handle.is_valid())
		{
			return fail_value<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set tracking failed");
		}

		return return_value(handle, error);
	}

	bool metal_arena_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.descriptorArena.reset");

		auto * arena		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = arena->owner;

		const std::uint64_t bumped = arena->arenaEpoch.fetch_add(1, std::memory_order_release) + 1;

		device->descriptorSets.retire_if(
			[arena, bumped](const MetalDescriptorSet & set)
			{
				return set.arena == arena && set.epoch < bumped;
			});

		return succeed(error);
	}

}

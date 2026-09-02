// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	bool EnsureComputeEncoder(MetalObject * object, Error * error) noexcept
	{
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}
		if (object->list->renderEncoder.get() != nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "a compute command cannot be recorded inside a rendering scope, so record it between passes");
		}
		if (object->list->computeEncoder.get() == nullptr)
		{
			const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			object->list->computeEncoder				  = NS::RetainPtr(object->list->commandBuffer->computeCommandEncoder());
			++object->list->encoderEpoch;
			ConsumeAliasWait(object->list, object->list->computeEncoder.get());
		}

		return Succeed(error);
	}

	bool MetalSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const auto * tracked = device->computePipelines.Resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setComputePipeline names a pipeline this device never created");
		}

		if (!EnsureComputeEncoder(object, error))
		{
			return false;
		}

		object->list->computeEncoder->setComputePipelineState(tracked->state.get());
		object->list->boundThreadGroup = tracked->threadsPerThreadgroup;
		return Succeed(error);
	}

	namespace
	{
		[[nodiscard]] constexpr std::uint32_t MembersFor(const DescriptorBinding & entry) noexcept
		{
			return (entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u) * std::max(entry.count, 1u);
		}

		[[nodiscard]] constexpr std::uint64_t DescriptorKey(const std::uint32_t binding, const std::uint32_t arrayIndex) noexcept
		{
			return (static_cast<std::uint64_t>(binding) << 32u) | arrayIndex;
		}

		[[nodiscard]] constexpr std::uint32_t BindingOf(const std::uint64_t key) noexcept
		{
			return static_cast<std::uint32_t>(key >> 32u);
		}

		[[nodiscard]] bool MetalArgumentMemberIndex(
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

				member += MembersFor(entry);
			}

			outCount = member;
			return found;
		}

		[[nodiscard]] constexpr std::uint64_t MetalArgumentBufferBytes(const std::uint32_t memberCount) noexcept
		{
			return static_cast<std::uint64_t>(memberCount) * sizeof(std::uint64_t);
		}

		void MetalWriteArgumentMember(const MetalDescriptorSet & set, const std::uint32_t member, const std::uint64_t value) noexcept
		{
			if (set.argumentBuffer.get() == nullptr)
			{
				return;
			}

			auto * words = static_cast<std::uint64_t *>(set.argumentBuffer->contents());
			if (words != nullptr && MetalArgumentBufferBytes(member + 1) <= set.argumentBuffer->length())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): an argument buffer is a flat run of words by construction.
				words[member] = value;
			}
		}

		void MetalEncodeArgumentBuffer(MetalDevice * device, MetalDescriptorSet & set) noexcept
		{
			if (set.argumentBuffer.get() == nullptr)
			{
				return;
			}

			const auto * layout = device->descriptorSetLayouts.Resolve(set.layout, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return;
			}

			std::uint32_t member = 0;
			for (const DescriptorBinding & entry : layout->bindings)
			{
				const std::uint32_t stride = entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u;

				for (std::uint32_t element = 0; element < std::max(entry.count, 1u); ++element)
				{
					const auto found = set.bindings.find(DescriptorKey(entry.binding, element));
					if (found == set.bindings.end())
					{
						continue;
					}

					const MetalDescriptor & descriptor = found->second;
					const std::uint32_t at			   = member + (element * stride);
					if (descriptor.buffer != nullptr)
					{
						MetalWriteArgumentMember(set, at, descriptor.buffer->gpuAddress() + descriptor.offset);
					}
					else if (descriptor.texture != nullptr)
					{
						MetalWriteArgumentMember(set, at, descriptor.texture->gpuResourceID()._impl);
					}
					else if (descriptor.sampler != nullptr)
					{
						MetalWriteArgumentMember(set, at, descriptor.sampler->gpuResourceID()._impl);
					}

					if (entry.type == DescriptorType::eCombinedImageSampler && descriptor.sampler != nullptr)
					{
						MetalWriteArgumentMember(set, at + 1, descriptor.sampler->gpuResourceID()._impl);
					}
				}

				member += MembersFor(entry);
			}
		}
	}

	bool MetalBindDescriptorSet(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, const std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.bindDescriptorSet");

		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;

		const auto * tracked = device->descriptorSets.Resolve(set, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet names a set this device never created");
		}

		const bool graphics = object->list != nullptr && object->list->renderEncoder.get() != nullptr;
		if (!graphics && !EnsureComputeEncoder(object, error))
		{
			return false;
		}
		MTL::RenderCommandEncoder * render	 = graphics ? object->list->renderEncoder.get() : nullptr;
		MTL::ComputeCommandEncoder * compute = graphics ? nullptr : object->list->computeEncoder.get();
		if (render == nullptr && compute == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "bindDescriptorSet outside a render or compute scope");
		}

		if (tracked->argumentBuffer.get() != nullptr)
		{
			if (render != nullptr)
			{
				render->setVertexBuffer(tracked->argumentBuffer.get(), 0, MetalArgumentBufferIndexForSet(setIndex));
				render->setFragmentBuffer(tracked->argumentBuffer.get(), 0, MetalArgumentBufferIndexForSet(setIndex));
			}
			else
			{
				compute->setBuffer(tracked->argumentBuffer.get(), 0, MetalArgumentBufferIndexForSet(setIndex));
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

			return Succeed(error);
		}

		for (const auto & [key, descriptor] : tracked->bindings)
		{
			const std::uint32_t binding = BindingOf(key);

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
		return Succeed(error);
	}

	bool MetalUpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsBuffer");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorWriteBuffer & write : writes)
		{
			auto * set = device->descriptorSets.Resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			MTL::Buffer * buffer = ResolveBuffer(device, write.buffer);
			if (buffer == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a buffer this device never created");
			}
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = MetalDescriptor{
				.type	= write.type,
				.buffer = buffer,
				.offset = write.offset,
			};
			MetalEncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	bool MetalUpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsTexture");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorWriteTexture & write : writes)
		{
			auto * set = device->descriptorSets.Resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			MetalDescriptor descriptor{ .type = write.type };
			descriptor.texture = ResolveTextureView(device, write.view);
			if (write.sampler.IsValid())
			{
				const auto * sampler = device->samplers.Resolve(write.sampler, kHandleAlreadyChecked);
				descriptor.sampler	 = sampler != nullptr ? sampler->get() : nullptr;
			}
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = descriptor;
			MetalEncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	bool MetalUpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.updateDescriptorsSampler");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorWriteSampler & write : writes)
		{
			auto * set = device->descriptorSets.Resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			const auto * sampler										  = device->samplers.Resolve(write.sampler, kHandleAlreadyChecked);
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = MetalDescriptor{
				.type	 = DescriptorType::eSampler,
				.sampler = sampler != nullptr ? sampler->get() : nullptr,
			};
			MetalEncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	bool MetalDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatch without a bound compute pipeline");
		}
		object->list->computeEncoder->dispatchThreadgroups(MTL::Size::Make(groupCountX, groupCountY, groupCountZ), object->list->boundThreadGroup);
		return Succeed(error);
	}

	bool MetalDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		if (object->list == nullptr || object->list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatchIndirect without a bound compute pipeline");
		}

		MTL::Buffer * indirect = ResolveBuffer(device, args);
		if (indirect == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect names a buffer this device never created");
		}

		object->list->computeEncoder->dispatchThreadgroups(indirect, offset, object->list->boundThreadGroup);
		return Succeed(error);
	}

	void * MetalCreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createDescriptorArena");

		void * arena = AllocObject(static_cast<MetalDevice *>(impl), PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>());
		if (arena == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor arena allocation failed");
		}

		return ReturnValue(arena, error);
	}

	void * MetalCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createCommandPool");

		auto * device = static_cast<MetalDevice *>(impl);

		void * pool = AllocObject(device, PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>(), desc.queueType);
		if (pool == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		auto record = HostNew<MetalCmdPool>();
		if (record == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		MetalCmdPool * raw = record.get();
		if (!detail::TryPushBack(device->cmdPools, std::move(record)))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool tracking failed");
		}

		static_cast<MetalObject *>(pool)->pool = raw;
		return ReturnValue(pool, error);
	}

	void * MetalGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device = static_cast<MetalDevice *>(impl);
		if (index >= QueueCountForType(device->caps, type))
		{
			return FailValue<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
		}

		void * queue = AllocObject(device, PublishingObject<Published<QueueApi, &QueueBlock>>(), type);
		if (queue == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal queue allocation failed");
		}

		return ReturnValue(queue, error);
	}

	DescriptorSetHandle MetalArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.descriptorArena.allocate");

		auto * arena		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = arena->owner;
		if (!Resolves(device, desc.layout))
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocated from an invalid or stale layout handle");
		}

		NS::SharedPtr<MTL::Buffer> argumentBuffer;
		if (device->caps.bindingTier >= BindingTier::eUnbounded)
		{
			const auto * setLayout	   = device->descriptorSetLayouts.Resolve(desc.layout, kHandleAlreadyChecked);
			std::uint32_t memberCount  = 0;
			std::uint32_t ignoredIndex = 0;
			if (setLayout != nullptr)
			{
				static_cast<void>(MetalArgumentMemberIndex(*setLayout, ~0u, ignoredIndex, memberCount));
			}

			if (memberCount > 0)
			{
				MTL::Buffer * raw = device->device->newBuffer(MetalArgumentBufferBytes(memberCount), MTL::ResourceStorageModeShared);
				if (raw == nullptr)
				{
					return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal descriptor set argument buffer allocation failed");
				}

				SetMetalLabel(raw, desc.debugName);
				argumentBuffer = NS::TransferPtr(raw);
				std::memset(argumentBuffer->contents(), 0, argumentBuffer->length());

				device->NoteAllocation(MetalDevice::Residency::eDescriptorSets, argumentBuffer.get());
			}
		}

		const DescriptorSetHandle handle = device->descriptorSets.Store(MetalDescriptorSet{
			.bindings		= {},
			.arena			= arena,
			.epoch			= arena->arenaEpoch.load(std::memory_order_acquire),
			.layout			= desc.layout,
			.argumentBuffer = std::move(argumentBuffer),
		});
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool MetalArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.descriptorArena.reset");

		auto * arena		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = arena->owner;

		const std::uint64_t bumped = arena->arenaEpoch.fetch_add(1, std::memory_order_release) + 1;

		device->descriptorSets.RetireIf(
			[arena, bumped](const MetalDescriptorSet & set)
			{
				return set.arena == arena && set.epoch < bumped;
			});

		return Succeed(error);
	}

}

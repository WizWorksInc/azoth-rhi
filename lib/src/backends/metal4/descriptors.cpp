// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
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
			const Metal4DescriptorSetLayout & layout, const std::uint32_t binding, std::uint32_t & outMember, std::uint32_t & outCount) noexcept
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

		void WriteArgumentMember(const Metal4DescriptorSet & set, const std::uint32_t member, const std::uint64_t value) noexcept
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

		void EncodeArgumentBuffer(Metal4Device * device, Metal4DescriptorSet & set) noexcept
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

					const Metal4Descriptor & descriptor = found->second;
					const std::uint32_t at				= member + (element * stride);
					if (descriptor.buffer != nullptr)
					{
						WriteArgumentMember(set, at, descriptor.buffer->gpuAddress() + descriptor.offset);
					}
					else if (descriptor.texture != nullptr)
					{
						WriteArgumentMember(set, at, descriptor.texture->gpuResourceID()._impl);
					}
					else if (descriptor.sampler != nullptr)
					{
						WriteArgumentMember(set, at, descriptor.sampler->gpuResourceID()._impl);
					}

					if (entry.type == DescriptorType::eCombinedImageSampler && descriptor.sampler != nullptr)
					{
						WriteArgumentMember(set, at + 1, descriptor.sampler->gpuResourceID()._impl);
					}
				}

				member += MembersFor(entry);
			}
		}
	}

	bool Metal4CmdBindDescriptorSet(void * impl, PipelineLayoutHandle, const std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.bindDescriptorSet");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no argument table");
		}

		const auto * tracked = device->descriptorSets.Resolve(set, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet names a set this device never created");
		}

		if (tracked->argumentBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "this device has no argument buffers, which the Metal 4 backend binds sets through");
		}

		static_cast<void>(dynamicOffsets);

		list->argumentTable->setAddress(tracked->argumentBuffer->gpuAddress(), MetalArgumentBufferIndexForSet(setIndex));
		return Succeed(error);
	}

	bool Metal4CmdPushConstants(
		void * impl, PipelineLayoutHandle, Flags<ShaderStage>, const std::uint32_t offset, const std::uint32_t size, const void * data, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.pushConstants");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no argument table");
		}
		if (data == nullptr || size == 0)
		{
			return Succeed(error);
		}

		if (offset != 0)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "the Metal 4 backend writes a push constant range whole, so a non-zero offset is refused");
		}

		const MTL::GPUAddress address = WritePushConstants(device, list, data, size);
		if (address == 0)
		{
			return Fail(error, ErrorCode::eOutOfDeviceMemory, "push constant storage could not be allocated");
		}

		list->argumentTable->setAddress(address, kMetalPushConstantIndex);
		return Succeed(error);
	}

	bool Metal4UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsBuffer");

		auto * device = static_cast<Metal4Device *>(impl);
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
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = Metal4Descriptor{
				.type	= write.type,
				.buffer = buffer,
				.offset = write.offset,
			};
			EncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	bool Metal4UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsTexture");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorWriteTexture & write : writes)
		{
			auto * set = device->descriptorSets.Resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			Metal4Descriptor descriptor{ .type = write.type };
			descriptor.texture = ResolveTextureView(device, write.view);
			if (write.sampler.IsValid())
			{
				const auto * sampler = device->samplers.Resolve(write.sampler, kHandleAlreadyChecked);
				descriptor.sampler	 = sampler != nullptr ? sampler->get() : nullptr;
			}
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = descriptor;
			EncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	bool Metal4UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.updateDescriptorsSampler");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorWriteSampler & write : writes)
		{
			auto * set = device->descriptorSets.Resolve(write.set, kHandleAlreadyChecked);
			if (set == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "descriptor write names a set this device never created");
			}
			const auto * sampler										  = device->samplers.Resolve(write.sampler, kHandleAlreadyChecked);
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = Metal4Descriptor{
				.type	 = DescriptorType::eSampler,
				.sampler = sampler != nullptr ? sampler->get() : nullptr,
			};
			EncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	void * Metal4CreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorArena");

		void * arena = AllocObject(static_cast<Metal4Device *>(impl), PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>());
		if (arena == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor arena allocation failed");
		}

		return ReturnValue(arena, error);
	}

	void * Metal4CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createCommandPool");

		auto * device = static_cast<Metal4Device *>(impl);

		void * pool = AllocObject(device, PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>(), desc.queueType);
		if (pool == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		auto record = HostNew<CmdPool>();
		if (record == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool allocation failed");
		}

		CmdPool * raw = record.get();
		if (!detail::TryPushBack(device->cmdPools, std::move(record)))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal command pool tracking failed");
		}

		static_cast<Metal4Object *>(pool)->pool = raw;
		return ReturnValue(pool, error);
	}

	void * Metal4GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);
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

	DescriptorSetHandle Metal4ArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.allocate");

		auto * arena		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;
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

				device->NoteAllocation(Metal4Device::Residency::eDescriptorSets, argumentBuffer.get());
			}
		}

		const DescriptorSetHandle handle = device->descriptorSets.Store(Metal4DescriptorSet{
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

	bool Metal4ArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.reset");

		auto * arena		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;

		const std::uint64_t bumped = arena->arenaEpoch.fetch_add(1, std::memory_order_release) + 1;

		device->descriptorSets.RetireIf(
			[arena, bumped](const Metal4DescriptorSet & set)
			{
				return set.arena == arena && set.epoch < bumped;
			});

		return Succeed(error);
	}

}

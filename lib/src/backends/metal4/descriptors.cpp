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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	namespace
	{
		/*
		 * How many members of the argument buffer one layout entry takes.
		 *
		 * An array binding is count of them and not one, because slangc lowers an array inside a ParameterBlock to that many consecutive words. A combined
		 * binding doubles it, Metal having no combined type so the texture and the sampler are a word each.
		 */
		[[nodiscard]] constexpr std::uint32_t MembersFor(const DescriptorBinding & entry) noexcept
		{
			return (entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u) * std::max(entry.count, 1u);
		}

		/*
		 * The key one descriptor is held under, which is its binding and its index within it.
		 *
		 * An array binding holds count descriptors and a write names which one, so the binding number alone cannot be the key: every element would overwrite
		 * the last and a bindless set would resolve to one texture.
		 */
		[[nodiscard]] constexpr std::uint64_t DescriptorKey(const std::uint32_t binding, const std::uint32_t arrayIndex) noexcept
		{
			return (static_cast<std::uint64_t>(binding) << 32u) | arrayIndex;
		}

		// The binding number a packed descriptor key names.
		[[nodiscard]] constexpr std::uint32_t BindingOf(const std::uint64_t key) noexcept
		{
			return static_cast<std::uint32_t>(key >> 32u);
		}

		/*
		 * Where one binding's eight bytes sit inside the set's argument buffer.
		 *
		 * The position in the layout's binding list and not the binding number, because that is what slangc lays a struct out by, and a combined binding takes two
		 * members since Metal has no combined type. Mirrors NativeBindingFor's tier 2 answer, which is what the agreement test holds it to.
		 */
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

		// Every member is one 64 bit word, a resource id or a GPU address, so the struct is this many bytes whatever it holds.
		[[nodiscard]] constexpr std::uint64_t MetalArgumentBufferBytes(const std::uint32_t memberCount) noexcept
		{
			return static_cast<std::uint64_t>(memberCount) * sizeof(std::uint64_t);
		}

		// Writes one member. Out of range is a caller error the layout already refused, so this only guards the buffer being absent below the tier.
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

		/*
		 * Fills the argument buffer from what the set already holds.
		 *
		 * Done on every write, not incrementally because a combined binding fills two members and a write may replace a binding's kind, so recomputing the whole
		 * struct is both shorter and impossible to get half right.
		 */
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

				/*
				 * Every element of the binding, since an array holds one descriptor per index and each takes its own word. A partially bound array leaves the
				 * elements nothing was written to as the zeros the buffer was created with, which is what ePartiallyBound means.
				 */
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

					// The texture half took the member above, so the sampler half takes the one after it.
					if (entry.type == DescriptorType::eCombinedImageSampler && descriptor.sampler != nullptr)
					{
						WriteArgumentMember(set, at + 1, descriptor.sampler->gpuResourceID()._impl);
					}
				}

				member += MembersFor(entry);
			}
		}
	} // namespace

	/*
	 * Binding a set, where the two generations differ least in what they mean and most in how they say it.
	 *
	 * The argument buffer a set lowers to is built above and is byte for byte what Metal 3 builds, so the ABI a shader was compiled against does not move with
	 * the backend. What changes is the step that makes it reachable: a setBuffer on each encoder becomes a setAddress on the table, and the useResource calls
	 * fall away.
	 */
	bool CmdBindDescriptorSet(void * impl, PipelineLayoutHandle, const std::uint32_t setIndex, DescriptorSetHandle set,
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

		// A dynamic offset shifts where inside the argument buffer the set starts, which is what the other generation expresses by passing an offset to
		// setBuffer.
		std::uint64_t offset = 0;
		for (const DynamicDescriptorOffset & dynamic : dynamicOffsets)
		{
			offset += dynamic.offset;
		}

		list->argumentTable->setAddress(tracked->argumentBuffer->gpuAddress() + offset, MetalArgumentBufferIndexForSet(setIndex));
		return Succeed(error);
	}

	/*
	 * Push constants, which need real memory here.
	 *
	 * Metal 3 hands the bytes to the encoder and lets it copy them. An argument table binds addresses and has no inline form, so the bytes are written into a
	 * block the list owns and the address of that goes in the table. Buffer zero is the index the ABI reserves for them on both generations.
	 */
	bool CmdPushConstants(void * impl, PipelineLayoutHandle, Flags<ShaderStage>, const std::uint32_t offset, const std::uint32_t size, const void * data,
		Error * error) noexcept
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

		/*
		 * A non-zero offset would mean writing into part of a range something else already wrote, which needs the whole range kept live across pushes rather
		 * than a fresh block per push. Refused instead of silently writing at the wrong place, since the other generation honors it and a caller relying on
		 * that would get a wrong picture here instead of an error.
		 */
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

	bool UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
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

	bool UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
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

	bool UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
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
			const auto * sampler		 = device->samplers.Resolve(write.sampler, kHandleAlreadyChecked);
			set->bindings[DescriptorKey(write.binding, write.arrayIndex)] = Metal4Descriptor{
				.type	 = DescriptorType::eSampler,
				.sampler = sampler != nullptr ? sampler->get() : nullptr,
			};
			EncodeArgumentBuffer(device, *set);
		}
		return Succeed(error);
	}

	void * CreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorArena");

		void * arena = AllocObject(static_cast<Metal4Device *>(impl), PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>());
		if (arena == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor arena allocation failed");
		}

		return ReturnValue(arena, error);
	}

	void * CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
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

	void * GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
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

	DescriptorSetHandle ArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.allocate");

		auto * arena		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;
		if (!Resolves(device, desc.layout))
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocated from an invalid or stale layout handle");
		}

		/*
		 * The argument buffer this set is, sized from its layout.
		 *
		 * Made here and not at the first write because a set that is bound before anything is written to it still has to bind something, and a null buffer at a slot
		 * the shader declares is a fault and not an empty read.
		 */
		NS::SharedPtr<MTL::Buffer> argumentBuffer;
		if (device->caps.bindingTier >= BindingTier::eUnbounded)
		{
			const auto * setLayout	  = device->descriptorSetLayouts.Resolve(desc.layout, kHandleAlreadyChecked);
			std::uint32_t memberCount = 0;
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

	bool ArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.descriptorArena.reset");

		// A Metal descriptor set is a plain table of resolved bindings with no pool memory to hand back so the reset bumps the epoch that makes every set allocated
		// before it stale, then drops those sets.
		auto * arena		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = arena->owner;

		const std::uint64_t bumped = arena->arenaEpoch.fetch_add(1, std::memory_order_release) + 1;

		// Retiring without erasing so the slots go back to the free list and a set allocated after this reset can take one. Bumping alone would leave every set alive
		// for the life of the device, which for the arena-per-frame pattern this serves means growing by a frame's worth each frame.
		device->descriptorSets.RetireIf(
			[arena, bumped](const Metal4DescriptorSet & set)
			{
				return set.arena == arena && set.epoch < bumped;
			});

		return Succeed(error);
	}

} // namespace azo::rhi::metal4

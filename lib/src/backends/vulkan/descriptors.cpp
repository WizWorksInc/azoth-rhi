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
	[[nodiscard]] vk::DescriptorType MapDescriptorType(DescriptorType type) noexcept
	{
		switch (type)
		{
		case DescriptorType::eSampler:				return vk::DescriptorType::eSampler;
		case DescriptorType::eCombinedImageSampler: return vk::DescriptorType::eCombinedImageSampler;
		case DescriptorType::eTextureSRV:			return vk::DescriptorType::eSampledImage;
		case DescriptorType::eTextureUAV:			return vk::DescriptorType::eStorageImage;
		case DescriptorType::eBufferSRV:
		case DescriptorType::eBufferUAV:
		case DescriptorType::eStorageBuffer:		 return vk::DescriptorType::eStorageBuffer;
		case DescriptorType::eDynamicUniformBuffer:	 return vk::DescriptorType::eUniformBufferDynamic;
		case DescriptorType::eDynamicStorageBuffer:	 return vk::DescriptorType::eStorageBufferDynamic;
		case DescriptorType::eTexelBufferSRV:		 return vk::DescriptorType::eUniformTexelBuffer;
		case DescriptorType::eTexelBufferUAV:		 return vk::DescriptorType::eStorageTexelBuffer;
		case DescriptorType::eAccelerationStructure: return vk::DescriptorType::eAccelerationStructureKHR;
		case DescriptorType::eUniformBuffer:		 break;
		}
		return vk::DescriptorType::eUniformBuffer;
	}

	[[nodiscard]] vk::DescriptorSetLayout ResolveDescriptorSetLayout(const VulkanDevice * device, DescriptorSetLayoutHandle handle) noexcept
	{
		const DescriptorSetLayoutSlot * slot = device->descriptorSetLayoutSlots.Resolve(handle, kHandleAlreadyChecked);
		return slot != nullptr ? slot->layout : vk::DescriptorSetLayout{};
	}

	[[nodiscard]] vk::Sampler ResolveSampler(const VulkanDevice * device, SamplerHandle handle) noexcept
	{
		const SamplerSlot * slot = device->samplerSlots.Resolve(handle, kHandleAlreadyChecked);
		return slot != nullptr ? slot->sampler : vk::Sampler{};
	}

	[[nodiscard]] vk::DescriptorSet ResolveDescriptorSet(const VulkanDevice * device, DescriptorSetHandle handle) noexcept
	{
		const DescriptorSetSlot * const slot = device->descriptorSetSlots.Resolve(handle, true);
		return slot != nullptr ? slot->set : vk::DescriptorSet{};
	}

	DescriptorSetLayoutHandle VulkanCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createDescriptorSetLayout");
		auto * device = static_cast<VulkanDevice *>(impl);
		detail::HostVector<vk::DescriptorSetLayoutBinding> bindings;
		detail::HostVector<vk::DescriptorBindingFlags> bindingFlags;
		if (!detail::TryReserve(bindings, desc.bindings.size()) || !detail::TryReserve(bindingFlags, desc.bindings.size()))
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor set layout binding storage allocation failed");
		}

		detail::HostVector<vk::Sampler> immutableSamplers;
		std::size_t immutableTotal = 0;
		for (const DescriptorBinding & b : desc.bindings)
		{
			immutableTotal += b.immutableSamplers.size();
		}
		if (!detail::TryReserve(immutableSamplers, immutableTotal))
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan immutable sampler storage allocation failed");
		}

		bool anyBindingFlags = false;
		for (const DescriptorBinding & b : desc.bindings)
		{
			const vk::Sampler * immutable = nullptr;
			if (!b.immutableSamplers.empty())
			{
				if (b.immutableSamplers.size() != b.count)
				{
					return FailValue<DescriptorSetLayoutHandle>(
						error, ErrorCode::eInvalidArgument, "a binding's immutable sampler list must hold exactly count entries");
				}

				immutable = immutableSamplers.data() + immutableSamplers.size();
				for (const SamplerHandle sampler : b.immutableSamplers)
				{
					const SamplerSlot * slot = device->samplerSlots.Resolve(sampler, kHandleAlreadyChecked);
					if (slot == nullptr)
					{
						return FailValue<DescriptorSetLayoutHandle>(
							error, ErrorCode::eInvalidHandle, "a binding names an immutable sampler this device never created");
					}
					immutableSamplers.push_back(slot->sampler);
				}
			}

			bindings.emplace_back(b.binding, MapDescriptorType(b.type), b.count, MapShaderStages(b.stages), immutable);

			vk::DescriptorBindingFlags f{};
			if (b.flags.Contains(DescriptorBindingFlag::ePartiallyBound) || b.flags.Contains(DescriptorBindingFlag::eBindless))
			{
				f |= vk::DescriptorBindingFlagBits::ePartiallyBound;
			}
			if (b.flags.Contains(DescriptorBindingFlag::eVariableDescriptorCount))
			{
				f |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
			}
			if (b.flags.Contains(DescriptorBindingFlag::eUpdateAfterBind))
			{
				f |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;
			}
			bindingFlags.push_back(f);
			anyBindingFlags = anyBindingFlags || static_cast<bool>(f);
		}

		vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
		vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo;
		if (anyBindingFlags)
		{
			flagsInfo.setBindingFlags(bindingFlags);
			layoutInfo.pNext = &flagsInfo;
			for (const vk::DescriptorBindingFlags & bf : bindingFlags)
			{
				if (bf & vk::DescriptorBindingFlagBits::eUpdateAfterBind)
				{
					layoutInfo.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
					break;
				}
			}
		}

		const auto created = device->device.createDescriptorSetLayout(layoutInfo, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eNativeApiError, "Vulkan descriptor set layout creation failed");
		}

		DescriptorSetLayoutSlot slot{ .layout = created.value };
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());
		for (DescriptorBinding & kept : slot.bindings)
		{
			kept.immutableSamplers = {};
		}

		const DescriptorSetLayoutHandle handle = device->descriptorSetLayoutSlots.Store(std::move(slot));
		if (!handle.IsValid())
		{
			device->device.destroyDescriptorSetLayout(created.value, nullptr, device->dispatch);
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor set layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	void * VulkanCreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createDescriptorArena");
		auto * device = static_cast<VulkanDevice *>(impl);
		const std::uint32_t perType = desc.maxDescriptors > 0 ? desc.maxDescriptors : 1;
		const std::array<vk::DescriptorPoolSize, 10> poolSizes{ { { vk::DescriptorType::eUniformBuffer, perType },
			{ vk::DescriptorType::eStorageBuffer, perType },
			{ vk::DescriptorType::eSampledImage, perType },
			{ vk::DescriptorType::eStorageImage, perType },
			{ vk::DescriptorType::eSampler, perType },
			{ vk::DescriptorType::eCombinedImageSampler, perType },
			{ vk::DescriptorType::eUniformBufferDynamic, perType },
			{ vk::DescriptorType::eStorageBufferDynamic, perType },
			{ vk::DescriptorType::eUniformTexelBuffer, perType },
			{ vk::DescriptorType::eStorageTexelBuffer, perType } } };
		const std::uint32_t maxSets = desc.maxSets > 0 ? desc.maxSets : 1;

		vk::DescriptorPoolCreateFlags poolFlags{};
		if (device->caps.supportsUpdateAfterBind)
		{
			poolFlags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
		}
		const auto created = device->device.createDescriptorPool(vk::DescriptorPoolCreateInfo(poolFlags, maxSets, poolSizes), nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Vulkan descriptor arena creation failed");
		}

		auto arena = HostNew<VulkanDescriptorArena>();
		if (arena == nullptr)
		{
			device->device.destroyDescriptorPool(created.value, nullptr, device->dispatch);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor arena allocation failed");
		}

		arena->object = PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>();
		arena->owner  = device;
		arena->pool	  = created.value;

		VulkanDescriptorArena * raw = arena.get();
		if (!detail::TryPushBack(device->descriptorArenas, std::move(arena)))
		{
			device->device.destroyDescriptorPool(created.value, nullptr, device->dispatch);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor arena allocation failed");
		}

		return ReturnValue(static_cast<void *>(raw), error);
	}

	DescriptorSetHandle VulkanArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.descriptorArena.allocate");
		auto * arena						 = static_cast<VulkanDescriptorArena *>(impl);
		VulkanDevice * device				 = arena->owner;
		const vk::DescriptorSetLayout layout = ResolveDescriptorSetLayout(device, desc.layout);
		if (!layout)
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocation with an invalid layout handle");
		}

		const vk::DescriptorSetAllocateInfo info(arena->pool, layout);
		const auto allocated = device->device.allocateDescriptorSets<HostAllocatorAdapter<vk::DescriptorSet>>(info, device->dispatch);

		if (allocated.result != vk::Result::eSuccess || allocated.value.empty())
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor set allocation failed");
		}

		const DescriptorSetHandle handle =
			device->descriptorSetSlots.Store(DescriptorSetSlot{ .set = allocated.value.front(), .arena = arena, .layout = desc.layout });
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan descriptor set handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool VulkanArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.descriptorArena.reset");
		auto * arena = static_cast<VulkanDescriptorArena *>(impl);
		if (arena->owner->device.resetDescriptorPool(arena->pool, {}, arena->owner->dispatch) != vk::Result::eSuccess)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan descriptor pool reset failed");
		}

		static_cast<void>(arena->owner->descriptorSetSlots.RetireIf(
			[arena](const DescriptorSetSlot & slot) noexcept
			{
				return slot.arena == arena;
			}));

		return Succeed(error);
	}

	const DescriptorArenaApi & DescriptorArenaBlock() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &VulkanArenaAllocate,
			.reset	  = &VulkanArenaReset,
		};

		return block;
	}

	bool VulkanUpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		auto * device = static_cast<VulkanDevice *>(impl);
		detail::HostVector<vk::DescriptorBufferInfo> bufferInfos;
		bufferInfos.reserve(writes.size());
		detail::HostVector<vk::WriteDescriptorSet> vkWrites;
		vkWrites.reserve(writes.size());

		for (const DescriptorWriteBuffer & w : writes)
		{
			const vk::DescriptorSet set = ResolveDescriptorSet(device, w.set);
			BufferSlot * buffer			= ResolveBuffer(device, w.buffer);
			if (!set || buffer == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsBuffer with an invalid set or buffer handle");
			}
			bufferInfos.emplace_back(vk::Buffer(buffer->buffer), w.offset, w.range);
			vkWrites.emplace_back(set, w.binding, w.arrayIndex, 1, MapDescriptorType(w.type), nullptr, &bufferInfos.back());
		}

		device->device.updateDescriptorSets(vkWrites, {}, device->dispatch);
		return Succeed(error);
	}

	bool VulkanUpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		auto * device = static_cast<VulkanDevice *>(impl);
		detail::HostVector<vk::DescriptorImageInfo> imageInfos;
		imageInfos.reserve(writes.size());
		detail::HostVector<vk::WriteDescriptorSet> vkWrites;
		vkWrites.reserve(writes.size());

		for (const DescriptorWriteTexture & w : writes)
		{
			const vk::DescriptorSet set = ResolveDescriptorSet(device, w.set);
			const vk::ImageView view	= ResolveTextureView(device, w.view);
			if (!set || !view)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsTexture with an invalid set or view handle");
			}
			const vk::Sampler sampler = w.sampler.IsValid() ? ResolveSampler(device, w.sampler) : vk::Sampler{};
			imageInfos.emplace_back(sampler, view, LayoutForUse(w.expectedUse, device->unifiedImageLayouts));
			vkWrites.emplace_back(set, w.binding, w.arrayIndex, 1, MapDescriptorType(w.type), &imageInfos.back());
		}

		device->device.updateDescriptorSets(vkWrites, {}, device->dispatch);
		return Succeed(error);
	}

	bool VulkanUpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		auto * device = static_cast<VulkanDevice *>(impl);
		detail::HostVector<vk::DescriptorImageInfo> imageInfos;
		imageInfos.reserve(writes.size());
		detail::HostVector<vk::WriteDescriptorSet> vkWrites;
		vkWrites.reserve(writes.size());

		for (const DescriptorWriteSampler & w : writes)
		{
			const vk::DescriptorSet set = ResolveDescriptorSet(device, w.set);
			const vk::Sampler sampler	= ResolveSampler(device, w.sampler);
			if (!set || !sampler)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsSampler with an invalid set or sampler handle");
			}
			imageInfos.emplace_back(sampler, vk::ImageView{}, vk::ImageLayout::eUndefined);
			vkWrites.emplace_back(set, w.binding, w.arrayIndex, 1, vk::DescriptorType::eSampler, &imageInfos.back());
		}

		device->device.updateDescriptorSets(vkWrites, {}, device->dispatch);
		return Succeed(error);
	}

	bool VulkanCmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
	{
		auto * list						  = static_cast<VulkanCommandList *>(impl);
		VulkanDevice * device			  = list->owner;
		const vk::PipelineLayout vkLayout = ResolvePipelineLayout(device, layout);
		const vk::DescriptorSet vkSet	  = ResolveDescriptorSet(device, set);
		if (!vkLayout || !vkSet)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet with an invalid layout or set handle");
		}

		const DescriptorSetSlot * const slot = device->descriptorSetSlots.Resolve(set, kHandleAlreadyChecked);
		const DescriptorSetLayoutSlot * const setLayout =
			slot != nullptr ? device->descriptorSetLayoutSlots.Resolve(slot->layout, kHandleAlreadyChecked) : nullptr;
		if (setLayout == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet cannot resolve the layout the set was allocated from");
		}

		detail::HostVector<const DescriptorBinding *> dynamics;
		for (const DescriptorBinding & entry : setLayout->bindings)
		{
			if (entry.type == DescriptorType::eDynamicUniformBuffer || entry.type == DescriptorType::eDynamicStorageBuffer)
			{
				dynamics.push_back(&entry);
			}
		}

		std::ranges::sort(dynamics,
			[](const DescriptorBinding * lhs, const DescriptorBinding * rhs) noexcept
			{
				return lhs->binding < rhs->binding;
			});

		detail::HostVector<std::uint32_t> offsets;
		for (const DescriptorBinding * dynamic : dynamics)
		{
			for (std::uint32_t element = 0; element < dynamic->count; ++element)
			{
				std::uint32_t chosen = 0;
				for (const DynamicDescriptorOffset & offset : dynamicOffsets)
				{
					if (offset.binding == dynamic->binding && offset.arrayIndex == element)
					{
						if (offset.offset > std::numeric_limits<std::uint32_t>::max())
						{
							return Fail(error, ErrorCode::eInvalidArgument, "a dynamic descriptor offset does not fit the 32 bits Vulkan binds it in");
						}

						chosen = static_cast<std::uint32_t>(offset.offset);
						break;
					}
				}

				offsets.push_back(chosen);
			}
		}

		list->buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, vkLayout, setIndex, vkSet, offsets, device->dispatch);
		list->buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, vkLayout, setIndex, vkSet, offsets, device->dispatch);
		return Succeed(error);
	}

}

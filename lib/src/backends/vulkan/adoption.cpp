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
	namespace
	{
		[[nodiscard]] SlotLifetime LifetimeOf(const AdoptedLifetime lifetime) noexcept
		{
			return lifetime == AdoptedLifetime::eRhiOwns ? SlotLifetime::eOwned : SlotLifetime::eAdopted;
		}
	}

	BufferHandle VulkanAdoptBuffer(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptBuffer");
		if (api != VulkanApi::id)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Buffer adopted = static_cast<const NativeBuffer<VulkanApi> *>(nativeImport)->buffer;
		if (!adopted)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkBuffer");
		}

		auto * device = static_cast<VulkanDevice *>(impl);

		const BufferHandle handle = device->bufferSlots.Store(BufferSlot{
			.buffer	  = static_cast<VkBuffer>(adopted),
			.size	  = desc.desc.size,
			.lifetime = LifetimeOf(desc.lifetime),
			.desc	  = detail::Recorded(desc.desc),
		});
		if (!handle.IsValid())
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted buffer handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(static_cast<VkBuffer>(adopted)), desc.debugName);
		return ReturnValue(handle, error);
	}

	TextureHandle VulkanAdoptTexture(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTexture");
		if (api != VulkanApi::id)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Image adopted = static_cast<const NativeTexture<VulkanApi> *>(nativeImport)->image;
		if (!adopted)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkImage");
		}

		auto * device = static_cast<VulkanDevice *>(impl);
		if (MapFormat(desc.desc.format) == vk::Format::eUndefined)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "adopted texture: undefined or unsupported format");
		}

		const TextureHandle handle = device->textureSlots.Store(TextureSlot{
			.image		   = static_cast<VkImage>(adopted),
			.format		   = MapFormat(desc.desc.format),
			.samples	   = MapSampleCount(desc.desc.samples),
			.mipLevels	   = desc.desc.mipLevels,
			.arrayLayers   = desc.desc.arrayLayers,
			.width		   = desc.desc.width,
			.height		   = desc.desc.height,
			.depth		   = desc.desc.depth,
			.rhiFormat	   = desc.desc.format,
			.usage		   = desc.desc.usage,
			.mutableFormat = desc.desc.allowFormatViews,
			.lifetime	   = LifetimeOf(desc.lifetime),
			.desc		   = detail::Recorded(desc.desc),
		});
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted texture handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eImage, std::bit_cast<std::uint64_t>(static_cast<VkImage>(adopted)), desc.debugName);
		return ReturnValue(handle, error);
	}

	bool VulkanGetNativeBuffer(void * impl, const GraphicsApiId api, const BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			= static_cast<VulkanDevice *>(impl);
		const BufferSlot * slot = ResolveBuffer(device, buffer);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid buffer handle");
		}

		static_cast<NativeBuffer<VulkanApi> *>(outNativeImport)->buffer = vk::Buffer(slot->buffer);
		return Succeed(error);
	}

	bool VulkanGetNativeTexture(void * impl, const GraphicsApiId api, const TextureHandle texture, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			 = static_cast<VulkanDevice *>(impl);
		const TextureSlot * slot = device->textureSlots.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture handle");
		}

		static_cast<NativeTexture<VulkanApi> *>(outNativeImport)->image = vk::Image(slot->image);
		return Succeed(error);
	}

	TextureViewHandle VulkanAdoptTextureView(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTextureView");
		if (api != VulkanApi::id)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::ImageView adopted = static_cast<const NativeTextureView<VulkanApi> *>(nativeImport)->view;
		if (!adopted)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkImageView");
		}

		auto * device = static_cast<VulkanDevice *>(impl);
		if (device->textureSlots.Resolve(desc.texture, kHandleAlreadyChecked) == nullptr)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "an adopted texture view names a texture this device never handed out");
		}

		const TextureViewHandle handle = device->textureViewSlots.Store(TextureViewSlot{
			.view	  = adopted,
			.format	  = MapFormat(desc.format),
			.samples  = MapSampleCount(desc.samples),
			.lifetime = LifetimeOf(desc.lifetime),
		});
		if (!handle.IsValid())
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted texture view handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eImageView, std::bit_cast<std::uint64_t>(static_cast<VkImageView>(adopted)), desc.debugName);
		return ReturnValue(handle, error);
	}

	SamplerHandle VulkanAdoptSampler(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptSampler");
		if (api != VulkanApi::id)
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Sampler adopted = static_cast<const NativeSampler<VulkanApi> *>(nativeImport)->sampler;
		if (!adopted)
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSampler");
		}

		auto * device			   = static_cast<VulkanDevice *>(impl);
		const SamplerHandle handle = device->samplerSlots.Store(SamplerSlot{ .sampler = adopted, .lifetime = LifetimeOf(desc.lifetime) });
		if (!handle.IsValid())
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted sampler handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eSampler, std::bit_cast<std::uint64_t>(static_cast<VkSampler>(adopted)), desc.debugName);
		return ReturnValue(handle, error);
	}

	bool VulkanGetNativeTextureView(void * impl, const GraphicsApiId api, const TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				 = static_cast<VulkanDevice *>(impl);
		const TextureViewSlot * slot = ResolveTextureViewSlot(device, view);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture view handle");
		}

		static_cast<NativeTextureView<VulkanApi> *>(outNativeImport)->view = slot->view;
		return Succeed(error);
	}

	bool VulkanGetNativeSampler(void * impl, const GraphicsApiId api, const SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			  = static_cast<VulkanDevice *>(impl);
		const vk::Sampler adopted = ResolveSampler(device, sampler);
		if (!adopted)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid sampler handle");
		}

		static_cast<NativeSampler<VulkanApi> *>(outNativeImport)->sampler = adopted;
		return Succeed(error);
	}

	TimelineHandle VulkanAdoptTimeline(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTimeline");
		if (api != VulkanApi::id)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Semaphore adopted = static_cast<const NativeTimeline<VulkanApi> *>(nativeImport)->semaphore;
		if (!adopted)
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSemaphore");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const TimelineHandle handle = device->timelineSlots.Store(TimelineSlot{ .semaphore = adopted, .lifetime = LifetimeOf(desc.lifetime) });
		if (!handle.IsValid())
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted timeline handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(adopted)), desc.debugName);
		return ReturnValue(handle, error);
	}

	BinarySemaphoreHandle VulkanAdoptBinarySemaphore(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptBinarySemaphore");
		if (api != VulkanApi::id)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Semaphore adopted = static_cast<const NativeBinarySemaphore<VulkanApi> *>(nativeImport)->semaphore;
		if (!adopted)
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSemaphore");
		}

		auto * device				 = static_cast<VulkanDevice *>(impl);
		BinarySemaphoreHandle handle = device->binarySemaphoreSlots.Store(BinarySemaphoreSlot{ .semaphore = adopted, .lifetime = LifetimeOf(desc.lifetime) });
		if (!handle.IsValid())
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted binary semaphore handle tracking failed");
		}

		NameVulkanObject(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(adopted)), desc.debugName);
		handle.index |= kDeviceBinarySemaphoreBit;
		return ReturnValue(handle, error);
	}

	bool VulkanGetNativeTimeline(void * impl, const GraphicsApiId api, const TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const vk::Semaphore adopted = ResolveTimeline(device, timeline);
		if (!adopted)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid timeline handle");
		}

		static_cast<NativeTimeline<VulkanApi> *>(outNativeImport)->semaphore = adopted;
		return Succeed(error);
	}

	bool VulkanGetNativeBinarySemaphore(
		void * impl, const GraphicsApiId api, const BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const vk::Semaphore adopted = ResolveBinarySemaphore(device, semaphore);
		if (!adopted)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "native read of an invalid binary semaphore handle");
		}

		static_cast<NativeBinarySemaphore<VulkanApi> *>(outNativeImport)->semaphore = adopted;
		return Succeed(error);
	}

	const AdoptionApi & AdoptionBlock() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &VulkanAdoptBuffer,
			.adoptTexture			  = &VulkanAdoptTexture,
			.getNativeBuffer		  = &VulkanGetNativeBuffer,
			.getNativeTexture		  = &VulkanGetNativeTexture,
			.adoptTextureView		  = &VulkanAdoptTextureView,
			.adoptSampler			  = &VulkanAdoptSampler,
			.getNativeTextureView	  = &VulkanGetNativeTextureView,
			.getNativeSampler		  = &VulkanGetNativeSampler,
			.adoptTimeline			  = &VulkanAdoptTimeline,
			.adoptBinarySemaphore	  = &VulkanAdoptBinarySemaphore,
			.getNativeTimeline		  = &VulkanGetNativeTimeline,
			.getNativeBinarySemaphore = &VulkanGetNativeBinarySemaphore,
		};

		return block;
	}

}

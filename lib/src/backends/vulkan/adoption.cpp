// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"
#include "backends/vulkan/internal.hpp"
#include "vulkan/vulkan.hpp"
#include <bit>
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace azo::rhi::vulkan
{
	namespace
	{
		[[nodiscard]] SlotLifetime lifetime_of(const AdoptedLifetime lifetime) noexcept
		{
			return lifetime == AdoptedLifetime::eRhiOwns ? SlotLifetime::eOwned : SlotLifetime::eAdopted;
		}
	}

	BufferHandle vulkan_adopt_buffer(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptBuffer");
		if (api != VulkanApi::kId)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Buffer adopted = static_cast<const NativeBuffer<VulkanApi> *>(nativeImport)->buffer;
		if (!adopted)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkBuffer");
		}

		auto * device = static_cast<VulkanDevice *>(impl);

		const BufferHandle handle = device->bufferSlots.store(BufferSlot{
			.buffer	  = static_cast<VkBuffer>(adopted),
			.size	  = desc.desc.size,
			.lifetime = lifetime_of(desc.lifetime),
			.desc	  = detail::recorded(desc.desc),
		});
		if (!handle.is_valid())
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted buffer handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(static_cast<VkBuffer>(adopted)), desc.debugName);
		return return_value(handle, error);
	}

	TextureHandle vulkan_adopt_texture(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTexture");
		if (api != VulkanApi::kId)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Image adopted = static_cast<const NativeTexture<VulkanApi> *>(nativeImport)->image;
		if (!adopted)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkImage");
		}

		auto * device = static_cast<VulkanDevice *>(impl);
		if (map_format(desc.desc.format) == vk::Format::eUndefined)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "adopted texture: undefined or unsupported format");
		}

		const TextureHandle handle = device->textureSlots.store(TextureSlot{
			.image		   = static_cast<VkImage>(adopted),
			.format		   = map_format(desc.desc.format),
			.samples	   = map_sample_count(desc.desc.samples),
			.mipLevels	   = desc.desc.mipLevels,
			.arrayLayers   = desc.desc.arrayLayers,
			.width		   = desc.desc.width,
			.height		   = desc.desc.height,
			.depth		   = desc.desc.depth,
			.rhiFormat	   = desc.desc.format,
			.usage		   = desc.desc.usage,
			.mutableFormat = desc.desc.allowFormatViews,
			.lifetime	   = lifetime_of(desc.lifetime),
			.desc		   = detail::recorded(desc.desc),
		});
		if (!handle.is_valid())
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted texture handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eImage, std::bit_cast<std::uint64_t>(static_cast<VkImage>(adopted)), desc.debugName);
		return return_value(handle, error);
	}

	bool vulkan_get_native_buffer(void * impl, const GraphicsApiId api, const BufferHandle buffer, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			= static_cast<VulkanDevice *>(impl);
		const BufferSlot * slot = resolve_buffer(device, buffer);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid buffer handle");
		}

		static_cast<NativeBuffer<VulkanApi> *>(outNativeImport)->buffer = vk::Buffer(slot->buffer);
		return succeed(error);
	}

	bool vulkan_get_native_texture(void * impl, const GraphicsApiId api, const TextureHandle texture, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			 = static_cast<VulkanDevice *>(impl);
		const TextureSlot * slot = device->textureSlots.resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture handle");
		}

		static_cast<NativeTexture<VulkanApi> *>(outNativeImport)->image = vk::Image(slot->image);
		return succeed(error);
	}

	TextureViewHandle vulkan_adopt_texture_view(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTextureView");
		if (api != VulkanApi::kId)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::ImageView adopted = static_cast<const NativeTextureView<VulkanApi> *>(nativeImport)->view;
		if (!adopted)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkImageView");
		}

		auto * device = static_cast<VulkanDevice *>(impl);
		if (device->textureSlots.resolve(desc.texture, kHandleAlreadyChecked) == nullptr)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "an adopted texture view names a texture this device never handed out");
		}

		const TextureViewHandle handle = device->textureViewSlots.store(TextureViewSlot{
			.view	  = adopted,
			.format	  = map_format(desc.format),
			.samples  = map_sample_count(desc.samples),
			.lifetime = lifetime_of(desc.lifetime),
		});
		if (!handle.is_valid())
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted texture view handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eImageView, std::bit_cast<std::uint64_t>(static_cast<VkImageView>(adopted)), desc.debugName);
		return return_value(handle, error);
	}

	SamplerHandle vulkan_adopt_sampler(void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptSampler");
		if (api != VulkanApi::kId)
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Sampler adopted = static_cast<const NativeSampler<VulkanApi> *>(nativeImport)->sampler;
		if (!adopted)
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSampler");
		}

		auto * device			   = static_cast<VulkanDevice *>(impl);
		const SamplerHandle handle = device->samplerSlots.store(SamplerSlot{ .sampler = adopted, .lifetime = lifetime_of(desc.lifetime) });
		if (!handle.is_valid())
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted sampler handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eSampler, std::bit_cast<std::uint64_t>(static_cast<VkSampler>(adopted)), desc.debugName);
		return return_value(handle, error);
	}

	bool vulkan_get_native_texture_view(void * impl, const GraphicsApiId api, const TextureViewHandle view, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				 = static_cast<VulkanDevice *>(impl);
		const TextureViewSlot * slot = resolve_texture_view_slot(device, view);
		if (slot == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid texture view handle");
		}

		static_cast<NativeTextureView<VulkanApi> *>(outNativeImport)->view = slot->view;
		return succeed(error);
	}

	bool vulkan_get_native_sampler(void * impl, const GraphicsApiId api, const SamplerHandle sampler, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device			  = static_cast<VulkanDevice *>(impl);
		const vk::Sampler adopted = resolve_sampler(device, sampler);
		if (!adopted)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid sampler handle");
		}

		static_cast<NativeSampler<VulkanApi> *>(outNativeImport)->sampler = adopted;
		return succeed(error);
	}

	TimelineHandle vulkan_adopt_timeline(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptTimeline");
		if (api != VulkanApi::kId)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Semaphore adopted = static_cast<const NativeTimeline<VulkanApi> *>(nativeImport)->semaphore;
		if (!adopted)
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSemaphore");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const TimelineHandle handle = device->timelineSlots.store(TimelineSlot{ .semaphore = adopted, .lifetime = lifetime_of(desc.lifetime) });
		if (!handle.is_valid())
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted timeline handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(adopted)), desc.debugName);
		return return_value(handle, error);
	}

	BinarySemaphoreHandle vulkan_adopt_binary_semaphore(
		void * impl, const GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.adoptBinarySemaphore");
		if (api != VulkanApi::kId)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedApi, "adoption payload API does not match the device backend");
		}

		const vk::Semaphore adopted = static_cast<const NativeBinarySemaphore<VulkanApi> *>(nativeImport)->semaphore;
		if (!adopted)
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eInvalidArgument, "adoption payload has a null VkSemaphore");
		}

		auto * device				 = static_cast<VulkanDevice *>(impl);
		BinarySemaphoreHandle handle = device->binarySemaphoreSlots.store(BinarySemaphoreSlot{ .semaphore = adopted, .lifetime = lifetime_of(desc.lifetime) });
		if (!handle.is_valid())
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan adopted binary semaphore handle tracking failed");
		}

		name_vulkan_object(device, vk::ObjectType::eSemaphore, std::bit_cast<std::uint64_t>(static_cast<VkSemaphore>(adopted)), desc.debugName);
		handle.index |= kDeviceBinarySemaphoreBit;
		return return_value(handle, error);
	}

	bool vulkan_get_native_timeline(void * impl, const GraphicsApiId api, const TimelineHandle timeline, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const vk::Semaphore adopted = resolve_timeline(device, timeline);
		if (!adopted)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid timeline handle");
		}

		static_cast<NativeTimeline<VulkanApi> *>(outNativeImport)->semaphore = adopted;
		return succeed(error);
	}

	bool vulkan_get_native_binary_semaphore(
		void * impl, const GraphicsApiId api, const BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept
	{
		if (api != VulkanApi::kId)
		{
			return fail(error, ErrorCode::eUnsupportedApi, "native payload API does not match the device backend");
		}

		auto * device				= static_cast<VulkanDevice *>(impl);
		const vk::Semaphore adopted = resolve_binary_semaphore(device, semaphore);
		if (!adopted)
		{
			return fail(error, ErrorCode::eInvalidHandle, "native read of an invalid binary semaphore handle");
		}

		static_cast<NativeBinarySemaphore<VulkanApi> *>(outNativeImport)->semaphore = adopted;
		return succeed(error);
	}

	const AdoptionApi & adoption_block() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &vulkan_adopt_buffer,
			.adoptTexture			  = &vulkan_adopt_texture,
			.getNativeBuffer		  = &vulkan_get_native_buffer,
			.getNativeTexture		  = &vulkan_get_native_texture,
			.adoptTextureView		  = &vulkan_adopt_texture_view,
			.adoptSampler			  = &vulkan_adopt_sampler,
			.getNativeTextureView	  = &vulkan_get_native_texture_view,
			.getNativeSampler		  = &vulkan_get_native_sampler,
			.adoptTimeline			  = &vulkan_adopt_timeline,
			.adoptBinarySemaphore	  = &vulkan_adopt_binary_semaphore,
			.getNativeTimeline		  = &vulkan_get_native_timeline,
			.getNativeBinarySemaphore = &vulkan_get_native_binary_semaphore,
		};

		return block;
	}

}

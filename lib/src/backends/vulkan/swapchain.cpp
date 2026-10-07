// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/swapchain.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace azo::rhi::vulkan
{
	bool rebuild_swapchain_semaphores(VulkanSwapchain * swapchain)
	{
		for (vk::Semaphore sem : swapchain->semaphores)
		{
			if (sem)
			{
				swapchain->owner->device.destroySemaphore(sem, nullptr, swapchain->owner->dispatch);
			}
		}

		const auto imageCount	 = static_cast<std::uint32_t>(swapchain->bundle.Images.size());
		swapchain->acquireBase	 = imageCount;
		swapchain->acquireCount	 = imageCount + 1;
		swapchain->acquireCursor = 0;

		const std::size_t total = static_cast<std::size_t>(imageCount) + swapchain->acquireCount;
		swapchain->semaphores.clear();
		if (!detail::try_reserve(swapchain->semaphores, total))
		{
			return false;
		}

		swapchain->semaphores.resize(total, vk::Semaphore{});

		for (vk::Semaphore & sem : swapchain->semaphores)
		{
			const auto created = swapchain->owner->device.createSemaphore({}, nullptr, swapchain->owner->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return false;
			}

			sem = created.value;
		}

		return true;
	}

	bool register_swapchain_back_buffers(VulkanSwapchain * swapchain)
	{
		VulkanDevice * device		 = swapchain->owner;
		const std::size_t imageCount = swapchain->bundle.Images.size();

		if (swapchain->bundle.Views.size() != imageCount || swapchain->backBufferViews.size() != swapchain->backBufferTextures.size())
		{
			return false;
		}

		const std::size_t shared = std::min(imageCount, swapchain->backBufferTextures.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		for (std::size_t i = 0; i < shared; ++i)
		{
			static_cast<void>(device->textureSlots.retire(swapchain->backBufferTextures[i], true));
			swapchain->backBufferTextures[i] = device->textureSlots.store(
				TextureSlot{
					.image	  = swapchain->bundle.Images[i],
					.format	  = swapchain->bundle.ColorFormat,
					.lifetime = SlotLifetime::eSwapchainBorrowed,
				}
			);

			static_cast<void>(device->textureViewSlots.retire(swapchain->backBufferViews[i], true));
			swapchain->backBufferViews[i] = device->textureViewSlots.store(
				TextureViewSlot{
					.view	  = swapchain->bundle.Views[i],
					.format	  = swapchain->bundle.ColorFormat,
					.lifetime = SlotLifetime::eSwapchainBorrowed,
				}
			);

			if (!swapchain->backBufferTextures[i].is_valid() || !swapchain->backBufferViews[i].is_valid())
			{
				return false;
			}
		}

		if (!detail::try_reserve(swapchain->backBufferTextures, imageCount) || !detail::try_reserve(swapchain->backBufferViews, imageCount))
		{
			return false;
		}

		for (std::size_t i = shared; i < imageCount; ++i)
		{
			const TextureHandle texture = device->textureSlots.store(
				TextureSlot{
					.image	  = swapchain->bundle.Images[i],
					.format	  = swapchain->bundle.ColorFormat,
					.lifetime = SlotLifetime::eSwapchainBorrowed,
				}
			);

			const TextureViewHandle view = device->textureViewSlots.store(
				TextureViewSlot{
					.view	  = swapchain->bundle.Views[i],
					.format	  = swapchain->bundle.ColorFormat,
					.lifetime = SlotLifetime::eSwapchainBorrowed,
				}
			);

			if (!texture.is_valid() || !view.is_valid())
			{
				return false;
			}

			swapchain->backBufferTextures.push_back(texture);
			swapchain->backBufferViews.push_back(view);
		}

		for (std::size_t i = imageCount; i < swapchain->backBufferTextures.size(); ++i)
		{
			static_cast<void>(device->textureSlots.retire(swapchain->backBufferTextures[i], true));
			static_cast<void>(device->textureViewSlots.retire(swapchain->backBufferViews[i], true));
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		swapchain->backBufferTextures.resize(imageCount);
		swapchain->backBufferViews.resize(imageCount);
		return true;
	}

	namespace
	{
		void release_unowned_swapchain(VulkanDevice * device, VulkanSwapchain * swapchain) noexcept
		{
			for (const vk::Semaphore sem : swapchain->semaphores)
			{
				if (sem)
				{
					device->device.destroySemaphore(sem, nullptr, device->dispatch);
				}
			}

			swapchain->semaphores.clear();
			destroy_swapchain(device->device, device->dispatch, device->allocator, swapchain->bundle);
		}
	}

	void * vulkan_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createSwapchain");
		auto * device = static_cast<VulkanDevice *>(impl);

		if (desc.surface.value == 0)
		{
			return fail_value<void *>(error, ErrorCode::eInvalidHandle, "swapchain creation requires a valid surface");
		}

		const vk::SurfaceKHR surface(std::bit_cast<VkSurfaceKHR>(desc.surface.value));

		if (device->graphicsQueues.empty())
		{
			return fail_value<void *>(error, ErrorCode::eInvalidState, "swapchain creation requires a graphics queue");
		}

		const auto presentSupport = device->phys.getSurfaceSupportKHR(device->graphicsFamily, surface, device->dispatch);
		if (presentSupport.result != vk::Result::eSuccess || presentSupport.value == VK_FALSE)
		{
			return fail_value<void *>(error, ErrorCode::eUnsupportedFeature, "the graphics queue family cannot present to this surface");
		}

		detail::HostVector<vk::Format> desiredFormats;
		detail::HostVector<vk::PresentModeKHR> desiredPresentModes;
		if (!detail::try_reserve(desiredFormats, 1 + desc.formatFallbacks.size()) ||
			!detail::try_reserve(desiredPresentModes, 1 + desc.presentModeFallbacks.size()))
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain preference list allocation failed");
		}

		desiredFormats.push_back(map_format(desc.preferredFormat));
		for (const Format format : desc.formatFallbacks)
		{
			desiredFormats.push_back(map_format(format));
		}

		desiredPresentModes.push_back(map_present_mode(desc.presentMode));
		for (const PresentMode mode : desc.presentModeFallbacks)
		{
			desiredPresentModes.push_back(map_present_mode(mode));
		}

		SwapchainBundle bundle = create_swapchain(
			device->device,
			device->dispatch,
			device->phys,
			device->allocator,
			surface,
			desc.width,
			desc.height,
			nullptr,
			desiredFormats,
			desiredPresentModes,
			desc.imageCount
		);
		if (!bundle.Swapchain)
		{
			return fail_native_value<void *>(error, "Vulkan swapchain creation failed", bundle.Failure);
		}

		auto swapchain = host_new<VulkanSwapchain>();
		if (swapchain == nullptr)
		{
			destroy_swapchain(device->device, device->dispatch, device->allocator, bundle);
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain allocation failed");
		}

		swapchain->object  = publishing_object<Published<SwapchainApi, &swapchain_block>>();
		swapchain->owner   = device;
		swapchain->surface = surface;
		swapchain->bundle  = std::move(bundle);
		if (!rebuild_swapchain_semaphores(swapchain.get()) || !register_swapchain_back_buffers(swapchain.get()))
		{
			release_unowned_swapchain(device, swapchain.get());
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain back buffer registration failed");
		}

		swapchain->desiredFormats	   = std::move(desiredFormats);
		swapchain->desiredPresentModes = std::move(desiredPresentModes);
		swapchain->desiredImageCount   = desc.imageCount;

		VulkanSwapchain * raw = swapchain.get();
		swapchain->id		  = device->nextSwapchainId++ & kSwapchainIdMask;
		if (!detail::try_push_back(device->swapchains, std::move(swapchain)))
		{
			release_unowned_swapchain(device, raw);
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain allocation failed");
		}

		return return_value(raw, error);
	}

	bool vulkan_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.swapchain.resize");
		auto * swapchain	  = static_cast<VulkanSwapchain *>(impl);
		VulkanDevice * device = swapchain->owner;

		SwapchainBundle next = create_swapchain(
			device->device,
			device->dispatch,
			device->phys,
			device->allocator,
			swapchain->surface,
			width,
			height,
			swapchain->bundle.Swapchain,
			swapchain->desiredFormats,
			swapchain->desiredPresentModes,
			swapchain->desiredImageCount
		);

		if (!next.Swapchain)
		{
			return fail_native(error, "Vulkan swapchain resize failed", next.Failure);
		}

		destroy_swapchain(device->device, device->dispatch, device->allocator, swapchain->bundle);
		swapchain->bundle = std::move(next);
		if (!rebuild_swapchain_semaphores(swapchain) || !register_swapchain_back_buffers(swapchain))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain back buffer registration failed");
		}

		return succeed(error);
	}

	bool vulkan_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		swapchain->desiredPresentModes.assign(1, map_present_mode(mode));
		return succeed(error);
	}

	[[nodiscard]] SwapchainStatus map_swapchain_status(vk::Result result) noexcept
	{
		switch (result)
		{
		case vk::Result::eSuboptimalKHR:	   return SwapchainStatus::eSuboptimal;
		case vk::Result::eErrorOutOfDateKHR:   return SwapchainStatus::eOutOfDate;
		case vk::Result::eErrorSurfaceLostKHR: return SwapchainStatus::eSurfaceLost;
		case vk::Result::eTimeout:
		case vk::Result::eNotReady:			   return SwapchainStatus::eTimeout;
		default:							   return SwapchainStatus::eOk;
		}
	}

	AcquireResult vulkan_acquire(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * swapchain		 = static_cast<VulkanSwapchain *>(impl);
		const std::uint32_t slot = swapchain->acquireBase + (swapchain->acquireCursor % swapchain->acquireCount);
		swapchain->acquireCursor = (swapchain->acquireCursor + 1) % swapchain->acquireCount;
		const vk::Semaphore sem	 = azo::rhi::detail::at(swapchain->semaphores, slot);

		const vk::ResultValue<std::uint32_t> acquired =
			swapchain->owner->device.acquireNextImageKHR(swapchain->bundle.Swapchain, timeoutNanoseconds, sem, nullptr, swapchain->owner->dispatch);
		const SwapchainStatus status = map_swapchain_status(acquired.result);

		if (status == SwapchainStatus::eOk && acquired.result != vk::Result::eSuccess)
		{
			fail_native(error, "vkAcquireNextImageKHR failed", acquired.result);
			return AcquireResult{ .status = SwapchainStatus::eError };
		}

		if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR)
		{
			return return_value(
				AcquireResult{
					.status			= status,
					.imageIndex		= 0,
					.imageAvailable = {},
				},
				error
			);
		}

		return return_value(AcquireResult{ .status = status,
							   .imageIndex		  = acquired.value,
							   .imageAvailable	  = { .index = encode_wsi_semaphore(swapchain->id, slot), .generation = 1, }, },
			error);
	}

	PresentResult vulkan_present(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.present");
		auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		auto * queue	 = static_cast<VulkanQueue *>(queueImpl);

		const std::uint32_t swapchainId = (renderFinished.index >> kSwapchainIdShift) & kSwapchainIdMask;
		const std::uint32_t slot		= renderFinished.index & kSwapchainSlotMask;
		if ((renderFinished.index & kDeviceBinarySemaphoreBit) != 0 || swapchainId != swapchain->id || slot >= swapchain->semaphores.size())
		{
			fail(error, ErrorCode::eInvalidHandle, "present render-finished semaphore is not a back buffer semaphore of this swapchain");
			return PresentResult{ .status = SwapchainStatus::eError };
		}
		const vk::Semaphore wait = azo::rhi::detail::at(swapchain->semaphores, slot);

		if (imageIndex >= swapchain->backBufferTextures.size())
		{
			fail(error, ErrorCode::eInvalidArgument, "present names an image index this swapchain does not have");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		const vk::PresentInfoKHR present(wait, swapchain->bundle.Swapchain, imageIndex);
		const vk::Result result		 = queue->queue.presentKHR(present, queue->owner->dispatch);
		const SwapchainStatus status = map_swapchain_status(result);

		if (status == SwapchainStatus::eOk && result != vk::Result::eSuccess)
		{
			fail_native(error, "vkQueuePresentKHR failed", result);
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		return return_value(PresentResult{ .status = status }, error);
	}

	Format vulkan_swapchain_format(void * impl) noexcept
	{
		return map_vk_format(static_cast<VulkanSwapchain *>(impl)->bundle.ColorFormat);
	}

	PresentMode vulkan_swapchain_get_present_mode(void * impl) noexcept
	{
		return map_vk_present_mode(static_cast<VulkanSwapchain *>(impl)->bundle.PresentMode);
	}

	bool vulkan_swapchain_supports_readback(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.CaptureCapable;
	}

	std::uint32_t vulkan_swapchain_image_count(void * impl) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<VulkanSwapchain *>(impl)->bundle.Images.size());
	}

	std::uint32_t vulkan_swapchain_width(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.Extent.width;
	}

	std::uint32_t vulkan_swapchain_height(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.Extent.height;
	}

	TextureHandle vulkan_swapchain_back_buffer(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		if (imageIndex >= swapchain->backBufferTextures.size())
		{
			return {};
		}

		return swapchain->backBufferTextures[imageIndex]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	TextureViewHandle vulkan_swapchain_back_buffer_view(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		if (imageIndex >= swapchain->backBufferViews.size())
		{
			return {};
		}

		return swapchain->backBufferViews[imageIndex]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	BinarySemaphoreHandle vulkan_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);

		if (imageIndex >= swapchain->acquireBase)
		{
			return {};
		}

		return BinarySemaphoreHandle{
			.index		= encode_wsi_semaphore(swapchain->id, imageIndex),
			.generation = 1,
		};
	}

}

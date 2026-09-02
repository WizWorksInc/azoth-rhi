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
	bool RebuildSwapchainSemaphores(VulkanSwapchain * swapchain)
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
		if (!detail::TryReserve(swapchain->semaphores, total))
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

	bool RegisterSwapchainBackBuffers(VulkanSwapchain * swapchain)
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
			static_cast<void>(device->textureSlots.Retire(swapchain->backBufferTextures[i], true));
			swapchain->backBufferTextures[i] = device->textureSlots.Store(TextureSlot{
				.image	  = swapchain->bundle.Images[i],
				.format	  = swapchain->bundle.ColorFormat,
				.lifetime = SlotLifetime::eSwapchainBorrowed,
			});

			static_cast<void>(device->textureViewSlots.Retire(swapchain->backBufferViews[i], true));
			swapchain->backBufferViews[i] = device->textureViewSlots.Store(TextureViewSlot{
				.view	  = swapchain->bundle.Views[i],
				.format	  = swapchain->bundle.ColorFormat,
				.lifetime = SlotLifetime::eSwapchainBorrowed,
			});

			if (!swapchain->backBufferTextures[i].IsValid() || !swapchain->backBufferViews[i].IsValid())
			{
				return false;
			}
		}

		if (!detail::TryReserve(swapchain->backBufferTextures, imageCount) || !detail::TryReserve(swapchain->backBufferViews, imageCount))
		{
			return false;
		}

		for (std::size_t i = shared; i < imageCount; ++i)
		{
			const TextureHandle texture = device->textureSlots.Store(TextureSlot{
				.image	  = swapchain->bundle.Images[i],
				.format	  = swapchain->bundle.ColorFormat,
				.lifetime = SlotLifetime::eSwapchainBorrowed,
			});

			const TextureViewHandle view = device->textureViewSlots.Store(TextureViewSlot{
				.view	  = swapchain->bundle.Views[i],
				.format	  = swapchain->bundle.ColorFormat,
				.lifetime = SlotLifetime::eSwapchainBorrowed,
			});

			if (!texture.IsValid() || !view.IsValid())
			{
				return false;
			}

			swapchain->backBufferTextures.push_back(texture);
			swapchain->backBufferViews.push_back(view);
		}

		for (std::size_t i = imageCount; i < swapchain->backBufferTextures.size(); ++i)
		{
			static_cast<void>(device->textureSlots.Retire(swapchain->backBufferTextures[i], true));
			static_cast<void>(device->textureViewSlots.Retire(swapchain->backBufferViews[i], true));
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		swapchain->backBufferTextures.resize(imageCount);
		swapchain->backBufferViews.resize(imageCount);
		return true;
	}

	namespace
	{
		void ReleaseUnownedSwapchain(VulkanDevice * device, VulkanSwapchain * swapchain) noexcept
		{
			for (const vk::Semaphore sem : swapchain->semaphores)
			{
				if (sem)
				{
					device->device.destroySemaphore(sem, nullptr, device->dispatch);
				}
			}

			swapchain->semaphores.clear();
			DestroySwapchain(device->device, device->dispatch, device->allocator, swapchain->bundle);
		}
	}

	void * VulkanCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createSwapchain");
		auto * device = static_cast<VulkanDevice *>(impl);

		if (desc.surface.value == 0)
		{
			return FailValue<void *>(error, ErrorCode::eInvalidHandle, "swapchain creation requires a valid surface");
		}

		const vk::SurfaceKHR surface(std::bit_cast<VkSurfaceKHR>(desc.surface.value));

		if (device->graphicsQueues.empty())
		{
			return FailValue<void *>(error, ErrorCode::eInvalidState, "swapchain creation requires a graphics queue");
		}

		const auto presentSupport = device->phys.getSurfaceSupportKHR(device->graphicsFamily, surface, device->dispatch);
		if (presentSupport.result != vk::Result::eSuccess || presentSupport.value == VK_FALSE)
		{
			return FailValue<void *>(error, ErrorCode::eUnsupportedFeature, "the graphics queue family cannot present to this surface");
		}

		detail::HostVector<vk::Format> desiredFormats;
		detail::HostVector<vk::PresentModeKHR> desiredPresentModes;
		if (!detail::TryReserve(desiredFormats, 1 + desc.formatFallbacks.size()) ||
			!detail::TryReserve(desiredPresentModes, 1 + desc.presentModeFallbacks.size()))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain preference list allocation failed");
		}

		desiredFormats.push_back(MapFormat(desc.preferredFormat));
		for (const Format format : desc.formatFallbacks)
		{
			desiredFormats.push_back(MapFormat(format));
		}

		desiredPresentModes.push_back(MapPresentMode(desc.presentMode));
		for (const PresentMode mode : desc.presentModeFallbacks)
		{
			desiredPresentModes.push_back(MapPresentMode(mode));
		}

		SwapchainBundle bundle = CreateSwapchain(device->device,
			device->dispatch,
			device->phys,
			device->allocator,
			surface,
			desc.width,
			desc.height,
			nullptr,
			desiredFormats,
			desiredPresentModes,
			desc.imageCount);
		if (!bundle.Swapchain)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "Vulkan swapchain creation failed");
		}

		auto swapchain = HostNew<VulkanSwapchain>();
		if (swapchain == nullptr)
		{
			DestroySwapchain(device->device, device->dispatch, device->allocator, bundle);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain allocation failed");
		}

		swapchain->object  = PublishingObject<Published<SwapchainApi, &SwapchainBlock>>();
		swapchain->owner   = device;
		swapchain->surface = surface;
		swapchain->bundle  = std::move(bundle);
		if (!RebuildSwapchainSemaphores(swapchain.get()) || !RegisterSwapchainBackBuffers(swapchain.get()))
		{
			ReleaseUnownedSwapchain(device, swapchain.get());
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain back buffer registration failed");
		}

		swapchain->desiredFormats	   = std::move(desiredFormats);
		swapchain->desiredPresentModes = std::move(desiredPresentModes);
		swapchain->desiredImageCount   = desc.imageCount;

		VulkanSwapchain * raw = swapchain.get();
		swapchain->id		  = device->nextSwapchainId++ & kSwapchainIdMask;
		if (!detail::TryPushBack(device->swapchains, std::move(swapchain)))
		{
			ReleaseUnownedSwapchain(device, raw);
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain allocation failed");
		}

		return ReturnValue(raw, error);
	}

	bool VulkanSwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.swapchain.resize");
		auto * swapchain	  = static_cast<VulkanSwapchain *>(impl);
		VulkanDevice * device = swapchain->owner;

		SwapchainBundle next = CreateSwapchain(device->device,
			device->dispatch,
			device->phys,
			device->allocator,
			swapchain->surface,
			width,
			height,
			swapchain->bundle.Swapchain,
			swapchain->desiredFormats,
			swapchain->desiredPresentModes,
			swapchain->desiredImageCount);

		if (!next.Swapchain)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Vulkan swapchain resize failed");
		}

		DestroySwapchain(device->device, device->dispatch, device->allocator, swapchain->bundle);
		swapchain->bundle = std::move(next);
		if (!RebuildSwapchainSemaphores(swapchain) || !RegisterSwapchainBackBuffers(swapchain))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain back buffer registration failed");
		}

		return Succeed(error);
	}

	bool VulkanSwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		swapchain->desiredPresentModes.assign(1, MapPresentMode(mode));
		return Succeed(error);
	}

	[[nodiscard]] SwapchainStatus MapSwapchainStatus(vk::Result result) noexcept
	{
		switch (result)
		{
		case vk::Result::eSuboptimalKHR:	   return SwapchainStatus::eSuboptimal;
		case vk::Result::eErrorOutOfDateKHR:   return SwapchainStatus::eOutOfDate;
		case vk::Result::eErrorSurfaceLostKHR: return SwapchainStatus::eSurfaceLost;
		case vk::Result::eTimeout:
		case vk::Result::eNotReady: return SwapchainStatus::eTimeout;
		default:					return SwapchainStatus::eOk;
		}
	}

	AcquireResult VulkanAcquire(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		auto * swapchain		 = static_cast<VulkanSwapchain *>(impl);
		const std::uint32_t slot = swapchain->acquireBase + (swapchain->acquireCursor % swapchain->acquireCount);
		swapchain->acquireCursor = (swapchain->acquireCursor + 1) % swapchain->acquireCount;
		const vk::Semaphore sem	 = swapchain->semaphores[slot];

		const vk::ResultValue<std::uint32_t> acquired =
			swapchain->owner->device.acquireNextImageKHR(swapchain->bundle.Swapchain, timeoutNanoseconds, sem, nullptr, swapchain->owner->dispatch);
		const SwapchainStatus status = MapSwapchainStatus(acquired.result);

		if (status == SwapchainStatus::eOk && acquired.result != vk::Result::eSuccess)
		{
			Fail(error, ErrorCode::eNativeApiError, "vkAcquireNextImageKHR failed");
			return AcquireResult{ .status = SwapchainStatus::eError };
		}

		if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR)
		{
			return ReturnValue(
				AcquireResult{
					.status			= status,
					.imageIndex		= 0,
					.imageAvailable = {},
				},
				error);
		}

		return ReturnValue(AcquireResult{ .status = status,
							   .imageIndex		  = acquired.value,
							   .imageAvailable	  = { .index = EncodeWsiSemaphore(swapchain->id, slot), .generation = 1, } },
			error);
	}

	PresentResult VulkanPresent(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.present");
		auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		auto * queue	 = static_cast<VulkanQueue *>(queueImpl);

		const std::uint32_t swapchainId = (renderFinished.index >> kSwapchainIdShift) & kSwapchainIdMask;
		const std::uint32_t slot		= renderFinished.index & kSwapchainSlotMask;
		if ((renderFinished.index & kDeviceBinarySemaphoreBit) != 0 || swapchainId != swapchain->id || slot >= swapchain->semaphores.size())
		{
			Fail(error, ErrorCode::eInvalidHandle, "present render-finished semaphore is not a back buffer semaphore of this swapchain");
			return PresentResult{ .status = SwapchainStatus::eError };
		}
		const vk::Semaphore wait = swapchain->semaphores[slot];

		if (imageIndex >= swapchain->backBufferTextures.size())
		{
			Fail(error, ErrorCode::eInvalidArgument, "present names an image index this swapchain does not have");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		const vk::PresentInfoKHR present(wait, swapchain->bundle.Swapchain, imageIndex);
		const vk::Result result		 = queue->queue.presentKHR(present, queue->owner->dispatch);
		const SwapchainStatus status = MapSwapchainStatus(result);

		if (status == SwapchainStatus::eOk && result != vk::Result::eSuccess)
		{
			Fail(error, ErrorCode::eNativeApiError, "vkQueuePresentKHR failed");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		return ReturnValue(PresentResult{ .status = status }, error);
	}

	Format VulkanSwapchainFormat(void * impl) noexcept
	{
		return MapVkFormat(static_cast<VulkanSwapchain *>(impl)->bundle.ColorFormat);
	}

	PresentMode VulkanSwapchainGetPresentMode(void * impl) noexcept
	{
		return MapVkPresentMode(static_cast<VulkanSwapchain *>(impl)->bundle.PresentMode);
	}

	bool VulkanSwapchainSupportsReadback(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.CaptureCapable;
	}

	std::uint32_t VulkanSwapchainImageCount(void * impl) noexcept
	{
		return static_cast<std::uint32_t>(static_cast<VulkanSwapchain *>(impl)->bundle.Images.size());
	}

	std::uint32_t VulkanSwapchainWidth(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.Extent.width;
	}

	std::uint32_t VulkanSwapchainHeight(void * impl) noexcept
	{
		return static_cast<VulkanSwapchain *>(impl)->bundle.Extent.height;
	}

	TextureHandle VulkanSwapchainBackBuffer(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		if (imageIndex >= swapchain->backBufferTextures.size())
		{
			return {};
		}

		return swapchain->backBufferTextures[imageIndex]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	TextureViewHandle VulkanSwapchainBackBufferView(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		if (imageIndex >= swapchain->backBufferViews.size())
		{
			return {};
		}

		return swapchain->backBufferViews[imageIndex]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	BinarySemaphoreHandle VulkanSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		const auto * swapchain = static_cast<VulkanSwapchain *>(impl);

		if (imageIndex >= swapchain->acquireBase)
		{
			return {};
		}

		return BinarySemaphoreHandle{
			.index		= EncodeWsiSemaphore(swapchain->id, imageIndex),
			.generation = 1,
		};
	}

}

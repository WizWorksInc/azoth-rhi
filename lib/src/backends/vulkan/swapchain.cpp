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

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	// Frees any previous semaphores and allocates the per-image present set plus the acquire ring for the swapchain's current image count. False leaves the set
	// partly built with the rest null, which the caller reports as the create or resize failing.
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
		swapchain->acquireCount	 = imageCount + 1; // one more than images so a slot is never reused while still pending
		swapchain->acquireCursor = 0;

		const std::size_t total = static_cast<std::size_t>(imageCount) + swapchain->acquireCount;
		swapchain->semaphores.clear();
		if (!detail::TryReserve(swapchain->semaphores, total))
		{
			return false;
		}

		// Inside the capacity reserved above, so this cannot grow.
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

	/*
	 * Registers the swapchain's back buffer color images and views as borrowed device slots so getBackBuffer and getBackBufferView hand back resolvable RHI
	 * handles. On the first call it allocates the slots, on resize it updates the existing slots in place (new image and view, bumped generation) so the registry
	 * does not grow per resize. Borrowed slots are skipped by destroy and teardown, since the swapchain owns the underlying images and views.
	 */
	bool RegisterSwapchainBackBuffers(VulkanSwapchain * swapchain)
	{
		VulkanDevice * device		 = swapchain->owner;
		const std::size_t imageCount = swapchain->bundle.Images.size();

		// The loops below index images and views by the same counter, so the pairing they assume is checked here, not left to the two places that build these lists
		// in step.
		if (swapchain->bundle.Views.size() != imageCount || swapchain->backBufferViews.size() != swapchain->backBufferTextures.size())
		{
			return false;
		}

		// Reuse the slots that already exist (first build has none), updating them in place and bumping the generation so a resize is visible to handle validation.
		const std::size_t shared = std::min(imageCount, swapchain->backBufferTextures.size());

		// Every loop below is bounded by the size of one of these lists, and the pairing between them is checked above.
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

			// The slot the retire above freed is the one this store takes back, so a refusal here means the table itself is exhausted.
			if (!swapchain->backBufferTextures[i].IsValid() || !swapchain->backBufferViews[i].IsValid())
			{
				return false;
			}
		}

		// Register any images a resized chain added (also the whole set on first build).
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

			// A back buffer that has no slot has no handle for getBackBuffer to hand back, so the whole registration fails without leaving an image index that resolves
			// to nothing.
			if (!texture.IsValid() || !view.IsValid())
			{
				return false;
			}

			// Both were reserved for the full image count above, so neither append can grow.
			swapchain->backBufferTextures.push_back(texture);
			swapchain->backBufferViews.push_back(view);
		}

		// Retire the slots a smaller resized chain dropped so a stale back-buffer handle no longer resolves to an image the bundle rebuild has freed.
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
		// Gives back everything a swapchain built before it reached device->swapchains. Teardown walks that list, so a failure between the bundle being created and
		// the swapchain being handed over has nothing else that would free these.
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
	} // namespace

	void * VulkanCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.vulkan.createSwapchain");
		auto * device = static_cast<VulkanDevice *>(impl);

		// A null surface is what a caller ends up with when window creation failed and every call below would hand it straight to the driver. Refuse it here
		//, not crash there.
		if (desc.surface.value == 0)
		{
			return FailValue<void *>(error, ErrorCode::eInvalidHandle, "swapchain creation requires a valid surface");
		}

		const vk::SurfaceKHR surface(std::bit_cast<VkSurfaceKHR>(desc.surface.value));

		// The swapchain presents on the graphics queue so its family must exist and support present on this surface. The device is created before any surface exists
		// so this is the first chance to confirm the assumed present queue is actually present-capable.
		if (device->graphicsQueues.empty())
		{
			return FailValue<void *>(error, ErrorCode::eInvalidState, "swapchain creation requires a graphics queue");
		}

		const auto presentSupport = device->phys.getSurfaceSupportKHR(device->graphicsFamily, surface, device->dispatch);
		if (presentSupport.result != vk::Result::eSuccess || presentSupport.value == VK_FALSE)
		{
			return FailValue<void *>(error, ErrorCode::eUnsupportedFeature, "the graphics queue family cannot present to this surface");
		}

		// Lower the RHI format and present-mode preference lists (primary choice first, then fallbacks) onto Vulkan. They are stored on the swapchain below so resize
		// rebuilds with the same preferences.
		detail::HostVector<vk::Format> desiredFormats;
		detail::HostVector<vk::PresentModeKHR> desiredPresentModes;
		if (!detail::TryReserve(desiredFormats, 1 + desc.formatFallbacks.size()) ||
			!detail::TryReserve(desiredPresentModes, 1 + desc.presentModeFallbacks.size()))
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Vulkan swapchain preference list allocation failed");
		}

		// Both were reserved for exactly the primary choice plus its fallbacks, so neither append below can grow.
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

		// The old chain is kept when the new one could not be built, so a failed resize leaves a swapchain that still presents at the old size.
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
		// Update the preferred present mode. It is re-selected against the surface on the next resize, which fully recreates the swapchain (SelectPresentMode falls
		// back to FIFO if the surface lacks it).
		auto * swapchain = static_cast<VulkanSwapchain *>(impl);
		swapchain->desiredPresentModes.assign(1, MapPresentMode(mode));
		return Succeed(error);
	}

	// Maps a Vulkan acquire or present result onto the RHI swapchain status.
	[[nodiscard]] SwapchainStatus MapSwapchainStatus(vk::Result result) noexcept
	{
		switch (result)
		{
		case vk::Result::eSuboptimalKHR:	   return SwapchainStatus::eSuboptimal;
		case vk::Result::eErrorOutOfDateKHR:   return SwapchainStatus::eOutOfDate;
		case vk::Result::eErrorSurfaceLostKHR: return SwapchainStatus::eSurfaceLost;
		// Success codes that acquire no image: the image index is left unwritten for both.
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

		// A result MapSwapchainStatus has no swapchain answer for is a driver failure and not a chain the caller can rebuild.
		if (status == SwapchainStatus::eOk && acquired.result != vk::Result::eSuccess)
		{
			Fail(error, ErrorCode::eNativeApiError, "vkAcquireNextImageKHR failed");
			return AcquireResult{ .status = SwapchainStatus::eError };
		}

		// Only a success wrote an image index. On eTimeout, eNotReady, eOutOfDate and eSurfaceLost acquired.value is unwritten, so report no image index and no
		// image-available semaphore and not a stale one.
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

		/*
		 * renderFinished must be one of this swapchain's own per-image semaphores. A device-tagged handle, one minted by a different swapchain or an out-of-range
		 * slot (a stale handle after a resize shrank the chain) is rejected, not indexed blindly, matching the validation the rest of the backend applies.
		 */
		const std::uint32_t swapchainId = (renderFinished.index >> kSwapchainIdShift) & kSwapchainIdMask;
		const std::uint32_t slot		= renderFinished.index & kSwapchainSlotMask;
		if ((renderFinished.index & kDeviceBinarySemaphoreBit) != 0 || swapchainId != swapchain->id || slot >= swapchain->semaphores.size())
		{
			Fail(error, ErrorCode::eInvalidHandle, "present render-finished semaphore is not a back buffer semaphore of this swapchain");
			return PresentResult{ .status = SwapchainStatus::eError };
		}
		const vk::Semaphore wait = swapchain->semaphores[slot];

		// The image index gets what the semaphore above gets, and for the same stale-after-a-resize reason. It is otherwise the one argument here handed to the
		// driver unexamined, and presenting an image the chain does not have is undefined rather than refused.
		if (imageIndex >= swapchain->backBufferTextures.size())
		{
			Fail(error, ErrorCode::eInvalidArgument, "present names an image index this swapchain does not have");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		const vk::PresentInfoKHR present(wait, swapchain->bundle.Swapchain, imageIndex);
		const vk::Result result		 = queue->queue.presentKHR(present, queue->owner->dispatch);
		const SwapchainStatus status = MapSwapchainStatus(result);

		// A result MapSwapchainStatus has no swapchain answer for is a driver failure and not a chain the caller can rebuild.
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
		// The bundle holds what SelectPresentMode settled on against the surface so an unsupported request reads back as the mode it fell back to.
		return MapVkPresentMode(static_cast<VulkanSwapchain *>(impl)->bundle.PresentMode);
	}

	bool VulkanSwapchainSupportsReadback(void * impl) noexcept
	{
		// The bundle records whether the surface allowed transfer-src usage on the swapchain images.
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

	// Back buffer textures and views are borrowed device slots registered at swapchain create and resize so these hand back resolvable RHI handles. The per-image
	// present semaphore is the render-finished semaphore a caller signals from submit and Present then waits on, the first imageCount slots.
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

		/*
		 * Empty for an index this chain has no image for, as the two accessors above answer the same question. Folding it onto slot zero handed back another
		 * image's semaphore looking entirely valid, so a caller that kept an index across a resize that shrank the chain would signal one image's semaphore for
		 * another image's frame, which is the double signal the one-semaphore-per-image rule exists to prevent.
		 */
		if (imageIndex >= swapchain->acquireBase)
		{
			return {};
		}

		return BinarySemaphoreHandle{
			.index		= EncodeWsiSemaphore(swapchain->id, imageIndex),
			.generation = 1,
		};
	}

} // namespace azo::rhi::vulkan

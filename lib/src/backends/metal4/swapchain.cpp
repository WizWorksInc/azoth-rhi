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
	AcquireResult Metal4SwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.acquire");

		auto * swapchain							  = static_cast<Metal4Swapchain *>(impl);
		Metal4Device * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		CA::MetalDrawable * drawable = swapchain->layer->nextDrawable();
		if (drawable == nullptr)
		{
			Fail(error, ErrorCode::eNativeApiError, "CAMetalLayer nextDrawable returned null");
			return AcquireResult{ .status = SwapchainStatus::eOutOfDate };
		}

		swapchain->currentDrawable = NS::RetainPtr(drawable);

		if (MTL4::CommandQueue * queue = device->CommandQueueFor(QueueType::eGraphics); queue != nullptr)
		{
			queue->wait(drawable);
		}

		{
			NS::SharedPtr<MTL::Texture> texture = NS::RetainPtr(drawable->texture());

			if (Metal4TextureSlot * slot = device->textures.Resolve(swapchain->backBuffer, false); slot != nullptr)
			{
				slot->texture = texture;
				slot->format  = swapchain->format;
			}

			if (Metal4TextureViewSlot * view = device->textureViews.Resolve(swapchain->backBufferView, false); view != nullptr)
			{
				view->texture = texture;
			}
		}

		const std::uint32_t imageIndex = swapchain->frameCursor;
		swapchain->frameCursor		   = (swapchain->frameCursor + 1) % swapchain->imageCount;

		Succeed(error);
		return AcquireResult{
			.status			= SwapchainStatus::eOk,
			.imageIndex		= imageIndex,
			.imageAvailable = swapchain->imageAvailable,
		};
	}

	PresentResult Metal4SwapchainPresent(
		void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, [[maybe_unused]] void * queueImpl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.present");

		auto * swapchain							  = static_cast<Metal4Swapchain *>(impl);
		Metal4Device * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (!swapchain->currentDrawable)
		{
			Fail(error, ErrorCode::eInvalidState, "Metal present called without an acquired drawable");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		MTL4::CommandQueue * presentQueue = device->CommandQueueFor(QueueType::eGraphics);
		if (presentQueue == nullptr)
		{
			Fail(error, ErrorCode::eInvalidState, "Metal present requires a graphics queue");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		if (const auto * tracked = device->binarySemaphores.Resolve(renderFinished, kHandleAlreadyChecked); tracked != nullptr)
		{
			presentQueue->wait(tracked->event.get(), tracked->value);
		}

		presentQueue->signalDrawable(swapchain->currentDrawable.get());
		swapchain->currentDrawable->present();
		swapchain->currentDrawable.reset();

		Succeed(error);
		return PresentResult{ .status = SwapchainStatus::eOk };
	}

	TextureHandle Metal4SwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->backBuffer;
	}

	TextureViewHandle Metal4SwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->backBufferView;
	}

	BinarySemaphoreHandle Metal4SwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * swapchain = static_cast<Metal4Swapchain *>(impl);

		if (swapchain->presentSemaphores.empty())
		{
			return {};
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the wrap above is what bounds this.
		return swapchain->presentSemaphores[imageIndex % swapchain->presentSemaphores.size()];
	}

	Format Metal4SwapchainFormat(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->format;
	}

	bool Metal4SwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept
	{
		return true;
	}

	std::uint32_t Metal4SwapchainImageCount(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->imageCount;
	}

	std::uint32_t Metal4SwapchainWidth(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->width;
	}

	std::uint32_t Metal4SwapchainHeight(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->height;
	}

	bool Metal4SwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		auto * swapchain  = static_cast<Metal4Swapchain *>(impl);
		swapchain->width  = width;
		swapchain->height = height;
		swapchain->layer->setDrawableSize(CGSize{ static_cast<CGFloat>(width), static_cast<CGFloat>(height) });
		return Succeed(error);
	}

	namespace
	{
		[[nodiscard]] PresentMode EffectivePresentMode(PresentMode mode) noexcept
		{
			return mode == PresentMode::eImmediate ? PresentMode::eImmediate : PresentMode::eFifo;
		}

		PresentMode Metal4SwapchainGetPresentMode(void * impl) noexcept
		{
			return static_cast<Metal4Swapchain *>(impl)->presentMode;
		}
	}

	bool Metal4SwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain	   = static_cast<Metal4Swapchain *>(impl);
		swapchain->presentMode = EffectivePresentMode(mode);
		swapchain->layer->setDisplaySyncEnabled(swapchain->presentMode != PresentMode::eImmediate);
		return Succeed(error);
	}

	const SwapchainApi & SwapchainBlock() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &Metal4SwapchainAcquire,
			.present					 = &Metal4SwapchainPresent,
			.getBackBuffer				 = &Metal4SwapchainBackBuffer,
			.getBackBufferView			 = &Metal4SwapchainBackBufferView,
			.getPerImagePresentSemaphore = &Metal4SwapchainPresentSemaphore,
			.getFormat					 = &Metal4SwapchainFormat,
			.getPresentMode				 = &Metal4SwapchainGetPresentMode,
			.getImageCount				 = &Metal4SwapchainImageCount,
			.getWidth					 = &Metal4SwapchainWidth,
			.getHeight					 = &Metal4SwapchainHeight,
			.resize						 = &Metal4SwapchainResize,
			.setPresentMode				 = &Metal4SwapchainSetPresentMode,
			.supportsReadback			 = &Metal4SwapchainSupportsReadback,
		};

		return block;
	}

	void * Metal4CreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createSwapchain");

		auto * device								  = static_cast<Metal4Device *>(impl);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// The surface handle carries the CAMetalLayer pointer the presentation backend made from the window. NOLINTNEXTLINE(performance-no-int-to-ptr)
		auto * layer = reinterpret_cast<CA::MetalLayer *>(static_cast<std::uintptr_t>(desc.surface.value));
		if (layer == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eInvalidArgument, "Metal swapchain requires a CAMetalLayer surface");
		}

		const std::uint32_t imageCount = desc.imageCount == 0 ? 3 : desc.imageCount;

		layer->setDevice(device->device.get());
		layer->setPixelFormat(MetalPixelFormat(desc.preferredFormat));
		layer->setDrawableSize(CGSize{ static_cast<CGFloat>(desc.width), static_cast<CGFloat>(desc.height) });
		layer->setFramebufferOnly(false);
		const PresentMode presentMode = EffectivePresentMode(desc.presentMode);
		layer->setDisplaySyncEnabled(presentMode != PresentMode::eImmediate);

		auto swapchain			  = HostNew<Metal4Swapchain>();
		swapchain->object		  = PublishingObject<Published<SwapchainApi, &SwapchainBlock>>();
		swapchain->owner		  = device;
		swapchain->layer		  = layer;
		swapchain->format		  = desc.preferredFormat;
		swapchain->presentMode	  = presentMode;
		swapchain->width		  = desc.width;
		swapchain->height		  = desc.height;
		swapchain->imageCount	  = imageCount;
		swapchain->backBuffer	  = device->textures.Store(Metal4TextureSlot{ .format = swapchain->format, .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->backBufferView = device->textureViews.Store(Metal4TextureViewSlot{ .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->imageAvailable = Metal4CreateBinarySemaphore(device, BinarySemaphoreDesc{}, nullptr);
		swapchain->presentSemaphores.reserve(imageCount);
		for (std::uint32_t i = 0; i < imageCount; ++i)
		{
			swapchain->presentSemaphores.push_back(Metal4CreateBinarySemaphore(device, BinarySemaphoreDesc{}, nullptr));
		}

		Metal4Swapchain * raw = swapchain.get();
		{
			device->swapchains.push_back(std::move(swapchain));
		}
		return ReturnValue<void *>(raw, error);
	}

}

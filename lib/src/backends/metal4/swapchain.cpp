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
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <CoreFoundation/CFCGTypes.h>

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTL4CommandQueue.hpp>
#include <Metal/MTLTexture.hpp>
#include <QuartzCore/CAMetalDrawable.hpp>

#include <cstdint>
#include <utility>

namespace azo::rhi::metal4
{
	AcquireResult metal4_swapchain_acquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.acquire");

		auto * swapchain							  = static_cast<Metal4Swapchain *>(impl);
		Metal4Device * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		CA::MetalDrawable * drawable = swapchain->layer->nextDrawable();
		if (drawable == nullptr)
		{
			fail(error, ErrorCode::eNativeApiError, "CAMetalLayer nextDrawable returned null");
			return AcquireResult{ .status = SwapchainStatus::eOutOfDate };
		}

		swapchain->currentDrawable = NS::RetainPtr(drawable);

		if (MTL4::CommandQueue * queue = device->command_queue_for(QueueType::eGraphics); queue != nullptr)
		{
			queue->wait(drawable);
		}

		{
			NS::SharedPtr<MTL::Texture> texture = NS::RetainPtr(drawable->texture());

			if (Metal4TextureSlot * slot = device->textures.resolve(swapchain->backBuffer, false); slot != nullptr)
			{
				slot->texture = texture;
				slot->format  = swapchain->format;
			}

			if (Metal4TextureViewSlot * view = device->textureViews.resolve(swapchain->backBufferView, false); view != nullptr)
			{
				view->texture = std::move(texture);
			}
		}

		const std::uint32_t imageIndex = swapchain->frameCursor;
		swapchain->frameCursor		   = (swapchain->frameCursor + 1) % swapchain->imageCount;

		succeed(error);
		return AcquireResult{
			.status			= SwapchainStatus::eOk,
			.imageIndex		= imageIndex,
			.imageAvailable = swapchain->imageAvailable,
		};
	}

	PresentResult metal4_swapchain_present(
		void * impl,
		[[maybe_unused]] std::uint32_t imageIndex,
		BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.present");

		auto * swapchain							  = static_cast<Metal4Swapchain *>(impl);
		Metal4Device * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (!swapchain->currentDrawable)
		{
			fail(error, ErrorCode::eInvalidState, "Metal present called without an acquired drawable");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		MTL4::CommandQueue * presentQueue = device->command_queue_for(QueueType::eGraphics);
		if (presentQueue == nullptr)
		{
			fail(error, ErrorCode::eInvalidState, "Metal present requires a graphics queue");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		if (const auto * tracked = device->binarySemaphores.resolve(renderFinished, kHandleAlreadyChecked); tracked != nullptr)
		{
			presentQueue->wait(tracked->event.get(), tracked->value);
		}

		presentQueue->signalDrawable(swapchain->currentDrawable.get());
		swapchain->currentDrawable->present();
		swapchain->currentDrawable.reset();

		succeed(error);
		return PresentResult{ .status = SwapchainStatus::eOk };
	}

	TextureHandle metal4_swapchain_back_buffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->backBuffer;
	}

	TextureViewHandle metal4_swapchain_back_buffer_view(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->backBufferView;
	}

	BinarySemaphoreHandle metal4_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * swapchain = static_cast<Metal4Swapchain *>(impl);

		if (swapchain->presentSemaphores.empty())
		{
			return {};
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the wrap above is what bounds this.
		return swapchain->presentSemaphores[imageIndex % swapchain->presentSemaphores.size()];
	}

	Format metal4_swapchain_format(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->format;
	}

	bool metal4_swapchain_supports_readback([[maybe_unused]] void * impl) noexcept
	{
		return true;
	}

	std::uint32_t metal4_swapchain_image_count(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->imageCount;
	}

	std::uint32_t metal4_swapchain_width(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->width;
	}

	std::uint32_t metal4_swapchain_height(void * impl) noexcept
	{
		return static_cast<Metal4Swapchain *>(impl)->height;
	}

	bool metal4_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		auto * swapchain  = static_cast<Metal4Swapchain *>(impl);
		swapchain->width  = width;
		swapchain->height = height;
		swapchain->layer->setDrawableSize(CGSize{ .width = static_cast<CGFloat>(width), .height = static_cast<CGFloat>(height) });
		return succeed(error);
	}

	namespace
	{
		[[nodiscard]] PresentMode effective_present_mode(PresentMode mode) noexcept
		{
			return mode == PresentMode::eImmediate ? PresentMode::eImmediate : PresentMode::eFifo;
		}

		PresentMode metal4_swapchain_get_present_mode(void * impl) noexcept
		{
			return static_cast<Metal4Swapchain *>(impl)->presentMode;
		}
	}

	bool metal4_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain	   = static_cast<Metal4Swapchain *>(impl);
		swapchain->presentMode = effective_present_mode(mode);
		swapchain->layer->setDisplaySyncEnabled(swapchain->presentMode != PresentMode::eImmediate);
		return succeed(error);
	}

	const SwapchainApi & swapchain_block() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &metal4_swapchain_acquire,
			.present					 = &metal4_swapchain_present,
			.getBackBuffer				 = &metal4_swapchain_back_buffer,
			.getBackBufferView			 = &metal4_swapchain_back_buffer_view,
			.getPerImagePresentSemaphore = &metal4_swapchain_present_semaphore,
			.getFormat					 = &metal4_swapchain_format,
			.getPresentMode				 = &metal4_swapchain_get_present_mode,
			.getImageCount				 = &metal4_swapchain_image_count,
			.getWidth					 = &metal4_swapchain_width,
			.getHeight					 = &metal4_swapchain_height,
			.resize						 = &metal4_swapchain_resize,
			.setPresentMode				 = &metal4_swapchain_set_present_mode,
			.supportsReadback			 = &metal4_swapchain_supports_readback,
		};

		return block;
	}

	void * metal4_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createSwapchain");

		auto * device								  = static_cast<Metal4Device *>(impl);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// The surface handle carries the CAMetalLayer pointer the presentation backend made from the window. NOLINTNEXTLINE(performance-no-int-to-ptr)
		auto * layer = reinterpret_cast<CA::MetalLayer *>(static_cast<std::uintptr_t>(desc.surface.value));
		if (layer == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eInvalidArgument, "Metal swapchain requires a CAMetalLayer surface");
		}

		const std::uint32_t imageCount = desc.imageCount == 0 ? 3 : desc.imageCount;

		layer->setDevice(device->device.get());
		layer->setPixelFormat(metal_pixel_format(desc.preferredFormat));
		layer->setDrawableSize(CGSize{ .width = static_cast<CGFloat>(desc.width), .height = static_cast<CGFloat>(desc.height) });
		layer->setFramebufferOnly(false);
		const PresentMode presentMode = effective_present_mode(desc.presentMode);
		layer->setDisplaySyncEnabled(presentMode != PresentMode::eImmediate);

		auto swapchain			  = host_new<Metal4Swapchain>();
		swapchain->object		  = publishing_object<Published<SwapchainApi, &swapchain_block>>();
		swapchain->owner		  = device;
		swapchain->layer		  = layer;
		swapchain->format		  = desc.preferredFormat;
		swapchain->presentMode	  = presentMode;
		swapchain->width		  = desc.width;
		swapchain->height		  = desc.height;
		swapchain->imageCount	  = imageCount;
		swapchain->backBuffer	  = device->textures.store(Metal4TextureSlot{ .format = swapchain->format, .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->backBufferView = device->textureViews.store(Metal4TextureViewSlot{ .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->imageAvailable = metal4_create_binary_semaphore(device, BinarySemaphoreDesc{}, nullptr);
		swapchain->presentSemaphores.reserve(imageCount);
		for (std::uint32_t i = 0; i < imageCount; ++i)
		{
			swapchain->presentSemaphores.push_back(metal4_create_binary_semaphore(device, BinarySemaphoreDesc{}, nullptr));
		}

		Metal4Swapchain * raw = swapchain.get();
		{
			device->swapchains.push_back(std::move(swapchain));
		}
		return return_value<void *>(raw, error);
	}

}

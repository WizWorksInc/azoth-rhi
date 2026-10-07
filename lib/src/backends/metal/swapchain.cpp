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

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <CoreFoundation/CFCGTypes.h>

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLCommandQueue.hpp>
#include <Metal/MTLTexture.hpp>
#include <QuartzCore/CAMetalDrawable.hpp>

#include <cstdint> // NOLINT
#include <utility>

namespace azo::rhi::metal
{
	AcquireResult metal_swapchain_acquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.acquire");

		auto * swapchain							  = static_cast<MetalSwapchain *>(impl);
		MetalDevice * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		CA::MetalDrawable * drawable = swapchain->layer->nextDrawable();
		if (drawable == nullptr)
		{
			fail(error, ErrorCode::eNativeApiError, "CAMetalLayer nextDrawable returned null");
			return AcquireResult{ .status = SwapchainStatus::eOutOfDate };
		}

		swapchain->currentDrawable = NS::RetainPtr(drawable);

		{
			NS::SharedPtr<MTL::Texture> texture = NS::RetainPtr(drawable->texture());

			if (MetalTextureSlot * slot = device->textures.resolve(swapchain->backBuffer, false); slot != nullptr)
			{
				slot->texture = texture;
				slot->format  = swapchain->format;
			}

			if (MetalTextureViewSlot * view = device->textureViews.resolve(swapchain->backBufferView, false); view != nullptr)
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

	PresentResult metal_swapchain_present(
		void * impl,
		[[maybe_unused]] std::uint32_t imageIndex,
		BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.present");

		auto * swapchain							  = static_cast<MetalSwapchain *>(impl);
		MetalDevice * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (!swapchain->currentDrawable)
		{
			fail(error, ErrorCode::eInvalidState, "Metal present called without an acquired drawable");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		MTL::CommandQueue * presentQueue = device->command_queue_for(QueueType::eGraphics);
		if (presentQueue == nullptr)
		{
			fail(error, ErrorCode::eInvalidState, "Metal present requires a graphics queue");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		MTL::CommandBuffer * commandBuffer = presentQueue->commandBuffer();
		{
			const auto * tracked = device->binarySemaphores.resolve(renderFinished, kHandleAlreadyChecked);
			if (tracked != nullptr)
			{
				commandBuffer->encodeWait(tracked->event.get(), tracked->value);
			}
		}
		commandBuffer->presentDrawable(swapchain->currentDrawable.get());
		commandBuffer->commit();
		swapchain->currentDrawable.reset();

		succeed(error);
		return PresentResult{ .status = SwapchainStatus::eOk };
	}

	TextureHandle metal_swapchain_back_buffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->backBuffer;
	}

	TextureViewHandle metal_swapchain_back_buffer_view(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->backBufferView;
	}

	BinarySemaphoreHandle metal_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * swapchain = static_cast<MetalSwapchain *>(impl);

		if (swapchain->presentSemaphores.empty())
		{
			return {};
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the wrap above is what bounds this.
		return swapchain->presentSemaphores[imageIndex % swapchain->presentSemaphores.size()];
	}

	Format metal_swapchain_format(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->format;
	}

	bool metal_swapchain_supports_readback([[maybe_unused]] void * impl) noexcept
	{
		return true;
	}

	std::uint32_t metal_swapchain_image_count(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->imageCount;
	}

	std::uint32_t metal_swapchain_width(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->width;
	}

	std::uint32_t metal_swapchain_height(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->height;
	}

	bool metal_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		auto * swapchain  = static_cast<MetalSwapchain *>(impl);
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

		PresentMode metal_swapchain_get_present_mode(void * impl) noexcept
		{
			return static_cast<MetalSwapchain *>(impl)->presentMode;
		}
	} // namespace

	bool metal_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain	   = static_cast<MetalSwapchain *>(impl);
		swapchain->presentMode = effective_present_mode(mode);
		swapchain->layer->setDisplaySyncEnabled(swapchain->presentMode != PresentMode::eImmediate);
		return succeed(error);
	}

	const SwapchainApi & swapchain_block() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &metal_swapchain_acquire,
			.present					 = &metal_swapchain_present,
			.getBackBuffer				 = &metal_swapchain_back_buffer,
			.getBackBufferView			 = &metal_swapchain_back_buffer_view,
			.getPerImagePresentSemaphore = &metal_swapchain_present_semaphore,
			.getFormat					 = &metal_swapchain_format,
			.getPresentMode				 = &metal_swapchain_get_present_mode,
			.getImageCount				 = &metal_swapchain_image_count,
			.getWidth					 = &metal_swapchain_width,
			.getHeight					 = &metal_swapchain_height,
			.resize						 = &metal_swapchain_resize,
			.setPresentMode				 = &metal_swapchain_set_present_mode,
			.supportsReadback			 = &metal_swapchain_supports_readback,
		};

		return block;
	}

	void * metal_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createSwapchain");

		auto * device								  = static_cast<MetalDevice *>(impl);
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

		auto swapchain			  = host_new<MetalSwapchain>();
		swapchain->object		  = publishing_object<Published<SwapchainApi, &swapchain_block>>();
		swapchain->owner		  = device;
		swapchain->layer		  = layer;
		swapchain->format		  = desc.preferredFormat;
		swapchain->presentMode	  = presentMode;
		swapchain->width		  = desc.width;
		swapchain->height		  = desc.height;
		swapchain->imageCount	  = imageCount;
		swapchain->backBuffer	  = device->textures.store(MetalTextureSlot{ .format = swapchain->format, .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->backBufferView = device->textureViews.store(MetalTextureViewSlot{ .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->imageAvailable = metal_create_binary_semaphore(device, BinarySemaphoreDesc{}, nullptr);
		swapchain->presentSemaphores.reserve(imageCount);
		for (std::uint32_t i = 0; i < imageCount; ++i)
		{
			swapchain->presentSemaphores.push_back(metal_create_binary_semaphore(device, BinarySemaphoreDesc{}, nullptr));
		}

		MetalSwapchain * raw = swapchain.get();
		{
			device->swapchains.push_back(std::move(swapchain));
		}
		return return_value<void *>(raw, error);
	}

} // namespace azo::rhi::metal

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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	AcquireResult MetalSwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.acquire");

		auto * swapchain							  = static_cast<MetalSwapchain *>(impl);
		MetalDevice * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		CA::MetalDrawable * drawable = swapchain->layer->nextDrawable();
		if (drawable == nullptr)
		{
			Fail(error, ErrorCode::eNativeApiError, "CAMetalLayer nextDrawable returned null");
			return AcquireResult{ .status = SwapchainStatus::eOutOfDate };
		}

		swapchain->currentDrawable = NS::RetainPtr(drawable);

		/*
		 * Re-point the persistent back-buffer handle at this frame's drawable texture so recording and barriers resolve it. The drawable owns the texture so retain
		 * it for as long as the slot holds it.
		 *
		 * The slots were claimed once when the swapchain was created and are written in place here, not stored afresh, which is what keeps the handle the caller
		 * holds naming the same slot every frame. Nothing else writes them and acquire belongs to the one thread driving presentation.
		 */
		{
			NS::SharedPtr<MTL::Texture> texture = NS::RetainPtr(drawable->texture());

			if (MetalTextureSlot * slot = device->textures.Resolve(swapchain->backBuffer, false); slot != nullptr)
			{
				slot->texture = texture;
				slot->format  = swapchain->format;
			}

			if (NS::SharedPtr<MTL::Texture> * view = device->textureViews.Resolve(swapchain->backBufferView, false); view != nullptr)
			{
				*view = texture;
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

	PresentResult MetalSwapchainPresent(
		void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, [[maybe_unused]] void * queueImpl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.present");

		auto * swapchain							  = static_cast<MetalSwapchain *>(impl);
		MetalDevice * device						  = swapchain->owner;
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (!swapchain->currentDrawable)
		{
			Fail(error, ErrorCode::eInvalidState, "Metal present called without an acquired drawable");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		// Present on its own command buffer that waits for the render-finished semaphore the submit signaled so the drawable is shown only after its frame has
		// rendered. Presentation runs on the graphics queue.
		MTL::CommandQueue * presentQueue = device->CommandQueueFor(QueueType::eGraphics);
		if (presentQueue == nullptr)
		{
			Fail(error, ErrorCode::eInvalidState, "Metal present requires a graphics queue");
			return PresentResult{ .status = SwapchainStatus::eError };
		}

		MTL::CommandBuffer * commandBuffer = presentQueue->commandBuffer();
		{
			const auto * tracked = device->binarySemaphores.Resolve(renderFinished, kHandleAlreadyChecked);
			if (tracked != nullptr)
			{
				commandBuffer->encodeWait(tracked->event.get(), tracked->value);
			}
		}
		commandBuffer->presentDrawable(swapchain->currentDrawable.get());
		commandBuffer->commit();
		swapchain->currentDrawable.reset();

		Succeed(error);
		return PresentResult{ .status = SwapchainStatus::eOk };
	}

	TextureHandle MetalSwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->backBuffer;
	}

	TextureViewHandle MetalSwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->backBufferView;
	}

	BinarySemaphoreHandle MetalSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * swapchain = static_cast<MetalSwapchain *>(impl);

		// Creation makes one per image and never fewer than one, so this only keeps the wrap below from dividing by zero on a swapchain that failed part way.
		if (swapchain->presentSemaphores.empty())
		{
			return {};
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the wrap above is what bounds this.
		return swapchain->presentSemaphores[imageIndex % swapchain->presentSemaphores.size()];
	}

	Format MetalSwapchainFormat(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->format;
	}

	bool MetalSwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept
	{
		// The layer is created with framebufferOnly off so drawable textures can be copied for capture.
		return true;
	}

	std::uint32_t MetalSwapchainImageCount(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->imageCount;
	}

	std::uint32_t MetalSwapchainWidth(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->width;
	}

	std::uint32_t MetalSwapchainHeight(void * impl) noexcept
	{
		return static_cast<MetalSwapchain *>(impl)->height;
	}

	bool MetalSwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		auto * swapchain  = static_cast<MetalSwapchain *>(impl);
		swapchain->width  = width;
		swapchain->height = height;
		swapchain->layer->setDrawableSize(CGSize{ static_cast<CGFloat>(width), static_cast<CGFloat>(height) });
		return Succeed(error);
	}

	namespace
	{
		// Metal presentation sync is a single layer property so the modes that describe how frames queue up have no equivalent here. Mailbox and relaxed FIFO both
		// collapse to plain FIFO and only immediate turns display sync off.
		[[nodiscard]] PresentMode EffectivePresentMode(PresentMode mode) noexcept
		{
			return mode == PresentMode::eImmediate ? PresentMode::eImmediate : PresentMode::eFifo;
		}

		PresentMode MetalSwapchainGetPresentMode(void * impl) noexcept
		{
			return static_cast<MetalSwapchain *>(impl)->presentMode;
		}
	} // namespace

	bool MetalSwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept
	{
		auto * swapchain	   = static_cast<MetalSwapchain *>(impl);
		swapchain->presentMode = EffectivePresentMode(mode);
		swapchain->layer->setDisplaySyncEnabled(swapchain->presentMode != PresentMode::eImmediate);
		return Succeed(error);
	}

	const SwapchainApi & SwapchainBlock() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &MetalSwapchainAcquire,
			.present					 = &MetalSwapchainPresent,
			.getBackBuffer				 = &MetalSwapchainBackBuffer,
			.getBackBufferView			 = &MetalSwapchainBackBufferView,
			.getPerImagePresentSemaphore = &MetalSwapchainPresentSemaphore,
			.getFormat					 = &MetalSwapchainFormat,
			.getPresentMode				 = &MetalSwapchainGetPresentMode,
			.getImageCount				 = &MetalSwapchainImageCount,
			.getWidth					 = &MetalSwapchainWidth,
			.getHeight					 = &MetalSwapchainHeight,
			.resize						 = &MetalSwapchainResize,
			.setPresentMode				 = &MetalSwapchainSetPresentMode,
			.supportsReadback			 = &MetalSwapchainSupportsReadback,
		};

		return block;
	}

	void * MetalCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createSwapchain");

		auto * device								  = static_cast<MetalDevice *>(impl);
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
		// Drawables default to framebuffer-only, which blocks frame-capture copies. Allow copying out.
		layer->setFramebufferOnly(false);
		const PresentMode presentMode = EffectivePresentMode(desc.presentMode);
		layer->setDisplaySyncEnabled(presentMode != PresentMode::eImmediate);

		auto swapchain		   = HostNew<MetalSwapchain>();
		swapchain->object	   = PublishingObject<Published<SwapchainApi, &SwapchainBlock>>();
		swapchain->owner	   = device;
		swapchain->layer	   = layer;
		swapchain->format	   = desc.preferredFormat;
		swapchain->presentMode = presentMode;
		swapchain->width	   = desc.width;
		swapchain->height	   = desc.height;
		swapchain->imageCount  = imageCount;
		// Claimed from the tables that hold them so acquire has a slot to write each frame and a caller holding one of these handles resolves it the same way it
		// resolves any other texture.
		swapchain->backBuffer	  = device->textures.Store(MetalTextureSlot{ .format = swapchain->format, .lifetime = SlotLifetime::eSwapchainBorrowed });
		swapchain->backBufferView = device->textureViews.Store(NS::SharedPtr<MTL::Texture>{});
		swapchain->imageAvailable = MetalCreateBinarySemaphore(device, BinarySemaphoreDesc{}, nullptr);
		swapchain->presentSemaphores.reserve(imageCount);
		for (std::uint32_t i = 0; i < imageCount; ++i)
		{
			swapchain->presentSemaphores.push_back(MetalCreateBinarySemaphore(device, BinarySemaphoreDesc{}, nullptr));
		}

		MetalSwapchain * raw = swapchain.get();
		{
			device->swapchains.push_back(std::move(swapchain));
		}
		return ReturnValue<void *>(raw, error);
	}

} // namespace azo::rhi::metal

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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	[[nodiscard]] DXGI_FORMAT StripSrgbFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
		default:							  return format;
		}
	}

	[[nodiscard]] SwapchainStatus MapPresentStatus(HRESULT hr) noexcept
	{
		if (hr == S_OK)
		{
			return SwapchainStatus::eOk;
		}
		if (hr == DXGI_STATUS_OCCLUDED)
		{
			return SwapchainStatus::eSuboptimal;
		}
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			return SwapchainStatus::eDeviceLost;
		}
		return SwapchainStatus::eError;
	}

	// Registers or, after a resize, refreshes the back buffers as borrowed texture and view slots, rebuilding an sRGB-capable RTV over each. Holds both
	// registry mutexes because it touches both registries and the device RTV heap.
	bool BuildSwapchainBackBuffers(D3D12Swapchain * sc, Error * error) noexcept
	{
		D3D12Device * device  = sc->owner;
		const bool firstBuild = sc->backBuffers.empty();

		for (std::uint32_t i = 0; i < sc->imageCount; ++i)
		{
			ComPtr<ID3D12Resource> resource;
			if (FAILED(sc->swapchain->GetBuffer(i, IID_PPV_ARGS(resource.GetAddressOf()))))
			{
				return Fail(error, ErrorCode::eNativeApiError, "IDXGISwapChain::GetBuffer failed");
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format		  = sc->viewFormat;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

			if (firstBuild)
			{
				const std::uint32_t rtvIndex = device->rtvHeap.Allocate();
				if (rtvIndex == kInvalidIndex)
				{
					return Fail(error, ErrorCode::eOutOfMemory, "out of RTV descriptors for the swapchain back buffers");
				}
				device->device->CreateRenderTargetView(resource.Get(), &rtvDesc, device->rtvHeap.Handle(rtvIndex));

				const TextureHandle texture	 = device->textureSlots.Store(TextureSlot{ .resource = resource,
					.format																		= sc->viewFormat,
					.type																		= TextureType::eTex2D,
					.mipLevels																	= 1,
					.arrayLayers																= 1,
					.usage																		= Flags<TextureUsage>(TextureUsage::eColorAttachment),
					.lifetime																	= SlotLifetime::eSwapchainBorrowed });
				const TextureViewHandle view = device->textureViewSlots.Store(TextureViewSlot{ .texture = texture,
					.format																				= sc->viewFormat,
					.type																				= TextureViewType::eTex2D,
					.rtvIndex																			= rtvIndex,
					.lifetime																			= SlotLifetime::eSwapchainBorrowed });
				sc->backBuffers.push_back(texture);
				sc->backBufferViews.push_back(view);
			}
			else
			{
				static_cast<void>(device->textureSlots.Retire(sc->backBuffers[i], true));
				sc->backBuffers[i] = device->textureSlots.Store(TextureSlot{ .resource = resource,
					.format															   = sc->viewFormat,
					.type															   = TextureType::eTex2D,
					.mipLevels														   = 1,
					.arrayLayers													   = 1,
					.usage															   = Flags<TextureUsage>(TextureUsage::eColorAttachment),
					.lifetime														   = SlotLifetime::eSwapchainBorrowed });

				// The new back-buffer view reuses the existing RTV descriptor, recreated over the new resource.
				const TextureViewSlot * const oldView = device->textureViewSlots.Resolve(sc->backBufferViews[i], true);
				const std::uint32_t rtvIndex		  = oldView != nullptr ? oldView->rtvIndex : kInvalidIndex;
				device->device->CreateRenderTargetView(resource.Get(), &rtvDesc, device->rtvHeap.Handle(rtvIndex));
				static_cast<void>(device->textureViewSlots.Retire(sc->backBufferViews[i], true));
				// Point the view's texture handle at the back buffer's new generation, else binding it as a shader resource fails after a resize.
				sc->backBufferViews[i] = device->textureViewSlots.Store(TextureViewSlot{ .texture = sc->backBuffers[i],
					.format																		  = sc->viewFormat,
					.type																		  = TextureViewType::eTex2D,
					.rtvIndex																	  = rtvIndex,
					.lifetime																	  = SlotLifetime::eSwapchainBorrowed });
			}
		}
		return Succeed(error);
	}

	void * D3D12CreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createSwapchain");

		auto * device = static_cast<D3D12Device *>(impl);
		if (!device->factory)
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "the device has no DXGI factory for swapchain creation");
		}

		auto sc				= HostNew<D3D12Swapchain>();
		sc->object			= PublishingObject<Published<SwapchainApi, &SwapchainBlock>>();
		sc->owner			= device;
		sc->hwnd			= reinterpret_cast<HWND>(static_cast<std::uintptr_t>(desc.surface.value)); // NOLINT(performance-no-int-to-ptr)
		sc->width			= std::max<std::uint32_t>(desc.width, 1);
		sc->height			= std::max<std::uint32_t>(desc.height, 1);
		sc->imageCount		= std::max<std::uint32_t>(desc.imageCount, 2);
		sc->format			= desc.preferredFormat;
		sc->viewFormat		= MapFormat(desc.preferredFormat);
		sc->swapchainFormat = StripSrgbFormat(sc->viewFormat);

		BOOL tearingSupported = FALSE;
		if (FAILED(device->factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported))))
		{
			tearingSupported = FALSE;
		}
		sc->allowTearing		= desc.allowTearing && tearingSupported != FALSE;
		sc->presentSyncInterval = desc.presentMode == PresentMode::eImmediate ? 0 : 1;
		sc->presentFlags		= (sc->allowTearing && sc->presentSyncInterval == 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;

		DXGI_SWAP_CHAIN_DESC1 scDesc{};
		scDesc.Width			= sc->width;
		scDesc.Height			= sc->height;
		scDesc.Format			= sc->swapchainFormat;
		scDesc.SampleDesc.Count = 1;
		scDesc.BufferUsage		= DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.BufferCount		= sc->imageCount;
		scDesc.SwapEffect		= DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scDesc.AlphaMode		= DXGI_ALPHA_MODE_UNSPECIFIED;
		scDesc.Scaling			= DXGI_SCALING_STRETCH;
		scDesc.Flags			= sc->allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

		if (device->graphicsQueues.empty())
		{
			return FailValue<void *>(error, ErrorCode::eInvalidState, "swapchain creation requires a graphics queue");
		}

		ComPtr<IDXGISwapChain1> swapchain1;
		if (FAILED(device->factory->CreateSwapChainForHwnd(
				device->graphicsQueues.front().queue.Get(), sc->hwnd, &scDesc, nullptr, nullptr, swapchain1.GetAddressOf())))
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "IDXGIFactory::CreateSwapChainForHwnd failed");
		}
		device->factory->MakeWindowAssociation(sc->hwnd, DXGI_MWA_NO_ALT_ENTER);
		if (FAILED(swapchain1.As(&sc->swapchain)))
		{
			return FailValue<void *>(error, ErrorCode::eNativeApiError, "IDXGISwapChain3 is unavailable");
		}

		if (!BuildSwapchainBackBuffers(sc.get(), error))
		{
			return nullptr;
		}

		D3D12Swapchain * raw = sc.get();
		device->swapchains.push_back(std::move(sc));
		Succeed(error);
		return raw;
	}

	AcquireResult D3D12SwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.acquire");

		auto * sc = static_cast<D3D12Swapchain *>(impl);
		Succeed(error);
		// Flip-model acquire is immediate and has no semaphore: reuse is ordered by the present queue and the caller's frame fence.
		return AcquireResult{
			.status			= SwapchainStatus::eOk,
			.imageIndex		= sc->swapchain->GetCurrentBackBufferIndex(),
			.imageAvailable = {},
		};
	}

	PresentResult D3D12SwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, [[maybe_unused]] BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.present");

		auto * sc					 = static_cast<D3D12Swapchain *>(impl);
		const HRESULT hr			 = sc->swapchain->Present(sc->presentSyncInterval, sc->presentFlags);
		const SwapchainStatus status = MapPresentStatus(hr);
		if (status == SwapchainStatus::eDeviceLost || status == SwapchainStatus::eError)
		{
			Fail(error, ErrorCode::eNativeApiError, "IDXGISwapChain::Present failed");
		}
		else
		{
			Succeed(error);
		}
		return PresentResult{ .status = status };
	}

	bool D3D12SwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept
	{
		// Flip-model back buffers can be used as a copy source so frame capture can read them back.
		return true;
	}

	bool D3D12SwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.swapchain.resize");

		auto * sc			 = static_cast<D3D12Swapchain *>(impl);
		D3D12Device * device = sc->owner;

		// ResizeBuffers requires every back-buffer reference released first.
		{
			for (const TextureHandle handle : sc->backBuffers)
			{
				if (TextureSlot * const slot = device->textureSlots.Resolve(handle, false); slot != nullptr)
				{
					slot->resource.Reset();
				}
			}
		}

		sc->width		 = std::max<std::uint32_t>(width, 1);
		sc->height		 = std::max<std::uint32_t>(height, 1);
		const UINT flags = sc->allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
		if (FAILED(sc->swapchain->ResizeBuffers(sc->imageCount, sc->width, sc->height, sc->swapchainFormat, flags)))
		{
			return Fail(error, ErrorCode::eNativeApiError, "IDXGISwapChain::ResizeBuffers failed");
		}
		return BuildSwapchainBackBuffers(sc, error);
	}

	bool D3D12SwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept
	{
		// Both are read on every Present so this takes effect next frame with no recreate. Tearing applies only if the swapchain allowed it.
		auto * sc				= static_cast<D3D12Swapchain *>(impl);
		sc->presentSyncInterval = mode == PresentMode::eImmediate ? 0 : 1;
		sc->presentFlags		= (sc->allowTearing && sc->presentSyncInterval == 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		return Succeed(error);
	}

	TextureHandle D3D12SwapchainGetBackBuffer(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * sc = static_cast<D3D12Swapchain *>(impl);
		return imageIndex < sc->backBuffers.size() ? sc->backBuffers[imageIndex] : TextureHandle{};
	}

	TextureViewHandle D3D12SwapchainGetBackBufferView(void * impl, std::uint32_t imageIndex) noexcept
	{
		auto * sc = static_cast<D3D12Swapchain *>(impl);
		return imageIndex < sc->backBufferViews.size() ? sc->backBufferViews[imageIndex] : TextureViewHandle{};
	}

	BinarySemaphoreHandle D3D12SwapchainGetPresentSemaphore([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept
	{
		// Flip-model present ordering needs no per-image binary semaphore.
		return {};
	}

	Format D3D12SwapchainGetFormat(void * impl) noexcept
	{
		return static_cast<D3D12Swapchain *>(impl)->format;
	}

	PresentMode D3D12SwapchainGetPresentMode(void * impl) noexcept
	{
		// DXGI has no mailbox or relaxed equivalent so the sync interval is the whole story: 0 presents without waiting, 1 is FIFO.
		return static_cast<D3D12Swapchain *>(impl)->presentSyncInterval == 0 ? PresentMode::eImmediate : PresentMode::eFifo;
	}

	std::uint32_t D3D12SwapchainGetImageCount(void * impl) noexcept
	{
		return static_cast<D3D12Swapchain *>(impl)->imageCount;
	}

	std::uint32_t D3D12SwapchainGetWidth(void * impl) noexcept
	{
		return static_cast<D3D12Swapchain *>(impl)->width;
	}

	std::uint32_t D3D12SwapchainGetHeight(void * impl) noexcept
	{
		return static_cast<D3D12Swapchain *>(impl)->height;
	}

	// Correlates a GPU timestamp with the CPU clock through GetClockCalibration, which is native here, not capability-gated.

} // namespace azo::rhi::d3d12

#endif // _WIN32

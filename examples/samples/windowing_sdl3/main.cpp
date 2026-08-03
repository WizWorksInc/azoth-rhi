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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include "FW/utility/Log.hpp"
#include "native/sdl_natives.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <span>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	// A slow trip around the color wheel so a window that is presenting looks different from one that is stuck.
	[[nodiscard]] rhi::ClearColor FrameColor(const std::uint64_t frame)
	{
		constexpr float kThird = 2.0f * std::numbers::pi_v<float> / 3.0f;
		const float phase	   = static_cast<float>(frame % 240) / 240.0f * 2.0f * std::numbers::pi_v<float>;

		return rhi::ClearColor{
			.r = 0.5f + (0.5f * std::sin(phase)),
			.g = 0.5f + (0.5f * std::sin(phase + kThird)),
			.b = 0.5f + (0.5f * std::sin(phase + (2.0f * kThird))),
			.a = 1.0f,
		};
	}

	/*
	 * The SDL3 window as an rhi::SurfaceSource, which is the whole of what the RHI wants to know about it.
	 *
	 * Written out here, not taken from fw::platform::Sdl3Window, which every other windowed sample links, because showing how a window answers the surface
	 * interface is what this sample is for. The others have nothing to teach about it and share one copy.
	 */
	class Window final : public rhi::SurfaceSource
	{
	public:
		Window()						   = default;
		Window(const Window &)			   = delete;
		Window & operator=(const Window &) = delete;
		Window(Window &&)				   = delete;
		Window & operator=(Window &&)	   = delete;

		~Window() override
		{
			// After the device is gone, since the RHI destroyed its surface with it.
			if (m_metalView != nullptr)
			{
				SDL_Metal_DestroyView(m_metalView);
			}
			if (m_window != nullptr)
			{
				SDL_DestroyWindow(m_window);
			}
			SDL_Quit();
		}

		// SDL wants to know which API the window is for before it makes one so the choice is already settled here.
		[[nodiscard]] bool Open(const rhi::GraphicsApiId api)
		{
			if (!SDL_Init(SDL_INIT_VIDEO))
			{
				LOG_ERROR(fw::Log(), "SDL_Init failed: {}", SDL_GetError());
				return false;
			}

			SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
			if (api == rhi::VulkanApi::id)
			{
				flags |= SDL_WINDOW_VULKAN;
			}
			else if (rhi::IsMetalFamily(api))
			{
				flags |= SDL_WINDOW_METAL;
			}

			m_window = SDL_CreateWindow("AzothRHI sdl3", 1280, 720, flags);
			if (m_window == nullptr)
			{
				LOG_ERROR(fw::Log(), "SDL_CreateWindow failed: {}", SDL_GetError());
				return false;
			}

			// SDL reads the layer off a Metal view, not off the window.
			if (rhi::IsMetalFamily(api))
			{
				m_metalView = SDL_Metal_CreateView(m_window);
			}

			return true;
		}

		// Drains the event queue. False once the user has asked to close the window.
		[[nodiscard]] bool PumpEvents() const
		{
			SDL_Event event{};
			while (SDL_PollEvent(&event))
			{
				const bool closed = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window);
				const bool quit	  = event.type == SDL_EVENT_QUIT || closed || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
				if (quit)
				{
					return false;
				}
			}

			return true;
		}

		// The drawable size in pixels, which is not the window size on a scaled display.
		[[nodiscard]] rhi::Extent2D GetDrawableSize() const
		{
			int width  = 0;
			int height = 0;
			static_cast<void>(SDL_GetWindowSizeInPixels(m_window, &width, &height));

			return rhi::Extent2D{ .width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height) };
		}

		/*
		 * One method, one branch per payload this window can answer and no graphics type in sight. Creating the Vulkan surface is the one part that cannot be written
		 * without them so it lives in native/ where they are allowed.
		 */
		[[nodiscard]] bool Provide(const rhi::SurfaceRequest & request) override
		{
			if (auto * loader = rhi::SurfacePayloadOf<rhi::native::VulkanLoaderPayload>(request); loader != nullptr)
			{
				// SDL's own loader, which is the one the dispatcher has to be seeded from, since a surface SDL makes and an instance dispatched through anything else
				// belong to two different Vulkans.
				loader->getInstanceProcAddr = sdl_native::VulkanInstanceProcAddr();
				return loader->getInstanceProcAddr != nullptr;
			}

			if (auto * vulkan = rhi::SurfacePayloadOf<rhi::native::VulkanSurfacePayload>(request); vulkan != nullptr)
			{
				vulkan->surface = sdl_native::CreateVulkanSurface(m_window, vulkan->instance);
				return vulkan->surface != 0;
			}

			if (auto * metal = rhi::SurfacePayloadOf<rhi::native::MetalSurfacePayload>(request); metal != nullptr)
			{
				metal->layer = m_metalView != nullptr ? SDL_Metal_GetLayer(m_metalView) : nullptr;
				return metal->layer != nullptr;
			}

			if (auto * win32 = rhi::SurfacePayloadOf<rhi::native::Win32SurfacePayload>(request); win32 != nullptr)
			{
				win32->window = sdl_native::Win32WindowHandle(m_window);
				return win32->window != nullptr;
			}

			return false;
		}

	private:
		SDL_Window * m_window	  = nullptr;
		SDL_MetalView m_metalView = nullptr;
	};

} // namespace

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const std::uint64_t frameLimit = args.size() > 1 ? std::strtoull(args[1], nullptr, 10) : 0;

	/*
	 * One API, decided before anything else because the window has to be made for it and a Vulkan device is dispatched through the loader the window library
	 * brought up. AZOTH_RHI_BACKEND names the one to use on a build that has several and Null is left out since this sample presents.
	 */
	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };
	if (backends.IsEmpty())
	{
		LOG_INFO(fw::Log(), "this build has no backend that can present");
		return 1;
	}

	const rhi::GraphicsApiId api = backends.Preferred().front().id;

	Window window;
	if (!window.Open(api))
	{
		return 1;
	}

	// Before the device, since a Vulkan device is dispatched through the loader this seeds.
	const rhi::HostUniquePtr<rhi::PresentationBackend> presentation = rhi::MakePresentationBackend(api);
	if (presentation == nullptr || !presentation->InitInstanceLoader(window))
	{
		LOG_ERROR(fw::Log(), "this build cannot present through the backend it picked");
		return 1;
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("sdl3").GraphicsQueue().Build(backends.Registry(), backends.PreferredApis().first(1));
	if (!device)
	{
		LOG_ERROR(fw::Log(), "failed to create a device: {}", device.GetError().message != nullptr ? device.GetError().message : "no diagnostic");
		return 1;
	}

	rhi::Device dev = device.Value().Get();
	rhi::Error error{};

	// After the device, since a Vulkan surface is made on that device's instance and its teardown transfers to the device so the destruction order stays valid.
	const rhi::SurfaceHandle surface = presentation->CreateSurface(window, dev);
	const rhi::Extent2D initial		 = window.GetDrawableSize();

	rhi::Swapchain swapchain = dev.CreateSwapchain(
		rhi::SwapchainDesc{ .surface = surface, .width = initial.width, .height = initial.height, .debugName = "present.swapchain" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "present.timeline" }, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "present.pool" }, error);
	if (surface.value == 0 || !swapchain.IsValid() || !queue.IsValid() || !timeline.IsValid() || !pool.IsValid())
	{
		LOG_ERROR(fw::Log(), "failed to set up presentation: {}", error.message != nullptr ? error.message : "no diagnostic");
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}, {} images", dev.GetGraphicsApiName(), swapchain.GetWidth(), swapchain.GetHeight(), swapchain.GetImageCount());

	std::uint64_t frame = 0;
	while (window.PumpEvents())
	{
		if (frameLimit != 0 && frame >= frameLimit)
		{
			break;
		}

		// Two routes reach the same recreation: the window reporting a new size and the swapchain reporting that it no longer matches its surface.
		const rhi::Extent2D size = window.GetDrawableSize();
		if (size.width == 0 || size.height == 0)
		{
			continue;
		}

		if (size.width != swapchain.GetWidth() || size.height != swapchain.GetHeight())
		{
			// Resize invalidates acquired back buffers so nothing may still be in flight against them.
			static_cast<void>(queue.WaitIdle(error));
			if (!swapchain.Resize(size.width, size.height, error))
			{
				LOG_ERROR(fw::Log(), "failed to resize the swapchain");
				return 1;
			}
			LOG_INFO(fw::Log(), "resized to {}x{}", swapchain.GetWidth(), swapchain.GetHeight());
		}

		const rhi::AcquireResult acquired = swapchain.AcquireNextImage(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			static_cast<void>(queue.WaitIdle(error));
			static_cast<void>(swapchain.Resize(size.width, size.height, error));
			continue;
		}
		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		++frame;

		// Safe because the previous frame was waited on at the bottom of this loop.
		if (frame > 1 && !pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = frame - 1 }, error))
		{
			LOG_ERROR(fw::Log(), "failed to reset the command pool");
			return 1;
		}

		rhi::CommandList list = pool.Allocate("present.frame", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			LOG_ERROR(fw::Log(), "failed to start recording");
			return 1;
		}

		// A back buffer arrives undefined every frame, since its previous contents were presented and it has to leave in the present layout.
		const rhi::TextureHandle backBuffer = acquired.texture;
		const std::array toAttachment{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
			.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		} };
		const std::array toPresent{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
			.after	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::ePresent },
		} };
		const std::array colors{ rhi::RenderingAttachment{
			.view		= acquired.view,
			.state		= { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = FrameColor(frame),
		} };

		// Nothing is drawn. The clear is the whole frame, which is all it takes to prove that the window, the surface and the swapchain are wired together.
		const bool recorded =
			list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
			list.BeginRendering(rhi::BeginRenderingDesc{ .colors = colors, .width = swapchain.GetWidth(), .height = swapchain.GetHeight() }, error) &&
			list.EndRendering(error) && list.Barriers(rhi::BarrierBatch{ .textures = toPresent }, error) && list.End(error);
		if (!recorded)
		{
			LOG_ERROR(fw::Log(), "failed to record the frame: {}", error.message != nullptr ? error.message : "no diagnostic");
			return 1;
		}

		// The acquire semaphore says the image is ready to be written and the swapchain's own per image semaphore says this frame is done writing it. Presentation
		// waits on the second.

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array present{ rhi::SwapchainSync{ .acquired = acquired.imageAvailable, .renderFinished = acquired.renderFinished } };
		const std::array retire{ rhi::TimelinePoint{ .timeline = timeline, .value = frame } };

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = retire,
			.swapchains	  = present,
			.debugName	  = "present.submit",
		};

		if (!queue.Submit(submit, error))
		{
			LOG_ERROR(fw::Log(), "failed to submit the frame");
			return 1;
		}

		static_cast<void>(swapchain.Present(queue, acquired.imageIndex, acquired.renderFinished, error));

		// One frame at a time. See frame_pacing for the version that keeps the CPU ahead of the GPU.
		if (!queue.Wait(timeline, frame, kNoTimeout, error))
		{
			LOG_ERROR(fw::Log(), "failed to wait for the frame");
			return 1;
		}
	}

	static_cast<void>(queue.WaitIdle(error));
	dev.CollectGarbage(timeline, frame, error);

	LOG_INFO(fw::Log(), "{} frames presented", frame);
	return 0;
}

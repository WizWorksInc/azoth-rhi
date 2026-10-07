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

		[[nodiscard]] bool Open(const rhi::GraphicsApiId api)
		{
			if (!SDL_Init(SDL_INIT_VIDEO))
			{
				LOG_ERROR(fw::Log(), "SDL_Init failed: {}", SDL_GetError());
				return false;
			}

			SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
			if (api == rhi::VulkanApi::kId)
			{
				flags |= SDL_WINDOW_VULKAN;
			}
			else if (rhi::is_metal_family(api))
			{
				flags |= SDL_WINDOW_METAL;
			}

			m_window = SDL_CreateWindow("AzothRHI sdl3", 1280, 720, flags);
			if (m_window == nullptr)
			{
				LOG_ERROR(fw::Log(), "SDL_CreateWindow failed: {}", SDL_GetError());
				return false;
			}

			if (rhi::is_metal_family(api))
			{
				m_metalView = SDL_Metal_CreateView(m_window);
			}

			return true;
		}

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

		[[nodiscard]] rhi::Extent2D GetDrawableSize() const
		{
			int width  = 0;
			int height = 0;
			static_cast<void>(SDL_GetWindowSizeInPixels(m_window, &width, &height));

			return rhi::Extent2D{ .width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height) };
		}

		[[nodiscard]] bool provide(const rhi::SurfaceRequest & request) override
		{
			if (auto * loader = rhi::surface_payload_of<rhi::native::VulkanLoaderPayload>(request); loader != nullptr)
			{
				loader->getInstanceProcAddr = sdl_native::VulkanInstanceProcAddr();
				return loader->getInstanceProcAddr != nullptr;
			}

			if (auto * vulkan = rhi::surface_payload_of<rhi::native::VulkanSurfacePayload>(request); vulkan != nullptr)
			{
				vulkan->surface = sdl_native::CreateVulkanSurface(m_window, vulkan->instance);
				return vulkan->surface != 0;
			}

			if (auto * metal = rhi::surface_payload_of<rhi::native::MetalSurfacePayload>(request); metal != nullptr)
			{
				metal->layer = m_metalView != nullptr ? SDL_Metal_GetLayer(m_metalView) : nullptr;
				return metal->layer != nullptr;
			}

			if (auto * win32 = rhi::surface_payload_of<rhi::native::Win32SurfacePayload>(request); win32 != nullptr)
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

}

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const std::uint64_t frameLimit = args.size() > 1 ? std::strtoull(args[1], nullptr, 10) : 0;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };
	if (backends.is_empty())
	{
		LOG_INFO(fw::Log(), "this build has no backend that can present");
		return 1;
	}

	const rhi::GraphicsApiId api = backends.preferred().front().id;

	Window window;
	if (!window.Open(api))
	{
		return 1;
	}

	const rhi::HostUniquePtr<rhi::PresentationBackend> presentation = rhi::make_presentation_backend(api);
	if (presentation == nullptr || !presentation->init_instance_loader(window))
	{
		LOG_ERROR(fw::Log(), "this build cannot present through the backend it picked");
		return 1;
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().debug_name("sdl3").graphics_queue().build(backends.registry(), backends.preferred_apis().first(1));
	if (!device)
	{
		LOG_ERROR(fw::Log(), "failed to create a device: {}", device.get_error().message != nullptr ? device.get_error().message : "no diagnostic");
		return 1;
	}

	rhi::Device dev = device.value().get();
	rhi::Error error{};

	const rhi::SurfaceHandle surface = presentation->create_surface(window, dev);
	const rhi::Extent2D initial		 = window.GetDrawableSize();

	rhi::Swapchain swapchain = dev.create_swapchain(
		rhi::SwapchainDesc{ .surface = surface, .width = initial.width, .height = initial.height, .debugName = "present.swapchain" }, error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "present.timeline" }, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "present.pool" }, error);
	if (surface.value == 0 || !swapchain.is_valid() || !queue.is_valid() || !timeline.is_valid() || !pool.is_valid())
	{
		LOG_ERROR(fw::Log(), "failed to set up presentation: {}", error.message != nullptr ? error.message : "no diagnostic");
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}, {} images", dev.get_graphics_api_name(), swapchain.get_width(), swapchain.get_height(), swapchain.get_image_count());

	std::uint64_t frame = 0;
	while (window.PumpEvents())
	{
		if (frameLimit != 0 && frame >= frameLimit)
		{
			break;
		}

		const rhi::Extent2D size = window.GetDrawableSize();
		if (size.width == 0 || size.height == 0)
		{
			continue;
		}

		if (size.width != swapchain.get_width() || size.height != swapchain.get_height())
		{
			static_cast<void>(queue.wait_idle(error));
			if (!swapchain.resize(size.width, size.height, error))
			{
				LOG_ERROR(fw::Log(), "failed to resize the swapchain");
				return 1;
			}
			LOG_INFO(fw::Log(), "resized to {}x{}", swapchain.get_width(), swapchain.get_height());
		}

		const rhi::AcquireResult acquired = swapchain.acquire_next_image(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			static_cast<void>(queue.wait_idle(error));
			static_cast<void>(swapchain.resize(size.width, size.height, error));
			continue;
		}
		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		++frame;

		if (frame > 1 && !pool.reset(rhi::RetirePoint{ .timeline = timeline, .value = frame - 1 }, error))
		{
			LOG_ERROR(fw::Log(), "failed to reset the command pool");
			return 1;
		}

		rhi::CommandList list = pool.allocate("present.frame", error);
		if (!list.is_valid() || !list.begin(error))
		{
			LOG_ERROR(fw::Log(), "failed to start recording");
			return 1;
		}

		const rhi::TextureHandle backBuffer = acquired.texture;
		const std::array toAttachment{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .use = rhi::ResourceUse::eDiscard, .stages = rhi::Stage::eColorOutput },
			.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		} };
		const std::array toPresent{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.after	 = { .use = rhi::ResourceUse::ePresent },
		} };
		const std::array colors{ rhi::RenderingAttachment{
			.view		= acquired.view,
			.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = FrameColor(frame),
		} };

		const bool recorded =
			list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
			list.begin_rendering(rhi::BeginRenderingDesc{ .colors = colors, .width = swapchain.get_width(), .height = swapchain.get_height() }, error) &&
			list.end_rendering(error) && list.barriers(rhi::BarrierBatch{ .textures = toPresent }, error) && list.end(error);
		if (!recorded)
		{
			LOG_ERROR(fw::Log(), "failed to record the frame: {}", error.message != nullptr ? error.message : "no diagnostic");
			return 1;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array present{ rhi::SwapchainSync{ .acquired = acquired.imageAvailable, .renderFinished = acquired.renderFinished } };
		const std::array retire{ rhi::TimelinePoint{ .timeline = timeline, .value = frame } };

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = retire,
			.swapchains	  = present,
			.debugName	  = "present.submit",
		};

		if (!queue.submit(submit, error))
		{
			LOG_ERROR(fw::Log(), "failed to submit the frame");
			return 1;
		}

		static_cast<void>(swapchain.present(queue, acquired.imageIndex, acquired.renderFinished, error));

		if (!queue.wait(timeline, frame, kNoTimeout, error))
		{
			LOG_ERROR(fw::Log(), "failed to wait for the frame");
			return 1;
		}
	}

	static_cast<void>(queue.wait_idle(error));
	dev.collect_garbage(timeline, frame, error);

	LOG_INFO(fw::Log(), "{} frames presented", frame);
	return 0;
}

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

#include "FW/platform/Sdl3Window.hpp"

#include "FW/utility/Log.hpp"
#include "native/Sdl3Natives.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <azoth/rhi/device/api_tags.hpp>
#include <azoth/rhi/native/surface_payloads.hpp>

namespace fw::platform
{
	Sdl3Window::~Sdl3Window()
	{
		// After the device is gone, since the RHI destroyed the surface it made from this window along with it.
		if (m_metalView != nullptr)
		{
			SDL_Metal_DestroyView(static_cast<::SDL_MetalView>(m_metalView));
		}

		if (m_window != nullptr)
		{
			SDL_DestroyWindow(m_window);
		}

		SDL_Quit();
	}

	bool Sdl3Window::Open(const azo::rhi::GraphicsApiId api, const Sdl3WindowDesc & desc)
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			LOG_ERROR(fw::Log(), "SDL_Init failed: {}", SDL_GetError());
			return false;
		}

		// SDL wants to know which API the window is for before it makes one, so the choice is already settled here.
		SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (desc.resizable)
		{
			flags |= SDL_WINDOW_RESIZABLE;
		}

		if (api == azo::rhi::VulkanApi::id)
		{
			flags |= SDL_WINDOW_VULKAN;
		}
		else if (azo::rhi::IsMetalFamily(api))
		{
			flags |= SDL_WINDOW_METAL;
		}

		m_window = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width), static_cast<int>(desc.height), flags);
		if (m_window == nullptr)
		{
			LOG_ERROR(fw::Log(), "SDL_CreateWindow failed: {}", SDL_GetError());
			return false;
		}

		// SDL reads the layer off a Metal view, not off the window.
		if (azo::rhi::IsMetalFamily(api))
		{
			m_metalView = SDL_Metal_CreateView(m_window);
		}

		return true;
	}

	bool Sdl3Window::PumpEvents(const EventSink & onEvent)
	{
		const SDL_WindowID id = SDL_GetWindowID(m_window);

		SDL_Event event{};
		while (SDL_PollEvent(&event))
		{
			// Offered first, so whatever is listening sees an event even when it is the one that closes the window.
			if (onEvent)
			{
				onEvent(event);
			}

			if (event.type == SDL_EVENT_QUIT)
			{
				return false;
			}

			if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
			{
				return false;
			}

			if (event.window.windowID != id)
			{
				continue;
			}

			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				return false;
			}

			// Both, because the pixel event is the one that fires on a move between displays of different scales, where the window's own size did not change.
			if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
			{
				m_resized = true;
			}
		}

		return true;
	}

	bool Sdl3Window::TakeResized() noexcept
	{
		const bool resized = m_resized;
		m_resized		   = false;

		return resized;
	}

	azo::rhi::Extent2D Sdl3Window::GetDrawableSize() const
	{
		int width  = 0;
		int height = 0;
		static_cast<void>(SDL_GetWindowSizeInPixels(m_window, &width, &height));

		return azo::rhi::Extent2D{ .width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height) };
	}

	float Sdl3Window::GetAspectRatio() const
	{
		const azo::rhi::Extent2D size = GetDrawableSize();

		// A minimized window reports no area on some platforms, and a projection built from that is full of infinities.
		return size.height == 0 ? 1.0f : static_cast<float>(size.width) / static_cast<float>(size.height);
	}

	bool Sdl3Window::Provide(const azo::rhi::SurfaceRequest & request)
	{
		if (auto * loader = azo::rhi::SurfacePayloadOf<azo::rhi::native::VulkanLoaderPayload>(request); loader != nullptr)
		{
			// SDL's own loader, which is the one the dispatcher has to be seeded from, since a surface SDL makes and an instance dispatched through anything
			// else belong to two different Vulkans.
			loader->getInstanceProcAddr = detail::VulkanInstanceProcAddr();
			return loader->getInstanceProcAddr != nullptr;
		}

		if (auto * vulkan = azo::rhi::SurfacePayloadOf<azo::rhi::native::VulkanSurfacePayload>(request); vulkan != nullptr)
		{
			vulkan->surface = detail::CreateVulkanSurface(m_window, vulkan->instance);
			return vulkan->surface != 0;
		}

		if (auto * metal = azo::rhi::SurfacePayloadOf<azo::rhi::native::MetalSurfacePayload>(request); metal != nullptr)
		{
			metal->layer = m_metalView != nullptr ? SDL_Metal_GetLayer(static_cast<::SDL_MetalView>(m_metalView)) : nullptr;
			return metal->layer != nullptr;
		}

		if (auto * win32 = azo::rhi::SurfacePayloadOf<azo::rhi::native::Win32SurfacePayload>(request); win32 != nullptr)
		{
			win32->window = detail::Win32WindowHandle(m_window);
			return win32->window != nullptr;
		}

		return false;
	}
} // namespace fw::platform

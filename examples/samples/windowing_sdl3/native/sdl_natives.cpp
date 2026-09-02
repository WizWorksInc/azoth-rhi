// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdl_natives.hpp"

#include "FW/utility/Log.hpp"

#include <SDL3/SDL_vulkan.h>

#include <bit>
#include <cstdint>

namespace sdl_native
{

	void * VulkanInstanceProcAddr()
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<void *>(SDL_Vulkan_GetVkGetInstanceProcAddr());
	}

	std::uint64_t CreateVulkanSurface(SDL_Window * window, void * instance)
	{
		static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "VkSurfaceKHR is expected to be 64 bits wide");

		VkSurfaceKHR surface{};
		if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, &surface))
		{
			LOG_ERROR(fw::Log(), "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
			return 0;
		}

		return std::bit_cast<std::uint64_t>(surface);
	}

	void * Win32WindowHandle([[maybe_unused]] SDL_Window * window)
	{
#ifdef _WIN32
		return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
		return nullptr;
#endif
	}

}

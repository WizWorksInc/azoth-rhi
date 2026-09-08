// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace sdl_native
{

	[[nodiscard]] void * VulkanInstanceProcAddr();

	[[nodiscard]] std::uint64_t CreateVulkanSurface(SDL_Window * window, void * instance);

	[[nodiscard]] void * Win32WindowHandle(SDL_Window * window);

}

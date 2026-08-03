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

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace fw::platform::detail
{

	/*
	 * The one SDL call that cannot be made without naming Vulkan's types, kept in its own translation unit because that is the only place the API boundary check permits them.
	 * Everything else the window asks of SDL it asks in Sdl3Window.cpp, since the surface interface takes opaque payloads.
	 */

	// The loader SDL brought up, as a plain pointer. Null when this SDL found no Vulkan.
	[[nodiscard]] void * VulkanInstanceProcAddr();

	// SDL_Vulkan_CreateSurface behind the opaque handles the surface payload uses. 0 on failure and the message is SDL_GetError's. Ownership of what it makes
	// transfers to the RHI, which destroys it with the device.
	[[nodiscard]] std::uint64_t CreateVulkanSurface(SDL_Window * window, void * instance);

	// The window's native handle on Windows, null anywhere else. Here because the property that names it names the type.
	[[nodiscard]] void * Win32WindowHandle(SDL_Window * window);

} // namespace fw::platform::detail

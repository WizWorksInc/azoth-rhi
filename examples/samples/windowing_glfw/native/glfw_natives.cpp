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

/*
 * This is the macro that pulls vulkan/vulkan.h in through glfw3.h and without it glfwCreateWindowSurface is not declared at all. GLFW offers no way to hand
 * back a surface that does not go through the Vulkan types, which is why this file exists and the SDL3 sample needs no counterpart to it.
 */
#define GLFW_INCLUDE_VULKAN

#ifdef _WIN32
	#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include "glfw_natives.hpp"

#ifdef _WIN32
	#include <GLFW/glfw3native.h>
#endif

#include <bit>
#include <cstdint>

namespace glfw_native
{

	void InitVulkanLoader(void * getInstanceProcAddr)
	{
		if (getInstanceProcAddr == nullptr)
		{
			return;
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a loader entry point arrives as a void pointer and this is the cast for it.
		glfwInitVulkanLoader(reinterpret_cast<PFN_vkGetInstanceProcAddr>(getInstanceProcAddr));
	}

	void * VulkanInstanceProcAddr()
	{
		/*
		 * GLFW's own loader, which is the one the RHI's dispatcher has to be seeded from. A function pointer reaching a void pointer is what the surface
		 * interface asks for and reinterpret_cast is the cast for it, since bit_cast refuses to go pointer to pointer.
		 */
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<void *>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
	}

	std::uint64_t CreateVulkanSurface(GLFWwindow * window, void * instance)
	{
		// Non-dispatchable so 64 bits wide on every target this library supports, which is what lets it ride back through the surface interface as a plain
		// integer.
		static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "VkSurfaceKHR is expected to be 64 bits wide");

		VkSurfaceKHR surface{};
		if (glfwCreateWindowSurface(static_cast<VkInstance>(instance), window, nullptr, &surface) != VK_SUCCESS)
		{
			return 0;
		}

		// No glfwDestroy counterpart because ownership transferred to the RHI and the device destroys it.
		return std::bit_cast<std::uint64_t>(surface);
	}

	void * Win32WindowHandle([[maybe_unused]] GLFWwindow * window)
	{
#ifdef _WIN32
		return glfwGetWin32Window(window);
#else
		return nullptr;
#endif
	}

#ifndef __APPLE__
	// The macOS one is in glfw_metal_layer.mm, since it takes Objective-C to write.
	void * MetalLayer([[maybe_unused]] GLFWwindow * window)
	{
		return nullptr;
	}
#endif

} // namespace glfw_native

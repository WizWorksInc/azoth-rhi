// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

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
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<void *>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
	}

	std::uint64_t CreateVulkanSurface(GLFWwindow * window, void * instance)
	{
		static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "VkSurfaceKHR is expected to be 64 bits wide");

		VkSurfaceKHR surface{};
		if (glfwCreateWindowSurface(static_cast<VkInstance>(instance), window, nullptr, &surface) != VK_SUCCESS)
		{
			return 0;
		}

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
	void * MetalLayer([[maybe_unused]] GLFWwindow * window)
	{
		return nullptr;
	}
#endif

}

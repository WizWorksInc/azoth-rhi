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

#include <GLFW/glfw3.h>

#include <cstdint>

namespace glfw_native
{

	/*
	 * GLFW's natives behind the shapes rhi::SurfaceSource asks for, so main.cpp can stay out of the graphics headers. This is why the GLFW sample is more than
	 * one file where the SDL3 one is not: GLFW declares glfwCreateWindowSurface only where it may include the Vulkan headers, and on macOS it stops at the
	 * NSWindow and leaves the CAMetalLayer to the caller. They live under native/ because that is the one place the API boundary check permits it.
	 */

	/*
	 * Hands GLFW the loader the RHI resolved, on the platforms where GLFW cannot find one itself. Call before glfwInit and pass what
	 * rhi::native::ResolveVulkanLoader answered.
	 *
	 * Null leaves GLFW to its own search. What it fixes otherwise is a loader the dynamic linker does not look for, which on macOS is where the SDK puts one:
	 * GLFW asks dyld for libvulkan.1.dylib and finds nothing.
	 */
	void InitVulkanLoader(void * getInstanceProcAddr);

	// The loader entry point GLFW brought up or null when this GLFW has no Vulkan.
	[[nodiscard]] void * VulkanInstanceProcAddr();

	// glfwCreateWindowSurface behind the opaque handles the surface interface uses. 0 on failure.
	[[nodiscard]] std::uint64_t CreateVulkanSurface(GLFWwindow * window, void * instance);

	// A CAMetalLayer attached to the window's content view or null anywhere but macOS.
	[[nodiscard]] void * MetalLayer(GLFWwindow * window);

	// The window's HWND or null anywhere but Windows.
	[[nodiscard]] void * Win32WindowHandle(GLFWwindow * window);

} // namespace glfw_native

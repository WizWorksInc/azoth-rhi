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

#include "azoth/rhi/backend/support/host_containers.hpp"

#include <vk_mem_alloc.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace azo::rhi::vulkan
{

	// Swapchain images and depth attachment rebuilt together on resize.
	struct SwapchainBundle
	{
		vk::SwapchainKHR Swapchain;
		vk::Format ColorFormat = vk::Format::eB8G8R8A8Srgb;
		vk::Extent2D Extent;
		detail::HostVector<vk::Image> Images;
		detail::HostVector<vk::ImageView> Views;
		vk::Image DepthImage;
		VmaAllocation DepthAllocation = nullptr;
		vk::ImageView DepthView;
		vk::Format DepthFormat = vk::Format::eD32Sfloat;

		// Present mode selected after surface support fallback.
		vk::PresentModeKHR PresentMode = vk::PresentModeKHR::eFifo;

		// True when swapchain images can be copied for frame capture.
		bool CaptureCapable = false;
	};

	// Selects the first supported present mode from an ordered preference list. Falls back to FIFO because Vulkan guarantees FIFO support.
	vk::PresentModeKHR SelectPresentMode(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, vk::SurfaceKHR surface, std::span<const vk::PresentModeKHR> desired);

	/*
	 * Creates a swapchain and matching depth attachment. desiredFormats and desiredPresentModes are ordered preference lists, primary choice first. The first
	 * entry the surface supports is used, otherwise a surface default. desiredImageCount of 0 selects the backend default, any other value is clamped to the
	 * surface's supported range.
	 *
	 * A default-constructed bundle, whose Swapchain is null, is what a failure looks like. Anything already created is released before returning.
	 */
	SwapchainBundle CreateSwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, vk::PhysicalDevice phys, VmaAllocator allocator,
		vk::SurfaceKHR surface, std::uint32_t width, std::uint32_t height, vk::SwapchainKHR old, std::span<const vk::Format> desiredFormats,
		std::span<const vk::PresentModeKHR> desiredPresentModes, std::uint32_t desiredImageCount = 0);

	void DestroySwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, VmaAllocator allocator, SwapchainBundle & sc);

} // namespace azo::rhi::vulkan

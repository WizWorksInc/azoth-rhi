// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		vk::PresentModeKHR PresentMode = vk::PresentModeKHR::eFifo;

		bool CaptureCapable = false;
	};

	vk::PresentModeKHR SelectPresentMode(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, vk::SurfaceKHR surface, std::span<const vk::PresentModeKHR> desired);

	SwapchainBundle CreateSwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, vk::PhysicalDevice phys, VmaAllocator allocator,
		vk::SurfaceKHR surface, std::uint32_t width, std::uint32_t height, vk::SwapchainKHR old, std::span<const vk::Format> desiredFormats,
		std::span<const vk::PresentModeKHR> desiredPresentModes, std::uint32_t desiredImageCount = 0);

	void DestroySwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, VmaAllocator allocator, SwapchainBundle & sc);

}

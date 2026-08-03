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

#include "backends/vulkan/swapchain_bundle.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace azo::rhi::vulkan
{

	vk::PresentModeKHR SelectPresentMode(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, vk::SurfaceKHR surface, std::span<const vk::PresentModeKHR> desired)
	{
		const auto supported = phys.getSurfacePresentModesKHR<HostAllocatorAdapter<vk::PresentModeKHR>>(surface, dispatch);
		if (supported.result != vk::Result::eSuccess)
		{
			// FIFO is the one mode Vulkan guarantees, so a surface whose modes cannot be read still has an answer, not no swapchain.
			return vk::PresentModeKHR::eFifo;
		}

		for (const vk::PresentModeKHR mode : desired)
		{
			// FIFO is always supported, the rest must be advertised by the surface.
			if (mode == vk::PresentModeKHR::eFifo || std::ranges::find(supported.value, mode) != supported.value.end())
			{
				return mode;
			}
		}

		return vk::PresentModeKHR::eFifo;
	}

	SwapchainBundle CreateSwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, vk::PhysicalDevice phys, VmaAllocator allocator,
		vk::SurfaceKHR surface, std::uint32_t width, std::uint32_t height, vk::SwapchainKHR old, std::span<const vk::Format> desiredFormats,
		std::span<const vk::PresentModeKHR> desiredPresentModes, std::uint32_t desiredImageCount)
	{
		SwapchainBundle sc;

		const auto surfaceCaps = phys.getSurfaceCapabilitiesKHR(surface, dispatch);
		if (surfaceCaps.result != vk::Result::eSuccess)
		{
			return {};
		}

		const vk::SurfaceCapabilitiesKHR & caps = surfaceCaps.value;

		// An empty format list would leave the front() picks below reading nothing, and a surface with no format is one no swapchain can be built on.
		const auto surfaceFormats = phys.getSurfaceFormatsKHR<HostAllocatorAdapter<vk::SurfaceFormatKHR>>(surface, dispatch);
		if (surfaceFormats.result != vk::Result::eSuccess || surfaceFormats.value.empty())
		{
			return {};
		}

		const detail::HostVector<vk::SurfaceFormatKHR> & formats = surfaceFormats.value;
		sc.ColorFormat											 = formats.front().format;
		vk::ColorSpaceKHR colorSpace							 = formats.front().colorSpace;

		// Take the first desired format the surface offers, preferring an SRGB-nonlinear color space for it.
		for (const vk::Format desiredFormat : desiredFormats)
		{
			const vk::SurfaceFormatKHR * match = nullptr;
			for (const vk::SurfaceFormatKHR & f : formats)
			{
				if (f.format != desiredFormat)
				{
					continue;
				}
				if (f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
				{
					match = &f;
					break;
				}
				if (match == nullptr)
				{
					match = &f;
				}
			}

			if (match != nullptr)
			{
				sc.ColorFormat = match->format;
				colorSpace	   = match->colorSpace;
				break;
			}
		}

		sc.Extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
														   : vk::Extent2D(std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
																 std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height));

		std::uint32_t imageCount = desiredImageCount != 0 ? desiredImageCount : caps.minImageCount + 1;
		imageCount				 = std::max(imageCount, caps.minImageCount);
		if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
		{
			imageCount = caps.maxImageCount;
		}

		sc.PresentMode = SelectPresentMode(phys, dispatch, surface, desiredPresentModes);

		vk::ImageUsageFlags scUsage = vk::ImageUsageFlagBits::eColorAttachment;
		if (caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
		{
			scUsage |= vk::ImageUsageFlagBits::eTransferSrc;
			sc.CaptureCapable = true;
		}

		vk::SwapchainCreateInfoKHR const info({},
			surface,
			imageCount,
			sc.ColorFormat,
			colorSpace,
			sc.Extent,
			1,
			scUsage,
			vk::SharingMode::eExclusive,
			{},
			caps.currentTransform,
			vk::CompositeAlphaFlagBitsKHR::eOpaque,
			sc.PresentMode,
			VK_TRUE,
			old);

		const auto created = device.createSwapchainKHR(info, nullptr, dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return {};
		}

		sc.Swapchain = created.value;

		// Everything from here on has the swapchain to give back, so a failure unwinds through DestroySwapchain without returning a bundle that owns objects nobody
		// holds a handle to.
		const auto images = device.getSwapchainImagesKHR<HostAllocatorAdapter<vk::Image>>(sc.Swapchain, dispatch);
		if (images.result != vk::Result::eSuccess)
		{
			DestroySwapchain(device, dispatch, allocator, sc);
			return {};
		}

		sc.Images = images.value;

		for (vk::Image const img : sc.Images)
		{
			const auto view = device.createImageView(
				vk::ImageViewCreateInfo(
					{}, img, vk::ImageViewType::e2D, sc.ColorFormat, {}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)),
				nullptr,
				dispatch);
			if (view.result != vk::Result::eSuccess || !detail::TryPushBack(sc.Views, view.value))
			{
				if (view.result == vk::Result::eSuccess)
				{
					device.destroyImageView(view.value, nullptr, dispatch);
				}

				DestroySwapchain(device, dispatch, allocator, sc);
				return {};
			}
		}

		const VkImageCreateInfo depthInfo = vk::ImageCreateInfo({},
			vk::ImageType::e2D,
			sc.DepthFormat,
			vk::Extent3D(sc.Extent.width, sc.Extent.height, 1),
			1,
			1,
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment);

		VmaAllocationCreateInfo depthAllocInfo{};
		depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VkImage rawDepth = VK_NULL_HANDLE;
		if (vmaCreateImage(allocator, &depthInfo, &depthAllocInfo, &rawDepth, &sc.DepthAllocation, nullptr) != VK_SUCCESS)
		{
			DestroySwapchain(device, dispatch, allocator, sc);
			return {};
		}

		sc.DepthImage		 = vk::Image(rawDepth);
		const auto depthView = device.createImageView(
			vk::ImageViewCreateInfo(
				{}, sc.DepthImage, vk::ImageViewType::e2D, sc.DepthFormat, {}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)),
			nullptr,
			dispatch);
		if (depthView.result != vk::Result::eSuccess)
		{
			DestroySwapchain(device, dispatch, allocator, sc);
			return {};
		}

		sc.DepthView = depthView.value;

		return sc;
	}

	void DestroySwapchain(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, VmaAllocator allocator, SwapchainBundle & sc)
	{
		if (sc.DepthView)
		{
			device.destroyImageView(sc.DepthView, nullptr, dispatch);
		}

		if (sc.DepthImage)
		{
			vmaDestroyImage(allocator, sc.DepthImage, sc.DepthAllocation);
		}

		for (vk::ImageView const v : sc.Views)
		{
			device.destroyImageView(v, nullptr, dispatch);
		}

		sc.Views.clear();
		sc.Images.clear();

		if (sc.Swapchain)
		{
			device.destroySwapchainKHR(sc.Swapchain, nullptr, dispatch);
		}

		sc = {};
	}

} // namespace azo::rhi::vulkan

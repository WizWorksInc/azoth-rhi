// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <span>

struct VmaAllocator_T;

namespace azo::rhi
{

	template <>
	struct NativeBuffer<VulkanApi> final
	{
		vk::Buffer buffer;
	};

	template <>
	struct NativeTexture<VulkanApi> final
	{
		vk::Image image;
	};

	template <>
	struct NativeTextureView<VulkanApi> final
	{
		vk::ImageView view;
	};

	template <>
	struct NativeSampler<VulkanApi> final
	{
		vk::Sampler sampler;
	};

	template <>
	struct NativeTimeline<VulkanApi> final
	{
		vk::Semaphore semaphore;
	};

	template <>
	struct NativeBinarySemaphore<VulkanApi> final
	{
		vk::Semaphore semaphore;
	};

	struct VulkanNativeDevice final
	{
		vk::Instance instance;
		vk::PhysicalDevice physicalDevice;
		vk::Device device;
		vk::Queue graphicsQueue;
		std::uint32_t graphicsQueueFamily = 0;
		VmaAllocator_T * allocator		  = nullptr;
		bool debugUtils					  = false;

		const vk::detail::DispatchLoaderDynamic * dispatch = nullptr;
	};

	[[nodiscard]] AZO_RHI_API Result<VulkanNativeDevice> GetVulkanNativeDevice(Device device);

	[[nodiscard]] Result<void> SetVulkanDeviceSurface(Device device, vk::SurfaceKHR surface);

	struct VulkanNativeSwapchain final
	{
		vk::SwapchainKHR swapchain;
		vk::Format colorFormat = vk::Format::eUndefined;
		vk::Format depthFormat = vk::Format::eUndefined;
		vk::Extent2D extent;
		std::span<const vk::Image> images;
		std::span<const vk::ImageView> views;
		vk::Image depthImage;
		vk::ImageView depthView;
		vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
		bool captureCapable			   = false;
	};

	[[nodiscard]] AZO_RHI_API Result<VulkanNativeSwapchain> GetVulkanNativeSwapchain(Swapchain swapchain);

	[[nodiscard]] AZO_RHI_API Result<vk::Semaphore> GetVulkanSemaphore(Swapchain swapchain, BinarySemaphoreHandle semaphore);

	[[nodiscard]] AZO_RHI_API vk::CommandBuffer GetVulkanCommandBuffer(CommandList commandList);

	[[nodiscard]] AZO_RHI_API vk::CommandPool GetVulkanCommandPool(CommandPool commandPool);

}

namespace azo::rhi::native
{

	struct VulkanQueueView final
	{
		vk::Queue queue;
		std::uint32_t familyIndex = 0;
	};

	struct VulkanCommandListView final
	{
		vk::CommandBuffer commandBuffer;
	};

	template <>
	struct NativeAccess<VulkanApi> final
	{
		using QueueView		  = VulkanQueueView;
		using CommandListView = VulkanCommandListView;

		[[nodiscard]] static AZO_RHI_API VulkanCommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

}

namespace azo::rhi
{

	[[nodiscard]] AZO_RHI_API Result<native::VulkanQueueView> GetVulkanQueueView(Queue queue);

}

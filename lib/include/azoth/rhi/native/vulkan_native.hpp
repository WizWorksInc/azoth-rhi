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

/**
 * \file
 * \brief Vulkan native interop handles and accessors.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include <vulkan/vulkan.hpp>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

struct VmaAllocator_T;

namespace azo::rhi
{

	/**
	 * \brief The VkBuffer an adopted buffer stands for, and the payload a native read hands back.
	 *
	 * \attention The object must have been made on the VkDevice that GetVulkanNativeDevice reports for this RHI device. No Vulkan entry point lets the RHI ask
	 * which device produced a handle, so this is a caller precondition and not a check, and violating it is diagnosed by the Vulkan validation layers , not here.
	 */
	template <>
	struct NativeBuffer<VulkanApi> final
	{
		vk::Buffer buffer;
	};

	/**
	 * \brief The VkImage an adopted texture stands for, on the same terms as the buffer payload.
	 *
	 * The RHI builds no view over an adopted image. A caller that needs one adopts its VkImageView too.
	 */
	template <>
	struct NativeTexture<VulkanApi> final
	{
		vk::Image image;
	};

	/**
	 * \brief The VkImageView an adopted texture view stands for.
	 *
	 * Adoptable, not rebuildable: a view can carry a VkSamplerYcbcrConversion, and the RHI has no surface for naming one, so a view it built from a description
	 * could not be the same view.
	 */
	template <>
	struct NativeTextureView<VulkanApi> final
	{
		vk::ImageView view;
	};

	/**
	 * \brief The VkSampler an adopted sampler stands for, adoptable for the same reason a view is.
	 */
	template <>
	struct NativeSampler<VulkanApi> final
	{
		vk::Sampler sampler;
	};

	/**
	 * \brief The VkSemaphore an adopted timeline stands for.
	 *
	 * \attention Must be a semaphore of the timeline type. Vulkan has one object for both kinds and the RHI does not, so nothing here can catch a binary
	 * semaphore handed to this entry: what diagnoses it is the validation layers, on the first wait or signal.
	 */
	template <>
	struct NativeTimeline<VulkanApi> final
	{
		vk::Semaphore semaphore;
	};

	/**
	 * \brief The VkSemaphore an adopted binary semaphore stands for, carrying the mirror of the timeline caveat.
	 */
	template <>
	struct NativeBinarySemaphore<VulkanApi> final
	{
		vk::Semaphore semaphore;
	};

	/**
	 * \brief Borrowed native Vulkan objects owned by a Vulkan-backed RHI device.
	 *
	 * The surface and swapchain are exposed separately. The device owns these handles and destroys them with the owning UniqueDevice.
	 *
	 * \attention Do not destroy these handles or keep them past the device that returned them.
	 */
	struct VulkanNativeDevice final
	{
		vk::Instance instance;
		vk::PhysicalDevice physicalDevice;
		vk::Device device;
		vk::Queue graphicsQueue;
		std::uint32_t graphicsQueueFamily = 0;
		VmaAllocator_T * allocator		  = nullptr;
		bool debugUtils					  = false;

		/**
		 * \brief Vulkan-Hpp dispatch table for these handles.
		 *
		 * Pass this to Vulkan-Hpp calls made on the returned handles. The default dispatcher is process-wide and can point at the wrong device when more than one
		 * device is alive.
		 */
		const vk::detail::DispatchLoaderDynamic * dispatch = nullptr;
	};

	/**
	 * \brief Returns native handles from a Vulkan-backed device.
	 *
	 * Returns eUnsupportedApi when device is not backed by Vulkan.
	 */
	[[nodiscard]] AZO_RHI_API Result<VulkanNativeDevice> GetVulkanNativeDevice(Device device);

	/**
	 * \brief Gives a Vulkan-backed device ownership of a platform-created surface.
	 *
	 * The device destroys the surface during teardown. Returns eUnsupportedApi when device is not backed by Vulkan.
	 */
	[[nodiscard]] Result<void> SetVulkanDeviceSurface(Device device, vk::SurfaceKHR surface);

	/**
	 * \brief Borrowed native Vulkan objects owned by a Vulkan-backed RHI swapchain.
	 *
	 * images and views point into swapchain-owned storage and are valid until resize, swapchain destruction, or device teardown.
	 *
	 * \attention Do not destroy these handles.
	 */
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

	/**
	 * \brief Returns native handles from a Vulkan-backed swapchain.
	 *
	 * Returns eUnsupportedApi when swapchain was not created by the Vulkan backend.
	 */
	[[nodiscard]] AZO_RHI_API Result<VulkanNativeSwapchain> GetVulkanNativeSwapchain(Swapchain swapchain);

	/**
	 * \brief Resolves a swapchain-owned WSI semaphore handle to its VkSemaphore.
	 *
	 * Returns eInvalidHandle when semaphore does not name a semaphore owned by this swapchain.
	 */
	[[nodiscard]] Result<vk::Semaphore> GetVulkanSemaphore(Swapchain swapchain, BinarySemaphoreHandle semaphore);

	/**
	 * \brief Returns the VkCommandBuffer behind an RHI command list.
	 *
	 * Intended for raw Vulkan recording while code is being moved onto the RHI command surface.
	 */
	[[nodiscard]] vk::CommandBuffer GetVulkanCommandBuffer(CommandList commandList);

	/**
	 * \brief Returns the VkCommandPool behind an RHI command pool.
	 *
	 * Intended for transitional helpers that still allocate Vulkan command buffers directly.
	 */
	[[nodiscard]] vk::CommandPool GetVulkanCommandPool(CommandPool commandPool);

} // namespace azo::rhi

namespace azo::rhi::native
{

	/**
	 * \brief Borrowed Vulkan objects backing an RHI Vulkan device, for the native access surface.
	 */
	struct VulkanDeviceView final
	{
		vk::Device device;
		vk::PhysicalDevice physicalDevice;
	};

	/**
	 * \brief Borrowed Vulkan queue backing an RHI queue.
	 */
	struct VulkanQueueView final
	{
		vk::Queue queue;
		std::uint32_t familyIndex = 0;
	};

	/**
	 * \brief Borrowed Vulkan command buffer backing an RHI command list, which is what a ModifyNative scope records into.
	 */
	struct VulkanCommandListView final
	{
		vk::CommandBuffer commandBuffer;
	};

	/**
	 * \brief Borrowed Vulkan buffer backing an RHI buffer.
	 */
	struct VulkanBufferView final
	{
		vk::Buffer buffer;
	};

	/**
	 * \brief Borrowed Vulkan image backing an RHI texture.
	 */
	struct VulkanTextureView final
	{
		vk::Image image;
	};

	/**
	 * \brief Native access surface for the Vulkan backend, which is what makes CommandList::ModifyNative compile here.
	 *
	 * Its absence was not a missing feature so much as a missing declaration: the concept gating ModifyNative is satisfied by this specialization existing, so
	 * without it the escape hatch the RHI documents did not compile on the backend a caller is most likely to want it on.
	 */
	template <>
	struct NativeAccess<VulkanApi> final
	{
		using DeviceView	  = VulkanDeviceView;
		using QueueView		  = VulkanQueueView;
		using CommandListView = VulkanCommandListView;
		using BufferView	  = VulkanBufferView;
		using TextureView	  = VulkanTextureView;

		/**
		 * \brief Builds a command-list native view from the backend's concrete command-list object.
		 */
		[[nodiscard]] static VulkanCommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

} // namespace azo::rhi::native

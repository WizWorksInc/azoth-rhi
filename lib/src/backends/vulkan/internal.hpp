// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/scope_guard.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"
#include "azoth/rhi/rhi.hpp"

#include "backends/vulkan/barrier_tables.hpp"
#include "backends/vulkan/layouts.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "support/driver_version.hpp"

#include <vk_mem_alloc.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace azo::rhi::vulkan
{
	struct VulkanInstance final
	{
		const BackendObject * object = nullptr;
		ApiVersion apiVersion{};
		vk::Instance instance;

		vk::detail::DispatchLoaderDynamic dispatch;

		bool debugUtils = false;
		detail::HostVector<detail::HostString> adapterNames;
		detail::HostVector<detail::HostString> driverInfos;
		detail::HostVector<detail::HostString> driverVersions;

		vk::DebugUtilsMessengerEXT debugMessenger;
		std::atomic<std::uint64_t> validationErrors{ 0 };
		std::atomic<std::uint64_t> validationWarnings{ 0 };

		bool breakOnError	= false;
		bool breakOnWarning = false;

		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;

		VulkanInstance()									   = default;
		VulkanInstance(const VulkanInstance &)				   = delete;
		VulkanInstance & operator=(const VulkanInstance &)	   = delete;
		VulkanInstance(VulkanInstance &&) noexcept			   = delete;
		VulkanInstance & operator=(VulkanInstance &&) noexcept = delete;

		~VulkanInstance()
		{
			if (instance)
			{
				if (debugMessenger)
				{
					instance.destroyDebugUtilsMessengerEXT(debugMessenger, nullptr, dispatch);
				}

				instance.destroy(nullptr, dispatch);
			}
		}
	};

	// Defined in backend.cpp beside the debug messenger, so every message this backend raises itself takes the one route.
	void report_instance_message(const VulkanInstance * instance, ValidationMessageSeverity severity, const char * source, const char * message) noexcept;

	struct BufferSlot final
	{
		VkBuffer buffer			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkDeviceSize size		 = 0;
		bool coherent			 = false;
		bool hostVisible		 = false;

		void * mapped = nullptr;
		BoundedCount mapCount;

		VkDeviceMemory placedMemory = VK_NULL_HANDLE;
		VkDeviceSize placedOffset	= 0;
		HeapHandle placedHeap{};

		bool sparse = false;

		Flags<ExternalHandleType> exportableHandleTypes;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		BufferDesc desc{};
	};

	struct TextureSlot final
	{
		VkImage image			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkImageView defaultView	 = VK_NULL_HANDLE;
		vk::Format format;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		std::uint32_t mipLevels			= 1;
		std::uint32_t arrayLayers		= 1;
		std::uint32_t width				= 1;
		std::uint32_t height			= 1;
		std::uint32_t depth				= 1;
		Format rhiFormat				= Format::eUndefined;
		Flags<TextureUsage> usage;
		bool mutableFormat = false;

		bool sparse = false;

		Flags<ExternalHandleType> exportableHandleTypes;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		TextureDesc desc{};
	};

	struct TextureViewSlot final
	{
		vk::ImageView view;
		vk::Format format				= vk::Format::eUndefined;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		SlotLifetime lifetime			= SlotLifetime::eOwned;
	};

	struct PipelineLayoutSlot final
	{
		vk::PipelineLayout layout;
		detail::HostVector<DescriptorSetLayoutHandle> sets;
	};

	struct GraphicsPipelineSlot final
	{
		vk::Pipeline pipeline;
	};

	struct TimelineSlot final
	{
		vk::Semaphore semaphore;

		Flags<ExternalHandleType> exportableHandleTypes;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct QueryPoolSlot final
	{
		vk::QueryPool pool;
		std::uint32_t queryCount = 0;
	};

	struct SamplerSlot final
	{
		vk::Sampler sampler;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct ComputePipelineSlot final
	{
		vk::Pipeline pipeline;
	};

	struct PipelineCacheSlot final
	{
		vk::PipelineCache cache;
		detail::HostVector<std::uint8_t> data;
	};

	struct BinarySemaphoreSlot final
	{
		vk::Semaphore semaphore;

		Flags<ExternalHandleType> exportableHandleTypes;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct DescriptorSetLayoutSlot final
	{
		vk::DescriptorSetLayout layout;

		detail::HostVector<DescriptorBinding> bindings;
	};

	struct VulkanDescriptorArena;

	struct DescriptorSetSlot final
	{
		vk::DescriptorSet set;

		const VulkanDescriptorArena * arena = nullptr;

		DescriptorSetLayoutHandle layout{};
	};

	struct HeapSlot final
	{
		vk::DeviceMemory memory;
		vk::DeviceSize size			  = 0;
		std::uint32_t memoryTypeIndex = 0;
		bool hostVisible			  = false;
		bool coherent				  = false;

		void * mapped = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct RenderPassAttachmentKey final
	{
		vk::Format format				= vk::Format::eUndefined;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		vk::AttachmentLoadOp loadOp		= vk::AttachmentLoadOp::eDontCare;
		vk::AttachmentStoreOp storeOp	= vk::AttachmentStoreOp::eDontCare;
		vk::ImageLayout layout			= vk::ImageLayout::eUndefined;

		bool operator==(const RenderPassAttachmentKey &) const noexcept = default;
	};

	struct RenderPassKey final
	{
		// NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): the line it suppresses is the next source line, so it cannot sit in the block above.
		std::array<RenderPassAttachmentKey, 8> colors{};
		std::uint32_t colorCount = 0;
		bool hasDepth			 = false;
		RenderPassAttachmentKey depth{};

		bool operator==(const RenderPassKey &) const noexcept = default;
	};

	struct RenderPassKeyHash final
	{
		[[nodiscard]] std::size_t operator()(const RenderPassKey & key) const noexcept
		{
			std::size_t hash = hash::kFnv1a64OffsetBasis;
			const auto fold	 = [&hash](std::size_t value) noexcept
			{
				hash ^= value;
				hash *= hash::kFnv1a64Prime;
			};

			const auto foldAttachment = [&fold](const RenderPassAttachmentKey & a) noexcept
			{
				fold(static_cast<std::size_t>(a.format));
				fold(static_cast<std::size_t>(a.samples));
				fold(static_cast<std::size_t>(a.loadOp));
				fold(static_cast<std::size_t>(a.storeOp));
				fold(static_cast<std::size_t>(a.layout));
			};

			for (std::uint32_t i = 0; i < key.colorCount; ++i)
			{
				// ColorCount is clamped to this array where the key is built. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				foldAttachment(key.colors[i]);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}

			fold(key.colorCount);
			if (key.hasDepth)
			{
				foldAttachment(key.depth);
			}

			return hash;
		}
	};

	struct VulkanDevice;

	struct VulkanSwapchain final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::SurfaceKHR surface;
		SwapchainBundle bundle;

		detail::HostVector<vk::Semaphore> semaphores;
		std::uint32_t acquireBase	= 0;
		std::uint32_t acquireCount	= 0;
		std::uint32_t acquireCursor = 0;

		std::uint32_t id = 0;

		detail::HostVector<TextureHandle> backBufferTextures;
		detail::HostVector<TextureViewHandle> backBufferViews;

		detail::HostVector<vk::Format> desiredFormats;
		detail::HostVector<vk::PresentModeKHR> desiredPresentModes;
		std::uint32_t desiredImageCount = 0;
	};

	inline constexpr std::uint32_t kNoSubmitTimeline = 0xffffffffu;

	// One timeline semaphore per real queue, signaled at an increasing value by every submit on it.
	struct SubmitTimeline final
	{
		vk::Semaphore semaphore;
		QueueType type		= QueueType::eGraphics;
		std::uint32_t index = 0;

		// Two Queue objects can name one real queue, and a caller may serialize per object rather than per queue.
		std::atomic<std::uint64_t> counter{ 0 };

		// The highest value actually handed to the GPU. A submit that failed after reserving leaves counter ahead of this,
		// and waiting on a value nothing will ever signal would stall teardown, so the drain waits on this one.
		std::atomic<std::uint64_t> submitted{ 0 };
	};

	struct VulkanQueue final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::Queue queue;
		QueueType type			  = QueueType::eGraphics;
		std::uint32_t familyIndex = 0;

		// GetQueue hands out a fresh VulkanQueue per call for the same vk::Queue, so the counter lives on the device and this only indexes it.
		std::uint32_t submitTimeline = kNoSubmitTimeline;
	};

	struct VulkanCommandList;

	// A buffer retired by Begin while its earlier submission was still running, freed once the timeline passes value.
	struct RetiredCommandBuffer final
	{
		vk::CommandBuffer buffer;
		std::uint32_t submitTimeline = kNoSubmitTimeline;
		std::uint64_t submitValue	 = 0;
		detail::HostVector<TimelinePoint> callerSignals;
	};

	struct VulkanCommandPool final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::CommandPool pool;
		std::uint32_t family = 0;

		// vkBeginCommandBuffer may only reset a used buffer in place when the pool carries eResetCommandBuffer.
		bool resetsIndividualLists = false;

		detail::HostVector<vk::Framebuffer> framebuffers;

		detail::HostVector<VulkanCommandList *> lists;
		std::size_t handedOut = 0;

		detail::HostVector<RetiredCommandBuffer> retired;

		detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> renderPasses;
	};

	struct VulkanCommandList final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		VulkanCommandPool * pool	 = nullptr;
		vk::CommandBuffer buffer;
		std::uint32_t family = 0;

		ListLifecycle lifecycle = ListLifecycle::eFresh;

		// Plain like lifecycle beside them: the threading model gives one list per thread, so moving one between threads is a caller hand-off.
		std::uint32_t submitTimeline = kNoSubmitTimeline;
		std::uint64_t submitValue	 = 0;
		// The submit's own timeline signals, since a caller who waited on one may reuse the list before submitValue is reached.
		detail::HostVector<TimelinePoint> callerSignals;

		vk::QueryPool pendingEndTimestamp;
		std::uint32_t pendingEndTimestampQuery = 0;
	};

	struct VulkanDescriptorArena final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::DescriptorPool pool;
	};

	struct PendingFree final
	{
		RetirePoint safeAfter{};
		VkBuffer buffer			 = VK_NULL_HANDLE;
		VkImage image			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		vk::ImageView view;
		vk::PipelineLayout pipelineLayout;
		vk::Pipeline pipeline;
		vk::Semaphore semaphore;
		vk::QueryPool queryPool;
		vk::Sampler sampler;
		vk::PipelineCache pipelineCache;
		vk::Semaphore binarySemaphore;
		vk::DescriptorSetLayout descriptorSetLayout;
		vk::DeviceMemory deviceMemory;
	};

	inline void free_pending(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, VmaAllocator allocator, const PendingFree & pending) noexcept
	{
		if (pending.view)
		{
			device.destroyImageView(pending.view, nullptr, dispatch);
		}

		if (pending.image != VK_NULL_HANDLE)
		{
			vmaDestroyImage(allocator, pending.image, pending.allocation);
		}
		else if (pending.buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, pending.buffer, pending.allocation);
		}

		if (pending.pipeline)
		{
			device.destroyPipeline(pending.pipeline, nullptr, dispatch);
		}

		if (pending.pipelineLayout)
		{
			device.destroyPipelineLayout(pending.pipelineLayout, nullptr, dispatch);
		}

		if (pending.semaphore)
		{
			device.destroySemaphore(pending.semaphore, nullptr, dispatch);
		}

		if (pending.queryPool)
		{
			device.destroyQueryPool(pending.queryPool, nullptr, dispatch);
		}

		if (pending.sampler)
		{
			device.destroySampler(pending.sampler, nullptr, dispatch);
		}

		if (pending.pipelineCache)
		{
			device.destroyPipelineCache(pending.pipelineCache, nullptr, dispatch);
		}

		if (pending.binarySemaphore)
		{
			device.destroySemaphore(pending.binarySemaphore, nullptr, dispatch);
		}

		if (pending.descriptorSetLayout)
		{
			device.destroyDescriptorSetLayout(pending.descriptorSetLayout, nullptr, dispatch);
		}

		if (pending.deviceMemory)
		{
			device.freeMemory(pending.deviceMemory, nullptr, dispatch);
		}
	}

	struct VulkanDevice final
	{
		const BackendObject * object = nullptr;
		vk::Instance instance;
		vk::PhysicalDevice phys;
		vk::Device device;

		vk::detail::DispatchLoaderDynamic dispatch;

		detail::HostVector<vk::Queue> graphicsQueues;
		std::uint32_t graphicsFamily = 0;
		detail::HostVector<vk::Queue> computeQueues;
		std::uint32_t computeFamily = 0;
		detail::HostVector<vk::Queue> copyQueues;
		std::uint32_t copyFamily = 0;

		bool graphicsFamilyBindsSparse = false;

		[[nodiscard]] const detail::HostVector<vk::Queue> & queues_for_type(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] std::uint32_t family_for_type(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeFamily;
			case QueueType::eCopy:	   return copyFamily;
			case QueueType::eGraphics: break;
			}

			return graphicsFamily;
		}

		VmaAllocator allocator	  = nullptr;
		ValidationMode validation = ValidationMode::eReleaseLight;
		bool debugUtils			  = false;
		bool debugNames			  = true;
		bool debugLabels		  = true;

		VulkanInstance * instanceWrapper = nullptr;

		std::uint32_t apiVersionMajor = 1;
		std::uint32_t apiVersionMinor = 3;
		bool coreVk13				  = true;

		bool dynamicRendering = true;

		bool unifiedImageLayouts = false;

		bool externalMemoryFd		= false;
		bool externalMemoryWin32	= false;
		bool externalSemaphoreFd	= false;
		bool externalSemaphoreWin32 = false;

		[[nodiscard]] bool shares_externally() const noexcept
		{
			return (externalMemoryFd || externalMemoryWin32) && (externalSemaphoreFd || externalSemaphoreWin32);
		}

		std::uint32_t deviceTag = 0;
		detail::HostVector<std::pair<SamplerYcbcrConversionDesc, vk::SamplerYcbcrConversion>> ycbcrConversions;

		DeviceCaps caps{};
		AdapterInfo adapter{};
		detail::HostString adapterName;
		detail::HostString driverInfo;
		detail::HostString driverVersionStr;

		HostUniquePtr<VulkanInstance> ownedInstance;

		vk::SurfaceKHR ownedSurface;

		SlotMap<BufferTag, BufferSlot> bufferSlots;

		SlotMap<TextureTag, TextureSlot> textureSlots;

		SlotMap<TextureViewTag, TextureViewSlot> textureViewSlots;

		SlotMap<PipelineLayoutTag, PipelineLayoutSlot> pipelineLayoutSlots;

		SlotMap<GraphicsPipelineTag, GraphicsPipelineSlot> graphicsPipelineSlots;

		detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> renderPasses;

		SlotMap<TimelineTag, TimelineSlot> timelineSlots;

		SlotMap<QueryPoolTag, QueryPoolSlot> queryPoolSlots;

		SlotMap<SamplerTag, SamplerSlot> samplerSlots;

		SlotMap<ComputePipelineTag, ComputePipelineSlot> computePipelineSlots;

		SlotMap<PipelineCacheTag, PipelineCacheSlot> pipelineCacheSlots;

		SlotMap<BinarySemaphoreTag, BinarySemaphoreSlot> binarySemaphoreSlots;

		SlotMap<DescriptorSetLayoutTag, DescriptorSetLayoutSlot> descriptorSetLayoutSlots;

		detail::HostVector<HostUniquePtr<VulkanDescriptorArena>> descriptorArenas;

		SlotMap<DescriptorSetTag, DescriptorSetSlot> descriptorSetSlots;

		SlotMap<HeapTag, HeapSlot> heapSlots;

		detail::HostVector<HostUniquePtr<VulkanSwapchain>> swapchains;
		std::uint32_t nextSwapchainId = 0;

		detail::HostVector<HostUniquePtr<VulkanQueue>> queues;

		// Built once at device create and never grown, so Submit may hold one across a submit while GetQueue runs elsewhere.
		detail::HostVector<HostUniquePtr<SubmitTimeline>> submitTimelines;

		detail::HostVector<HostUniquePtr<VulkanCommandPool>> commandPools;
		detail::HostVector<HostUniquePtr<VulkanCommandList>> commandLists;

		std::array<detail::HostVector<PendingFree>, kResourceTypeCount> garbage;
		std::atomic<std::uint64_t> pendingRetire{ 0 };

		VulkanDevice()									   = default;
		VulkanDevice(const VulkanDevice &)				   = delete;
		VulkanDevice & operator=(const VulkanDevice &)	   = delete;
		VulkanDevice(VulkanDevice &&) noexcept			   = delete;
		VulkanDevice & operator=(VulkanDevice &&) noexcept = delete;

		void destroy_pending_garbage() const
		{
			for (const detail::HostVector<PendingFree> & queue : garbage)
			{
				for (const PendingFree & pending : queue)
				{
					free_pending(device, dispatch, allocator, pending);
				}
			}
		}

		void destroy_submit_timelines() const
		{
			for (const HostUniquePtr<SubmitTimeline> & tracked : submitTimelines)
			{
				if (tracked->semaphore)
				{
					device.destroySemaphore(tracked->semaphore, nullptr, dispatch);
				}
			}
		}

		// vkDestroyCommandPool takes its buffers with it, so anything parked on a pool needs no separate free here.
		void destroy_command_pools() const
		{
			for (const HostUniquePtr<VulkanCommandPool> & cmdPool : commandPools)
			{
				for (const vk::Framebuffer framebuffer : cmdPool->framebuffers)
				{
					if (framebuffer)
					{
						device.destroyFramebuffer(framebuffer, nullptr, dispatch);
					}
				}

				for (const auto & [key, pass] : cmdPool->renderPasses)
				{
					if (pass)
					{
						device.destroyRenderPass(pass, nullptr, dispatch);
					}
				}

				if (cmdPool->pool)
				{
					device.destroyCommandPool(cmdPool->pool, nullptr, dispatch);
				}
			}
		}

		void destroy_swapchains() const
		{
			for (const HostUniquePtr<VulkanSwapchain> & sc : swapchains)
			{
				for (vk::Semaphore sem : sc->semaphores)
				{
					if (sem)
					{
						device.destroySemaphore(sem, nullptr, dispatch);
					}
				}

				destroy_swapchain(device, dispatch, allocator, sc->bundle);
			}
		}

		void destroy_owned_surface() const
		{
			if (ownedSurface)
			{
				instance.destroySurfaceKHR(ownedSurface, nullptr, dispatch);
			}
		}

		void destroy_texture_views()
		{
			textureViewSlots.for_each_live(
				[this](const TextureViewSlot & slot)
				{
					if (slot.lifetime == SlotLifetime::eOwned && slot.view)
					{
						device.destroyImageView(slot.view, nullptr, dispatch);
					}
				});
		}

		void destroy_textures()
		{
			textureSlots.for_each_live(
				[this](const TextureSlot & slot)
				{
					if (slot.lifetime != SlotLifetime::eOwned)
					{
						return;
					}

					if (slot.defaultView != VK_NULL_HANDLE)
					{
						device.destroyImageView(slot.defaultView, nullptr, dispatch);
					}

					if (slot.image != VK_NULL_HANDLE)
					{
						vmaDestroyImage(allocator, slot.image, slot.allocation);
					}
				});
		}

		void destroy_buffers()
		{
			bufferSlots.for_each_live(
				[this](const BufferSlot & slot)
				{
					if (slot.buffer != VK_NULL_HANDLE && slot.lifetime == SlotLifetime::eOwned)
					{
						vmaDestroyBuffer(allocator, slot.buffer, slot.allocation);
					}
				});
		}

		void destroy_graphics_pipelines()
		{
			graphicsPipelineSlots.for_each_live(
				[this](const GraphicsPipelineSlot & slot)
				{
					if (slot.pipeline)
					{
						device.destroyPipeline(slot.pipeline, nullptr, dispatch);
					}
				});
		}

		void destroy_render_passes()
		{
			for (const auto & [key, pass] : renderPasses)
			{
				if (pass)
				{
					device.destroyRenderPass(pass, nullptr, dispatch);
				}
			}
		}

		void destroy_pipeline_layouts()
		{
			pipelineLayoutSlots.for_each_live(
				[this](const PipelineLayoutSlot & slot)
				{
					if (slot.layout)
					{
						device.destroyPipelineLayout(slot.layout, nullptr, dispatch);
					}
				});
		}

		void destroy_timelines()
		{
			timelineSlots.for_each_live(
				[this](const TimelineSlot & slot)
				{
					if (slot.semaphore && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySemaphore(slot.semaphore, nullptr, dispatch);
					}
				});
		}

		void destroy_query_pools()
		{
			queryPoolSlots.for_each_live(
				[this](const QueryPoolSlot & slot)
				{
					if (slot.pool)
					{
						device.destroyQueryPool(slot.pool, nullptr, dispatch);
					}
				});
		}

		void destroy_samplers()
		{
			samplerSlots.for_each_live(
				[this](const SamplerSlot & slot)
				{
					if (slot.sampler && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySampler(slot.sampler, nullptr, dispatch);
					}
				});
		}

		void destroy_ycbcr_conversions()
		{
			for (const auto & [desc, conversion] : ycbcrConversions)
			{
				if (conversion)
				{
					device.destroySamplerYcbcrConversion(conversion, nullptr, dispatch);
				}
			}
			ycbcrConversions.clear();
		}

		void destroy_compute_pipelines()
		{
			computePipelineSlots.for_each_live(
				[this](const ComputePipelineSlot & slot)
				{
					if (slot.pipeline)
					{
						device.destroyPipeline(slot.pipeline, nullptr, dispatch);
					}
				});
		}

		void destroy_pipeline_caches()
		{
			pipelineCacheSlots.for_each_live(
				[this](const PipelineCacheSlot & slot)
				{
					if (slot.cache)
					{
						device.destroyPipelineCache(slot.cache, nullptr, dispatch);
					}
				});
		}

		void destroy_binary_semaphores()
		{
			binarySemaphoreSlots.for_each_live(
				[this](const BinarySemaphoreSlot & slot)
				{
					if (slot.semaphore && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySemaphore(slot.semaphore, nullptr, dispatch);
					}
				});
		}

		void destroy_descriptor_arenas()
		{
			for (const HostUniquePtr<VulkanDescriptorArena> & arena : descriptorArenas)
			{
				if (arena->pool)
				{
					device.destroyDescriptorPool(arena->pool, nullptr, dispatch);
				}
			}
		}

		void destroy_descriptor_set_layouts()
		{
			descriptorSetLayoutSlots.for_each_live(
				[this](const DescriptorSetLayoutSlot & slot)
				{
					if (slot.layout)
					{
						device.destroyDescriptorSetLayout(slot.layout, nullptr, dispatch);
					}
				});
		}

		void destroy_heaps()
		{
			heapSlots.for_each_live(
				[this](const HeapSlot & slot)
				{
					if (slot.memory)
					{
						device.freeMemory(slot.memory, nullptr, dispatch);
					}
				});
		}

		void destroy_allocator() const
		{
			if (allocator != nullptr)
			{
				vmaDestroyAllocator(allocator);
			}
		}

		void destroy_logical_device() const
		{
			if (device)
			{
				device.destroy(nullptr, dispatch);
			}
		}

		// vkDeviceWaitIdle never returns while a queue sits on a wait nobody will signal, so the drain is bounded and waits on
		// what this device actually submitted rather than on the queues going idle. Two seconds rather than something longer
		// because MoltenVK's own GPU watchdog fires near five, and a bound that lands after it would be the driver's rather
		// than ours. Two seconds is still some hundred frames of slack for work that was already submitted.
		static constexpr std::uint64_t kTeardownDrainNanoseconds = 2'000'000'000ULL;

		// Set by a drain that reached every submitted value, so the destructor does not wait again on what the destroy entry already waited for.
		bool submitTimelinesDrained = false;

		[[nodiscard]] bool drain_submit_timelines()
		{
			if (submitTimelinesDrained)
			{
				return true;
			}

			detail::HostVector<vk::Semaphore> semaphores;
			detail::HostVector<std::uint64_t> values;
			if (!detail::try_reserve(semaphores, submitTimelines.size()) || !detail::try_reserve(values, submitTimelines.size()))
			{
				return false;
			}

			for (const HostUniquePtr<SubmitTimeline> & tracked : submitTimelines)
			{
				const std::uint64_t reached = tracked->submitted.load(std::memory_order_acquire);
				if (!tracked->semaphore || reached == 0)
				{
					continue;
				}

				if (!detail::try_push_back(semaphores, tracked->semaphore) || !detail::try_push_back(values, reached))
				{
					return false;
				}
			}

			if (semaphores.empty())
			{
				submitTimelinesDrained = true;
				return true;
			}

			const vk::SemaphoreWaitInfo waitInfo({}, semaphores, values);
			submitTimelinesDrained = device.waitSemaphores(waitInfo, kTeardownDrainNanoseconds, dispatch) == vk::Result::eSuccess;
			return submitTimelinesDrained;
		}

		void report_teardown_stall() const
		{
			report_instance_message(instanceWrapper,
				ValidationMessageSeverity::eError,
				"vulkan teardown",
				"device destroyed while submitted work was still executing and did not drain in time, so its objects were leaked rather than destroyed");
		}

		~VulkanDevice()
		{
			// Nothing below may be destroyed while the GPU still references it. VulkanDestroyDevice drains first and leaks the whole
			// record when it cannot, so this is the backstop for a device destroyed any other way, such as one that failed to finish being built.
			if (device && !drain_submit_timelines())
			{
				// Every object below could be referenced by the work that did not finish, not just the semaphores and pools,
				// so the whole teardown is skipped. Leaking is recoverable where destroying in use is not.
				report_teardown_stall();
				return;
			}

			destroy_pending_garbage();
			destroy_command_pools();
			destroy_submit_timelines();
			destroy_swapchains();
			destroy_owned_surface();
			destroy_texture_views();
			destroy_textures();
			destroy_buffers();
			destroy_graphics_pipelines();
			destroy_render_passes();
			destroy_pipeline_layouts();
			destroy_timelines();
			destroy_query_pools();
			destroy_samplers();
			destroy_ycbcr_conversions();
			destroy_compute_pipelines();
			destroy_pipeline_caches();
			destroy_binary_semaphores();
			destroy_descriptor_arenas();
			destroy_descriptor_set_layouts();
			destroy_heaps();
			destroy_allocator();
			destroy_logical_device();
		}
	};

	constexpr std::uint32_t kDeviceBinarySemaphoreBit = 0x80000000u;
	constexpr std::uint32_t kSwapchainIdShift		  = 23u;
	constexpr std::uint32_t kSwapchainSlotMask		  = (1u << kSwapchainIdShift) - 1u;
	constexpr std::uint32_t kSwapchainIdMask		  = 0x7Fu;

	struct VulkanBackendOwner final
	{
		std::optional<vk::detail::DynamicLoader> loader;

		vk::detail::DispatchLoaderDynamic dispatch;

		detail::HostVector<HostUniquePtr<VulkanInstance>> instances;
		detail::HostVector<HostUniquePtr<VulkanDevice>> devices;
	};

	[[nodiscard]] constexpr std::uint32_t encode_wsi_semaphore(std::uint32_t swapchainId, std::uint32_t slot) noexcept
	{
		return ((swapchainId & kSwapchainIdMask) << kSwapchainIdShift) | (slot & kSwapchainSlotMask);
	}

	[[nodiscard]] BufferSlot * resolve_buffer(VulkanDevice * device, BufferHandle handle) noexcept;
	[[nodiscard]] bool bound_buffer_range(VkDeviceSize bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept;
	[[nodiscard]] vk::PipelineLayout resolve_pipeline_layout(VulkanDevice * device, PipelineLayoutHandle handle) noexcept;
	[[nodiscard]] vk::Image resolve_texture(const VulkanDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] const TextureViewSlot * resolve_texture_view_slot(const VulkanDevice * device, TextureViewHandle handle) noexcept;
	[[nodiscard]] vk::ImageView resolve_texture_view(const VulkanDevice * device, TextureViewHandle handle) noexcept;
	[[nodiscard]] vk::Pipeline resolve_graphics_pipeline(VulkanDevice * device, GraphicsPipelineHandle handle) noexcept;
	[[nodiscard]] vk::Semaphore resolve_timeline(VulkanDevice * device, TimelineHandle handle) noexcept;
	[[nodiscard]] vk::Semaphore resolve_binary_semaphore(VulkanDevice * device, BinarySemaphoreHandle handle) noexcept;
	[[nodiscard]] VulkanBackendOwner & backend_owner();
	[[nodiscard]] bool ensure_dispatcher_initialized(VulkanBackendOwner & owner);
	std::pair<std::uint32_t, std::uint32_t> resolve_api_version(ApiVersion requested) noexcept;
	std::uint32_t pack_vk_api_version(std::uint32_t major, std::uint32_t minor) noexcept;
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		[[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT types, const vk::DebugUtilsMessengerCallbackDataEXT * data, void * userData) noexcept;
	[[nodiscard]] HostUniquePtr<VulkanInstance> build_instance(const InstanceDesc & desc, Error * error);
	[[nodiscard]] VulkanInstance * make_owned_instance(const InstanceDesc & desc, Error * error);
	[[nodiscard]] VulkanDevice * make_owned_device(VulkanInstance * instance, const DeviceDesc & desc, Error * error);
	void vulkan_destroy_device(void * impl) noexcept;
	void vulkan_destroy_instance(void * impl) noexcept;
	bool succeed(Error * error) noexcept;
	bool fail(Error * error, ErrorCode code, const char * message) noexcept;
	bool fail_native(Error * error, const char * message, vk::Result result) noexcept;
	bool fail_allocation(Error * error, const char * message, vk::Result result) noexcept;
	GraphicsApiId vulkan_device_api_id([[maybe_unused]] void * impl) noexcept;
	std::string_view vulkan_device_api_name([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & vulkan_device_caps(void * impl) noexcept;
	const AdapterInfo & vulkan_device_adapter_info(void * impl) noexcept;
	ValidationMessageCounts vulkan_device_validation_message_counts(void * impl) noexcept;
	FormatSupport vulkan_device_format_support(void * impl, Format format) noexcept;
	bool vulkan_get_texture_info(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool vulkan_get_buffer_info(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	void name_vulkan_object(const VulkanDevice * device, vk::ObjectType type, std::uint64_t handle, CString name) noexcept;
	BufferHandle vulkan_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	MappedMemory vulkan_map(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept;
	bool vulkan_unmap(void * impl, BufferHandle handle, Error * error) noexcept;
	bool vulkan_flush_mapped_range(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept;
	bool vulkan_invalidate_mapped_range(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept;
	TextureHandle vulkan_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	[[nodiscard]] bool find_memory_type_for_heap(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, HeapType type,
		std::uint32_t & outIndex, bool & outHostVisible, bool & outCoherent) noexcept;
	[[nodiscard]] HeapSlot * resolve_heap(VulkanDevice * device, HeapHandle handle) noexcept;
	HeapHandle vulkan_create_heap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle vulkan_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle vulkan_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	bool vulkan_get_texture_memory_info(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool vulkan_get_buffer_memory_info(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	TextureViewHandle vulkan_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	PipelineLayoutHandle vulkan_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::RenderPass get_or_create_render_pass(
		VulkanDevice * device, detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> & cache, const RenderPassKey & key, vk::Result & outResult);
	[[nodiscard]] RenderPassKey make_pipeline_render_pass_key(const GraphicsPipelineDesc & desc) noexcept;
	[[nodiscard]] vk::PipelineCache resolve_pipeline_cache(VulkanDevice * device, PipelineCacheHandle handle) noexcept;
	GraphicsPipelineHandle vulkan_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	TimelineHandle vulkan_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::QueryType map_query_type(QueryType type) noexcept;
	[[nodiscard]] vk::QueryPipelineStatisticFlags map_pipeline_statistics(Flags<PipelineStatistic> stats) noexcept;
	QueryPoolHandle vulkan_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	[[nodiscard]] QueryPoolSlot * resolve_query_pool(VulkanDevice * device, QueryPoolHandle handle) noexcept;
	[[nodiscard]] vk::Filter map_filter(Filter filter) noexcept;
	[[nodiscard]] vk::SamplerMipmapMode map_mipmap_mode(MipmapMode mode) noexcept;
	[[nodiscard]] vk::SamplerAddressMode map_address_mode(AddressMode mode) noexcept;
	[[nodiscard]] vk::BorderColor map_border_color(BorderColor color) noexcept;
	SamplerHandle vulkan_create_sampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	ComputePipelineHandle vulkan_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::Pipeline resolve_compute_pipeline(const VulkanDevice * device, ComputePipelineHandle handle) noexcept;
	PipelineCacheHandle vulkan_create_pipeline_cache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept;
	bool vulkan_get_pipeline_cache_data(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept;
	BinarySemaphoreHandle vulkan_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & /*desc*/, Error * error) noexcept;
	bool vulkan_query_memory_budget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	bool vulkan_set_residency_priority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept;
	bool vulkan_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	void * vulkan_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * vulkan_command_pool_allocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool vulkan_command_pool_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	[[nodiscard]] bool recording_thread_mismatch(const VulkanCommandList * list) noexcept;
	bool vulkan_command_list_begin(void * impl, Error * error) noexcept;
	bool vulkan_command_list_end(void * impl, Error * error) noexcept;
	bool vulkan_cmd_set_viewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool vulkan_cmd_set_scissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool vulkan_cmd_copy_buffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool vulkan_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool vulkan_cmd_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
	bool vulkan_cmd_begin_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool vulkan_cmd_end_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool vulkan_cmd_resolve_query_data(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	[[nodiscard]] std::array<float, 4> unpack_label_color(std::uint32_t color) noexcept;
	bool vulkan_cmd_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool vulkan_cmd_end_debug_label(void * impl, Error * error) noexcept;
	bool vulkan_cmd_begin_native_mutation(void * impl, GraphicsApiId api, const NativeMutationDesc & /*unused*/, Error * error) noexcept;
	bool vulkan_cmd_end_native_mutation(void * impl, const NativeMutationDesc & /*unused*/, Error * error) noexcept;
	bool vulkan_queue_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool vulkan_queue_end_debug_label(void * impl, Error * error) noexcept;
	bool vulkan_cmd_barriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool vulkan_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool vulkan_cmd_begin_render_pass_scope(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool vulkan_cmd_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool vulkan_cmd_end_rendering(void * impl, Error * error) noexcept;
	bool vulkan_cmd_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool vulkan_cmd_push_constants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;
	bool vulkan_cmd_set_vertex_buffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool vulkan_cmd_set_index_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool vulkan_cmd_draw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool vulkan_cmd_draw_indexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool vulkan_cmd_draw_indirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool vulkan_cmd_draw_indexed_indirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool vulkan_cmd_draw_indirect_count(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	bool vulkan_cmd_draw_indexed_indirect_count(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	[[nodiscard]] vk::ImageSubresourceLayers map_subresource_layers(const TextureSubresource & sub) noexcept;
	bool vulkan_cmd_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool vulkan_cmd_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool vulkan_cmd_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	[[nodiscard]] bool format_supports_blit(const VulkanDevice * device, vk::Format format, bool asSource, bool linearFilter) noexcept;
	bool vulkan_cmd_blit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	bool vulkan_cmd_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool vulkan_cmd_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool vulkan_cmd_clear_texture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool vulkan_cmd_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool vulkan_cmd_set_blend_constants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool vulkan_cmd_set_stencil_reference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool vulkan_cmd_set_depth_bias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool vulkan_cmd_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool vulkan_cmd_dispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool vulkan_cmd_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	[[nodiscard]] vk::BufferUsageFlags map_buffer_usage(Flags<BufferUsage> usage) noexcept;

	[[nodiscard]] bool vulkan_refuse_ray_tracing_usage(Flags<BufferUsage> usage, bool supportsRayTracing, Error * error) noexcept;
	[[nodiscard]] VmaMemoryUsage map_memory_usage(MemoryUsage memory, VmaAllocationCreateFlags & outFlags) noexcept;
	[[nodiscard]] VmaMemoryUsage map_buffer_memory_usage(MemoryUsage memory, VmaAllocationCreateFlags & outFlags) noexcept;
	[[nodiscard]] vk::Format map_format(Format format) noexcept;
	[[nodiscard]] vk::ImageUsageFlags map_texture_usage(Flags<TextureUsage> usage) noexcept;
	[[nodiscard]] vk::ImageType map_image_type(TextureType type) noexcept;
	[[nodiscard]] vk::ImageViewType map_view_type(TextureType type) noexcept;
	[[nodiscard]] vk::SampleCountFlagBits map_sample_count(SampleCount samples) noexcept;
	[[nodiscard]] vk::PresentModeKHR map_present_mode(PresentMode mode) noexcept;
	[[nodiscard]] PresentMode map_vk_present_mode(vk::PresentModeKHR mode) noexcept;
	[[nodiscard]] Format map_vk_format(vk::Format format) noexcept;
	[[nodiscard]] bool adapter_supports_feature(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, DeviceFeature feature) noexcept;
	[[nodiscard]] const char * vulkan_adapter_refusal(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept;

	struct PortabilitySubsetFeatures final
	{
		VkStructureType sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		void * pNext		  = nullptr;
		VkBool32 constantAlphaColorBlendFactors{};
		VkBool32 events{};
		VkBool32 imageViewFormatReinterpretation{};
		VkBool32 imageViewFormatSwizzle{};
		VkBool32 imageView2DOn3DImage{};
		VkBool32 multisampleArrayImage{};
		VkBool32 mutableComparisonSamplers{};
		VkBool32 pointPolygons{};
		VkBool32 samplerMipLodBias{};
		VkBool32 separateStencilMaskRef{};
		VkBool32 shaderSampleRateInterpolationFunctions{};
		VkBool32 tessellationIsolines{};
		VkBool32 tessellationPointMode{};
		VkBool32 triangleFans{};
		VkBool32 vertexAttributeAccessBeyondStride{};
	};

	[[nodiscard]] bool query_portability_subset_features(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, PortabilitySubsetFeatures & out) noexcept;

	[[nodiscard]] bool adapter_supports_view_swizzle(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept;
	[[nodiscard]] bool adapter_supports_multi_planar_formats(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept;

	[[nodiscard]] vk::SamplerYcbcrConversion acquire_ycbcr_conversion(
		VulkanDevice * device, const SamplerYcbcrConversionDesc & desc, vk::Result & outResult) noexcept;
	[[nodiscard]] vk::ComponentMapping map_component_mapping(ComponentMapping mapping) noexcept;
	[[nodiscard]] bool adapter_supports_all_features(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, std::span<const DeviceFeature> features) noexcept;
	void enable_feature_bit(vk::PhysicalDeviceFeatures & features, vk::PhysicalDeviceVulkan11Features & features11, DeviceFeature feature) noexcept;
	[[nodiscard]] const char * required_feature_message(DeviceFeature feature) noexcept;
	[[nodiscard]] vk::ShaderStageFlags map_shader_stages(Flags<ShaderStage> stages) noexcept;
	[[nodiscard]] vk::ShaderStageFlagBits map_shader_stage_bit(ShaderStage stage) noexcept;
	[[nodiscard]] vk::PrimitiveTopology map_topology(PrimitiveTopology topology) noexcept;
	[[nodiscard]] vk::PolygonMode map_fill_mode(FillMode mode) noexcept;
	[[nodiscard]] vk::CullModeFlags map_cull_mode(CullMode mode) noexcept;
	[[nodiscard]] vk::FrontFace map_front_face(FrontFace face) noexcept;
	[[nodiscard]] vk::CompareOp map_compare_op(CompareOp op) noexcept;
	[[nodiscard]] vk::StencilOp map_stencil_op(StencilOp op) noexcept;
	[[nodiscard]] vk::StencilOpState map_stencil_face(const StencilFaceDesc & face) noexcept;
	[[nodiscard]] vk::BlendFactor map_blend_factor(BlendFactor factor) noexcept;
	[[nodiscard]] vk::BlendOp map_blend_op(BlendOp op) noexcept;
	[[nodiscard]] vk::ColorComponentFlags map_color_write_mask(Flags<ColorWrite> mask) noexcept;
	[[nodiscard]] detail::HostVector<vk::DynamicState> map_dynamic_states(Flags<DynamicState> states);
	[[nodiscard]] bool has_stencil_aspect(Format format) noexcept;
	[[nodiscard]] vk::ImageViewType map_image_view_type(TextureViewType type) noexcept;
	[[nodiscard]] vk::ImageAspectFlags map_aspect(Flags<TextureAspect> aspects) noexcept;
	[[nodiscard]] vk::AttachmentLoadOp map_load_op(LoadOp op) noexcept;
	[[nodiscard]] vk::AttachmentStoreOp map_store_op(StoreOp op) noexcept;
	[[nodiscard]] vk::ImageSubresourceRange map_subresource_range(const TextureSubresourceRange & range) noexcept;
	[[nodiscard]] vk::ImageAspectFlags aspect_for_view_format(vk::Format format) noexcept;
	[[nodiscard]] AdapterType map_adapter_type(vk::PhysicalDeviceType type) noexcept;
	[[nodiscard]] DriverId map_driver_id(vk::DriverId id) noexcept;
	void fill_adapter_identity(AdapterInfo & adapter, const vk::PhysicalDeviceIDProperties & id) noexcept;
	[[nodiscard]] vk::DescriptorType map_descriptor_type(DescriptorType type) noexcept;
	[[nodiscard]] vk::DescriptorSetLayout resolve_descriptor_set_layout(const VulkanDevice * device, DescriptorSetLayoutHandle handle) noexcept;
	[[nodiscard]] vk::Sampler resolve_sampler(const VulkanDevice * device, SamplerHandle handle) noexcept;
	[[nodiscard]] vk::DescriptorSet resolve_descriptor_set(const VulkanDevice * device, DescriptorSetHandle handle) noexcept;
	DescriptorSetLayoutHandle vulkan_create_descriptor_set_layout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
	void * vulkan_create_descriptor_arena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
	DescriptorSetHandle vulkan_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool vulkan_arena_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const DescriptorArenaApi & descriptor_arena_block() noexcept;
	bool vulkan_update_descriptors_buffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool vulkan_update_descriptors_texture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool vulkan_update_descriptors_sampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool vulkan_cmd_bind_descriptor_set(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool retire_native(VulkanDevice * device, ResourceType type, const DestroyDesc & desc, const PendingFree & pending, Error * error) noexcept;
	bool vulkan_collect_garbage(void * impl, ResourceType type, Error * error) noexcept;
	bool vulkan_collect_garbage_timeline(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept;
	bool vulkan_destroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept;
	GraphicsApiId vulkan_instance_api_id([[maybe_unused]] void * impl) noexcept;
	bool vulkan_enumerate_adapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool vulkan_query_external_handle_support(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;
	[[nodiscard]] ExternalHandleSupport vulkan_external_support_of(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch,
		const ExternalHandleSupportDesc & desc, vk::BufferUsageFlags bufferUsage) noexcept;
	[[nodiscard]] bool vulkan_refuse_unexportable(const VulkanDevice * device, Flags<ExternalHandleType> declared, ExternalObjectKind kind, Format format,
		vk::BufferUsageFlags bufferUsage, const char * what, Error * error) noexcept;

	inline constexpr vk::BufferUsageFlags kExternalQueryBufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;

	[[nodiscard]] std::optional<vk::ExternalMemoryHandleTypeFlagBits> map_memory_handle_type(ExternalHandleType type) noexcept;
	[[nodiscard]] std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> map_semaphore_handle_type(ExternalHandleType type) noexcept;
	[[nodiscard]] vk::ExternalMemoryHandleTypeFlags map_memory_handle_types(Flags<ExternalHandleType> types) noexcept;
	[[nodiscard]] vk::ExternalSemaphoreHandleTypeFlags map_semaphore_handle_types(Flags<ExternalHandleType> types) noexcept;

	bool vulkan_export_buffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool vulkan_export_heap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool vulkan_export_texture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool vulkan_export_timeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool vulkan_export_binary_semaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle vulkan_import_buffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle vulkan_import_heap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle vulkan_import_texture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle vulkan_import_timeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle vulkan_import_binary_semaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool vulkan_close_exported_handle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & external_sharing_block() noexcept;

	BufferHandle vulkan_adopt_buffer(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle vulkan_adopt_texture(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool vulkan_get_native_buffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool vulkan_get_native_texture(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept;
	TextureViewHandle vulkan_adopt_texture_view(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle vulkan_adopt_sampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool vulkan_get_native_texture_view(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool vulkan_get_native_sampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle vulkan_adopt_timeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle vulkan_adopt_binary_semaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool vulkan_get_native_timeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool vulkan_get_native_binary_semaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & adoption_block() noexcept;

	[[nodiscard]] bool vulkan_image_create_info(const TextureDesc & desc, vk::ImageCreateInfo & out, Error * error) noexcept;
	[[nodiscard]] TextureHandle vulkan_finish_texture(
		VulkanDevice * device, const TextureDesc & desc, VkImage image, VmaAllocation allocation, Error * error) noexcept;
	void * vulkan_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * vulkan_create_instance(const void * instanceDesc, Error * error) noexcept;
	void * vulkan_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	QueueType vulkan_queue_type(void * impl) noexcept;
	bool vulkan_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool vulkan_queue_bind_sparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
	bool vulkan_queue_wait_idle(void * impl, Error * error) noexcept;
	bool vulkan_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	[[nodiscard]] std::uint32_t acquire_submit_timeline(VulkanDevice * device, QueueType type, std::uint32_t index) noexcept;
	[[nodiscard]] bool build_submit_timelines(VulkanDevice * device, Error * error) noexcept;
	[[nodiscard]] bool caller_signal_reached(VulkanDevice * device, std::span<const TimelinePoint> callerSignals) noexcept;
	[[nodiscard]] bool submission_still_running(
		VulkanDevice * device, std::uint32_t submitTimeline, std::uint64_t submitValue, std::span<const TimelinePoint> callerSignals) noexcept;
	[[nodiscard]] bool list_still_running(const VulkanCommandList * list) noexcept;
	void sweep_retired_command_buffers(VulkanCommandPool * pool) noexcept;
	bool vulkan_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool vulkan_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	std::uint32_t queue_family_for_type(const VulkanDevice * device, QueueType type) noexcept;
	[[nodiscard]] bool rebuild_swapchain_semaphores(VulkanSwapchain * swapchain);
	[[nodiscard]] bool register_swapchain_back_buffers(VulkanSwapchain * swapchain);
	void * vulkan_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	bool vulkan_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool vulkan_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept;
	[[nodiscard]] SwapchainStatus map_swapchain_status(vk::Result result) noexcept;
	AcquireResult vulkan_acquire(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult vulkan_present(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept;
	Format vulkan_swapchain_format(void * impl) noexcept;
	PresentMode vulkan_swapchain_get_present_mode(void * impl) noexcept;
	bool vulkan_swapchain_supports_readback(void * impl) noexcept;
	std::uint32_t vulkan_swapchain_image_count(void * impl) noexcept;
	std::uint32_t vulkan_swapchain_width(void * impl) noexcept;
	std::uint32_t vulkan_swapchain_height(void * impl) noexcept;
	TextureHandle vulkan_swapchain_back_buffer(void * impl, std::uint32_t imageIndex) noexcept;
	TextureViewHandle vulkan_swapchain_back_buffer_view(void * impl, std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle vulkan_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept;
	const CoreDeviceApi & core_device_block() noexcept;
	const PresentApi & present_block() noexcept;
	const PlacedMemoryApi & placed_memory_block() noexcept;
	const ResourceIntrospectionApi & resource_introspection_block() noexcept;
	const QueryApi & query_block() noexcept;
	const PipelineCacheApi & pipeline_cache_block() noexcept;
	const ResidencyApi & residency_block() noexcept;
	const InstanceApi & instance_block() noexcept;
	const ExternalCapabilityApi & external_capability_block() noexcept;
	const SwapchainApi & swapchain_block() noexcept;
	const QueueApi & queue_block() noexcept;
	const SparseApi & sparse_block() noexcept;
	const CommandPoolApi & command_pool_block() noexcept;
	const RenderCommandApi & render_command_block() noexcept;
	const AliasingCommandApi & aliasing_command_block() noexcept;
	const QueryCommandApi & query_command_block() noexcept;
	const IndirectApi & indirect_block() noexcept;
	const IndirectCountApi & indirect_count_block() noexcept;
	const NativeEscapeApi & native_escape_block() noexcept;

	template <typename... Args>
	[[nodiscard]] Error * last_error(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	template <typename T>
	[[nodiscard]] bool store(T * out, T value, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
		}

		*out = std::move(value);
		return succeed(error);
	}

	template <typename T>
	[[nodiscard]] T return_value(T value, Error * error) noexcept
	{
		succeed(error);
		return value;
	}

	template <typename T>
	[[nodiscard]] T fail_value(Error * error, ErrorCode code, const char * message) noexcept
	{
		fail(error, code, message);
		return {};
	}

	template <typename T>
	[[nodiscard]] T fail_native_value(Error * error, const char * message, vk::Result result) noexcept
	{
		fail_native(error, message, result);
		return {};
	}

	template <typename T>
	[[nodiscard]] T fail_allocation_value(Error * error, const char * message, vk::Result result) noexcept
	{
		fail_allocation(error, message, result);
		return {};
	}

	template <typename T, typename... Args>
	T vulkan_unimplemented_value([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return fail_value<T>(last_error(args...), ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: operation not implemented yet");
	}

	template <typename... Args>
	bool vulkan_unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(args...);
		if constexpr (sizeof...(Args) > 1)
		{
			auto && value = std::get<sizeof...(Args) - 2>(tuple);
			using Value	  = std::remove_reference_t<decltype(value)>;
			if constexpr (std::is_pointer_v<Value> && !std::is_void_v<std::remove_pointer_t<Value>> && !std::is_const_v<std::remove_pointer_t<Value>>)
			{
				if (value != nullptr)
				{
					*value = {};
				}
			}
		}

		return fail(last_error(args...), ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: operation not implemented yet");
	}

}

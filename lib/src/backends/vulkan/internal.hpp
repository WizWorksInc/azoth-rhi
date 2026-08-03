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

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
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
		vk::Instance instance;

		/*
		 * This instance's own Vulkan-Hpp entry points, seeded from the loader's and then specialized to this VkInstance.
		 *
		 * Not the process-global default dispatcher because that is one table for the whole process and its init overwrites what every earlier instance is still
		 * using. Whether the validation layer sits in the chain is decided per instance so two instances that answered that differently need two tables. Every
		 * Vulkan-Hpp call on this instance passes this one explicitly.
		 */
		vk::detail::DispatchLoaderDynamic dispatch;

		bool debugUtils = false;							   // VK_EXT_debug_utils enabled for labels
		detail::HostVector<detail::HostString> adapterNames;   // stable storage backing AdapterInfo::name
		detail::HostVector<detail::HostString> driverInfos;	   // stable storage backing AdapterInfo::driverInfo
		detail::HostVector<detail::HostString> driverVersions; // stable storage backing AdapterInfo::driverVersion

		// The debug-utils messenger installed in the validating modes when debugUtils is set and the running tally of the validation messages it routes here. The
		// callback runs on whatever thread trips a check so the counts are atomic. A device reads them through its instanceWrapper.
		vk::DebugUtilsMessengerEXT debugMessenger;
		std::atomic<std::uint64_t> validationErrors{ 0 };
		std::atomic<std::uint64_t> validationWarnings{ 0 };

		// NativeValidationDesc breakOnError and breakOnWarning, read by the messenger callback. Plain bools: they are written once during instance creation, before
		// the messenger that reads them exists.
		bool breakOnError	= false;
		bool breakOnWarning = false;

		// NativeValidationDesc onMessage and messageUserData, written at the same point and for the same reason. Null is the stderr echo, which is what a Vulkan
		// message gets when nothing else is listening for it.
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

	// Stored per buffer: the VkBuffer, its VMA allocation, the size and whether the memory is coherent.
	struct BufferSlot final
	{
		VkBuffer buffer			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkDeviceSize size		 = 0;
		bool coherent			 = false;
		bool hostVisible		 = false; // memory the host can map so Map rejects a non-mappable buffer

		// Created with VMA_ALLOCATION_CREATE_MAPPED_BIT: Map reuses VMA's persistent mapping without calling vmaMapMemory so the map count stays zero and destroy
		// does not trip VMA's unmap assert.
		bool persistentMapped = false;

		// Placed buffers are bound into a heap's VkDeviceMemory instead of owning a VMA allocation (allocation stays null). Map/Unmap go through the heap memory at
		// this offset.
		VkDeviceMemory placedMemory = VK_NULL_HANDLE;
		VkDeviceSize placedOffset	= 0;

		// A sparse buffer owns neither an allocation nor placed memory: its pages arrive one at a time through bindSparse, so destroy releases the VkBuffer alone and
		// the heaps the pages came from outlive it on their own terms.
		bool sparse = false;

		// What BufferDesc declared, kept because vkGetMemoryFdKHR refuses a handle type the allocation was not created for. Export reads it, not asking the adapter
		// again: the adapter's answer covers what could have been declared, this covers what was.
		Flags<ExternalHandleType> exportableHandleTypes;

		// Who frees the VkBuffer. An adopted one owns neither an allocation nor placed memory, its store belonging to whoever handed it over.
		SlotLifetime lifetime = SlotLifetime::eOwned;

		// What the buffer was created with, answered by getBufferInfo. Last so every site that fills a slot names it last, designated initializers having to follow
		// declaration order. debugName is null: the name is borrowed for the creation call.
		BufferDesc desc{};
	};

	// Stored per texture: the VkImage, its VMA allocation and the default view that spans every mip and layer.
	struct TextureSlot final
	{
		VkImage image			 = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkImageView defaultView	 = VK_NULL_HANDLE;
		vk::Format format;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1; // propagated to views for the render-pass path
		std::uint32_t mipLevels			= 1;						   // recorded so a texture view's subresource range can be bounds checked
		std::uint32_t arrayLayers		= 1;						   // (the VkImage layer count, six of them for one cube)
		std::uint32_t width				= 1;						   // base mip extent, used by generateMips to derive each level
		std::uint32_t height			= 1;
		std::uint32_t depth				= 1;
		Format rhiFormat				= Format::eUndefined; // the portability format, which a plane view needs to resolve that plane's single-plane format
		Flags<TextureUsage> usage;							  // what a view over this image is judged against when it narrows nothing of its own
		bool mutableFormat = false;							  // created with eMutableFormat, so a view may name a format other than this one

		// A sparse image owns no allocation: its tiles arrive through bindSparse and destroy releases the VkImage alone.
		bool sparse = false;


		// What TextureDesc declared. Read by export for the same reason the buffer slot's copy is.
		Flags<ExternalHandleType> exportableHandleTypes;

		// Who frees the VkImage: this device, a swapchain, or the caller that adopted it in.
		SlotLifetime lifetime = SlotLifetime::eOwned;

		/*
		 * What the texture was created with, answered by getTextureInfo. The fields above are the same values in the shapes the record paths want them in, so both
		 * are written from this one at the same Store call. debugName is null here: the name is borrowed for the creation call.
		 *
		 * Last on purpose: designated initializers have to follow declaration order, and every site that fills a slot names this one last.
		 */
		TextureDesc desc{};
	};

	// Stored per texture view: the VkImageView a rendering attachment or descriptor binds.
	struct TextureViewSlot final
	{
		vk::ImageView view;
		// The view's own format and the source texture's sample count so the no-dynamic-rendering case can build a render pass and framebuffer attachment for a scope
		// that only names the view.
		vk::Format format				= vk::Format::eUndefined;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		SlotLifetime lifetime			= SlotLifetime::eOwned; // who frees the VkImageView, on the same three terms as the texture's
	};

	// Stored per pipeline layout: the VkPipelineLayout a graphics pipeline binds and push constants target, plus the set layouts it was built from so a pipeline
	// can rebuild the ShaderAbiLayout its shaders are checked against.
	struct PipelineLayoutSlot final
	{
		vk::PipelineLayout layout;
		detail::HostVector<DescriptorSetLayoutHandle> sets;
	};

	// Stored per graphics pipeline: the VkPipeline a draw binds.
	struct GraphicsPipelineSlot final
	{
		vk::Pipeline pipeline;
	};

	// Stored per timeline semaphore: the VkSemaphore (timeline type) used for CPU and GPU progress and frame pacing.
	struct TimelineSlot final
	{
		vk::Semaphore semaphore;

		// What TimelineDesc declared. vkGetSemaphoreFdKHR refuses a type the semaphore was not created exportable to, the same rule the memory side has.
		Flags<ExternalHandleType> exportableHandleTypes;

		// Who frees the VkSemaphore.
		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	// Stored per query pool: the VkQueryPool the timestamp, occlusion or pipeline-statistics commands target, and the count they refuse an index past.
	struct QueryPoolSlot final
	{
		vk::QueryPool pool;
		std::uint32_t queryCount = 0;
	};

	struct SamplerSlot final
	{
		vk::Sampler sampler;

		// Who frees the VkSampler. An adopted one belongs to whatever library made it, commonly alongside the Y'CbCr conversion it carries.
		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct ComputePipelineSlot final
	{
		vk::Pipeline pipeline;
	};

	// Holds the VkPipelineCache plus a buffer that getPipelineCacheData fills and hands back a view of so the returned pointer stays valid until the next query on
	// the same cache.
	struct PipelineCacheSlot final
	{
		vk::PipelineCache cache;
		detail::HostVector<std::uint8_t> data;
	};

	// A device-created binary semaphore (as opposed to the swapchain-owned WSI semaphores). Its handle index carries kDeviceBinarySemaphoreBit, which is what
	// ResolveBinarySemaphore tests to read the device registry and not a swapchain's semaphore array.
	struct BinarySemaphoreSlot final
	{
		vk::Semaphore semaphore;

		// What BinarySemaphoreDesc declared. Queried separately from a timeline's because Vulkan lets the two answer differently on one device.
		Flags<ExternalHandleType> exportableHandleTypes;

		// Who frees the VkSemaphore.
		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct DescriptorSetLayoutSlot final
	{
		vk::DescriptorSetLayout layout;

		/*
		 * What the layout was declared with, kept so a pipeline can check a shader's binding map against the ABI this set resolves under.
		 *
		 * immutableSamplers is cleared on the way in. The span is borrowed for the duration of creation only, and nothing here reads it back, so keeping it would
		 * leave a dangling span in a slot that outlives the call by design.
		 */
		detail::HostVector<DescriptorBinding> bindings;
	};

	struct VulkanDescriptorArena;

	// A descriptor set allocated from an arena's pool. A set outliving the reset that freed its VkDescriptorSet is refused above this, which is why the slot
	// carries nothing about the arena it came from.
	struct DescriptorSetSlot final
	{
		vk::DescriptorSet set;

		// What it was allocated against, kept because the dynamic offsets a bind carries have to be ordered by the layout and not by the order the caller listed
		// them, and a bind is given only the set.
		DescriptorSetLayoutHandle layout{};
	};

	// A heap is one VkDeviceMemory block that placed buffers and textures bind into at an offset. memory type and host visibility are fixed at allocation and
	// validated against each placed resource.
	struct HeapSlot final
	{
		vk::DeviceMemory memory;
		vk::DeviceSize size			  = 0;
		std::uint32_t memoryTypeIndex = 0;
		bool hostVisible			  = false;
		bool coherent				  = false;

		// What HeapDesc declared. A heap is the one memory-backed kind whose VkDeviceMemory this backend allocates itself, so the export structure is chained
		// straight into vkAllocateMemory, not through VMA.
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/*
	 * One attachment of a synthesized render pass (no dynamic rendering). Format and sample count decide pipeline and framebuffer compatibility. The load and
	 * store ops and the layout finish the key so a pass built for one scope is reused by every identical one. One layout is used for initial, subpass and final so
	 * the pass performs no implicit transitions, the RHI's explicit barriers owning those.
	 */
	struct RenderPassAttachmentKey final
	{
		vk::Format format				= vk::Format::eUndefined;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		vk::AttachmentLoadOp loadOp		= vk::AttachmentLoadOp::eDontCare;
		vk::AttachmentStoreOp storeOp	= vk::AttachmentStoreOp::eDontCare;
		vk::ImageLayout layout			= vk::ImageLayout::eUndefined;

		bool operator==(const RenderPassAttachmentKey &) const noexcept = default;
	};

	// The full description a synthesized VkRenderPass is keyed and built from: the color attachments in order plus an optional depth attachment last.
	struct RenderPassKey final
	{
		/*
		 * Reported wrongly by bugprone-invalid-enum-default-initialization. Value initializing the array runs each element's default member initializers so samples
		 * is e1 and not the zero the check assumes, which a reduced case compiled and printed to confirm.
		 */
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

	// A presentation swapchain, wrapping a SwapchainBundle: the swapchain, back buffer images and views, depth attachment, extent, format and present mode. Owned
	// by the device, which destroys it at teardown.
	struct VulkanSwapchain final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::SurfaceKHR surface;
		SwapchainBundle bundle;

		// WSI binary semaphores the swapchain owns, in one flat vector a BinarySemaphoreHandle indexes by slot: the first imageCount are the per-image
		// render-finished (present) semaphores, the rest are the acquire ring (imageCount + 1 of them, cycled so one is never reused while still pending).
		detail::HostVector<vk::Semaphore> semaphores;
		std::uint32_t acquireBase	= 0; // slot of the first acquire semaphore
		std::uint32_t acquireCount	= 0;
		std::uint32_t acquireCursor = 0;

		// Stable per-device id packed into the WSI semaphore handles this swapchain mints so a handle resolves against the swapchain that created it and not the
		// first one whose semaphore vector is long enough.
		std::uint32_t id = 0;

		// Per-image RHI handles for the back buffer color images and views, registered as borrowed device slots so getBackBuffer and getBackBufferView hand back
		// resolvable handles. Rebuilt on resize.
		detail::HostVector<TextureHandle> backBufferTextures;
		detail::HostVector<TextureViewHandle> backBufferViews;

		// Ordered creation preferences, primary choice first, reused so resize rebuilds the same way.
		detail::HostVector<vk::Format> desiredFormats;
		detail::HostVector<vk::PresentModeKHR> desiredPresentModes;
		std::uint32_t desiredImageCount = 0;
	};

	// A device queue. vk::Queue handles are owned by the logical device so this record only needs freeing (no Vulkan destroy).
	struct VulkanQueue final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::Queue queue;
		QueueType type			  = QueueType::eGraphics;
		std::uint32_t familyIndex = 0;
	};

	struct VulkanCommandList;

	// A command pool wrapping a VkCommandPool. Owned by the device, destroyed at teardown (its command buffers go with it).
	struct VulkanCommandPool final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::CommandPool pool;
		std::uint32_t family = 0; // the queue family its command lists record for

		/*
		 * Framebuffers built by BeginRendering without dynamic rendering. Each references the views a scope bound so it must outlive that command buffer's GPU
		 * execution. A pool is owned by one host thread so this needs no lock. Reset frees them (the caller's safeAfter proves the GPU is done), the same point the
		 * pool's command buffers are recycled.
		 */
		detail::HostVector<vk::Framebuffer> framebuffers;

		/*
		 * Every list this pool has built, and how many of them are currently out.
		 *
		 * A reset puts the pool's buffers back in the initial state, so the ones already built are the ones the next frame records into. Allocating a new
		 * buffer per call would grow both this and the device's list storage by one a frame. The cursor rewinds at Reset, so within a frame each allocate
		 * answers with a list nothing else is recording.
		 */
		detail::HostVector<VulkanCommandList *> lists;
		std::size_t handedOut = 0;

		/*
		 * Render passes built by BeginRendering without dynamic rendering, cached here and not on the device. BeginRendering records and recording is guarded in no
		 * threading mode, where a device-wide cache would need a lock against a concurrent create. A pool is owned by one host thread so a cache here needs nothing.
		 *
		 * Two pools may each build a pass for the same key and Vulkan asks only that a pass be compatible with the pipeline's. Freed with the pool.
		 */
		detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> renderPasses;
	};

	// A command list wrapping a primary VkCommandBuffer allocated from a pool. The buffer is freed with its pool so this record only needs freeing. The engine
	// reaches the native buffer through GetVulkanCommandBuffer to record raw Vulkan into it until the recording moves onto the RHI.
	struct VulkanCommandList final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		VulkanCommandPool * pool	 = nullptr; // the pool it came from, which owns the scratch framebuffers it builds
		vk::CommandBuffer buffer;
		std::uint32_t family = 0; // the queue family it was allocated for

		// A rendering scope's end timestamp, left by beginRendering for endRendering to write. Null outside a scope, and outside one that asked for no end timestamp.
		// Borrowed from the query pool slot, which the caller keeps alive across the scope.
		vk::QueryPool pendingEndTimestamp;
		std::uint32_t pendingEndTimestampQuery = 0;
	};

	// A descriptor arena wrapping one VkDescriptorPool. Owned by the device, freed at teardown.
	struct VulkanDescriptorArena final
	{
		const BackendObject * object = nullptr;
		VulkanDevice * owner		 = nullptr;
		vk::DescriptorPool pool;
	};

	// A native object whose RHI handle has been retired but whose backend release is deferred until a retire point passes. Only the fields for the one resource it
	// came from are set so FreePending releases whatever is present (an image view before its image).
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

	inline void FreePending(vk::Device device, const vk::detail::DispatchLoaderDynamic & dispatch, VmaAllocator allocator, const PendingFree & pending) noexcept
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
		vk::Instance instance; // non-owning, owned by the VulkanInstance
		vk::PhysicalDevice phys;
		vk::Device device;

		/*
		 * This device's own Vulkan-Hpp entry points: a copy of its instance's table, specialized to this VkDevice.
		 *
		 * Copied, not shared because init(VkDevice) rewrites the device-level entries in place so a table shared with a sibling device would hand that sibling this
		 * device's function pointers. The instance-level entries survive the copy, which is what lets instance calls the device owns (destroying its surface, for
		 * one) go through this table too.
		 */
		vk::detail::DispatchLoaderDynamic dispatch;

		/*
		 * Queues created for each capability, honoring the DeviceDesc queue requests. Each family supplies as many queues as the request asks for, clamped to the
		 * family's queueCount. Compute and copy resolve to the most specific capable family (an async compute family without graphics, a transfer family without
		 * graphics or compute), falling back to the graphics family when the adapter exposes no dedicated one. When a family is shared, the types map onto the same
		 * underlying queues.
		 */
		detail::HostVector<vk::Queue> graphicsQueues;
		std::uint32_t graphicsFamily = 0;
		detail::HostVector<vk::Queue> computeQueues;
		std::uint32_t computeFamily = 0;
		detail::HostVector<vk::Queue> copyQueues;
		std::uint32_t copyFamily = 0;

		/*
		 * Whether the graphics family can bind sparse memory, which is a queue capability and not a device feature.
		 *
		 * An adapter can enable every sparse feature and still have no family carrying VK_QUEUE_SPARSE_BINDING_BIT, in which case there is nothing to call
		 * vkQueueBindSparse on. Only the graphics family is asked, since that is the one this backend binds on.
		 */
		bool graphicsFamilyBindsSparse = false;

		[[nodiscard]] const detail::HostVector<vk::Queue> & QueuesForType(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] std::uint32_t FamilyForType(QueueType type) const noexcept
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
		bool debugUtils			  = false; // VK_EXT_debug_utils enabled on the instance (labels)
		bool debugNames			  = true;  // DeviceDesc.enableDebugNames: name objects for captures and validation.
		bool debugLabels		  = true;  // DeviceDesc.enableDebugLabels: bracket command spans for captures and profilers.

		// The instance this device was made from, owned (static form) or borrowed (dynamic form). Used to read the validation-message counts the instance's debug
		// messenger keeps. Always set at creation.
		VulkanInstance * instanceWrapper = nullptr;

		/*
		 * The Vulkan version this device targets (default 1.3). coreVk13 is true when >= 1.3, selecting the core synchronization2 and dynamic-rendering entry points.
		 * Below 1.3 the KHR-suffixed variants are used (the features come from VK_KHR_synchronization2 and VK_KHR_dynamic_rendering instead).
		 */
		std::uint32_t apiVersionMajor = 1;
		std::uint32_t apiVersionMinor = 3;
		bool coreVk13				  = true;

		// Whether rendering scopes lower onto vkCmdBeginRendering (true) or synthesized render-pass and framebuffer objects (false). Decided at creation from
		// DeviceDesc::dynamicRendering and adapter support and mirrored in caps.supportsDynamicRendering.
		bool dynamicRendering = true;

		/*
		 * Which external transports this device enabled, feature tested per extension, not inferred from the platform.
		 *
		 * The four are separate because a driver answers them separately and because the two families are separate extensions. The block is published only when a
		 * memory transport and a semaphore transport are both here, since memory whose readiness nothing can signal is memory whose only correct use is idling the
		 * device.
		 */
		bool externalMemoryFd		= false; // VK_KHR_external_memory_fd
		bool externalMemoryWin32	= false; // VK_KHR_external_memory_win32
		bool externalSemaphoreFd	= false; // VK_KHR_external_semaphore_fd
		bool externalSemaphoreWin32 = false; // VK_KHR_external_semaphore_win32

		[[nodiscard]] bool SharesExternally() const noexcept
		{
			return (externalMemoryFd || externalMemoryWin32) && (externalSemaphoreFd || externalSemaphoreWin32);
		}

		// Tags this device's handles so another device rejects them. Returned to the pool at teardown.
		std::uint32_t deviceTag = 0;
		// Y'CbCr conversions this device has handed out, keyed by the description that made them. Vulkan compares these by object identity, so the same description
		// has to come back as the same handle for a sampler and its view to agree.
		detail::HostVector<std::pair<SamplerYcbcrConversionDesc, vk::SamplerYcbcrConversion>> ycbcrConversions;

		DeviceCaps caps{};
		AdapterInfo adapter{};
		detail::HostString adapterName;		 // stable storage backing adapter.name
		detail::HostString driverInfo;		 // stable storage backing adapter.driverInfo
		detail::HostString driverVersionStr; // stable storage backing adapter.driverVersion

		/*
		 * The static CreateDevice form has the device own its instance and tear it down with itself so teardown happens when the owning UniqueDevice is reset. Not at
		 * process exit, where the loader or a validation layer may already be unloaded. Null on the dynamic form, where the instance lives in the process owner.
		 */
		HostUniquePtr<VulkanInstance> ownedInstance;

		// A surface this device owns and destroys after its swapchains and before its instance, the one correct order. Set by platform code through
		// SetVulkanDeviceSurface. Null if the device has none.
		vk::SurfaceKHR ownedSurface;

		SlotMap<BufferTag, BufferSlot> bufferSlots;

		SlotMap<TextureTag, TextureSlot> textureSlots;

		SlotMap<TextureViewTag, TextureViewSlot> textureViewSlots;

		SlotMap<PipelineLayoutTag, PipelineLayoutSlot> pipelineLayoutSlots;

		SlotMap<GraphicsPipelineTag, GraphicsPipelineSlot> graphicsPipelineSlots;

		// Render-pass objects synthesized when dynamic rendering is off, keyed by attachment formats, ops and layouts. Empty and unused when dynamicRendering is
		// true. Built on demand by graphics pipeline creation and BeginRendering, destroyed at teardown.
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
		std::uint32_t nextSwapchainId = 0; // hands each swapchain a stable id for its WSI semaphore handles

		detail::HostVector<HostUniquePtr<VulkanQueue>> queues;

		detail::HostVector<HostUniquePtr<VulkanCommandPool>> commandPools;
		detail::HostVector<HostUniquePtr<VulkanCommandList>> commandLists;

		// Objects whose handle has been retired but whose native release was deferred to a retire point. Drained by CollectGarbage and, for anything still pending,
		// by this destructor.
		/*
		 * One queue a kind and not one queue a device, which is what lets this hold no lock.
		 *
		 * A destroy appends while it holds the RHI's guard for the kind it destroyed and a collect drains one kind while holding that same guard so the queue is
		 * already serialized by a lock the caller took. One shared queue would need a second lock taken under the first, which is the nesting the cooperative rules
		 * forbid.
		 */
		std::array<detail::HostVector<PendingFree>, kResourceTypeCount> garbage;
		std::atomic<std::uint64_t> pendingRetire{ 0 };

		VulkanDevice()									   = default;
		VulkanDevice(const VulkanDevice &)				   = delete;
		VulkanDevice & operator=(const VulkanDevice &)	   = delete;
		VulkanDevice(VulkanDevice &&) noexcept			   = delete;
		VulkanDevice & operator=(VulkanDevice &&) noexcept = delete;

		void DestroyPendingGarbage() const
		{
			for (const detail::HostVector<PendingFree> & queue : garbage)
			{
				for (const PendingFree & pending : queue)
				{
					FreePending(device, dispatch, allocator, pending);
				}
			}
		}

		void DestroyCommandPools() const
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

		void DestroySwapchains() const
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

				DestroySwapchain(device, dispatch, allocator, sc->bundle);
			}
		}

		void DestroyOwnedSurface() const
		{
			if (ownedSurface)
			{
				instance.destroySurfaceKHR(ownedSurface, nullptr, dispatch);
			}
		}

		void DestroyTextureViews()
		{
			textureViewSlots.ForEachLive(
				[this](const TextureViewSlot & slot)
				{
					// Only what this device made. A swapchain frees its own views and an adopted one belongs to whoever handed it over.
					if (slot.lifetime == SlotLifetime::eOwned && slot.view)
					{
						device.destroyImageView(slot.view, nullptr, dispatch);
					}
				});
		}

		void DestroyTextures()
		{
			textureSlots.ForEachLive(
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

		void DestroyBuffers()
		{
			bufferSlots.ForEachLive(
				[this](const BufferSlot & slot)
				{
					// Only what this device made. An adopted VkBuffer is freed by whatever library handed it over.
					if (slot.buffer != VK_NULL_HANDLE && slot.lifetime == SlotLifetime::eOwned)
					{
						vmaDestroyBuffer(allocator, slot.buffer, slot.allocation);
					}
				});
		}

		void DestroyGraphicsPipelines()
		{
			graphicsPipelineSlots.ForEachLive(
				[this](const GraphicsPipelineSlot & slot)
				{
					if (slot.pipeline)
					{
						device.destroyPipeline(slot.pipeline, nullptr, dispatch);
					}
				});
		}

		void DestroyRenderPasses()
		{
			for (const auto & [key, pass] : renderPasses)
			{
				if (pass)
				{
					device.destroyRenderPass(pass, nullptr, dispatch);
				}
			}
		}

		void DestroyPipelineLayouts()
		{
			pipelineLayoutSlots.ForEachLive(
				[this](const PipelineLayoutSlot & slot)
				{
					if (slot.layout)
					{
						device.destroyPipelineLayout(slot.layout, nullptr, dispatch);
					}
				});
		}

		void DestroyTimelines()
		{
			timelineSlots.ForEachLive(
				[this](const TimelineSlot & slot)
				{
					if (slot.semaphore && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySemaphore(slot.semaphore, nullptr, dispatch);
					}
				});
		}

		void DestroyQueryPools()
		{
			queryPoolSlots.ForEachLive(
				[this](const QueryPoolSlot & slot)
				{
					if (slot.pool)
					{
						device.destroyQueryPool(slot.pool, nullptr, dispatch);
					}
				});
		}

		void DestroySamplers()
		{
			samplerSlots.ForEachLive(
				[this](const SamplerSlot & slot)
				{
					if (slot.sampler && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySampler(slot.sampler, nullptr, dispatch);
					}
				});
		}

		// The conversions the device cached for samplers and views. They outlive both, being shared, so they go after the samplers that named them.
		void DestroyYcbcrConversions()
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

		void DestroyComputePipelines()
		{
			computePipelineSlots.ForEachLive(
				[this](const ComputePipelineSlot & slot)
				{
					if (slot.pipeline)
					{
						device.destroyPipeline(slot.pipeline, nullptr, dispatch);
					}
				});
		}

		void DestroyPipelineCaches()
		{
			pipelineCacheSlots.ForEachLive(
				[this](const PipelineCacheSlot & slot)
				{
					if (slot.cache)
					{
						device.destroyPipelineCache(slot.cache, nullptr, dispatch);
					}
				});
		}

		void DestroyBinarySemaphores()
		{
			binarySemaphoreSlots.ForEachLive(
				[this](const BinarySemaphoreSlot & slot)
				{
					if (slot.semaphore && slot.lifetime == SlotLifetime::eOwned)
					{
						device.destroySemaphore(slot.semaphore, nullptr, dispatch);
					}
				});
		}

		void DestroyDescriptorArenas()
		{
			for (const HostUniquePtr<VulkanDescriptorArena> & arena : descriptorArenas)
			{
				if (arena->pool)
				{
					device.destroyDescriptorPool(arena->pool, nullptr, dispatch);
				}
			}
		}

		void DestroyDescriptorSetLayouts()
		{
			descriptorSetLayoutSlots.ForEachLive(
				[this](const DescriptorSetLayoutSlot & slot)
				{
					if (slot.layout)
					{
						device.destroyDescriptorSetLayout(slot.layout, nullptr, dispatch);
					}
				});
		}

		// Frees the heap memory blocks. Runs after DestroyBuffers/DestroyTextures, which destroy the placed VkBuffer/VkImage handles without freeing this memory.
		void DestroyHeaps()
		{
			heapSlots.ForEachLive(
				[this](const HeapSlot & slot)
				{
					if (slot.memory)
					{
						device.freeMemory(slot.memory, nullptr, dispatch);
					}
				});
		}

		void DestroyAllocator() const
		{
			if (allocator != nullptr)
			{
				vmaDestroyAllocator(allocator);
			}
		}

		void DestroyLogicalDevice() const
		{
			if (device)
			{
				device.destroy(nullptr, dispatch);
			}
		}

		// Release resources before VMA, then destroy the logical device last.
		~VulkanDevice()
		{
			DestroyPendingGarbage();
			DestroyCommandPools();
			DestroySwapchains();
			DestroyOwnedSurface();
			DestroyTextureViews();
			DestroyTextures();
			DestroyBuffers();
			DestroyGraphicsPipelines();
			DestroyRenderPasses();
			DestroyPipelineLayouts();
			DestroyTimelines();
			DestroyQueryPools();
			DestroySamplers();
			DestroyYcbcrConversions();
			DestroyComputePipelines();
			DestroyPipelineCaches();
			DestroyBinarySemaphores();
			DestroyDescriptorArenas();
			DestroyDescriptorSetLayouts();
			DestroyHeaps();
			DestroyAllocator();
			DestroyLogicalDevice();
		}
	};

	constexpr std::uint32_t kDeviceBinarySemaphoreBit = 0x80000000u;
	constexpr std::uint32_t kSwapchainIdShift		  = 23u;
	constexpr std::uint32_t kSwapchainSlotMask		  = (1u << kSwapchainIdShift) - 1u; // low 23 bits: the slot
	constexpr std::uint32_t kSwapchainIdMask		  = 0x7Fu;							// next 7 bits: the swapchain id

	/*
	 * Process lifetime owner for the loader, the instances and the devices.
	 *
	 * Unguarded. The RHI serializes createInstance, createDevice, destroyDevice and destroyInstance, which are the only entries that reach any of this so a
	 * backend writes no synchronization of its own.
	 */
	struct VulkanBackendOwner final
	{
		std::optional<vk::detail::DynamicLoader> loader;

		/*
		 * The loader-level entry points, holding only what can be called before there is an instance: the instance and layer enumerations and createInstance itself.
		 * Every instance table starts as a copy of this one.
		 */
		vk::detail::DispatchLoaderDynamic dispatch;

		detail::HostVector<HostUniquePtr<VulkanInstance>> instances;
		detail::HostVector<HostUniquePtr<VulkanDevice>> devices;
	};

	[[nodiscard]] constexpr std::uint32_t EncodeWsiSemaphore(std::uint32_t swapchainId, std::uint32_t slot) noexcept
	{
		return ((swapchainId & kSwapchainIdMask) << kSwapchainIdShift) | (slot & kSwapchainSlotMask);
	}

	// Declared here so every Vulkan translation unit can reach the others.
	[[nodiscard]] BufferSlot * ResolveBuffer(VulkanDevice * device, BufferHandle handle) noexcept;
	[[nodiscard]] bool BoundBufferRange(VkDeviceSize bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept;
	[[nodiscard]] vk::PipelineLayout ResolvePipelineLayout(VulkanDevice * device, PipelineLayoutHandle handle) noexcept;
	[[nodiscard]] vk::Image ResolveTexture(const VulkanDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] const TextureViewSlot * ResolveTextureViewSlot(const VulkanDevice * device, TextureViewHandle handle) noexcept;
	[[nodiscard]] vk::ImageView ResolveTextureView(const VulkanDevice * device, TextureViewHandle handle) noexcept;
	[[nodiscard]] vk::Pipeline ResolveGraphicsPipeline(VulkanDevice * device, GraphicsPipelineHandle handle) noexcept;
	[[nodiscard]] vk::Semaphore ResolveTimeline(VulkanDevice * device, TimelineHandle handle) noexcept;
	[[nodiscard]] vk::Semaphore ResolveBinarySemaphore(VulkanDevice * device, BinarySemaphoreHandle handle) noexcept;
	[[nodiscard]] VulkanBackendOwner & Owner();
	[[nodiscard]] bool EnsureDispatcherInitialized(VulkanBackendOwner & owner);
	std::pair<std::uint32_t, std::uint32_t> ResolveApiVersion(ApiVersion requested) noexcept;
	std::uint32_t PackVkApiVersion(std::uint32_t major, std::uint32_t minor) noexcept;
	VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		[[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT types, const vk::DebugUtilsMessengerCallbackDataEXT * data, void * userData) noexcept;
	[[nodiscard]] HostUniquePtr<VulkanInstance> BuildInstance(const InstanceDesc & desc, Error * error);
	[[nodiscard]] VulkanInstance * MakeOwnedInstance(const InstanceDesc & desc, Error * error);
	[[nodiscard]] VulkanDevice * MakeOwnedDevice(VulkanInstance * instance, const DeviceDesc & desc, Error * error);
	void VulkanDestroyDevice(void * impl) noexcept;
	void VulkanDestroyInstance(void * impl) noexcept;
	bool Succeed(Error * error) noexcept;
	bool Fail(Error * error, ErrorCode code, const char * message) noexcept;
	GraphicsApiId VulkanDeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view VulkanDeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & VulkanDeviceCaps(void * impl) noexcept;
	const AdapterInfo & VulkanDeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts VulkanDeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport VulkanDeviceFormatSupport(void * impl, Format format) noexcept;
	bool VulkanGetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool VulkanGetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	void NameVulkanObject(const VulkanDevice * device, vk::ObjectType type, std::uint64_t handle, CString name) noexcept;
	BufferHandle VulkanCreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	MappedMemory VulkanMap(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept;
	bool VulkanUnmap(void * impl, BufferHandle handle, Error * error) noexcept;
	bool VulkanFlushMappedRange(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept;
	bool VulkanInvalidateMappedRange(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept;
	TextureHandle VulkanCreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	[[nodiscard]] bool FindMemoryTypeForHeap(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, HeapType type,
		std::uint32_t & outIndex, bool & outHostVisible, bool & outCoherent) noexcept;
	[[nodiscard]] HeapSlot * ResolveHeap(VulkanDevice * device, HeapHandle handle) noexcept;
	HeapHandle VulkanCreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle VulkanCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle VulkanCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	bool VulkanGetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool VulkanGetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	TextureViewHandle VulkanCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	PipelineLayoutHandle VulkanCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::RenderPass GetOrCreateRenderPass(
		VulkanDevice * device, detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> & cache, const RenderPassKey & key);
	[[nodiscard]] RenderPassKey MakePipelineRenderPassKey(const GraphicsPipelineDesc & desc) noexcept;
	[[nodiscard]] vk::PipelineCache ResolvePipelineCache(VulkanDevice * device, PipelineCacheHandle handle) noexcept;
	GraphicsPipelineHandle VulkanCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	TimelineHandle VulkanCreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::QueryType MapQueryType(QueryType type) noexcept;
	[[nodiscard]] vk::QueryPipelineStatisticFlags MapPipelineStatistics(Flags<PipelineStatistic> stats) noexcept;
	QueryPoolHandle VulkanCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	[[nodiscard]] QueryPoolSlot * ResolveQueryPool(VulkanDevice * device, QueryPoolHandle handle) noexcept;
	[[nodiscard]] vk::Filter MapFilter(Filter filter) noexcept;
	[[nodiscard]] vk::SamplerMipmapMode MapMipmapMode(MipmapMode mode) noexcept;
	[[nodiscard]] vk::SamplerAddressMode MapAddressMode(AddressMode mode) noexcept;
	[[nodiscard]] vk::BorderColor MapBorderColor(BorderColor color) noexcept;
	SamplerHandle VulkanCreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	ComputePipelineHandle VulkanCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] vk::Pipeline ResolveComputePipeline(const VulkanDevice * device, ComputePipelineHandle handle) noexcept;
	PipelineCacheHandle VulkanCreatePipelineCache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept;
	bool VulkanGetPipelineCacheData(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept;
	BinarySemaphoreHandle VulkanCreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & /*desc*/, Error * error) noexcept;
	bool VulkanQueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	bool VulkanSetResidencyPriority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept;
	bool VulkanCalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	void * VulkanCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * VulkanCommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool VulkanCommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	[[nodiscard]] bool RecordingThreadMismatch(const VulkanCommandList * list) noexcept;
	bool VulkanCommandListBegin(void * impl, Error * error) noexcept;
	bool VulkanCommandListEnd(void * impl, Error * error) noexcept;
	bool VulkanCmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool VulkanCmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool VulkanCmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool VulkanCmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool VulkanCmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error * error) noexcept;
	bool VulkanCmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool VulkanCmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool VulkanCmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	[[nodiscard]] std::array<float, 4> UnpackLabelColor(std::uint32_t color) noexcept;
	bool VulkanCmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool VulkanCmdEndDebugLabel(void * impl, Error * error) noexcept;
	bool VulkanCmdBeginNativeMutation(void * impl, GraphicsApiId api, const NativeMutationDesc & /*desc*/, Error * error) noexcept;
	bool VulkanCmdEndNativeMutation(void * impl, const NativeMutationDesc & /*desc*/, Error * error) noexcept;
	bool VulkanQueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool VulkanQueueEndDebugLabel(void * impl, Error * error) noexcept;
	bool VulkanCmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool VulkanCmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool VulkanCmdBeginRenderPassScope(VulkanCommandList * list, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool VulkanCmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool VulkanCmdEndRendering(void * impl, Error * error) noexcept;
	bool VulkanCmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool VulkanCmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;
	bool VulkanCmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool VulkanCmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool VulkanCmdDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool VulkanCmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool VulkanCmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool VulkanCmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool VulkanCmdDrawIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	bool VulkanCmdDrawIndexedIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	[[nodiscard]] vk::ImageSubresourceLayers MapSubresourceLayers(const TextureSubresource & sub) noexcept;
	bool VulkanCmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool VulkanCmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool VulkanCmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	[[nodiscard]] bool FormatSupportsBlit(const VulkanDevice * device, vk::Format format, bool asSource, bool linearFilter) noexcept;
	bool VulkanCmdBlit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	bool VulkanCmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool VulkanCmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool VulkanCmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool VulkanCmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool VulkanCmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool VulkanCmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool VulkanCmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool VulkanCmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool VulkanCmdDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool VulkanCmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	[[nodiscard]] vk::BufferUsageFlags MapBufferUsage(Flags<BufferUsage> usage) noexcept;
	[[nodiscard]] VmaMemoryUsage MapMemoryUsage(MemoryUsage memory, bool persistentMap, VmaAllocationCreateFlags & outFlags) noexcept;
	[[nodiscard]] vk::Format MapFormat(Format format) noexcept;
	[[nodiscard]] vk::ImageUsageFlags MapTextureUsage(Flags<TextureUsage> usage) noexcept;
	[[nodiscard]] vk::ImageType MapImageType(TextureType type) noexcept;
	[[nodiscard]] vk::ImageViewType MapViewType(TextureType type) noexcept;
	[[nodiscard]] vk::SampleCountFlagBits MapSampleCount(SampleCount samples) noexcept;
	[[nodiscard]] vk::PresentModeKHR MapPresentMode(PresentMode mode) noexcept;
	[[nodiscard]] PresentMode MapVkPresentMode(vk::PresentModeKHR mode) noexcept;
	[[nodiscard]] Format MapVkFormat(vk::Format format) noexcept;
	[[nodiscard]] bool AdapterSupportsFeature(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, DeviceFeature feature) noexcept;

	/*
	 * Portability subset feature bits, mirrored, not included.
	 *
	 * VkPhysicalDevicePortabilitySubsetFeaturesKHR sits behind VK_ENABLE_BETA_EXTENSIONS, which would pull every other beta extension in with it for the sake of
	 * one struct. The layout is fixed by revision 1 of a stable extension and the structure type is a core enumerant, so a local mirror chained by sType reads and
	 * writes the same bits without flipping that switch for the whole build.
	 */
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

	/**
	 * \brief Reads what the portability subset offers, or false when the adapter is a full implementation and the extension is absent.
	 *
	 * Both what a device enables and what its caps report come from here, so the two cannot disagree. That matters because the portability rules are validated
	 * against the enabled value and not the supported one: a feature left off the create chain is off however the adapter answered.
	 */
	[[nodiscard]] bool QueryPortabilitySubsetFeatures(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, PortabilitySubsetFeatures & out) noexcept;

	[[nodiscard]] bool AdapterSupportsViewSwizzle(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept;
	[[nodiscard]] bool AdapterSupportsMultiPlanarFormats(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept;

	/**
	 * \brief Hands back the one VkSamplerYcbcrConversion this device uses for a given description, creating it on first ask.
	 *
	 * Vulkan requires the sampler and the view it reads to name the same conversion object, so equal descriptions have to resolve to one handle and not to two
	 * that merely agree. Cached per device and destroyed with it, the objects being immutable and cheap to keep.
	 */
	[[nodiscard]] vk::SamplerYcbcrConversion AcquireYcbcrConversion(VulkanDevice * device, const SamplerYcbcrConversionDesc & desc) noexcept;
	[[nodiscard]] vk::ComponentMapping MapComponentMapping(ComponentMapping mapping) noexcept;
	[[nodiscard]] bool AdapterSupportsAllFeatures(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, std::span<const DeviceFeature> features) noexcept;
	void EnableFeatureBit(vk::PhysicalDeviceFeatures & features, vk::PhysicalDeviceVulkan11Features & features11, DeviceFeature feature) noexcept;
	[[nodiscard]] const char * RequiredFeatureMessage(DeviceFeature feature) noexcept;
	[[nodiscard]] vk::ShaderStageFlags MapShaderStages(Flags<ShaderStage> stages) noexcept;
	[[nodiscard]] vk::ShaderStageFlagBits MapShaderStageBit(ShaderStage stage) noexcept;
	[[nodiscard]] vk::PrimitiveTopology MapTopology(PrimitiveTopology topology) noexcept;
	[[nodiscard]] vk::PolygonMode MapFillMode(FillMode mode) noexcept;
	[[nodiscard]] vk::CullModeFlags MapCullMode(CullMode mode) noexcept;
	[[nodiscard]] vk::FrontFace MapFrontFace(FrontFace face) noexcept;
	[[nodiscard]] vk::CompareOp MapCompareOp(CompareOp op) noexcept;
	[[nodiscard]] vk::StencilOp MapStencilOp(StencilOp op) noexcept;
	[[nodiscard]] vk::StencilOpState MapStencilFace(const StencilFaceDesc & face) noexcept;
	[[nodiscard]] vk::BlendFactor MapBlendFactor(BlendFactor factor) noexcept;
	[[nodiscard]] vk::BlendOp MapBlendOp(BlendOp op) noexcept;
	[[nodiscard]] vk::ColorComponentFlags MapColorWriteMask(Flags<ColorWrite> mask) noexcept;
	[[nodiscard]] detail::HostVector<vk::DynamicState> MapDynamicStates(Flags<DynamicState> states);
	[[nodiscard]] bool HasStencilAspect(Format format) noexcept;
	[[nodiscard]] vk::ImageViewType MapImageViewType(TextureViewType type) noexcept;
	[[nodiscard]] vk::PipelineStageFlags2 MapStages2(Flags<PipelineStage> stages) noexcept;
	[[nodiscard]] vk::AccessFlags2 MapAccess2(Flags<Access> access) noexcept;
	[[nodiscard]] vk::ImageLayout MapTextureLayout(TextureLayout layout) noexcept;
	[[nodiscard]] vk::ImageAspectFlags MapAspect(Flags<TextureAspect> aspects) noexcept;
	[[nodiscard]] vk::AttachmentLoadOp MapLoadOp(LoadOp op) noexcept;
	[[nodiscard]] vk::AttachmentStoreOp MapStoreOp(StoreOp op) noexcept;
	[[nodiscard]] vk::ImageSubresourceRange MapSubresourceRange(const TextureSubresourceRange & range) noexcept;
	[[nodiscard]] vk::ImageAspectFlags AspectForViewFormat(vk::Format format) noexcept;
	[[nodiscard]] AdapterType MapAdapterType(vk::PhysicalDeviceType type) noexcept;
	[[nodiscard]] DriverId MapDriverId(vk::DriverId id) noexcept;
	void FillAdapterIdentity(AdapterInfo & adapter, const vk::PhysicalDeviceIDProperties & id) noexcept;
	[[nodiscard]] vk::DescriptorType MapDescriptorType(DescriptorType type) noexcept;
	[[nodiscard]] vk::DescriptorSetLayout ResolveDescriptorSetLayout(const VulkanDevice * device, DescriptorSetLayoutHandle handle) noexcept;
	[[nodiscard]] vk::Sampler ResolveSampler(const VulkanDevice * device, SamplerHandle handle) noexcept;
	[[nodiscard]] vk::DescriptorSet ResolveDescriptorSet(const VulkanDevice * device, DescriptorSetHandle handle) noexcept;
	DescriptorSetLayoutHandle VulkanCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
	void * VulkanCreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
	DescriptorSetHandle VulkanArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool VulkanArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;
	bool VulkanUpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool VulkanUpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool VulkanUpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool VulkanCmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool RetireNative(VulkanDevice * device, ResourceType type, const DestroyDesc & desc, const PendingFree & pending, Error * error) noexcept;
	bool VulkanCollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool VulkanCollectGarbageTimeline(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept;
	bool VulkanDestroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept;
	GraphicsApiId VulkanInstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool VulkanEnumerateAdapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool VulkanQueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;
	/*
	 * What this adapter shares for one kind and handle type.
	 *
	 * bufferUsage is read for the buffer and heap kinds alone and cannot be zero, which is what Vulkan's external buffer query demands. The adapter query asks
	 * with the transfer pair, being the floor for a buffer that crosses this boundary at all, while creation asks with the usage the caller actually declared, so
	 * what creation enforces is exact even where a driver differentiates.
	 */
	[[nodiscard]] ExternalHandleSupport VulkanExternalSupportOf(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch,
		const ExternalHandleSupportDesc & desc, vk::BufferUsageFlags bufferUsage) noexcept;
	[[nodiscard]] bool VulkanRefuseUnexportable(const VulkanDevice * device, Flags<ExternalHandleType> declared, ExternalObjectKind kind, Format format,
		vk::BufferUsageFlags bufferUsage, const char * what, Error * error) noexcept;

	// The usage the adapter-scoped query asks about, where no object exists yet to read one from.
	inline constexpr vk::BufferUsageFlags kExternalQueryBufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;

	/*
	 * The RHI handle type as Vulkan names it, for the two families that name it separately. A type with no counterpart in a family returns nothing, not a nearest
	 * neighbour, since answering with a substitute would report support for a pairing that cannot exist.
	 *
	 * Shared between the capability query and the create and export paths so all three read one table. Two tables would let what a caller is told and what
	 * creation enforces drift apart.
	 */
	[[nodiscard]] std::optional<vk::ExternalMemoryHandleTypeFlagBits> MapMemoryHandleType(ExternalHandleType type) noexcept;
	[[nodiscard]] std::optional<vk::ExternalSemaphoreHandleTypeFlagBits> MapSemaphoreHandleType(ExternalHandleType type) noexcept;
	[[nodiscard]] vk::ExternalMemoryHandleTypeFlags MapMemoryHandleTypes(Flags<ExternalHandleType> types) noexcept;
	[[nodiscard]] vk::ExternalSemaphoreHandleTypeFlags MapSemaphoreHandleTypes(Flags<ExternalHandleType> types) noexcept;

	bool VulkanExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool VulkanExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool VulkanExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool VulkanExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool VulkanExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle VulkanImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle VulkanImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle VulkanImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle VulkanImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle VulkanImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool VulkanCloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;

	BufferHandle VulkanAdoptBuffer(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle VulkanAdoptTexture(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool VulkanGetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool VulkanGetNativeTexture(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept;
	TextureViewHandle VulkanAdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle VulkanAdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool VulkanGetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool VulkanGetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle VulkanAdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle VulkanAdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool VulkanGetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool VulkanGetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;

	// Shared by texture creation and texture import so both build the same image from one description and finish it the same way. See their definitions.
	[[nodiscard]] bool VulkanImageCreateInfo(const TextureDesc & desc, vk::ImageCreateInfo & out, Error * error) noexcept;
	[[nodiscard]] TextureHandle VulkanFinishTexture(
		VulkanDevice * device, const TextureDesc & desc, VkImage image, VmaAllocation allocation, Error * error) noexcept;
	void * VulkanInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * VulkanCreateInstance(const void * instanceDesc, Error * error) noexcept;
	void * VulkanGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	QueueType VulkanQueueType(void * impl) noexcept;
	std::uint32_t VulkanQueueFamilyIndex(void * impl) noexcept;
	bool VulkanQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool VulkanQueueBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
	bool VulkanQueueWaitIdle(void * impl, Error * error) noexcept;
	bool VulkanQueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool VulkanQueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool VulkanQueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	std::uint32_t QueueFamilyForType(const VulkanDevice * device, QueueType type) noexcept;
	[[nodiscard]] bool RebuildSwapchainSemaphores(VulkanSwapchain * swapchain);
	[[nodiscard]] bool RegisterSwapchainBackBuffers(VulkanSwapchain * swapchain);
	void * VulkanCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	bool VulkanSwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool VulkanSwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	[[nodiscard]] SwapchainStatus MapSwapchainStatus(vk::Result result) noexcept;
	AcquireResult VulkanAcquire(void * impl, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult VulkanPresent(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept;
	Format VulkanSwapchainFormat(void * impl) noexcept;
	PresentMode VulkanSwapchainGetPresentMode(void * impl) noexcept;
	bool VulkanSwapchainSupportsReadback(void * impl) noexcept;
	std::uint32_t VulkanSwapchainImageCount(void * impl) noexcept;
	std::uint32_t VulkanSwapchainWidth(void * impl) noexcept;
	std::uint32_t VulkanSwapchainHeight(void * impl) noexcept;
	TextureHandle VulkanSwapchainBackBuffer(void * impl, std::uint32_t imageIndex) noexcept;
	TextureViewHandle VulkanSwapchainBackBufferView(void * impl, std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle VulkanSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const PipelineCacheApi & PipelineCacheBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const SparseApi & SparseBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const AliasingCommandApi & AliasingCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const IndirectCountApi & IndirectCountBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;

	template <typename... Args>
	[[nodiscard]] Error * LastError(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	template <typename T>
	[[nodiscard]] bool Store(T * out, T value, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
		}

		*out = std::move(value);
		return Succeed(error);
	}

	template <typename T>
	[[nodiscard]] T ReturnValue(T value, Error * error) noexcept
	{
		Succeed(error);
		return value;
	}

	template <typename T>
	[[nodiscard]] T FailValue(Error * error, ErrorCode code, const char * message) noexcept
	{
		Fail(error, code, message);
		return {};
	}

	template <typename T, typename... Args>
	T VulkanUnimplementedValue([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<T>(LastError(args...), ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: operation not implemented yet");
	}

	template <typename... Args>
	bool VulkanUnimplemented([[maybe_unused]] void * impl, Args... args) noexcept
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

		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "Vulkan RHI backend: operation not implemented yet");
	}

} // namespace azo::rhi::vulkan

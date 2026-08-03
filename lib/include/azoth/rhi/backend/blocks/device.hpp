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
 * \brief Device-level ABI blocks published by backend device objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string_view>

namespace azo::rhi
{

	/**
	 * \brief Required backend device table.
	 *
	 * Handles returned by create functions are invalid on failure. Backend child objects such as queues, command pools, and descriptor arenas return nullptr on
	 * failure. Functions that take Error * may leave it untouched on success and write backend-specific failure detail when they return a failure value.
	 */
	struct CoreDeviceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(CoreDeviceApi), .version = 1 };

		/**
		 * \brief Backend identity for dispatch selection, native access, and diagnostics.
		 */
		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept		 = nullptr;
		std::string_view (*getGraphicsApiName)(void * impl) noexcept = nullptr;

		/// Resource creation.

		BufferHandle (*createBuffer)(void * impl, const BufferDesc & desc, Error * error) noexcept										  = nullptr;
		TextureHandle (*createTexture)(void * impl, const TextureDesc & desc, Error * error) noexcept									  = nullptr;
		TextureViewHandle (*createTextureView)(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept  = nullptr;
		SamplerHandle (*createSampler)(void * impl, const SamplerDesc & desc, Error * error) noexcept									  = nullptr;
		DescriptorSetLayoutHandle (*createDescriptorSetLayout)(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept = nullptr;
		PipelineLayoutHandle (*createPipelineLayout)(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept				  = nullptr;
		GraphicsPipelineHandle (*createGraphicsPipeline)(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept		  = nullptr;
		ComputePipelineHandle (*createComputePipeline)(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept			  = nullptr;
		TimelineHandle (*createTimeline)(void * impl, const TimelineDesc & desc, Error * error) noexcept								  = nullptr;
		BinarySemaphoreHandle (*createBinarySemaphore)(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept			  = nullptr;

		/// Backend child-object creation.

		/**
		 * \note Returned objects publish their own ABI blocks and are wrapped by public facades outside the backend.
		 */
		void * (*createDescriptorArena)(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept = nullptr;
		void * (*createCommandPool)(void * impl, const CommandPoolDesc & desc, Error * error) noexcept		   = nullptr;
		void * (*getQueue)(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept		   = nullptr;

		/// CPU mapping.

		/**
		 * \attention Flush and invalidate ranges are byte ranges in the mapped buffer, not element ranges.
		 */
		MappedMemory (*map)(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept								  = nullptr;
		bool (*unmap)(void * impl, BufferHandle buffer, Error * error) noexcept															  = nullptr;
		bool (*flushMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept	  = nullptr;
		bool (*invalidateMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		/// Descriptor updates.

		bool (*updateDescriptorsBuffer)(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept	  = nullptr;
		bool (*updateDescriptorsTexture)(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept = nullptr;
		bool (*updateDescriptorsSampler)(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept = nullptr;

		/// Device queries.

		/**
		 * \note Returned references must remain valid for the lifetime of the backend device object.
		 */
		const DeviceCaps & (*getCaps)(void * impl) noexcept							= nullptr;
		FormatSupport (*getFormatSupport)(void * impl, Format format) noexcept		= nullptr;
		const AdapterInfo & (*getAdapterInfo)(void * impl) noexcept					= nullptr;
		ValidationMessageCounts (*getValidationMessageCounts)(void * impl) noexcept = nullptr;

		/// Destruction and deferred collection.

		/**
		 * \attention destroy may only retire a resource when DestroyDesc asks for deferred destruction. collectGarbage completes release once the retire point
		 * has passed.
		 */
		bool (*destroy)(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept							  = nullptr;
		bool (*collectGarbage)(void * impl, ResourceType type, Error * error) noexcept																  = nullptr;
		bool (*collectGarbageTimeline)(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept = nullptr;

		/**
		 * \attention After destroyDevice returns, no other function in any block published by this backend device may be called.
		 */
		void (*destroyDevice)(void * impl) noexcept = nullptr;
	};

	/**
	 * \brief Optional presentation table for devices that can create swapchains.
	 */
	struct PresentApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PresentApi), .version = 1 };

		void * (*createSwapchain)(void * impl, const SwapchainDesc & desc, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional placed-memory table for explicit heap allocation and resource placement.
	 *
	 * \note Resource memory-info queries describe backend placement requirements before a heap-backed resource is created.
	 */
	struct PlacedMemoryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PlacedMemoryApi), .version = 1 };

		HeapHandle (*createHeap)(void * impl, const HeapDesc & desc, Error * error) noexcept						  = nullptr;
		BufferHandle (*createPlacedBuffer)(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept		  = nullptr;
		TextureHandle (*createPlacedTexture)(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept	  = nullptr;
		bool (*getTextureMemoryInfo)(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;
		bool (*getBufferMemoryInfo)(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept	  = nullptr;
	};

	/**
	 * \brief Optional device table for ray tracing pipeline objects, acceleration structures, and acceleration-structure descriptor writes.
	 *
	 * \note Designed and unimplemented as of 2026-07-31. Only the Null backend publishes this with entries that do anything. Vulkan and Direct3D 12 decline it
	 * despite both having first-class ray tracing, and Metal publishes it with both creation entries refusing. An external backend may implement it and the host
	 * dispatches to it correctly.
	 */
	struct RayTracingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(RayTracingApi), .version = 1 };

		RayTracingPipelineHandle (*createRayTracingPipeline)(void * impl, const RayTracingPipelineDesc & desc, Error * error) noexcept			= nullptr;
		AccelerationStructureHandle (*createAccelerationStructure)(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept = nullptr;
		bool (*updateDescriptorsAccelerationStructure)(
			void * impl, std::span<const DescriptorWriteAccelerationStructure> writes, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional query table for query-pool creation and timestamp calibration.
	 *
	 * \note Timestamp calibration is queue-type specific because different queue domains may expose different timestamp behavior.
	 */
	struct QueryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueryApi), .version = 1 };

		QueryPoolHandle (*createQueryPool)(void * impl, const QueryPoolDesc & desc, Error * error) noexcept				 = nullptr;
		bool (*calibrateTimestamp)(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional pipeline-cache table for creating caches and extracting backend cache blobs.
	 */
	struct PipelineCacheApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PipelineCacheApi), .version = 1 };

		PipelineCacheHandle (*createPipelineCache)(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept		  = nullptr;
		bool (*getPipelineCacheData)(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional residency table for memory-budget queries and backend residency-priority hints.
	 *
	 * \note Residency priority is a hint to pageable backends. It is not a guarantee that a resource will remain resident.
	 */
	struct ResidencyApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResidencyApi), .version = 1 };

		bool (*queryMemoryBudget)(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept				 = nullptr;
		bool (*setResidencyPriority)(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional table for reading back what a resource was created with, given only its handle.
	 *
	 * Anything layered over the RHI receives resources as handles. Without this the extent, format and mip count of a texture must travel beside every handle. A
	 * backend holds this already so publishing costs storage, not work.
	 *
	 * Optional, not part of CoreDeviceApi because the required set is a ratchet. A backend with nothing to introspect declines it and is still a backend.
	 */
	struct ResourceIntrospectionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResourceIntrospectionApi), .version = 1 };

		/**
		 * \brief What a texture was created with.
		 *
		 * TextureInfo::desc::debugName is always null. The name a caller passed at creation is borrowed for the duration of that call, so no backend owns a copy to
		 * hand back.
		 *
		 * \attention A swapchain back buffer never came from a TextureDesc. Refuse it without inventing one, since the swapchain answers for its own.
		 */
		bool (*getTextureInfo)(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief What a buffer was created with, plus what the allocation actually cost and whether the host can reach it.
		 *
		 * BufferInfo::desc::debugName is always null, for the reason the texture form's is.
		 */
		bool (*getBufferInfo)(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional table for adopting a live native object made on this device and reading back what a handle stands for.
	 *
	 * Distinct from ExternalSharingApi below, which crosses an API, device or process boundary. Adopting needs no extension.
	 *
	 * \attention The object must have been made on the device behind this one. No graphics API lets the RHI check this.
	 * \attention nativeImport and outNativeImport are API-specific payloads selected by the GraphicsApiId argument, whose layout the RHI does not inspect.
	 */
	struct AdoptionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(AdoptionApi), .version = 1 };

		BufferHandle (*adoptBuffer)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept = nullptr;
		TextureHandle (*adoptTexture)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept			= nullptr;
		bool (*getNativeBuffer)(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept	= nullptr;
		bool (*getNativeTexture)(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Views and samplers, which are adoptable in their own right, not rebuildable from an adopted texture.
		 *
		 * A view or a sampler can carry a Y'CbCr conversion, which the RHI has no surface for naming and so no way to reconstruct. A backend whose views are
		 * descriptors, not objects declines these four by name.
		 */
		TextureViewHandle (*adoptTextureView)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept = nullptr;
		SamplerHandle (*adoptSampler)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept				 = nullptr;
		bool (*getNativeTextureView)(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept = nullptr;
		bool (*getNativeSampler)(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept		 = nullptr;

		/**
		 * \brief Synchronization objects, adopted on the same terms as everything else here.
		 *
		 * Adopting a semaphore made on this device is the same operation as adopting an image: record the object, allocate nothing, need no extension. Exporting one
		 * so another process can wait on it is ExternalSharingApi's job and needs both.
		 */
		TimelineHandle (*adoptTimeline)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept = nullptr;
		BinarySemaphoreHandle (*adoptBinarySemaphore)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept	   = nullptr;
		bool (*getNativeTimeline)(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept = nullptr;
		bool (*getNativeBinarySemaphore)(
			void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional device table for moving memory and synchronization across an API, device or process boundary.
	 *
	 * Distinct from AdoptionApi above, which adopts a live native object made on this same device and needs no extension. Memory and synchronization ship together
	 * because importing a handle aliases memory without ordering it.
	 *
	 * \note A resource crossing this boundary also needs a queue family ownership transfer, a recorded barrier naming kExternalQueueFamily on the far side.
	 * Skipping it leaves the contents undefined.
	 */
	struct ExternalSharingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalSharingApi), .version = 1 };

		/**
		 * \brief Produces a handle for an object created with that handle type in its exportable set.
		 *
		 * \note The caller owns what comes back and releases it through closeExportedHandle, whether or not it imported it anywhere first.
		 */
		bool (*exportBuffer)(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept	 = nullptr;
		bool (*exportHeap)(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept			 = nullptr;
		bool (*exportTexture)(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;
		/**
		 * \brief Produces a handle for a synchronization primitive created with that handle type in its exportable set.
		 *
		 * \attention Handing a semaphore out takes on the other side's ordering rules, which can be stricter than this one's. Vulkan permits waiting on a
		 * timeline value before the signal reaching it is submitted. CUDA forbids wait before signal on an imported semaphore. A program legal here can deadlock
		 * under a stricter importer. Nothing here can see the far side to check.
		 */
		bool (*exportTimeline)(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;
		bool (*exportBinarySemaphore)(
			void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Builds an object over memory or a synchronization primitive another component produced.
		 *
		 * \note An import is validated by the implementation and fails with a defined error without producing a broken object. A handle from the wrong
		 * device is refused here without becoming a caller precondition.
		 *
		 * \note An import never consumes the handle it is given. A file descriptor is duplicated, not taken so the object built here outlives whatever
		 * the caller does with its own copy.
		 */
		BufferHandle (*importBuffer)(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept							= nullptr;
		HeapHandle (*importHeap)(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept									= nullptr;
		TextureHandle (*importTexture)(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept							= nullptr;
		TimelineHandle (*importTimeline)(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept						= nullptr;
		BinarySemaphoreHandle (*importBinarySemaphore)(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases a handle this device produced or was given.
		 *
		 * Accepts every exported handle and every handle a caller obtained elsewhere and imported here, before or after that import, since an import consumes
		 * nothing. It deliberately leaves the global share handle alone, which nobody owns. A caller releases uniformly without branching on rules the three target
		 * APIs disagree about.
		 */
		bool (*closeExportedHandle)(void * impl, const ExternalHandle & handle, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

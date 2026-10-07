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

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace azo::rhi
{

	/**
	 * \brief Capability and adapter references remain valid for the device lifetime.
	 */
	struct CoreDeviceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(CoreDeviceApi), .version = 1 };

		/**
		 * \brief Returns the backend's graphics API identifier.
		 */
		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept		 = nullptr;
		/**
		 * \brief Returns the backend's graphics API name.
		 */
		std::string_view (*getGraphicsApiName)(void * impl) noexcept = nullptr;

		/**
		 * \brief Creates a buffer.
		 */
		BufferHandle (*createBuffer)(void * impl, const BufferDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a texture.
		 */
		TextureHandle (*createTexture)(void * impl, const TextureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a view of a texture.
		 */
		TextureViewHandle (*createTextureView)(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a sampler.
		 */
		SamplerHandle (*createSampler)(void * impl, const SamplerDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a descriptor set layout.
		 */
		DescriptorSetLayoutHandle (*createDescriptorSetLayout)(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a pipeline layout.
		 */
		PipelineLayoutHandle (*createPipelineLayout)(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a graphics pipeline.
		 */
		GraphicsPipelineHandle (*createGraphicsPipeline)(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a compute pipeline.
		 */
		ComputePipelineHandle (*createComputePipeline)(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a timeline for GPU synchronization.
		 */
		TimelineHandle (*createTimeline)(void * impl, const TimelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a binary semaphore.
		 */
		BinarySemaphoreHandle (*createBinarySemaphore)(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a descriptor arena.
		 */
		void * (*createDescriptorArena)(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a command pool.
		 */
		void * (*createCommandPool)(void * impl, const CommandPoolDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Returns the queue at a zero based index within its type, or nullptr if unavailable.
		 */
		void * (*getQueue)(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept = nullptr;

		/**
		 * \brief Maps a byte range of a buffer for host access.
		 * \return A null data pointer on failure.
		 */
		MappedMemory (*map)(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases an outstanding buffer mapping.
		 */
		bool (*unmap)(void * impl, BufferHandle buffer, Error * error) noexcept = nullptr;

		/**
		 * \brief Flushes host writes in a mapped buffer range.
		 * \param impl Device.
		 * \param buffer Mapped buffer.
		 * \param offset Byte offset from the start of the buffer.
		 * \param size Number of bytes in the range.
		 * \param[out] error Optional error details.
		 */
		bool (*flushMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		/**
		 * \brief Invalidates host caches for a mapped buffer range.
		 * \param impl Device.
		 * \param buffer Mapped buffer.
		 * \param offset Byte offset from the start of the buffer.
		 * \param size Number of bytes in the range.
		 * \param[out] error Optional error details.
		 */
		bool (*invalidateMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes buffer bindings into descriptor sets.
		 */
		bool (*updateDescriptorsBuffer)(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes texture bindings into descriptor sets.
		 */
		bool (*updateDescriptorsTexture)(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes sampler bindings into descriptor sets.
		 */
		bool (*updateDescriptorsSampler)(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Returns the device's capability limits and supported features.
		 */
		const DeviceCaps & (*getCaps)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns the supported uses of a resource format.
		 */
		FormatSupport (*getFormatSupport)(void * impl, Format format) noexcept = nullptr;

		/**
		 * \brief Returns information about the device's adapter.
		 */
		const AdapterInfo & (*getAdapterInfo)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns counts of reported validation messages.
		 */
		ValidationMessageCounts (*getValidationMessageCounts)(void * impl) noexcept = nullptr;

		/**
		 * \brief Destroys a resource using the supplied retirement policy.
		 */
		bool (*destroy)(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases pending resources once the caller has made them safe.
		 */
		bool (*collectGarbage)(void * impl, ResourceType type, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases resources retired at or before a known completed timeline value.
		 * \param impl Device.
		 * \param type Resource kind.
		 * \param timeline Timeline used to retire the resources.
		 * \param completedValue Known completed value.
		 * \param[out] error Optional error details.
		 */
		bool (*collectGarbageTimeline)(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept = nullptr;

		/**
		 * \brief Destroys the backend device instance.
		 */
		void (*destroyDevice)(void * impl) noexcept = nullptr;
	};

	struct PresentApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PresentApi), .version = 1 };

		/**
		 * \brief Creates a swapchain.
		 */
		void * (*createSwapchain)(void * impl, const SwapchainDesc & desc, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Placed resource offsets and queried memory sizes and alignments are in bytes.
	 */
	struct PlacedMemoryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PlacedMemoryApi), .version = 1 };

		/**
		 * \brief Creates a heap for placed resources.
		 */
		HeapHandle (*createHeap)(void * impl, const HeapDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a buffer at a specified heap offset.
		 */
		BufferHandle (*createPlacedBuffer)(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a texture at a specified heap offset.
		 */
		TextureHandle (*createPlacedTexture)(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries the memory required by a texture description.
		 */
		bool (*getTextureMemoryInfo)(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries the memory required by a buffer description.
		 */
		bool (*getBufferMemoryInfo)(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;
	};

	struct RayTracingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(RayTracingApi), .version = 1 };

		/**
		 * \brief Creates a ray tracing pipeline.
		 */
		RayTracingPipelineHandle (*createRayTracingPipeline)(void * impl, const RayTracingPipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates an acceleration structure.
		 */
		AccelerationStructureHandle (*createAccelerationStructure)(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes acceleration structure bindings into descriptor sets.
		 */
		bool (*updateDescriptorsAccelerationStructure)(void * impl, std::span<const DescriptorWriteAccelerationStructure> writes, Error * error) noexcept =
			nullptr;
	};

	struct QueryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueryApi), .version = 1 };

		/**
		 * \brief Creates a query pool.
		 */
		QueryPoolHandle (*createQueryPool)(void * impl, const QueryPoolDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Successful samples may be uncalibrated, with GPU time in ticks and CPU time in nanoseconds.
		 */
		bool (*calibrateTimestamp)(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept = nullptr;
	};

	struct PipelineCacheApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PipelineCacheApi), .version = 1 };

		/**
		 * \brief Creates a pipeline cache.
		 */
		PipelineCacheHandle (*createPipelineCache)(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Returned cache bytes are borrowed until the next retrieval or cache destruction.
		 */
		bool (*getPipelineCacheData)(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept = nullptr;
	};

	struct ResidencyApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResidencyApi), .version = 1 };

		/**
		 * \brief Queries the memory budget and usage for a heap type.
		 */
		bool (*queryMemoryBudget)(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Requests residency priorities for buffers and textures.
		 */
		bool (*setResidencyPriority)(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept = nullptr;
	};

	struct ResourceIntrospectionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResourceIntrospectionApi), .version = 1 };

		/**
		 * \brief Queries a texture's description and allocation size.
		 */
		bool (*getTextureInfo)(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries a buffer's description, allocation size and memory access.
		 */
		bool (*getBufferInfo)(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Native structure types must match the backend graphics API.
	 */
	struct AdoptionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(AdoptionApi), .version = 1 };

		/**
		 * \brief Adopts a buffer from the selected API's NativeBuffer structure.
		 */
		BufferHandle (*adoptBuffer)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept =
			nullptr;
		/**
		 * \brief Adopts a texture from the selected API's NativeTexture structure.
		 */
		TextureHandle (*adoptTexture)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeBuffer output structure.
		 */
		bool (*getNativeBuffer)(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeTexture output structure.
		 */
		bool (*getNativeTexture)(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Adopts a texture view from the selected API's NativeTextureView structure.
		 */
		TextureViewHandle (*adoptTextureView)(
			void * impl,
			GraphicsApiId api,
			const void * nativeImport,
			const AdoptedTextureViewDesc & desc,
			Error * error
		) noexcept = nullptr;
		/**
		 * \brief Adopts a sampler from the selected API's NativeSampler structure.
		 */
		SamplerHandle (*adoptSampler)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeTextureView output structure.
		 */
		bool (*getNativeTextureView)(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeSampler output structure.
		 */
		bool (*getNativeSampler)(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Adopts a timeline from the selected API's NativeTimeline structure.
		 */
		TimelineHandle (*adoptTimeline)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept =
			nullptr;
		/**
		 * \brief Adopts a binary semaphore from the selected API's NativeBinarySemaphore structure.
		 */
		BinarySemaphoreHandle (*adoptBinarySemaphore)(
			void * impl,
			GraphicsApiId api,
			const void * nativeImport,
			const AdoptedBinarySemaphoreDesc & desc,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeTimeline output structure.
		 */
		bool (*getNativeTimeline)(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed reference to the selected API's NativeBinarySemaphore output structure.
		 */
		bool (*getNativeBinarySemaphore)(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept =
			nullptr;
	};

	/**
	 * \brief Imports retain caller ownership of external handles, and exports must be released through closeExportedHandle.
	 */
	struct ExternalSharingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalSharingApi), .version = 1 };

		/**
		 * \brief Exports an external handle for a buffer.
		 */
		bool (*exportBuffer)(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a heap.
		 */
		bool (*exportHeap)(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a texture.
		 */
		bool (*exportTexture)(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a timeline.
		 */
		bool (*exportTimeline)(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a binary semaphore.
		 */
		bool (*exportBinarySemaphore)(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Imports a buffer from an external handle.
		 */
		BufferHandle (*importBuffer)(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a heap from an external handle.
		 */
		HeapHandle (*importHeap)(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a texture from an external handle.
		 */
		TextureHandle (*importTexture)(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a timeline from an external handle.
		 */
		TimelineHandle (*importTimeline)(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a binary semaphore from an external handle.
		 */
		BinarySemaphoreHandle (*importBinarySemaphore)(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases an exported handle without clearing its stored value.
		 * \return True if the handle type was accepted for release, false otherwise.
		 */
		bool (*closeExportedHandle)(void * impl, const ExternalHandle & handle, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

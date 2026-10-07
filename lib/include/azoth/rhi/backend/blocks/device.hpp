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
	 * \brief Callbacks for device resources, capabilities and lifetime.
	 */
	struct CoreDeviceApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(CoreDeviceApi), .version = 1 };

		/**
		 * \brief Returns the backend's graphics API identifier.
		 * \param impl Backend device instance.
		 * \return Graphics API identifier.
		 */
		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept		 = nullptr;
		/**
		 * \brief Returns the backend's graphics API name.
		 * \param impl Backend device instance.
		 * \return Graphics API name.
		 */
		std::string_view (*getGraphicsApiName)(void * impl) noexcept = nullptr;

		/**
		 * \brief Creates a buffer.
		 * \param impl Backend device instance.
		 * \param desc Buffer creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Buffer handle, or an invalid handle on failure.
		 */
		BufferHandle (*createBuffer)(void * impl, const BufferDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a texture.
		 * \param impl Backend device instance.
		 * \param desc Texture creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Texture handle, or an invalid handle on failure.
		 */
		TextureHandle (*createTexture)(void * impl, const TextureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a view of a texture.
		 * \param impl Backend device instance.
		 * \param texture Texture to view.
		 * \param desc View format and subresource range.
		 * \param[out] error Optional output for failure details.
		 * \return Texture view handle, or an invalid handle on failure.
		 */
		TextureViewHandle (*createTextureView)(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a sampler.
		 * \param impl Backend device instance.
		 * \param desc Sampler filtering and address modes.
		 * \param[out] error Optional output for failure details.
		 * \return Sampler handle, or an invalid handle on failure.
		 */
		SamplerHandle (*createSampler)(void * impl, const SamplerDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a descriptor set layout.
		 * \param impl Backend device instance.
		 * \param desc Descriptor bindings and layout settings.
		 * \param[out] error Optional output for failure details.
		 * \return Descriptor set layout handle, or an invalid handle on failure.
		 */
		DescriptorSetLayoutHandle (*createDescriptorSetLayout)(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a pipeline layout.
		 * \param impl Backend device instance.
		 * \param desc Descriptor set layouts and push constant ranges.
		 * \param[out] error Optional output for failure details.
		 * \return Pipeline layout handle, or an invalid handle on failure.
		 */
		PipelineLayoutHandle (*createPipelineLayout)(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a graphics pipeline.
		 * \param impl Backend device instance.
		 * \param desc Graphics pipeline shaders, layout and render state.
		 * \param[out] error Optional output for failure details.
		 * \return Graphics pipeline handle, or an invalid handle on failure.
		 */
		GraphicsPipelineHandle (*createGraphicsPipeline)(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a compute pipeline.
		 * \param impl Backend device instance.
		 * \param desc Compute pipeline shader and layout.
		 * \param[out] error Optional output for failure details.
		 * \return Compute pipeline handle, or an invalid handle on failure.
		 */
		ComputePipelineHandle (*createComputePipeline)(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a timeline for GPU synchronization.
		 * \param impl Backend device instance.
		 * \param desc Timeline creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Timeline handle, or an invalid handle on failure.
		 */
		TimelineHandle (*createTimeline)(void * impl, const TimelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a binary semaphore.
		 * \param impl Backend device instance.
		 * \param desc Binary semaphore creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Binary semaphore handle, or an invalid handle on failure.
		 */
		BinarySemaphoreHandle (*createBinarySemaphore)(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a descriptor arena.
		 * \param impl Backend device instance.
		 * \param desc Descriptor arena capacity and allocation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Backend descriptor arena instance, or nullptr on failure.
		 */
		void * (*createDescriptorArena)(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a command pool.
		 * \param impl Backend device instance.
		 * \param desc Queue type and command pool settings.
		 * \param[out] error Optional output for failure details.
		 * \return Backend command pool instance, or nullptr on failure.
		 */
		void * (*createCommandPool)(void * impl, const CommandPoolDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Returns a queue of the requested type and index.
		 * \param impl Backend device instance.
		 * \param type Queue type.
		 * \param index Queue index within the requested type, starting at zero.
		 * \param[out] error Optional output for failure details.
		 * \return Backend queue instance, or nullptr on failure.
		 */
		void * (*getQueue)(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept = nullptr;

		/**
		 * \brief Maps a buffer range for host access.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param desc Mapping mode and byte range.
		 * \param[out] error Optional output for failure details.
		 * \return Mapped range, or a null data pointer on failure.
		 */
		MappedMemory (*map)(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases an outstanding buffer mapping.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*unmap)(void * impl, BufferHandle buffer, Error * error) noexcept = nullptr;

		/**
		 * \brief Flushes host writes in a mapped buffer range.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param offset Byte offset from the start of the buffer.
		 * \param size Number of bytes to flush.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*flushMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		/**
		 * \brief Invalidates host caches for a mapped buffer range.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param offset Byte offset from the start of the buffer.
		 * \param size Number of bytes to invalidate.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*invalidateMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes buffer bindings into descriptor sets.
		 * \param impl Backend device instance.
		 * \param writes Buffer descriptor writes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*updateDescriptorsBuffer)(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes texture bindings into descriptor sets.
		 * \param impl Backend device instance.
		 * \param writes Texture descriptor writes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*updateDescriptorsTexture)(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes sampler bindings into descriptor sets.
		 * \param impl Backend device instance.
		 * \param writes Sampler descriptor writes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*updateDescriptorsSampler)(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept = nullptr;

		/**
		 * \brief Returns the device's capability limits and supported features.
		 * \param impl Backend device instance.
		 * \return Capability information owned by the device.
		 */
		const DeviceCaps & (*getCaps)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns the supported uses of a resource format.
		 * \param impl Backend device instance.
		 * \param format Resource format to query.
		 * \return Format support information.
		 */
		FormatSupport (*getFormatSupport)(void * impl, Format format) noexcept = nullptr;

		/**
		 * \brief Returns information about the device's adapter.
		 * \param impl Backend device instance.
		 * \return Adapter information owned by the device.
		 */
		const AdapterInfo & (*getAdapterInfo)(void * impl) noexcept = nullptr;

		/**
		 * \brief Returns counts of reported validation messages.
		 * \param impl Backend device instance.
		 * \return Counts of reported validation messages.
		 */
		ValidationMessageCounts (*getValidationMessageCounts)(void * impl) noexcept = nullptr;

		/**
		 * \brief Destroys a resource using the supplied retirement policy.
		 * \param impl Backend device instance.
		 * \param type Resource type of the handle.
		 * \param handle Resource handle to destroy.
		 * \param desc Destruction policy and retire point.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*destroy)(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases pending resources once the caller has made them safe.
		 * \param impl Backend device instance.
		 * \param type Resource type to collect.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*collectGarbage)(void * impl, ResourceType type, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases resources retired at or before a known completed timeline value.
		 * \param impl Backend device instance.
		 * \param type Resource type to collect.
		 * \param timeline Timeline used to retire the resources.
		 * \param completedValue Known completed value of the timeline.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*collectGarbageTimeline)(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept = nullptr;

		/**
		 * \brief Destroys the backend device instance.
		 * \param impl Backend device instance.
		 */
		void (*destroyDevice)(void * impl) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for creating swapchains.
	 */
	struct PresentApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(PresentApi), .version = 1 };

		/**
		 * \brief Creates a swapchain.
		 * \param impl Backend device instance.
		 * \param desc Surface, extent and presentation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Backend swapchain instance, or nullptr on failure.
		 */
		void * (*createSwapchain)(void * impl, const SwapchainDesc & desc, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for heaps, placed resources and memory requirements.
	 */
	struct PlacedMemoryApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(PlacedMemoryApi), .version = 1 };

		/**
		 * \brief Creates a heap for placed resources.
		 * \param impl Backend device instance.
		 * \param desc Heap type, size and allocation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Heap handle, or an invalid handle on failure.
		 */
		HeapHandle (*createHeap)(void * impl, const HeapDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a buffer at a specified heap offset.
		 * \param impl Backend device instance.
		 * \param desc Buffer properties, heap and byte offset.
		 * \param[out] error Optional output for failure details.
		 * \return Buffer handle, or an invalid handle on failure.
		 */
		BufferHandle (*createPlacedBuffer)(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a texture at a specified heap offset.
		 * \param impl Backend device instance.
		 * \param desc Texture properties, heap and byte offset.
		 * \param[out] error Optional output for failure details.
		 * \return Texture handle, or an invalid handle on failure.
		 */
		TextureHandle (*createPlacedTexture)(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries the memory required by a texture description.
		 * \param impl Backend device instance.
		 * \param desc Texture creation settings.
		 * \param[out] out Output allocation size and alignment, in bytes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getTextureMemoryInfo)(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries the memory required by a buffer description.
		 * \param impl Backend device instance.
		 * \param desc Buffer creation settings.
		 * \param[out] out Output allocation size and alignment, in bytes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getBufferMemoryInfo)(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for ray tracing resources and descriptor writes.
	 */
	struct RayTracingApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(RayTracingApi), .version = 1 };

		/**
		 * \brief Creates a ray tracing pipeline.
		 * \param impl Backend device instance.
		 * \param desc Ray tracing shaders, groups and pipeline settings.
		 * \param[out] error Optional output for failure details.
		 * \return Ray tracing pipeline handle, or an invalid handle on failure.
		 */
		RayTracingPipelineHandle (*createRayTracingPipeline)(void * impl, const RayTracingPipelineDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates an acceleration structure.
		 * \param impl Backend device instance.
		 * \param desc Acceleration structure creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Acceleration structure handle, or an invalid handle on failure.
		 */
		AccelerationStructureHandle (*createAccelerationStructure)(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes acceleration structure bindings into descriptor sets.
		 * \param impl Backend device instance.
		 * \param writes Acceleration structure descriptor writes.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*updateDescriptorsAccelerationStructure)(void * impl, std::span<const DescriptorWriteAccelerationStructure> writes, Error * error) noexcept =
			nullptr;
	};

	/**
	 * \brief Callbacks for query pools and timestamp calibration.
	 */
	struct QueryApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(QueryApi), .version = 1 };

		/**
		 * \brief Creates a query pool.
		 * \param impl Backend device instance.
		 * \param desc Query kind, count and statistics flags.
		 * \param[out] error Optional output for failure details.
		 * \return Query pool handle, or an invalid handle on failure.
		 */
		QueryPoolHandle (*createQueryPool)(void * impl, const QueryPoolDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Samples GPU and CPU clocks for timestamp calibration.
		 * \param impl Backend device instance.
		 * \param queueType Queue type associated with the sample.
		 * \param[out] out Output GPU ticks, CPU nanoseconds, GPU tick period and calibration status.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*calibrateTimestamp)(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for pipeline cache creation and data retrieval.
	 */
	struct PipelineCacheApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(PipelineCacheApi), .version = 1 };

		/**
		 * \brief Creates a pipeline cache.
		 * \param impl Backend device instance.
		 * \param desc Initial cache bytes and creation settings.
		 * \param[out] error Optional output for failure details.
		 * \return Pipeline cache handle, or an invalid handle on failure.
		 */
		PipelineCacheHandle (*createPipelineCache)(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Retrieves a borrowed view of serialized pipeline cache bytes.
		 * \param impl Backend device instance.
		 * \param cache Pipeline cache handle.
		 * \param[out] out Borrowed cache bytes, valid until the next retrieval or cache destruction.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getPipelineCacheData)(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for memory budgets and residency priorities.
	 */
	struct ResidencyApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(ResidencyApi), .version = 1 };

		/**
		 * \brief Queries the memory budget and usage for a heap type.
		 * \param impl Backend device instance.
		 * \param heap Heap type to query.
		 * \param[out] out Output memory budget and usage in bytes, with budget precision.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*queryMemoryBudget)(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Requests residency priorities for buffers and textures.
		 * \param impl Backend device instance.
		 * \param priorities Buffer and texture handles with their requested priorities.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*setResidencyPriority)(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for querying buffer and texture information.
	 */
	struct ResourceIntrospectionApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(ResourceIntrospectionApi), .version = 1 };

		/**
		 * \brief Queries a texture's description and allocation size.
		 * \param impl Backend device instance.
		 * \param texture Texture handle.
		 * \param[out] out Output texture description and allocation size.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getTextureInfo)(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Queries a buffer's description, allocation size and memory access.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param[out] out Output buffer description, allocation size and memory access.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getBufferInfo)(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for native resource adoption and access.
	 */
	struct AdoptionApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(AdoptionApi), .version = 1 };

		/**
		 * \brief Adopts a native buffer.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeBuffer structure.
		 * \param desc Buffer properties and lifetime settings.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted buffer handle, or an invalid handle on failure.
		 */
		BufferHandle (*adoptBuffer)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept =
			nullptr;
		/**
		 * \brief Adopts a native texture.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeTexture structure.
		 * \param desc Texture properties and lifetime settings.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted texture handle, or an invalid handle on failure.
		 */
		TextureHandle (*adoptTexture)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Writes a borrowed native buffer reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param buffer Buffer handle.
		 * \param[out] outNativeImport Output NativeBuffer structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeBuffer)(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed native texture reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param texture Texture handle.
		 * \param[out] outNativeImport Output NativeTexture structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeTexture)(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Adopts a native texture view.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeTextureView structure.
		 * \param desc View properties, parent texture and lifetime settings.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted texture view handle, or an invalid handle on failure.
		 */
		TextureViewHandle (*adoptTextureView)(
			void * impl,
			GraphicsApiId api,
			const void * nativeImport,
			const AdoptedTextureViewDesc & desc,
			Error * error
		) noexcept = nullptr;
		/**
		 * \brief Adopts a native sampler.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeSampler structure.
		 * \param desc Sampler adoption lifetime and debug name.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted sampler handle, or an invalid handle on failure.
		 */
		SamplerHandle (*adoptSampler)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Writes a borrowed native texture view reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param view Texture view handle.
		 * \param[out] outNativeImport Output NativeTextureView structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeTextureView)(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed native sampler reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param sampler Sampler handle.
		 * \param[out] outNativeImport Output NativeSampler structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeSampler)(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Adopts a native timeline.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeTimeline structure.
		 * \param desc Timeline adoption lifetime and debug name.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted timeline handle, or an invalid handle on failure.
		 */
		TimelineHandle (*adoptTimeline)(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept =
			nullptr;
		/**
		 * \brief Adopts a native binary semaphore.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param nativeImport Pointer to the API's NativeBinarySemaphore structure.
		 * \param desc Binary semaphore adoption lifetime and debug name.
		 * \param[out] error Optional output for failure details.
		 * \return Adopted binary semaphore handle, or an invalid handle on failure.
		 */
		BinarySemaphoreHandle (*adoptBinarySemaphore)(
			void * impl,
			GraphicsApiId api,
			const void * nativeImport,
			const AdoptedBinarySemaphoreDesc & desc,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed native timeline reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param timeline Timeline handle.
		 * \param[out] outNativeImport Output NativeTimeline structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeTimeline)(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept = nullptr;

		/**
		 * \brief Writes a borrowed native binary semaphore reference to the output structure.
		 * \param impl Backend device instance.
		 * \param api Graphics API identifying the native structure type, matching the backend.
		 * \param semaphore Binary semaphore handle.
		 * \param[out] outNativeImport Output NativeBinarySemaphore structure for the selected API.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*getNativeBinarySemaphore)(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept =
			nullptr;
	};

	/**
	 * \brief Callbacks for sharing resources through external handles.
	 */
	struct ExternalSharingApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(ExternalSharingApi), .version = 1 };

		/**
		 * \brief Exports an external handle for a buffer.
		 * \param impl Backend device instance.
		 * \param buffer Buffer handle.
		 * \param type Requested external handle type.
		 * \param[out] out Output handle to release through closeExportedHandle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*exportBuffer)(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a heap.
		 * \param impl Backend device instance.
		 * \param heap Heap handle.
		 * \param type Requested external handle type.
		 * \param[out] out Output handle to release through closeExportedHandle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*exportHeap)(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a texture.
		 * \param impl Backend device instance.
		 * \param texture Texture handle.
		 * \param type Requested external handle type.
		 * \param[out] out Output handle to release through closeExportedHandle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*exportTexture)(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a timeline.
		 * \param impl Backend device instance.
		 * \param timeline Timeline handle.
		 * \param type Requested external handle type.
		 * \param[out] out Output handle to release through closeExportedHandle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*exportTimeline)(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Exports an external handle for a binary semaphore.
		 * \param impl Backend device instance.
		 * \param semaphore Binary semaphore handle.
		 * \param type Requested external handle type.
		 * \param[out] out Output handle to release through closeExportedHandle.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*exportBinarySemaphore)(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Imports a buffer from an external handle.
		 * \param impl Backend device instance.
		 * \param desc External handle and buffer settings. The caller retains handle ownership.
		 * \param[out] error Optional output for failure details.
		 * \return Imported buffer handle, or an invalid handle on failure.
		 */
		BufferHandle (*importBuffer)(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a heap from an external handle.
		 * \param impl Backend device instance.
		 * \param desc External handle and heap settings. The caller retains handle ownership.
		 * \param[out] error Optional output for failure details.
		 * \return Imported heap handle, or an invalid handle on failure.
		 */
		HeapHandle (*importHeap)(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a texture from an external handle.
		 * \param impl Backend device instance.
		 * \param desc External handle and texture settings. The caller retains handle ownership.
		 * \param[out] error Optional output for failure details.
		 * \return Imported texture handle, or an invalid handle on failure.
		 */
		TextureHandle (*importTexture)(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a timeline from an external handle.
		 * \param impl Backend device instance.
		 * \param desc External handle and timeline settings. The caller retains handle ownership.
		 * \param[out] error Optional output for failure details.
		 * \return Imported timeline handle, or an invalid handle on failure.
		 */
		TimelineHandle (*importTimeline)(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Imports a binary semaphore from an external handle.
		 * \param impl Backend device instance.
		 * \param desc External handle and semaphore settings. The caller retains handle ownership.
		 * \param[out] error Optional output for failure details.
		 * \return Imported binary semaphore handle, or an invalid handle on failure.
		 */
		BinarySemaphoreHandle (*importBinarySemaphore)(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Releases an exported external handle.
		 * \param impl Backend device instance.
		 * \param handle Exported handle to release. Its stored value is unchanged.
		 * \param[out] error Optional output for failure details.
		 * \return True if the handle type was accepted for release, false otherwise.
		 */
		bool (*closeExportedHandle)(void * impl, const ExternalHandle & handle, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

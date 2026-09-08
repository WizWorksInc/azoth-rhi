// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace azo::rhi
{

	struct CoreDeviceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(CoreDeviceApi), .version = 1 };

		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept		 = nullptr;
		std::string_view (*getGraphicsApiName)(void * impl) noexcept = nullptr;

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

		void * (*createDescriptorArena)(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept = nullptr;
		void * (*createCommandPool)(void * impl, const CommandPoolDesc & desc, Error * error) noexcept		   = nullptr;
		void * (*getQueue)(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept		   = nullptr;

		MappedMemory (*map)(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept								  = nullptr;
		bool (*unmap)(void * impl, BufferHandle buffer, Error * error) noexcept															  = nullptr;
		bool (*flushMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept	  = nullptr;
		bool (*invalidateMappedRange)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error * error) noexcept = nullptr;

		bool (*updateDescriptorsBuffer)(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept	  = nullptr;
		bool (*updateDescriptorsTexture)(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept = nullptr;
		bool (*updateDescriptorsSampler)(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept = nullptr;

		const DeviceCaps & (*getCaps)(void * impl) noexcept							= nullptr;
		FormatSupport (*getFormatSupport)(void * impl, Format format) noexcept		= nullptr;
		const AdapterInfo & (*getAdapterInfo)(void * impl) noexcept					= nullptr;
		ValidationMessageCounts (*getValidationMessageCounts)(void * impl) noexcept = nullptr;

		bool (*destroy)(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept							  = nullptr;
		bool (*collectGarbage)(void * impl, ResourceType type, Error * error) noexcept																  = nullptr;
		bool (*collectGarbageTimeline)(void * impl, ResourceType type, TimelineHandle timeline, std::uint64_t completedValue, Error * error) noexcept = nullptr;

		void (*destroyDevice)(void * impl) noexcept = nullptr;
	};

	struct PresentApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PresentApi), .version = 1 };

		void * (*createSwapchain)(void * impl, const SwapchainDesc & desc, Error * error) noexcept = nullptr;
	};

	struct PlacedMemoryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PlacedMemoryApi), .version = 1 };

		HeapHandle (*createHeap)(void * impl, const HeapDesc & desc, Error * error) noexcept						  = nullptr;
		BufferHandle (*createPlacedBuffer)(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept		  = nullptr;
		TextureHandle (*createPlacedTexture)(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept	  = nullptr;
		bool (*getTextureMemoryInfo)(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept = nullptr;
		bool (*getBufferMemoryInfo)(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept	  = nullptr;
	};

	struct RayTracingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(RayTracingApi), .version = 1 };

		RayTracingPipelineHandle (*createRayTracingPipeline)(void * impl, const RayTracingPipelineDesc & desc, Error * error) noexcept			= nullptr;
		AccelerationStructureHandle (*createAccelerationStructure)(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept = nullptr;
		bool (*updateDescriptorsAccelerationStructure)(
			void * impl, std::span<const DescriptorWriteAccelerationStructure> writes, Error * error) noexcept = nullptr;
	};

	struct QueryApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueryApi), .version = 1 };

		QueryPoolHandle (*createQueryPool)(void * impl, const QueryPoolDesc & desc, Error * error) noexcept				 = nullptr;
		bool (*calibrateTimestamp)(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept = nullptr;
	};

	struct PipelineCacheApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(PipelineCacheApi), .version = 1 };

		PipelineCacheHandle (*createPipelineCache)(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept		  = nullptr;
		bool (*getPipelineCacheData)(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept = nullptr;
	};

	struct ResidencyApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResidencyApi), .version = 1 };

		bool (*queryMemoryBudget)(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept				 = nullptr;
		bool (*setResidencyPriority)(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept = nullptr;
	};

	struct ResourceIntrospectionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ResourceIntrospectionApi), .version = 1 };

		bool (*getTextureInfo)(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept = nullptr;

		bool (*getBufferInfo)(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept = nullptr;
	};

	struct AdoptionApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(AdoptionApi), .version = 1 };

		BufferHandle (*adoptBuffer)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept = nullptr;
		TextureHandle (*adoptTexture)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept			= nullptr;
		bool (*getNativeBuffer)(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept	= nullptr;
		bool (*getNativeTexture)(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept = nullptr;

		TextureViewHandle (*adoptTextureView)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept = nullptr;
		SamplerHandle (*adoptSampler)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept				 = nullptr;
		bool (*getNativeTextureView)(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept = nullptr;
		bool (*getNativeSampler)(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept		 = nullptr;

		TimelineHandle (*adoptTimeline)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept = nullptr;
		BinarySemaphoreHandle (*adoptBinarySemaphore)(
			void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept	   = nullptr;
		bool (*getNativeTimeline)(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept = nullptr;
		bool (*getNativeBinarySemaphore)(
			void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept = nullptr;
	};

	struct ExternalSharingApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalSharingApi), .version = 1 };

		bool (*exportBuffer)(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept		= nullptr;
		bool (*exportHeap)(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept				= nullptr;
		bool (*exportTexture)(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept	= nullptr;
		bool (*exportTimeline)(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;
		bool (*exportBinarySemaphore)(
			void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept = nullptr;

		BufferHandle (*importBuffer)(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept							= nullptr;
		HeapHandle (*importHeap)(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept									= nullptr;
		TextureHandle (*importTexture)(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept							= nullptr;
		TimelineHandle (*importTimeline)(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept						= nullptr;
		BinarySemaphoreHandle (*importBinarySemaphore)(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept = nullptr;

		bool (*closeExportedHandle)(void * impl, const ExternalHandle & handle, Error * error) noexcept = nullptr;
	};

}

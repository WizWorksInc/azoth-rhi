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
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/metal_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include "backends/metal_common/conversions.hpp"
#include "backends/registration.hpp"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace azo::rhi::metal
{
	// How an RHI value becomes a Metal value, which is the same answer here and on the Metal 4 backend. NOLINTNEXTLINE(google-build-using-namespace): the
	using namespace azo::rhi::metal_common;

	struct MetalDevice;
	struct MetalObject;

	struct MetalCmdList final
	{
		NS::SharedPtr<MTL::CommandBuffer> commandBuffer;
		std::uint8_t lifecycle = 0;

		detail::HostVector<NS::SharedPtr<MTL::Buffer>> keepAlive;

		NS::SharedPtr<MTL::RenderCommandEncoder> renderEncoder;
		NS::SharedPtr<MTL::ComputeCommandEncoder> computeEncoder;
		MTL::PrimitiveType boundPrimitive = MTL::PrimitiveTypeTriangle;
		MTL::Buffer * boundIndexBuffer	  = nullptr;
		std::uint64_t boundIndexOffset	  = 0;
		MTL::IndexType boundIndexType	  = MTL::IndexTypeUInt32;
		MTL::Size boundThreadGroup{ 1, 1, 1 };

		NS::SharedPtr<MTL::Fence> aliasFence;
		bool aliasWaitPending = false;

		NS::SharedPtr<MTL::CounterSampleBuffer> pendingEndTimestamp;
		std::uint32_t pendingEndQuery = 0;

		NS::SharedPtr<MTL::Fence> timestampFence;

		std::uint64_t encoderEpoch = 0;
		detail::HostVector<std::uint64_t> debugLabelScopes;

		detail::HostString debugName;
	};

	struct MetalQueryPool final
	{
		NS::SharedPtr<MTL::CounterSampleBuffer> sampleBuffer;

		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	struct MetalTimeline final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct MetalBinarySemaphore final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		std::uint64_t value = 0;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct MetalGraphicsPipeline final
	{
		NS::SharedPtr<MTL::RenderPipelineState> state;
		NS::SharedPtr<MTL::DepthStencilState> depthStencil;
		MTL::PrimitiveType primitive = MTL::PrimitiveTypeTriangle;
		MTL::CullMode cull			 = MTL::CullModeBack;
		MTL::Winding winding		 = MTL::WindingCounterClockwise;
		MTL::TriangleFillMode fill	 = MTL::TriangleFillModeFill;
		bool depthBiasEnable		 = false;
		float depthBiasConstant		 = 0.0f;
		float depthBiasSlope		 = 0.0f;
		float depthBiasClamp		 = 0.0f;
	};

	struct MetalComputePipeline final
	{
		NS::SharedPtr<MTL::ComputePipelineState> state;
		MTL::Size threadsPerThreadgroup{ 1, 1, 1 };
	};

	struct MetalDescriptor final
	{
		DescriptorType type			= DescriptorType::eUniformBuffer;
		MTL::Buffer * buffer		= nullptr;
		std::uint64_t offset		= 0;
		MTL::Texture * texture		= nullptr;
		MTL::SamplerState * sampler = nullptr;
	};

	struct MetalDescriptorSet final
	{
		detail::HostMap<std::uint64_t, MetalDescriptor> bindings;

		const MetalObject * arena = nullptr;
		std::uint64_t epoch		  = 0;

		DescriptorSetLayoutHandle layout{};

		NS::SharedPtr<MTL::Buffer> argumentBuffer;
	};

	struct MetalCmdPool final
	{
		detail::HostVector<MetalObject *> lists;
		std::size_t handedOut = 0;
	};

	struct MetalObject final
	{
		const BackendObject * object = nullptr;
		MetalDevice * owner			 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		MetalCmdList * list = nullptr;

		MetalCmdPool * pool = nullptr;

		std::atomic<std::uint64_t> arenaEpoch{ 0 };
	};

	struct MetalInstance final
	{
		const BackendObject * object = nullptr;
	};

	struct MetalTextureSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;
		Format format = Format::eUndefined;

		Flags<TextureUsage> usage;

		bool mutableFormat = false;

		bool shared = false;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		TextureDesc desc{};
	};

	struct MetalTextureViewSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct MetalBufferSlot final
	{
		NS::SharedPtr<MTL::Buffer> buffer;

		BufferDesc desc{};
	};

	struct MetalSlotTag final
	{
	};

	struct MetalDescriptorSetLayout final
	{
		detail::HostVector<DescriptorBinding> bindings;
	};

	struct MetalPipelineLayout final
	{
		detail::HostVector<DescriptorSetLayoutHandle> sets;

		bool hasPushConstants = false;
	};

	struct MetalSwapchain final
	{
		const BackendObject * object = nullptr;
		MetalDevice * owner			 = nullptr;
		CA::MetalLayer * layer		 = nullptr;
		Format format				 = Format::eBGRA8Srgb;
		PresentMode presentMode		 = PresentMode::eFifo;
		std::uint32_t width			 = 1;
		std::uint32_t height		 = 1;
		std::uint32_t imageCount	 = 0;
		std::uint32_t frameCursor	 = 0;

		NS::SharedPtr<CA::MetalDrawable> currentDrawable;
		TextureHandle backBuffer{};
		TextureViewHandle backBufferView{};
		BinarySemaphoreHandle imageAvailable{};
		detail::HostVector<BinarySemaphoreHandle> presentSemaphores;
	};

	struct MetalDevice final
	{
		const BackendObject * object = nullptr;
		NS::SharedPtr<MTL::Device> device;

		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> graphicsQueues;
		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> computeQueues;
		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> copyQueues;

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & QueuesForType(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] MTL::CommandQueue * CommandQueueFor(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & pool = QueuesForType(type);
			return pool.empty() ? nullptr : pool.front().get();
		}

		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		bool debugLabels = true;

		bool allowDeviceLocalMapping = false;

		MetalInstance * instanceWrapper = nullptr;

		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		detail::HostString adapterName;

		detail::HostString driverVersion;
		detail::HostString driverInfo;

		detail::TypedObjectPool<MetalObject> objects{ 64, 0, "rhi.metal.objects" };

		SlotMap<BufferTag, MetalBufferSlot> buffers;
		SlotMap<TextureTag, MetalTextureSlot> textures;
		SlotMap<TextureViewTag, MetalTextureViewSlot> textureViews;
		SlotMap<SamplerTag, NS::SharedPtr<MTL::SamplerState>> samplers;
		SlotMap<HeapTag, NS::SharedPtr<MTL::Heap>> heaps;
		SlotMap<TimelineTag, MetalTimeline> timelines;
		SlotMap<BinarySemaphoreTag, MetalBinarySemaphore> binarySemaphores;
		SlotMap<GraphicsPipelineTag, MetalGraphicsPipeline> graphicsPipelines;
		SlotMap<ComputePipelineTag, MetalComputePipeline> computePipelines;
		SlotMap<DescriptorSetTag, MetalDescriptorSet> descriptorSets;
		SlotMap<QueryPoolTag, MetalQueryPool> queryPools;

		NS::SharedPtr<MTL::CounterSet> timestampCounterSet;

		bool samplesAtStageBoundary	   = false;
		bool samplesAtDrawBoundary	   = false;
		bool samplesAtDispatchBoundary = false;
		bool samplesAtBlitBoundary	   = false;

		SlotMap<DescriptorSetLayoutTag, MetalDescriptorSetLayout> descriptorSetLayouts;
		SlotMap<PipelineLayoutTag, MetalPipelineLayout> pipelineLayouts;

		detail::ResourceTables<MetalSlotTag, std::monostate> tracked;

		detail::HostVector<HostUniquePtr<MetalCmdList>> cmdLists;
		detail::HostVector<HostUniquePtr<MetalCmdPool>> cmdPools;

		enum class Residency : std::uint8_t
		{
			eBuffers,
			eTextures,
			eHeaps,
			eDescriptorSets,
			eCount,
		};

		std::array<NS::SharedPtr<MTL::ResidencySet>, static_cast<std::size_t>(Residency::eCount)> residencySets;

		void NoteAllocation(Residency kind, const MTL::Allocation * allocation) noexcept;

		detail::HostVector<HostUniquePtr<MetalSwapchain>> swapchains;

		~MetalDevice()
		{
			objects.Reset();
		}

		MetalDevice()								 = default;
		MetalDevice(const MetalDevice &)			 = delete;
		MetalDevice & operator=(const MetalDevice &) = delete;
		MetalDevice(MetalDevice &&)					 = delete;
		MetalDevice & operator=(MetalDevice &&)		 = delete;
	};

	struct MetalBackendOwner final
	{
		detail::HostVector<HostUniquePtr<MetalInstance>> instances;
		detail::HostVector<HostUniquePtr<MetalDevice>> devices;
	};

	[[nodiscard]] void * AllocObject(MetalDevice * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool MetalRefuseUnexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] MetalBackendOwner & Owner();
	GraphicsApiId MetalDeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view MetalDeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & MetalDeviceCaps(void * impl) noexcept;
	const AdapterInfo & MetalDeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts MetalDeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport MetalDeviceFormatSupport(void * impl, Format format) noexcept;
	bool MetalGetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool MetalGetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle MetalCreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalCreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle MetalCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle MetalCreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool MetalGetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool MetalGetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * ResolveHeap(MetalDevice * device, HeapHandle handle) noexcept;
	HeapHandle MetalCreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle MetalCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle MetalCreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * ResolveBuffer(MetalDevice * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTexture(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format ResolveTextureFormat(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] MTL::CommandBuffer * CmdBufferOf(MetalObject * object) noexcept;
	void EndActiveEncoders(MetalObject * object) noexcept;
	[[nodiscard]] MTL::BlitCommandEncoder * BeginBlit(MetalObject * object, Error * error) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::RenderCommandEncoder * encoder) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::ComputeCommandEncoder * encoder) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::BlitCommandEncoder * encoder) noexcept;
	[[nodiscard]] MetalCmdList * NewCmdList(MetalDevice * device, QueueType queueType);
	BinarySemaphoreHandle MetalCreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;
	bool MetalCmdBegin(void * impl, Error * error) noexcept;
	bool MetalCmdEnd(void * impl, Error * error) noexcept;
	bool MetalCmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool MetalCmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool MetalCmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool MetalCmdEndDebugLabel(void * impl, Error * error) noexcept;
	bool MetalClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool MetalResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool MetalCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool MetalCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool MetalCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool MetalCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool MetalBlit(void * impl, [[maybe_unused]] TextureHandle dst, [[maybe_unused]] TextureHandle src, [[maybe_unused]] std::span<const TextureBlit> regions,
		[[maybe_unused]] Filter filter, Error * error) noexcept;
	bool MetalGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool MetalClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTextureView(MetalDevice * device, TextureViewHandle handle) noexcept;
	PipelineLayoutHandle MetalCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle MetalCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle MetalCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	bool MetalBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool MetalEndRendering(void * impl, Error * error) noexcept;
	bool MetalSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool MetalSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool MetalSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool MetalSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool MetalSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool MetalSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool MetalSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool MetalSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool MetalPushConstants(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;
	bool MetalDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool MetalDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool MetalDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool MetalDrawIndexedIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	inline constexpr std::uint64_t kDebugScopeCommandBuffer = 0;
	inline constexpr std::uint64_t kDebugScopeClosed		= std::numeric_limits<std::uint64_t>::max();

	void PopEncoderDebugGroups(MetalCmdList * rec, MTL::CommandEncoder * encoder) noexcept;

	[[nodiscard]] bool EnsureComputeEncoder(MetalObject * object, Error * error) noexcept;
	bool MetalSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool MetalBindDescriptorSet(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, [[maybe_unused]] std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool MetalUpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool MetalUpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool MetalUpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool MetalDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool MetalDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	void * MetalCreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * MetalCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * MetalGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory MetalMap(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool MetalQueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] MetalQueryPool * ResolveQueryPool(MetalDevice * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle MetalCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool MetalCalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool MetalCmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool MetalCmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
	bool MetalCmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool MetalCmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool MetalCmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	bool MetalDestroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool MetalCollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool MetalCollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle MetalAdoptBuffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalAdoptTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool MetalGetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept;
	AccelerationStructureHandle MetalCreateAccelerationStructure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept;
	RayTracingPipelineHandle MetalCreateRayTracingPipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept;
	bool MetalBeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle MetalArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool MetalArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const RayTracingApi & RayTracingBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	TextureViewHandle MetalAdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle MetalAdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool MetalGetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle MetalAdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle MetalAdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool MetalGetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const AliasingCommandApi & AliasingCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;
	AcquireResult MetalSwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult MetalSwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	TextureHandle MetalSwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle MetalSwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle MetalSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format MetalSwapchainFormat(void * impl) noexcept;
	bool MetalSwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t MetalSwapchainImageCount(void * impl) noexcept;
	std::uint32_t MetalSwapchainWidth(void * impl) noexcept;
	std::uint32_t MetalSwapchainHeight(void * impl) noexcept;
	bool MetalSwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool MetalSwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	void * MetalCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType MetalQueueTypeOf(void * impl) noexcept;
	bool MetalQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool MetalQueueWaitIdle(void * impl, Error * error) noexcept;
	bool MetalQueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool MetalQueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool MetalQueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool MetalQueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool MetalQueueEndDebugLabel(void * impl, Error * error) noexcept;
	void * MetalCommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool MetalCommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId MetalInstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool MetalEnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool MetalQueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool MetalExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle MetalImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle MetalImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle MetalImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle MetalImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle MetalImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool MetalCloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;
	void PopulateCaps(MetalDevice * device);
	[[nodiscard]] MetalDevice * MakeOwnedDevice(MetalInstance * instance, const DeviceDesc & desc, Error & refusal);
	[[nodiscard]] MetalInstance * MakeOwnedInstance();
	void MetalDestroyDevice(void * impl) noexcept;
	void MetalDestroyInstance(void * impl) noexcept;
	void * MetalInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * MetalCreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	template <typename HandleT>
	[[nodiscard]] HandleT MintHandle(MetalDevice * device)
	{
		return device->tracked.Store<HandleT>(std::monostate{});
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, BufferHandle handle) noexcept
	{
		return device->buffers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TextureHandle handle) noexcept
	{
		return device->textures.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, SamplerHandle handle) noexcept
	{
		return device->samplers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, HeapHandle handle) noexcept
	{
		return device->heaps.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TimelineHandle handle) noexcept
	{
		return device->timelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename HandleT>
	[[nodiscard]] bool Resolves(MetalDevice * device, HandleT handle) noexcept
	{
		return device->tracked.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool MetalUnimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT MetalUnimplementedHandle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<HandleT>(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT MetalCreateHandle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.create");

		Error * error		 = LastError(args...);
		const HandleT handle = MintHandle<HandleT>(static_cast<MetalDevice *>(impl));
		if (!handle.IsValid())
		{
			return FailValue<HandleT>(error, ErrorCode::eOutOfHostMemory, "Metal backend handle allocation failed");
		}

		return ReturnValue(handle, error);
	}

	inline DescriptorSetLayoutHandle MetalCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return FailValue<DescriptorSetLayoutHandle>(
					error, ErrorCode::eUnsupportedFeature, "Metal does not bake samplers into a descriptor set layout, so write the sampler into the set");
			}
		}

		MetalDescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<MetalDevice *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

}

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

namespace azo::rhi::metal4
{
	// How an RHI value becomes a Metal value, which is the same answer here and on the Metal 4 backend. NOLINTNEXTLINE(google-build-using-namespace): the
	using namespace azo::rhi::metal_common;

	struct Metal4Device;
	struct Metal4Object;

	struct CmdList final
	{
		NS::SharedPtr<MTL4::CommandBuffer> commandBuffer;

		NS::SharedPtr<MTL4::CommandAllocator> allocator;

		NS::SharedPtr<MTL4::ArgumentTable> argumentTable;

		std::uint8_t lifecycle = 0;

		NS::SharedPtr<MTL4::RenderCommandEncoder> renderEncoder;

		NS::SharedPtr<MTL4::ComputeCommandEncoder> computeEncoder;

		MTL::PrimitiveType boundPrimitive = MTL::PrimitiveTypeTriangle;
		MTL::Size boundThreadGroup{ 1, 1, 1 };

		MTL::GPUAddress boundIndexBuffer = 0;
		std::uint64_t boundIndexLength	 = 0;
		MTL::IndexType boundIndexType	 = MTL::IndexTypeUInt32;

		detail::HostVector<NS::SharedPtr<MTL::Buffer>> pushConstantBlocks;
		std::size_t pushConstantBlock	 = 0;
		std::uint64_t pushConstantOffset = 0;

		detail::HostVector<NS::SharedPtr<MTL::Buffer>> keepAlive;

		NS::SharedPtr<MTL4::CounterHeap> pendingEndHeap;
		std::uint32_t pendingEndQuery = 0;

		bool scopeDrew = false;

		NS::SharedPtr<MTL::Fence> timestampFence;

		bool wroteEncoderTimestamps = false;

		MTL::Stages pendingProducer				  = static_cast<MTL::Stages>(0);
		MTL::Stages pendingConsumer				  = static_cast<MTL::Stages>(0);
		MTL4::VisibilityOptions pendingVisibility = MTL4::VisibilityOptionNone;

		NS::SharedPtr<MTL::ResidencySet> residency;

		std::uint64_t encoderEpoch = 0;
		detail::HostVector<std::uint64_t> debugLabelScopes;

		detail::HostString debugName;
	};

	struct CmdPool final
	{
		detail::HostVector<Metal4Object *> lists;
		std::size_t handedOut = 0;
	};

	struct Metal4QueryPool final
	{
		NS::SharedPtr<MTL4::CounterHeap> heap;

		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	struct Metal4Timeline final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct Metal4BinarySemaphore final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		std::uint64_t value = 0;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct Metal4GraphicsPipeline final
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

	struct Metal4ComputePipeline final
	{
		NS::SharedPtr<MTL::ComputePipelineState> state;
		MTL::Size threadsPerThreadgroup{ 1, 1, 1 };
	};

	struct Metal4Descriptor final
	{
		DescriptorType type			= DescriptorType::eUniformBuffer;
		MTL::Buffer * buffer		= nullptr;
		std::uint64_t offset		= 0;
		MTL::Texture * texture		= nullptr;
		MTL::SamplerState * sampler = nullptr;
	};

	struct Metal4DescriptorSet final
	{
		detail::HostMap<std::uint64_t, Metal4Descriptor> bindings;

		const Metal4Object * arena = nullptr;
		std::uint64_t epoch		   = 0;

		DescriptorSetLayoutHandle layout{};

		NS::SharedPtr<MTL::Buffer> argumentBuffer;
	};

	struct Metal4Object final
	{
		const BackendObject * object = nullptr;
		Metal4Device * owner		 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		CmdList * list = nullptr;

		CmdPool * pool = nullptr;

		std::atomic<std::uint64_t> arenaEpoch{ 0 };
	};

	struct Metal4Instance final
	{
		const BackendObject * object = nullptr;
	};

	struct Metal4TextureSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;
		Format format = Format::eUndefined;

		Flags<TextureUsage> usage;

		bool mutableFormat = false;

		bool shared = false;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		TextureDesc desc{};
	};

	struct Metal4TextureViewSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct Metal4BufferSlot final
	{
		NS::SharedPtr<MTL::Buffer> buffer;

		BufferDesc desc{};
	};

	struct Metal4SlotTag final
	{
	};

	struct Metal4DescriptorSetLayout final
	{
		detail::HostVector<DescriptorBinding> bindings;
	};

	struct Metal4PipelineLayout final
	{
		detail::HostVector<DescriptorSetLayoutHandle> sets;

		bool hasPushConstants = false;
	};

	struct Metal4Swapchain final
	{
		const BackendObject * object = nullptr;
		Metal4Device * owner		 = nullptr;
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

	struct Metal4Device final
	{
		const BackendObject * object = nullptr;
		NS::SharedPtr<MTL::Device> device;

		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> graphicsQueues;
		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> computeQueues;
		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> copyQueues;

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & QueuesForType(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] MTL4::CommandQueue * CommandQueueFor(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & pool = QueuesForType(type);
			return pool.empty() ? nullptr : pool.front().get();
		}

		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		bool debugLabels = true;

		bool allowDeviceLocalMapping = false;

		Metal4Instance * instanceWrapper = nullptr;

		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		detail::HostString adapterName;

		detail::HostString driverVersion;
		detail::HostString driverInfo;

		detail::TypedObjectPool<Metal4Object> objects{ 64, 0, "rhi.metal4.objects" };

		SlotMap<BufferTag, Metal4BufferSlot> buffers;
		SlotMap<TextureTag, Metal4TextureSlot> textures;
		SlotMap<TextureViewTag, Metal4TextureViewSlot> textureViews;
		SlotMap<SamplerTag, NS::SharedPtr<MTL::SamplerState>> samplers;
		SlotMap<HeapTag, NS::SharedPtr<MTL::Heap>> heaps;
		SlotMap<TimelineTag, Metal4Timeline> timelines;
		SlotMap<BinarySemaphoreTag, Metal4BinarySemaphore> binarySemaphores;
		SlotMap<GraphicsPipelineTag, Metal4GraphicsPipeline> graphicsPipelines;
		SlotMap<ComputePipelineTag, Metal4ComputePipeline> computePipelines;
		SlotMap<DescriptorSetTag, Metal4DescriptorSet> descriptorSets;
		SlotMap<QueryPoolTag, Metal4QueryPool> queryPools;

		NS::SharedPtr<MTL::CounterSet> timestampCounterSet;

		bool samplesAtStageBoundary	   = false;
		bool samplesAtDrawBoundary	   = false;
		bool samplesAtDispatchBoundary = false;
		bool samplesAtBlitBoundary	   = false;

		SlotMap<DescriptorSetLayoutTag, Metal4DescriptorSetLayout> descriptorSetLayouts;
		SlotMap<PipelineLayoutTag, Metal4PipelineLayout> pipelineLayouts;

		detail::ResourceTables<Metal4SlotTag, std::monostate> tracked;

		detail::HostVector<HostUniquePtr<CmdList>> cmdLists;
		detail::HostVector<HostUniquePtr<CmdPool>> cmdPools;

		NS::SharedPtr<MTL4::Compiler> compiler;

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

		NS::SharedPtr<MTL::SharedEvent> drainEvent;
		std::atomic<std::uint64_t> drainValue{ 0 };

		detail::HostVector<HostUniquePtr<Metal4Swapchain>> swapchains;

		~Metal4Device()
		{
			objects.Reset();
		}

		Metal4Device()								   = default;
		Metal4Device(const Metal4Device &)			   = delete;
		Metal4Device & operator=(const Metal4Device &) = delete;
		Metal4Device(Metal4Device &&)				   = delete;
		Metal4Device & operator=(Metal4Device &&)	   = delete;
	};

	struct Metal4BackendOwner final
	{
		detail::HostVector<HostUniquePtr<Metal4Instance>> instances;
		detail::HostVector<HostUniquePtr<Metal4Device>> devices;
	};

	[[nodiscard]] void * AllocObject(Metal4Device * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool Metal4RefuseUnexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] Metal4BackendOwner & Owner();
	GraphicsApiId Metal4DeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view Metal4DeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & Metal4DeviceCaps(void * impl) noexcept;
	const AdapterInfo & Metal4DeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts Metal4DeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport Metal4DeviceFormatSupport(void * impl, Format format) noexcept;
	bool Metal4GetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool Metal4GetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle Metal4CreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle Metal4CreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle Metal4CreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle Metal4CreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool Metal4GetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool Metal4GetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * ResolveHeap(Metal4Device * device, HeapHandle handle) noexcept;
	HeapHandle Metal4CreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle Metal4CreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle Metal4CreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle Metal4CreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * ResolveBuffer(Metal4Device * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTexture(Metal4Device * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format ResolveTextureFormat(Metal4Device * device, TextureHandle handle) noexcept;

	[[nodiscard]] inline CmdList * ListOf(Metal4Object * object) noexcept
	{
		return object != nullptr ? object->list : nullptr;
	}

	[[nodiscard]] inline CmdList * RecordingListOf(Metal4Object * object) noexcept
	{
		CmdList * list = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr || list->lifecycle != 1)
		{
			return nullptr;
		}

		return list;
	}

	void EndActiveEncoders(CmdList * list) noexcept;
	[[nodiscard]] MTL4::ComputeCommandEncoder * BeginCompute(Metal4Object * object, Error * error) noexcept;

	void NoteListAllocation(CmdList * list, const MTL::Allocation * allocation) noexcept;

	inline constexpr std::uint64_t kDebugScopeCommandBuffer = 0;
	inline constexpr std::uint64_t kDebugScopeClosed		= std::numeric_limits<std::uint64_t>::max();

	void PopEncoderDebugGroups(CmdList * list, MTL4::CommandEncoder * encoder) noexcept;

	void FlushPendingBarrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept;
	void FlushPendingBarrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept;

	[[nodiscard]] MTL::GPUAddress WritePushConstants(Metal4Device * device, CmdList * list, const void * data, std::uint32_t size) noexcept;

	BinarySemaphoreHandle Metal4CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;

	bool Metal4CmdBegin(void * impl, Error * error) noexcept;
	bool Metal4CmdEnd(void * impl, Error * error) noexcept;
	bool Metal4CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool Metal4CmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool Metal4CmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool Metal4CmdEndDebugLabel(void * impl, Error * error) noexcept;

	bool Metal4CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool Metal4CmdDispatch(void * impl, std::uint32_t x, std::uint32_t y, std::uint32_t z, Error * error) noexcept;
	bool Metal4CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	bool Metal4CmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool Metal4CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool Metal4CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool Metal4CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool Metal4CmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool Metal4CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool Metal4CmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool Metal4CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool Metal4CmdBlit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTextureView(Metal4Device * device, TextureViewHandle handle) noexcept;

	[[nodiscard]] bool BindingMapsAgree(Metal4Device * device, PipelineLayoutHandle layout, std::span<const ShaderBinary> shaders, Error * error) noexcept;

	[[nodiscard]] bool FunctionBuffersAreBound(Metal4Device * device, PipelineLayoutHandle layout, const NS::Array * bindings, Error * error) noexcept;

	PipelineLayoutHandle Metal4CreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle Metal4CreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle Metal4CreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;

	bool Metal4CmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool Metal4CmdEndRendering(void * impl, Error * error) noexcept;
	bool Metal4CmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool Metal4CmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool Metal4CmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool Metal4CmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool Metal4CmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool Metal4CmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool Metal4CmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool Metal4CmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool Metal4CmdDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool Metal4CmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool Metal4CmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool Metal4CmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;

	bool Metal4CmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool Metal4CmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;

	bool Metal4UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool Metal4UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool Metal4UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	void * Metal4CreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * Metal4CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * Metal4GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory Metal4Map(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool Metal4QueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] Metal4QueryPool * ResolveQueryPool(Metal4Device * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle Metal4CreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool Metal4CalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool Metal4CmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool Metal4CmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
	bool Metal4CmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool Metal4CmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool Metal4CmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	bool Metal4Destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool Metal4CollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool Metal4CollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle Metal4AdoptBuffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle Metal4AdoptTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool Metal4GetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool Metal4GetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept;
	AccelerationStructureHandle Metal4CreateAccelerationStructure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept;
	RayTracingPipelineHandle Metal4CreateRayTracingPipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept;
	bool Metal4BeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle Metal4ArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool Metal4ArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const RayTracingApi & RayTracingBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	TextureViewHandle Metal4AdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle Metal4AdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool Metal4GetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool Metal4GetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle Metal4AdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle Metal4AdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool Metal4GetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool Metal4GetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
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
	AcquireResult Metal4SwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult Metal4SwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	TextureHandle Metal4SwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle Metal4SwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle Metal4SwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format Metal4SwapchainFormat(void * impl) noexcept;
	bool Metal4SwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t Metal4SwapchainImageCount(void * impl) noexcept;
	std::uint32_t Metal4SwapchainWidth(void * impl) noexcept;
	std::uint32_t Metal4SwapchainHeight(void * impl) noexcept;
	bool Metal4SwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool Metal4SwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	void * Metal4CreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType Metal4QueueTypeOf(void * impl) noexcept;
	bool Metal4QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool Metal4QueueWaitIdle(void * impl, Error * error) noexcept;
	bool Metal4QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool Metal4QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool Metal4QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool Metal4QueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool Metal4QueueEndDebugLabel(void * impl, Error * error) noexcept;
	void * Metal4CommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool Metal4CommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId Metal4InstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool Metal4EnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool Metal4QueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool Metal4ExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool Metal4ExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool Metal4ExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool Metal4ExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool Metal4ExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle Metal4ImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle Metal4ImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle Metal4ImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle Metal4ImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle Metal4ImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool Metal4CloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;
	void PopulateCaps(Metal4Device * device);

	[[nodiscard]] bool AdapterHasMetal4(MTL::Device * device) noexcept;

	[[nodiscard]] Metal4Device * MakeOwnedDevice(Metal4Instance * instance, const DeviceDesc & desc, Error & refusal);
	[[nodiscard]] Metal4Instance * MakeOwnedInstance();
	void Metal4DestroyDevice(void * impl) noexcept;
	void Metal4DestroyInstance(void * impl) noexcept;
	void * Metal4InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * Metal4CreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	template <typename HandleT>
	[[nodiscard]] HandleT MintHandle(Metal4Device * device)
	{
		return device->tracked.Store<HandleT>(std::monostate{});
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, BufferHandle handle) noexcept
	{
		return device->buffers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TextureHandle handle) noexcept
	{
		return device->textures.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, SamplerHandle handle) noexcept
	{
		return device->samplers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, HeapHandle handle) noexcept
	{
		return device->heaps.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TimelineHandle handle) noexcept
	{
		return device->timelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename HandleT>
	[[nodiscard]] bool Resolves(Metal4Device * device, HandleT handle) noexcept
	{
		return device->tracked.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool Metal4Unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT UnimplementedHandle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<HandleT>(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT Metal4CreateHandle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.create");

		Error * error		 = LastError(args...);
		const HandleT handle = MintHandle<HandleT>(static_cast<Metal4Device *>(impl));
		if (!handle.IsValid())
		{
			return FailValue<HandleT>(error, ErrorCode::eOutOfHostMemory, "Metal backend handle allocation failed");
		}

		return ReturnValue(handle, error);
	}

	inline DescriptorSetLayoutHandle Metal4CreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return FailValue<DescriptorSetLayoutHandle>(
					error, ErrorCode::eUnsupportedFeature, "Metal does not bake samplers into a descriptor set layout, so write the sampler into the set");
			}
		}

		Metal4DescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<Metal4Device *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

}

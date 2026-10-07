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
#include "azoth/rhi/backend/support/bounded_count.hpp"
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

		ListLifecycle lifecycle = ListLifecycle::eFresh;

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

		BoundedCount mapCount;

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

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & queues_for_type(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] MTL4::CommandQueue * command_queue_for(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & pool = queues_for_type(type);
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

		void note_allocation(Residency kind, const MTL::Allocation * allocation) noexcept;

		NS::SharedPtr<MTL::SharedEvent> drainEvent;
		std::atomic<std::uint64_t> drainValue{ 0 };

		detail::HostVector<HostUniquePtr<Metal4Swapchain>> swapchains;

		~Metal4Device()
		{
			objects.reset();
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

	[[nodiscard]] void * alloc_object(Metal4Device * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool metal4_refuse_unexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] Metal4BackendOwner & backend_owner();
	GraphicsApiId metal4_device_api_id([[maybe_unused]] void * impl) noexcept;
	std::string_view metal4_device_api_name([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & metal4_device_caps(void * impl) noexcept;
	const AdapterInfo & metal4_device_adapter_info(void * impl) noexcept;
	ValidationMessageCounts metal4_device_validation_message_counts(void * impl) noexcept;
	FormatSupport metal4_device_format_support(void * impl, Format format) noexcept;
	bool metal4_get_texture_info(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool metal4_get_buffer_info(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle metal4_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle metal4_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle metal4_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle metal4_create_sampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool metal4_get_texture_memory_info(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool metal4_get_buffer_memory_info(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * resolve_heap(Metal4Device * device, HeapHandle handle) noexcept;
	HeapHandle metal4_create_heap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle metal4_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle metal4_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle metal4_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * resolve_buffer(Metal4Device * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * resolve_texture(Metal4Device * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format resolve_texture_format(Metal4Device * device, TextureHandle handle) noexcept;

	[[nodiscard]] inline CmdList * list_of(Metal4Object * object) noexcept
	{
		return object != nullptr ? object->list : nullptr;
	}

	[[nodiscard]] inline CmdList * recording_list_of(Metal4Object * object) noexcept
	{
		CmdList * list = list_of(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr || list->lifecycle != ListLifecycle::eRecording)
		{
			return nullptr;
		}

		return list;
	}

	void end_active_encoders(CmdList * list) noexcept;
	[[nodiscard]] MTL4::ComputeCommandEncoder * begin_compute(Metal4Object * object, Error * error) noexcept;

	void note_list_allocation(CmdList * list, const MTL::Allocation * allocation) noexcept;

	inline constexpr std::uint64_t kDebugScopeCommandBuffer = 0;
	inline constexpr std::uint64_t kDebugScopeClosed		= std::numeric_limits<std::uint64_t>::max();

	void pop_encoder_debug_groups(CmdList * list, MTL4::CommandEncoder * encoder) noexcept;

	void flush_pending_barrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept;
	void flush_pending_barrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept;

	[[nodiscard]] MTL::GPUAddress write_push_constants(Metal4Device * device, CmdList * list, const void * data, std::uint32_t size) noexcept;

	BinarySemaphoreHandle metal4_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;

	bool metal4_cmd_begin(void * impl, Error * error) noexcept;
	bool metal4_cmd_end(void * impl, Error * error) noexcept;
	bool metal4_cmd_barriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool metal4_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool metal4_cmd_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool metal4_cmd_end_debug_label(void * impl, Error * error) noexcept;

	bool metal4_cmd_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool metal4_cmd_dispatch(void * impl, std::uint32_t x, std::uint32_t y, std::uint32_t z, Error * error) noexcept;
	bool metal4_cmd_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	bool metal4_cmd_copy_buffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool metal4_cmd_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool metal4_cmd_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool metal4_cmd_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool metal4_cmd_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool metal4_cmd_clear_texture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool metal4_cmd_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool metal4_cmd_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool metal4_cmd_blit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * resolve_texture_view(Metal4Device * device, TextureViewHandle handle) noexcept;

	[[nodiscard]] bool binding_maps_agree(Metal4Device * device, PipelineLayoutHandle layout, std::span<const ShaderBinary> shaders, Error * error) noexcept;

	[[nodiscard]] bool function_buffers_are_bound(Metal4Device * device, PipelineLayoutHandle layout, const NS::Array * bindings, Error * error) noexcept;

	PipelineLayoutHandle metal4_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle metal4_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle metal4_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;

	bool metal4_cmd_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool metal4_cmd_end_rendering(void * impl, Error * error) noexcept;
	bool metal4_cmd_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool metal4_cmd_set_viewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool metal4_cmd_set_scissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool metal4_cmd_set_blend_constants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool metal4_cmd_set_stencil_reference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool metal4_cmd_set_depth_bias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool metal4_cmd_set_vertex_buffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool metal4_cmd_set_index_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool metal4_cmd_draw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool metal4_cmd_draw_indexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool metal4_cmd_draw_indirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool metal4_cmd_draw_indexed_indirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;

	bool metal4_cmd_bind_descriptor_set(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool metal4_cmd_push_constants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;

	bool metal4_update_descriptors_buffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool metal4_update_descriptors_texture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool metal4_update_descriptors_sampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	void * metal4_create_descriptor_arena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * metal4_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * metal4_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory metal4_map(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool metal4_unmap(void * impl, BufferHandle buffer, Error * error) noexcept;
	bool metal4_query_memory_budget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] Metal4QueryPool * resolve_query_pool(Metal4Device * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle metal4_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool metal4_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool metal4_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool metal4_cmd_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
	bool metal4_cmd_begin_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool metal4_cmd_end_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool metal4_cmd_resolve_query_data(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	bool metal4_destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool metal4_collect_garbage(void * impl, ResourceType type, Error * error) noexcept;
	bool metal4_collect_garbage_timeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle metal4_adopt_buffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle metal4_adopt_texture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool metal4_get_native_buffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool metal4_get_native_texture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept;
	AccelerationStructureHandle metal4_create_acceleration_structure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept;
	RayTracingPipelineHandle metal4_create_ray_tracing_pipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept;
	bool metal4_begin_native_mutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle metal4_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool metal4_arena_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & core_device_block() noexcept;
	const PresentApi & present_block() noexcept;
	const PlacedMemoryApi & placed_memory_block() noexcept;
	const RayTracingApi & ray_tracing_block() noexcept;
	const ResourceIntrospectionApi & resource_introspection_block() noexcept;
	const QueryApi & query_block() noexcept;
	const ResidencyApi & residency_block() noexcept;
	TextureViewHandle metal4_adopt_texture_view(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle metal4_adopt_sampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool metal4_get_native_texture_view(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool metal4_get_native_sampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle metal4_adopt_timeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle metal4_adopt_binary_semaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool metal4_get_native_timeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool metal4_get_native_binary_semaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & adoption_block() noexcept;
	const InstanceApi & instance_block() noexcept;
	const ExternalCapabilityApi & external_capability_block() noexcept;
	const QueueApi & queue_block() noexcept;
	const CommandPoolApi & command_pool_block() noexcept;
	const RenderCommandApi & render_command_block() noexcept;
	const QueryCommandApi & query_command_block() noexcept;
	const AliasingCommandApi & aliasing_command_block() noexcept;
	const IndirectApi & indirect_block() noexcept;
	const NativeEscapeApi & native_escape_block() noexcept;
	const DescriptorArenaApi & descriptor_arena_block() noexcept;
	AcquireResult metal4_swapchain_acquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult metal4_swapchain_present(void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	TextureHandle metal4_swapchain_back_buffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle metal4_swapchain_back_buffer_view(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle metal4_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format metal4_swapchain_format(void * impl) noexcept;
	bool metal4_swapchain_supports_readback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t metal4_swapchain_image_count(void * impl) noexcept;
	std::uint32_t metal4_swapchain_width(void * impl) noexcept;
	std::uint32_t metal4_swapchain_height(void * impl) noexcept;
	bool metal4_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool metal4_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & swapchain_block() noexcept;
	void * metal4_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType metal4_queue_type_of(void * impl) noexcept;
	bool metal4_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool metal4_queue_wait_idle(void * impl, Error * error) noexcept;
	bool metal4_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool metal4_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool metal4_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool metal4_queue_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool metal4_queue_end_debug_label(void * impl, Error * error) noexcept;
	void * metal4_command_pool_allocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool metal4_command_pool_reset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId metal4_instance_api_id([[maybe_unused]] void * impl) noexcept;
	bool metal4_enumerate_adapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool metal4_query_external_handle_support(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool metal4_export_buffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal4_export_heap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal4_export_texture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal4_export_timeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal4_export_binary_semaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle metal4_import_buffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle metal4_import_heap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle metal4_import_texture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle metal4_import_timeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle metal4_import_binary_semaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool metal4_close_exported_handle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & external_sharing_block() noexcept;
	void populate_caps(Metal4Device * device);

	[[nodiscard]] bool adapter_has_metal4(MTL::Device * device) noexcept;

	[[nodiscard]] Metal4Device * make_owned_device(Metal4Instance * instance, const DeviceDesc & desc, Error & refusal);
	[[nodiscard]] Metal4Instance * make_owned_instance();
	void metal4_destroy_device(void * impl) noexcept;
	void metal4_destroy_instance(void * impl) noexcept;
	void * metal4_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * metal4_create_instance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	template <typename HandleT>
	[[nodiscard]] HandleT mint_handle(Metal4Device * device)
	{
		return device->tracked.store<HandleT>(std::monostate{});
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, BufferHandle handle) noexcept
	{
		return device->buffers.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, TextureHandle handle) noexcept
	{
		return device->textures.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, SamplerHandle handle) noexcept
	{
		return device->samplers.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, HeapHandle handle) noexcept
	{
		return device->heaps.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, TimelineHandle handle) noexcept
	{
		return device->timelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(Metal4Device * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename HandleT>
	[[nodiscard]] bool resolves(Metal4Device * device, HandleT handle) noexcept
	{
		return device->tracked.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool metal4_unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return fail(last_error(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT unimplemented_handle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return fail_value<HandleT>(last_error(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT metal4_create_handle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.create");

		Error * error		 = last_error(args...);
		const HandleT handle = mint_handle<HandleT>(static_cast<Metal4Device *>(impl));
		if (!handle.is_valid())
		{
			return fail_value<HandleT>(error, ErrorCode::eOutOfHostMemory, "Metal backend handle allocation failed");
		}

		return return_value(handle, error);
	}

	inline DescriptorSetLayoutHandle metal4_create_descriptor_set_layout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return fail_value<DescriptorSetLayoutHandle>(
					error, ErrorCode::eUnsupportedFeature, "Metal does not bake samplers into a descriptor set layout, so write the sampler into the set");
			}
		}

		Metal4DescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<Metal4Device *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.store(std::move(slot));
		if (!handle.is_valid())
		{
			return fail_value<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return return_value(handle, error);
	}

}

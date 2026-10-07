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

namespace azo::rhi::metal
{
	// How an RHI value becomes a Metal value, which is the same answer here and on the Metal 4 backend. NOLINTNEXTLINE(google-build-using-namespace): the
	using namespace azo::rhi::metal_common;

	struct MetalDevice;
	struct MetalObject;

	struct MetalCmdList final
	{
		NS::SharedPtr<MTL::CommandBuffer> commandBuffer;
		ListLifecycle lifecycle = ListLifecycle::eFresh;

		bool holdsListSlot = false;

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

		void end_encoders() noexcept;

		MetalCmdList() = default;
		~MetalCmdList();

		MetalCmdList(const MetalCmdList &)			   = delete;
		MetalCmdList & operator=(const MetalCmdList &) = delete;
		MetalCmdList(MetalCmdList &&)				   = delete;
		MetalCmdList & operator=(MetalCmdList &&)	   = delete;
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

		BoundedCount mapCount;

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

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & queues_for_type(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		[[nodiscard]] MTL::CommandQueue * command_queue_for(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & pool = queues_for_type(type);
			return pool.empty() ? nullptr : pool.front().get();
		}

		// Submit and waitIdle commit a command buffer of their own for every wait and signal, so those slots cannot go to open lists.
		static constexpr std::uint32_t kCommandBufferHeadroom = 8;

		// A queue sized for the setting plus the headroom, clamped so a caller asking for a huge budget cannot wrap the count.
		[[nodiscard]] static std::uint32_t command_buffers_per_queue(std::uint32_t openListBound) noexcept
		{
			constexpr std::uint32_t kCeiling = std::numeric_limits<std::uint32_t>::max() - kCommandBufferHeadroom;
			return (openListBound > kCeiling ? kCeiling : openListBound) + kCommandBufferHeadroom;
		}

		OpenListBudget openLists;

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

		void note_allocation(Residency kind, const MTL::Allocation * allocation) noexcept;

		detail::HostVector<HostUniquePtr<MetalSwapchain>> swapchains;

		~MetalDevice()
		{
			objects.reset();
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

	[[nodiscard]] void * alloc_object(MetalDevice * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool metal_refuse_unexportable(
		Flags<ExternalHandleType> declared,
		Flags<ExternalHandleType> allowed,
		const char * what,
		Error * error
	) noexcept;
	[[nodiscard]] MetalBackendOwner & backend_owner();
	GraphicsApiId metal_device_api_id([[maybe_unused]] void * impl) noexcept;
	std::string_view metal_device_api_name([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & metal_device_caps(void * impl) noexcept;
	const AdapterInfo & metal_device_adapter_info(void * impl) noexcept;
	ValidationMessageCounts metal_device_validation_message_counts(void * impl) noexcept;
	FormatSupport metal_device_format_support(void * impl, Format format) noexcept;
	bool metal_get_texture_info(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool metal_get_buffer_info(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle metal_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle metal_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle metal_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle metal_create_sampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool metal_get_texture_memory_info(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool metal_get_buffer_memory_info(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * resolve_heap(MetalDevice * device, HeapHandle handle) noexcept;
	HeapHandle metal_create_heap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle metal_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle metal_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle metal_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * resolve_buffer(MetalDevice * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * resolve_texture(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format resolve_texture_format(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] MTL::CommandBuffer * cmd_buffer_of(MetalObject * object) noexcept;
	void end_active_encoders(MetalObject * object) noexcept;
	void release_cmd_buffer(MetalDevice * device, MetalCmdList * rec, QueueType queueType) noexcept;
	[[nodiscard]] MTL::BlitCommandEncoder * begin_blit(MetalObject * object, Error * error) noexcept;
	void consume_alias_wait(MetalCmdList * rec, MTL::RenderCommandEncoder * encoder) noexcept;
	void consume_alias_wait(MetalCmdList * rec, MTL::ComputeCommandEncoder * encoder) noexcept;
	void consume_alias_wait(MetalCmdList * rec, MTL::BlitCommandEncoder * encoder) noexcept;
	[[nodiscard]] MetalCmdList * new_cmd_list(MetalDevice * device);
	BinarySemaphoreHandle metal_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;
	bool metal_cmd_begin(void * impl, Error * error) noexcept;
	bool metal_cmd_end(void * impl, Error * error) noexcept;
	bool metal_cmd_barriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool metal_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool metal_cmd_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool metal_cmd_end_debug_label(void * impl, Error * error) noexcept;
	bool metal_clear_texture(
		void * impl,
		TextureHandle texture,
		const ClearColor & color,
		std::span<const TextureSubresourceRange> ranges,
		Error * error
	) noexcept;
	bool metal_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool metal_copy_buffer(
		void * impl,
		BufferHandle dst,
		std::uint64_t dstOffset,
		BufferHandle src,
		std::uint64_t srcOffset,
		std::uint64_t size,
		Error * error
	) noexcept;
	bool metal_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool metal_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool metal_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool metal_blit(
		void * impl,
		[[maybe_unused]] TextureHandle dst,
		[[maybe_unused]] TextureHandle src,
		[[maybe_unused]] std::span<const TextureBlit> regions,
		[[maybe_unused]] Filter filter,
		Error * error
	) noexcept;
	bool metal_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool metal_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * resolve_texture_view(MetalDevice * device, TextureViewHandle handle) noexcept;
	PipelineLayoutHandle metal_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle metal_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle metal_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	bool metal_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool metal_end_rendering(void * impl, Error * error) noexcept;
	bool metal_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool metal_set_viewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool metal_set_scissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool metal_set_blend_constants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool metal_set_stencil_reference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool metal_set_depth_bias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool metal_set_vertex_buffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool metal_set_index_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool metal_push_constants(
		void * impl,
		[[maybe_unused]] PipelineLayoutHandle layout,
		Flags<ShaderStage> stages,
		std::uint32_t offset,
		std::uint32_t size,
		const void * data,
		Error * error
	) noexcept;
	bool metal_draw(
		void * impl,
		std::uint32_t vertexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstVertex,
		std::uint32_t firstInstance,
		Error * error
	) noexcept;
	bool metal_draw_indexed(
		void * impl,
		std::uint32_t indexCount,
		std::uint32_t instanceCount,
		std::uint32_t firstIndex,
		std::int32_t vertexOffset,
		std::uint32_t firstInstance,
		Error * error
	) noexcept;
	bool metal_draw_indirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool metal_draw_indexed_indirect(
		void * impl,
		BufferHandle args,
		std::uint64_t offset,
		std::uint32_t drawCount,
		std::uint32_t stride,
		Error * error
	) noexcept;
	inline constexpr std::uint64_t kDebugScopeCommandBuffer = 0;
	inline constexpr std::uint64_t kDebugScopeClosed		= std::numeric_limits<std::uint64_t>::max();

	void pop_encoder_debug_groups(MetalCmdList * rec, MTL::CommandEncoder * encoder) noexcept;

	[[nodiscard]] bool ensure_compute_encoder(MetalObject * object, Error * error) noexcept;
	bool metal_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool metal_bind_descriptor_set(
		void * impl,
		[[maybe_unused]] PipelineLayoutHandle layout,
		[[maybe_unused]] std::uint32_t setIndex,
		DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets,
		Error * error
	) noexcept;
	bool metal_update_descriptors_buffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool metal_update_descriptors_texture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool metal_update_descriptors_sampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool metal_dispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool metal_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	void * metal_create_descriptor_arena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * metal_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * metal_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory metal_map(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool metal_unmap(void * impl, BufferHandle buffer, Error * error) noexcept;
	bool metal_query_memory_budget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] MetalQueryPool * resolve_query_pool(MetalDevice * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle metal_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool metal_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool metal_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool metal_cmd_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
	bool metal_cmd_begin_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool metal_cmd_end_query(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool metal_cmd_resolve_query_data(
		void * impl,
		QueryPoolHandle pool,
		std::uint32_t firstQuery,
		std::uint32_t queryCount,
		BufferHandle dst,
		std::uint64_t dstOffset,
		Error * error
	) noexcept;
	bool metal_destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool metal_collect_garbage(void * impl, ResourceType type, Error * error) noexcept;
	bool metal_collect_garbage_timeline(
		void * impl,
		ResourceType type,
		[[maybe_unused]] TimelineHandle timeline,
		[[maybe_unused]] std::uint64_t completedValue,
		Error * error
	) noexcept;
	BufferHandle metal_adopt_buffer(
		void * impl,
		GraphicsApiId api,
		const void * nativeImport,
		[[maybe_unused]] const AdoptedBufferDesc & desc,
		Error * error
	) noexcept;
	TextureHandle metal_adopt_texture(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] GraphicsApiId api,
		[[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc,
		Error * error
	) noexcept;
	bool metal_get_native_buffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool metal_get_native_texture(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] GraphicsApiId api,
		[[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport,
		Error * error
	) noexcept;
	AccelerationStructureHandle metal_create_acceleration_structure(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const AccelerationStructureDesc & desc,
		Error * error
	) noexcept;
	RayTracingPipelineHandle metal_create_ray_tracing_pipeline(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] const RayTracingPipelineDesc & desc,
		Error * error
	) noexcept;
	bool metal_begin_native_mutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle metal_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool metal_arena_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & core_device_block() noexcept;
	const PresentApi & present_block() noexcept;
	const PlacedMemoryApi & placed_memory_block() noexcept;
	const RayTracingApi & ray_tracing_block() noexcept;
	const ResourceIntrospectionApi & resource_introspection_block() noexcept;
	const QueryApi & query_block() noexcept;
	const ResidencyApi & residency_block() noexcept;
	TextureViewHandle metal_adopt_texture_view(
		void * impl,
		GraphicsApiId api,
		const void * nativeImport,
		const AdoptedTextureViewDesc & desc,
		Error * error
	) noexcept;
	SamplerHandle metal_adopt_sampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool metal_get_native_texture_view(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool metal_get_native_sampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle metal_adopt_timeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle metal_adopt_binary_semaphore(
		void * impl,
		GraphicsApiId api,
		const void * nativeImport,
		const AdoptedBinarySemaphoreDesc & desc,
		Error * error
	) noexcept;
	bool metal_get_native_timeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool metal_get_native_binary_semaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
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
	AcquireResult metal_swapchain_acquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult metal_swapchain_present(
		void * impl,
		[[maybe_unused]] std::uint32_t imageIndex,
		BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl,
		Error * error
	) noexcept;
	TextureHandle metal_swapchain_back_buffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle metal_swapchain_back_buffer_view(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle metal_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format metal_swapchain_format(void * impl) noexcept;
	bool metal_swapchain_supports_readback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t metal_swapchain_image_count(void * impl) noexcept;
	std::uint32_t metal_swapchain_width(void * impl) noexcept;
	std::uint32_t metal_swapchain_height(void * impl) noexcept;
	bool metal_swapchain_resize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool metal_swapchain_set_present_mode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & swapchain_block() noexcept;
	void * metal_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType metal_queue_type_of(void * impl) noexcept;
	bool metal_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool metal_queue_wait_idle(void * impl, Error * error) noexcept;
	bool metal_queue_get_completed_value(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool metal_queue_signal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool metal_queue_wait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool metal_queue_begin_debug_label(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool metal_queue_end_debug_label(void * impl, Error * error) noexcept;
	void * metal_command_pool_allocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool metal_command_pool_reset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId metal_instance_api_id([[maybe_unused]] void * impl) noexcept;
	bool metal_enumerate_adapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool metal_query_external_handle_support(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool metal_export_buffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal_export_heap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal_export_texture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal_export_timeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool metal_export_binary_semaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle metal_import_buffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle metal_import_heap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle metal_import_texture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle metal_import_timeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle metal_import_binary_semaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool metal_close_exported_handle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & external_sharing_block() noexcept;
	void populate_caps(MetalDevice * device);
	[[nodiscard]] MetalDevice * make_owned_device(MetalInstance * instance, const DeviceDesc & desc, Error & refusal);
	[[nodiscard]] MetalInstance * make_owned_instance();
	void metal_destroy_device(void * impl) noexcept;
	void metal_destroy_instance(void * impl) noexcept;
	void * metal_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * metal_create_instance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	template <typename HandleT>
	[[nodiscard]] HandleT mint_handle(MetalDevice * device)
	{
		return device->tracked.store<HandleT>(std::monostate{});
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, BufferHandle handle) noexcept
	{
		return device->buffers.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, TextureHandle handle) noexcept
	{
		return device->textures.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, SamplerHandle handle) noexcept
	{
		return device->samplers.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, HeapHandle handle) noexcept
	{
		return device->heaps.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, TimelineHandle handle) noexcept
	{
		return device->timelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool resolves(MetalDevice * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename HandleT>
	[[nodiscard]] bool resolves(MetalDevice * device, HandleT handle) noexcept
	{
		return device->tracked.resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool metal_unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return fail(last_error(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT metal_unimplemented_handle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return fail_value<HandleT>(last_error(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT metal_create_handle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.create");

		Error * error		 = last_error(args...);
		const HandleT handle = mint_handle<HandleT>(static_cast<MetalDevice *>(impl));
		if (!handle.is_valid())
		{
			return fail_value<HandleT>(error, ErrorCode::eOutOfHostMemory, "Metal backend handle allocation failed");
		}

		return return_value(handle, error);
	}

	inline DescriptorSetLayoutHandle metal_create_descriptor_set_layout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return fail_value<DescriptorSetLayoutHandle>(
					error,
					ErrorCode::eUnsupportedFeature,
					"Metal does not bake samplers into a descriptor set layout, so write the sampler into the set"
				);
			}
		}

		MetalDescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<MetalDevice *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.store(std::move(slot));
		if (!handle.is_valid())
		{
			return fail_value<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return return_value(handle, error);
	}

}

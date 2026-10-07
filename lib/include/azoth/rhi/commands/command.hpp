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

#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi
{

	struct CommandListBlocks;

	namespace detail
	{
		struct FacadeBuilder;
	}

	struct CommandPoolApi;
	class BackendBlockSet;

	struct Viewport final
	{
		float x		   = 0.0f;
		float y		   = 0.0f;
		float width	   = 0.0f;
		float height   = 0.0f;
		float minDepth = 0.0f;
		float maxDepth = 1.0f;
	};

	struct Rect2D final
	{
		std::int32_t x		 = 0;
		std::int32_t y		 = 0;
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;
	};

	enum class ListReuse : std::uint8_t
	{
		eBulkReset,

		ePerListReset,
	};

	struct CommandPoolDesc final
	{
		QueueType queueType	   = QueueType::eGraphics;
		bool transient		   = true;
		ListReuse reuse		   = ListReuse::eBulkReset;
		const char * debugName = nullptr;
	};

	class AZO_RHI_API CommandPool final
	{
	public:
		CommandPool() = default;

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr && m_dispatch != nullptr;
		}

		[[nodiscard]] CommandList allocate(const char * debugName = nullptr) noexcept;
		[[nodiscard]] CommandList allocate(const char * debugName, Error & error) noexcept;
		[[nodiscard]] Result<CommandList> allocate_with_result(const char * debugName = nullptr) noexcept;
		[[nodiscard]] bool reset(RetirePoint safeAfter) noexcept;
		[[nodiscard]] bool reset(RetirePoint safeAfter, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		CommandPool(void * impl, const CommandPoolApi * dispatch, BackendBlockSet * blocks) noexcept : m_impl(impl), m_dispatch(dispatch), m_blocks(blocks) {}

		void * m_impl					  = nullptr;
		const CommandPoolApi * m_dispatch = nullptr;

		BackendBlockSet * m_blocks = nullptr;
	};

	class AZO_RHI_API CommandList final
	{
	public:
		CommandList() = default;

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		[[nodiscard]] bool begin() noexcept;
		[[nodiscard]] bool begin(Error & error) noexcept;
		[[nodiscard]] bool end() noexcept;
		[[nodiscard]] bool end(Error & error) noexcept;

		bool barriers(const BarrierBatch & barriers) noexcept;
		bool barriers(const BarrierBatch & barriers, Error & error) noexcept;
		bool alias_barriers(std::span<const AliasBarrier> barriers) noexcept;
		bool alias_barriers(std::span<const AliasBarrier> barriers, Error & error) noexcept;

		bool transition(TextureHandle texture, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse) noexcept;
		bool transition(TextureHandle texture, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse, Error & error) noexcept;
		bool transition(BufferHandle buffer, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse) noexcept;
		bool transition(BufferHandle buffer, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse, Error & error) noexcept;

		bool begin_rendering(const BeginRenderingDesc & desc) noexcept;
		bool begin_rendering(const BeginRenderingDesc & desc, Error & error) noexcept;
		bool end_rendering() noexcept;
		bool end_rendering(Error & error) noexcept;

		bool set_graphics_pipeline(GraphicsPipelineHandle pipeline) noexcept;
		bool set_graphics_pipeline(GraphicsPipelineHandle pipeline, Error & error) noexcept;
		bool set_compute_pipeline(ComputePipelineHandle pipeline) noexcept;
		bool set_compute_pipeline(ComputePipelineHandle pipeline, Error & error) noexcept;
		bool set_ray_tracing_pipeline(RayTracingPipelineHandle pipeline) noexcept;
		bool set_ray_tracing_pipeline(RayTracingPipelineHandle pipeline, Error & error) noexcept;

		bool bind_descriptor_set(
			PipelineLayoutHandle layout,
			std::uint32_t setIndex,
			DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets = {}
		) noexcept;
		bool bind_descriptor_set(
			PipelineLayoutHandle layout,
			std::uint32_t setIndex,
			DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets,
			Error & error
		) noexcept;

		bool push_constants(PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data) noexcept;
		bool push_constants(
			PipelineLayoutHandle layout,
			Flags<ShaderStage> stages,
			std::uint32_t offset,
			std::uint32_t size,
			const void * data,
			Error & error
		) noexcept;

		bool set_viewport(const Viewport & viewport) noexcept;
		bool set_viewport(const Viewport & viewport, Error & error) noexcept;
		bool set_scissor(const Rect2D & scissor) noexcept;
		bool set_scissor(const Rect2D & scissor, Error & error) noexcept;
		bool set_blend_constants(float r, float g, float b, float a) noexcept;
		bool set_blend_constants(float r, float g, float b, float a, Error & error) noexcept;
		bool set_stencil_reference(std::uint32_t reference) noexcept;
		bool set_stencil_reference(std::uint32_t reference, Error & error) noexcept;

		bool set_depth_bias(float constantFactor, float clamp, float slopeFactor) noexcept;
		bool set_depth_bias(float constantFactor, float clamp, float slopeFactor, Error & error) noexcept;

		bool set_vertex_buffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) noexcept;
		bool set_vertex_buffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error & error) noexcept;

		bool set_index_buffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept;
		bool set_index_buffer(BufferHandle buffer, std::uint64_t offset, bool index32, Error & error) noexcept;

		bool draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) noexcept;
		bool draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error & error) noexcept;
		bool draw_indexed(
			std::uint32_t indexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstIndex,
			std::int32_t vertexOffset,
			std::uint32_t firstInstance
		) noexcept;
		bool draw_indexed(
			std::uint32_t indexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstIndex,
			std::int32_t vertexOffset,
			std::uint32_t firstInstance,
			Error & error
		) noexcept;

		bool draw_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool draw_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		bool draw_indexed_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool draw_indexed_indirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		bool draw_indirect_count(
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride
		) noexcept;
		bool draw_indirect_count(
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride,
			Error & error
		) noexcept;

		bool draw_indexed_indirect_count(
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride
		) noexcept;
		bool draw_indexed_indirect_count(
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride,
			Error & error
		) noexcept;

		bool dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) noexcept;
		bool dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error & error) noexcept;

		bool dispatch_indirect(BufferHandle args, std::uint64_t offset) noexcept;
		bool dispatch_indirect(BufferHandle args, std::uint64_t offset, Error & error) noexcept;

		bool build_acceleration_structures(std::span<const AccelerationStructureBuildDesc> builds) noexcept;
		bool build_acceleration_structures(std::span<const AccelerationStructureBuildDesc> builds, Error & error) noexcept;
		bool copy_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool copy_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;
		bool compact_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool compact_acceleration_structure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;

		bool trace_rays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept;
		bool trace_rays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error & error) noexcept;

		bool copy_buffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size) noexcept;
		bool copy_buffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error & error) noexcept;

		bool copy_buffer_to_texture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool copy_buffer_to_texture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool copy_texture_to_buffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool copy_texture_to_buffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool copy_texture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions) noexcept;
		bool copy_texture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error & error) noexcept;

		bool clear_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) noexcept;
		bool clear_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error & error) noexcept;

		bool clear_texture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges) noexcept;
		bool clear_texture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error & error) noexcept;
		bool resolve_texture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions) noexcept;
		bool resolve_texture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error & error) noexcept;

		bool blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter) noexcept;
		bool blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept;

		bool generate_mips(TextureHandle texture) noexcept;
		bool generate_mips(TextureHandle texture, Error & error) noexcept;

		bool reset_query_pool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount) noexcept;
		bool reset_query_pool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error & error) noexcept;
		bool write_timestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage) noexcept;
		bool write_timestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error & error) noexcept;
		bool begin_query(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool begin_query(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;
		bool end_query(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool end_query(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;

		bool resolve_query_data(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset) noexcept;
		bool resolve_query_data(
			QueryPoolHandle pool,
			std::uint32_t firstQuery,
			std::uint32_t queryCount,
			BufferHandle dst,
			std::uint64_t dstOffset,
			Error & error
		) noexcept;

		bool begin_debug_label(const char * name, std::uint32_t color = 0) noexcept;
		bool begin_debug_label(const char * name, std::uint32_t color, Error & error) noexcept;
		bool end_debug_label() noexcept;
		bool end_debug_label(Error & error) noexcept;

		template <GraphicsApiTag Api, class Fn>
		bool modify_native(const NativeMutationDesc & desc, Fn && fn) noexcept;

		template <GraphicsApiTag Api, class Fn>
		bool modify_native(const NativeMutationDesc & desc, Fn && fn, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		CommandList(void * impl, const CommandListBlocks * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		bool BeginNativeMutation(GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept;
		bool EndNativeMutation(const NativeMutationDesc & desc, Error * error) noexcept;

		void * m_impl					   = nullptr;
		const CommandListBlocks * m_blocks = nullptr;
	};

	template <GraphicsApiTag Api, class Fn>
	bool CommandList::modify_native(const NativeMutationDesc & desc, Fn && fn) noexcept
	{
		static_assert(native::HasNativeAccess<Api>, "include azoth/rhi/native/<backend>.hpp for this backend before calling ModifyNative");

		if (!BeginNativeMutation(Api::kId, desc, nullptr))
		{
			return false;
		}

		std::forward<Fn>(fn)(native::NativeAccess<Api>::make_command_list_view(m_impl));

		return EndNativeMutation(desc, nullptr);
	}

	template <GraphicsApiTag Api, class Fn>
	bool CommandList::modify_native(const NativeMutationDesc & desc, Fn && fn, Error & error) noexcept
	{
		static_assert(native::HasNativeAccess<Api>, "include azoth/rhi/native/<backend>.hpp for this backend before calling ModifyNative");

		error = {};
		if (!BeginNativeMutation(Api::kId, desc, &error))
		{
			return false;
		}

		std::forward<Fn>(fn)(native::NativeAccess<Api>::make_command_list_view(m_impl));
		return EndNativeMutation(desc, &error);
	}

}

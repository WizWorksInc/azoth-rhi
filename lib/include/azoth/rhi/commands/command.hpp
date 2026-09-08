// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_dispatch != nullptr;
		}

		[[nodiscard]] CommandList Allocate(const char * debugName = nullptr) noexcept;
		[[nodiscard]] CommandList Allocate(const char * debugName, Error & error) noexcept;
		[[nodiscard]] Result<CommandList> AllocateWithResult(const char * debugName = nullptr) noexcept;
		[[nodiscard]] bool Reset(RetirePoint safeAfter) noexcept;
		[[nodiscard]] bool Reset(RetirePoint safeAfter, Error & error) noexcept;

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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		[[nodiscard]] bool Begin() noexcept;
		[[nodiscard]] bool Begin(Error & error) noexcept;
		[[nodiscard]] bool End() noexcept;
		[[nodiscard]] bool End(Error & error) noexcept;

		bool Barriers(const BarrierBatch & barriers) noexcept;
		bool Barriers(const BarrierBatch & barriers, Error & error) noexcept;
		bool AliasBarriers(std::span<const AliasBarrier> barriers) noexcept;
		bool AliasBarriers(std::span<const AliasBarrier> barriers, Error & error) noexcept;

		bool Transition(TextureHandle texture, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse) noexcept;
		bool Transition(TextureHandle texture, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse, Error & error) noexcept;
		bool Transition(BufferHandle buffer, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse) noexcept;
		bool Transition(BufferHandle buffer, Flags<ResourceUse> fromUse, Flags<ResourceUse> toUse, Error & error) noexcept;

		bool BeginRendering(const BeginRenderingDesc & desc) noexcept;
		bool BeginRendering(const BeginRenderingDesc & desc, Error & error) noexcept;
		bool EndRendering() noexcept;
		bool EndRendering(Error & error) noexcept;

		bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept;
		bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline, Error & error) noexcept;
		bool SetComputePipeline(ComputePipelineHandle pipeline) noexcept;
		bool SetComputePipeline(ComputePipelineHandle pipeline, Error & error) noexcept;
		bool SetRayTracingPipeline(RayTracingPipelineHandle pipeline) noexcept;
		bool SetRayTracingPipeline(RayTracingPipelineHandle pipeline, Error & error) noexcept;

		bool BindDescriptorSet(PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets = {}) noexcept;
		bool BindDescriptorSet(PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets, Error & error) noexcept;

		bool PushConstants(PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data) noexcept;
		bool PushConstants(
			PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data, Error & error) noexcept;

		bool SetViewport(const Viewport & viewport) noexcept;
		bool SetViewport(const Viewport & viewport, Error & error) noexcept;
		bool SetScissor(const Rect2D & scissor) noexcept;
		bool SetScissor(const Rect2D & scissor, Error & error) noexcept;
		bool SetBlendConstants(float r, float g, float b, float a) noexcept;
		bool SetBlendConstants(float r, float g, float b, float a, Error & error) noexcept;
		bool SetStencilReference(std::uint32_t reference) noexcept;
		bool SetStencilReference(std::uint32_t reference, Error & error) noexcept;

		bool SetDepthBias(float constantFactor, float clamp, float slopeFactor) noexcept;
		bool SetDepthBias(float constantFactor, float clamp, float slopeFactor, Error & error) noexcept;

		bool SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) noexcept;
		bool SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error & error) noexcept;

		bool SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept;
		bool SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32, Error & error) noexcept;

		bool Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) noexcept;
		bool Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error & error) noexcept;
		bool DrawIndexed(
			std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance) noexcept;
		bool DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, Error & error) noexcept;

		bool DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		bool DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		bool DrawIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride) noexcept;
		bool DrawIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride, Error & error) noexcept;

		bool DrawIndexedIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride) noexcept;
		bool DrawIndexedIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride, Error & error) noexcept;

		bool Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) noexcept;
		bool Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error & error) noexcept;

		bool DispatchIndirect(BufferHandle args, std::uint64_t offset) noexcept;
		bool DispatchIndirect(BufferHandle args, std::uint64_t offset, Error & error) noexcept;

		bool BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds) noexcept;
		bool BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds, Error & error) noexcept;
		bool CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;
		bool CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;

		bool TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept;
		bool TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error & error) noexcept;

		bool CopyBuffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size) noexcept;
		bool CopyBuffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error & error) noexcept;

		bool CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions) noexcept;
		bool CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error & error) noexcept;

		bool ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) noexcept;
		bool ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error & error) noexcept;

		bool ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges) noexcept;
		bool ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error & error) noexcept;
		bool ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions) noexcept;
		bool ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error & error) noexcept;

		bool Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter) noexcept;
		bool Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept;

		bool GenerateMips(TextureHandle texture) noexcept;
		bool GenerateMips(TextureHandle texture, Error & error) noexcept;

		bool ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount) noexcept;
		bool ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error & error) noexcept;
		bool WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage) noexcept;
		bool WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error & error) noexcept;
		bool BeginQuery(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool BeginQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;
		bool EndQuery(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool EndQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;

		bool ResolveQueryData(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset) noexcept;
		bool ResolveQueryData(
			QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset, Error & error) noexcept;

		bool BeginDebugLabel(const char * name, std::uint32_t color = 0) noexcept;
		bool BeginDebugLabel(const char * name, std::uint32_t color, Error & error) noexcept;
		bool EndDebugLabel() noexcept;
		bool EndDebugLabel(Error & error) noexcept;

		template <GraphicsApiTag Api, class Fn>
		bool ModifyNative(const NativeMutationDesc & desc, Fn && fn) noexcept;

		template <GraphicsApiTag Api, class Fn>
		bool ModifyNative(const NativeMutationDesc & desc, Fn && fn, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		CommandList(void * impl, const CommandListBlocks * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		bool BeginNativeMutation(GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept;
		bool EndNativeMutation(const NativeMutationDesc & desc, Error * error) noexcept;

		void * m_impl					   = nullptr;
		const CommandListBlocks * m_blocks = nullptr;
	};

	template <GraphicsApiTag Api, class Fn>
	bool CommandList::ModifyNative(const NativeMutationDesc & desc, Fn && fn) noexcept
	{
		static_assert(native::HasNativeAccess<Api>, "include azoth/rhi/native/<backend>.hpp for this backend before calling ModifyNative");

		if (!BeginNativeMutation(Api::id, desc, nullptr))
		{
			return false;
		}

		std::forward<Fn>(fn)(native::NativeAccess<Api>::MakeCommandListView(m_impl));

		return EndNativeMutation(desc, nullptr);
	}

	template <GraphicsApiTag Api, class Fn>
	bool CommandList::ModifyNative(const NativeMutationDesc & desc, Fn && fn, Error & error) noexcept
	{
		static_assert(native::HasNativeAccess<Api>, "include azoth/rhi/native/<backend>.hpp for this backend before calling ModifyNative");

		error = {};
		if (!BeginNativeMutation(Api::id, desc, &error))
		{
			return false;
		}

		std::forward<Fn>(fn)(native::NativeAccess<Api>::MakeCommandListView(m_impl));
		return EndNativeMutation(desc, &error);
	}

}

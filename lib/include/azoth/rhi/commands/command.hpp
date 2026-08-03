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
 * \brief Public command-pool and command-list facade API.
 */

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

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi
{

	struct CommandListBlocks;

	namespace detail
	{
		struct FacadeBuilder;
	} // namespace detail

	struct CommandPoolApi;
	class BackendBlockSet;

	/**
	 * \brief Floating-point viewport rectangle and normalized depth range used by dynamic viewport state.
	 */
	struct Viewport final
	{
		float x		   = 0.0f;
		float y		   = 0.0f;
		float width	   = 0.0f;
		float height   = 0.0f;
		float minDepth = 0.0f;
		float maxDepth = 1.0f;
	};

	/**
	 * \brief Integer scissor rectangle used by dynamic scissor state.
	 */
	struct Rect2D final
	{
		std::int32_t x		 = 0;
		std::int32_t y		 = 0;
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;
	};

	/**
	 * \brief Command-pool creation request for one queue type.
	 *
	 * transient hints that allocated command lists are short-lived. individualReset requests per-list reset support when the backend exposes it.
	 */
	struct CommandPoolDesc final
	{
		QueueType queueType	   = QueueType::eGraphics;
		bool transient		   = true;
		bool individualReset   = false;
		const char * debugName = nullptr;
	};

	/**
	 * \brief Allocates command lists for one queue type.
	 *
	 * Command pools are host-thread owned. Reset is valid only once safeAfter has passed and the GPU no longer uses lists from this pool.
	 */
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

		// Allocated command lists use the owning device block set for command-list block resolution.
		BackendBlockSet * m_blocks = nullptr;
	};

	/**
	 * \brief Records backend commands for one command-list lifetime.
	 *
	 * A command list is not synchronized internally. Record it from one host thread and do not reuse it across frame contexts.
	 */
	class AZO_RHI_API CommandList final
	{
	public:
		CommandList() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		// Recording lifetime.

		[[nodiscard]] bool Begin() noexcept;
		[[nodiscard]] bool Begin(Error & error) noexcept;
		[[nodiscard]] bool End() noexcept;
		[[nodiscard]] bool End(Error & error) noexcept;

		// Synchronization.

		bool Barriers(const BarrierBatch & barriers) noexcept;
		bool Barriers(const BarrierBatch & barriers, Error & error) noexcept;
		bool AliasBarriers(std::span<const AliasBarrier> barriers) noexcept;
		bool AliasBarriers(std::span<const AliasBarrier> barriers, Error & error) noexcept;

		// Rendering scope.

		bool BeginRendering(const BeginRenderingDesc & desc) noexcept;
		bool BeginRendering(const BeginRenderingDesc & desc, Error & error) noexcept;
		bool EndRendering() noexcept;
		bool EndRendering(Error & error) noexcept;

		// Pipeline binding.

		bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept;
		bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline, Error & error) noexcept;
		bool SetComputePipeline(ComputePipelineHandle pipeline) noexcept;
		bool SetComputePipeline(ComputePipelineHandle pipeline, Error & error) noexcept;
		bool SetRayTracingPipeline(RayTracingPipelineHandle pipeline) noexcept;
		bool SetRayTracingPipeline(RayTracingPipelineHandle pipeline, Error & error) noexcept;

		// Descriptor and push-constant binding.

		bool BindDescriptorSet(PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets = {}) noexcept;
		bool BindDescriptorSet(PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets, Error & error) noexcept;

		/**
		 * \brief Writes push-constant bytes to the selected shader stages.
		 *
		 * \param offset Byte offset inside the pipeline layout's push-constant storage.
		 * \param size Byte count to copy from data.
		 * \param data Source bytes copied into the command stream.
		 */
		bool PushConstants(PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data) noexcept;
		bool PushConstants(
			PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data, Error & error) noexcept;

		// Dynamic state.

		bool SetViewport(const Viewport & viewport) noexcept;
		bool SetViewport(const Viewport & viewport, Error & error) noexcept;
		bool SetScissor(const Rect2D & scissor) noexcept;
		bool SetScissor(const Rect2D & scissor, Error & error) noexcept;
		bool SetBlendConstants(float r, float g, float b, float a) noexcept;
		bool SetBlendConstants(float r, float g, float b, float a, Error & error) noexcept;
		bool SetStencilReference(std::uint32_t reference) noexcept;
		bool SetStencilReference(std::uint32_t reference, Error & error) noexcept;

		/**
		 * \brief Sets dynamic depth-bias factors for later draw calls.
		 *
		 * \param constantFactor Constant depth-bias term.
		 * \param clamp Maximum absolute depth bias after scaling. Zero means no clamp.
		 * \param slopeFactor Slope-scaled depth-bias term.
		 */
		bool SetDepthBias(float constantFactor, float clamp, float slopeFactor) noexcept;
		bool SetDepthBias(float constantFactor, float clamp, float slopeFactor, Error & error) noexcept;

		// Vertex and index input.

		/**
		 * \brief Binds a vertex buffer at a byte offset.
		 */
		bool SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) noexcept;
		bool SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error & error) noexcept;

		/**
		 * \brief Binds an index buffer at a byte offset.
		 *
		 * \param index32 True for 32-bit indices. False for 16-bit indices.
		 */
		bool SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept;
		bool SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32, Error & error) noexcept;

		// Direct and indirect draws.

		bool Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) noexcept;
		bool Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error & error) noexcept;
		bool DrawIndexed(
			std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance) noexcept;
		bool DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, Error & error) noexcept;

		/**
		 * \brief Records indirect non-indexed draws from args at a byte offset.
		 *
		 * \param stride Byte distance between consecutive indirect draw records.
		 */
		bool DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool DrawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		/**
		 * \brief Records indirect indexed draws from args at a byte offset.
		 *
		 * \param stride Byte distance between consecutive indirect indexed draw records.
		 */
		bool DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) noexcept;
		bool DrawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error & error) noexcept;

		/**
		 * \brief Records counted indirect non-indexed draws.
		 *
		 * \param argsOffset Byte offset of the first indirect draw record.
		 * \param countOffset Byte offset of the draw-count value.
		 * \param maxDrawCount Upper bound applied to the count read from count.
		 * \param stride Byte distance between consecutive indirect draw records.
		 */
		bool DrawIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride) noexcept;
		bool DrawIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride, Error & error) noexcept;

		/**
		 * \brief Records counted indirect indexed draws.
		 *
		 * \param argsOffset Byte offset of the first indirect indexed draw record.
		 * \param countOffset Byte offset of the draw-count value.
		 * \param maxDrawCount Upper bound applied to the count read from count.
		 * \param stride Byte distance between consecutive indirect indexed draw records.
		 */
		bool DrawIndexedIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride) noexcept;
		bool DrawIndexedIndirectCount(BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDrawCount,
			std::uint32_t stride, Error & error) noexcept;

		// Compute dispatch.

		bool Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) noexcept;
		bool Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error & error) noexcept;

		/**
		 * \brief Records an indirect compute dispatch from args at a byte offset.
		 */
		bool DispatchIndirect(BufferHandle args, std::uint64_t offset) noexcept;
		bool DispatchIndirect(BufferHandle args, std::uint64_t offset, Error & error) noexcept;

		// Ray tracing.

		bool BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds) noexcept;
		bool BuildAccelerationStructures(std::span<const AccelerationStructureBuildDesc> builds, Error & error) noexcept;
		bool CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool CopyAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;
		bool CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src) noexcept;
		bool CompactAccelerationStructure(AccelerationStructureHandle dst, AccelerationStructureHandle src, Error & error) noexcept;

		/**
		 * \brief Records a ray dispatch using shader binding-table regions.
		 *
		 * \param width Dispatch width in rays.
		 * \param height Dispatch height in rays.
		 * \param depth Dispatch depth in rays.
		 */
		bool TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept;
		bool TraceRays(const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error & error) noexcept;

		// Copies, clears and resolves.

		/**
		 * \brief Copies a byte range between buffers.
		 *
		 * \param dstOffset Byte offset into dst.
		 * \param srcOffset Byte offset into src.
		 * \param size Byte count to copy.
		 */
		bool CopyBuffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size) noexcept;
		bool CopyBuffer(BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error & error) noexcept;

		bool CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool CopyBufferToTexture(TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions) noexcept;
		bool CopyTextureToBuffer(BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error & error) noexcept;
		bool CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions) noexcept;
		bool CopyTexture(TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error & error) noexcept;

		/**
		 * \brief Clears a byte range of a buffer with a 32-bit pattern value.
		 *
		 * \attention The buffer needs BufferUsage::eStorage. Direct3D 12 clears through a UAV, which a buffer can only carry when it was created able
		 * to, and refuses by name otherwise. Vulkan fills without one and Metal fills without one, so a buffer that omits this works on two of the three
		 * and is refused on the third.
		 */
		bool ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) noexcept;
		bool ClearBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error & error) noexcept;

		/**
		 * \brief Clears whole subresources of a color texture to one value.
		 *
		 * \attention The texture needs TextureUsage::eColorAttachment. Direct3D 12 clears through a render target view and Metal by opening a render pass, and
		 * neither can be built over a texture that was not created able to be one, so both refuse by name. Vulkan clears without it, so a texture that omits
		 * this works on one of the three.
		 */
		bool ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges) noexcept;
		bool ClearTexture(TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error & error) noexcept;
		bool ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions) noexcept;
		bool ResolveTexture(TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error & error) noexcept;

		/**
		 * \brief Records a scaled image copy. Source and destination boxes of different sizes are resampled with filter.
		 */
		bool Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter) noexcept;
		bool Blit(TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept;

		/**
		 * \brief Generates the full mip chain of a texture from mip 0 by successive linear downsamples.
		 */
		bool GenerateMips(TextureHandle texture) noexcept;
		bool GenerateMips(TextureHandle texture, Error & error) noexcept;

		// Queries.

		/**
		 * \brief Resets a contiguous range of query slots in a query pool.
		 */
		bool ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount) noexcept;
		bool ResetQueryPool(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error & error) noexcept;
		bool WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage) noexcept;
		bool WriteTimestamp(QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error & error) noexcept;
		bool BeginQuery(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool BeginQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;
		bool EndQuery(QueryPoolHandle pool, std::uint32_t query) noexcept;
		bool EndQuery(QueryPoolHandle pool, std::uint32_t query, Error & error) noexcept;

		/**
		 * \brief Resolves a contiguous range of query results into dst at a byte offset.
		 */
		bool ResolveQueryData(QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset) noexcept;
		bool ResolveQueryData(
			QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst, std::uint64_t dstOffset, Error & error) noexcept;

		// Debug labels.

		/**
		 * \brief Begins a backend debug-label region.
		 *
		 * \param color Packed 0xRRGGBBAA color. Zero requests the backend default.
		 */
		bool BeginDebugLabel(const char * name, std::uint32_t color = 0) noexcept;
		bool BeginDebugLabel(const char * name, std::uint32_t color, Error & error) noexcept;
		bool EndDebugLabel() noexcept;
		bool EndDebugLabel(Error & error) noexcept;

		// Native backend access.

		/**
		 * \brief Runs native recording against a matching backend and reconciles declared resource mutations afterward.
		 *
		 * The callback runs only when Api matches the command-list backend. Include the backend native-access header before calling this template.
		 * \attention desc must declare every resource state mutation performed by the native callback.
		 */
		template <GraphicsApiTag Api, class Fn>
		bool ModifyNative(const NativeMutationDesc & desc, Fn && fn) noexcept;

		template <GraphicsApiTag Api, class Fn>
		bool ModifyNative(const NativeMutationDesc & desc, Fn && fn, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		CommandList(void * impl, const CommandListBlocks * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		// Begin validates the API and declared resources. End reconciles final resource states after native recording.
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

} // namespace azo::rhi

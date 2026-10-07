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

#include <cstdint> // NOLINT
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Command recording callbacks for rendering, compute, and resource transfers.
	 */
	struct RenderCommandApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(RenderCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Begins recording commands.
		 */
		bool (*begin)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Finishes recording so the command list can be submitted.
		 */
		bool (*end)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Records resource state transitions and memory dependencies.
		 */
		bool (*barriers)(void * impl, const BarrierBatch & barriers, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins a rendering scope with the supplied attachments.
		 */
		bool (*beginRendering)(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the current rendering scope.
		 */
		bool (*endRendering)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Selects the graphics pipeline for subsequent draws.
		 */
		bool (*setGraphicsPipeline)(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Selects the compute pipeline for subsequent dispatches.
		 */
		bool (*setComputePipeline)(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds a descriptor set with byte offsets for its dynamic buffer bindings.
		 */
		bool (*bindDescriptorSet)(
			void * impl,
			PipelineLayoutHandle layout,
			std::uint32_t setIndex,
			DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Writes push constants for the selected stages using a byte offset and size.
		 */
		bool (*pushConstants)(
			void * impl,
			PipelineLayoutHandle layout,
			Flags<ShaderStage> stages,
			std::uint32_t offset,
			std::uint32_t size,
			const void * data,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Sets the viewport and depth range for subsequent draws.
		 */
		bool (*setViewport)(void * impl, const Viewport & viewport, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the scissor rectangle in pixels.
		 */
		bool (*setScissor)(void * impl, const Rect2D & scissor, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the RGBA constants used by constant blend factors.
		 */
		bool (*setBlendConstants)(void * impl, float r, float g, float b, float a, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the reference value for stencil tests on both faces.
		 */
		bool (*setStencilReference)(void * impl, std::uint32_t reference, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets constant and slope depth bias with a clamp value.
		 */
		bool (*setDepthBias)(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds a vertex buffer at a byte offset to the given slot.
		 */
		bool (*setVertexBuffer)(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds 32 bit indices when index32 is true, otherwise 16 bit.
		 * \param impl Command list.
		 * \param buffer Index buffer.
		 * \param offset Byte offset into the buffer.
		 * \param index32 Use 32 bit indices.
		 * \param[out] error Optional error details.
		 */
		bool (*setIndexBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept = nullptr;

		/**
		 * \brief Draws vertexCount vertices per instance.
		 */
		bool (*draw)(
			void * impl,
			std::uint32_t vertexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstVertex,
			std::uint32_t firstInstance,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Draws indexCount indices per instance, adding vertexOffset to each index.
		 */
		bool (*drawIndexed)(
			void * impl,
			std::uint32_t indexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstIndex,
			std::int32_t vertexOffset,
			std::uint32_t firstInstance,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Dispatches compute workgroups with the given counts on each axis.
		 */
		bool (*dispatch)(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies size bytes between byte offsets in the source and destination buffers.
		 */
		bool (*copyBuffer)(
			void * impl,
			BufferHandle dst,
			std::uint64_t dstOffset,
			BufferHandle src,
			std::uint64_t srcOffset,
			std::uint64_t size,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Copies buffer regions into texture subresources.
		 */
		bool (*copyBufferToTexture)(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Copies texture subresources into buffer regions.
		 */
		bool (*copyTextureToBuffer)(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Copies texture regions without scaling.
		 */
		bool (*copyTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept = nullptr;

		/**
		 * \brief Fills size bytes with a repeated 32 bit value, starting at a byte offset.
		 */
		bool (*clearBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept = nullptr;

		/**
		 * \brief Clears selected texture subresources to the supplied color.
		 */
		bool (*clearTexture)(
			void * impl,
			TextureHandle texture,
			const ClearColor & color,
			std::span<const TextureSubresourceRange> ranges,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Resolves multisampled source regions into the destination texture.
		 */
		bool (*resolveTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies and scales texture regions with the selected filter.
		 */
		bool (*blit)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept = nullptr;

		/**
		 * \brief Generates the lower mip levels from mip level zero.
		 */
		bool (*generateMips)(void * impl, TextureHandle texture, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins a nested debug label for subsequent commands.
		 * \param impl Command list.
		 * \param name Label text.
		 * \param color Packed RGBA color, with red in the most significant byte.
		 * \param[out] error Optional error details.
		 */
		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the current debug label scope.
		 */
		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	struct AliasingCommandApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(AliasingCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Orders access when resources reuse the same memory.
		 */
		bool (*aliasBarriers)(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional command callbacks for ray tracing and acceleration structures.
	 */
	struct RayTracingCommandApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(RayTracingCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Selects the ray tracing pipeline for subsequent ray dispatches.
		 */
		bool (*setRayTracingPipeline)(void * impl, RayTracingPipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Records the supplied acceleration structure builds.
		 */
		bool (*buildAccelerationStructures)(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies the source acceleration structure into the destination.
		 */
		bool (*copyAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies the source acceleration structure into the destination in compact form.
		 */
		bool (*compactAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept = nullptr;

		/**
		 * \brief Dispatches rays over the given dimensions using the supplied shader binding table.
		 */
		bool (*traceRays)(
			void * impl,
			const ShaderBindingTableDesc & sbt,
			std::uint32_t width,
			std::uint32_t height,
			std::uint32_t depth,
			Error * error
		) noexcept = nullptr;
	};

	/**
	 * \brief Optional command callbacks for GPU queries and timestamps.
	 */
	struct QueryCommandApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(QueryCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Resets the selected query slots for reuse.
		 */
		bool (*resetQueryPool)(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept = nullptr;

		/**
		 * \brief Records a GPU timestamp in the selected query slot at a single pipeline stage.
		 */
		bool (*writeTimestamp)(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins collecting results for the selected query slot.
		 */
		bool (*beginQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends collection for the selected query slot.
		 */
		bool (*endQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies query results into the destination buffer at the given byte offset.
		 */
		bool (*resolveQueryData)(
			void * impl,
			QueryPoolHandle pool,
			std::uint32_t firstQuery,
			std::uint32_t queryCount,
			BufferHandle dst,
			std::uint64_t dstOffset,
			Error * error
		) noexcept = nullptr;
	};

	/**
	 * \brief Optional command callbacks that read draw and dispatch arguments from buffers.
	 */
	struct IndirectApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(IndirectApi),
			.version  = 1,
		};

		/**
		 * \brief Records drawCount draws from argument records addressed by a byte offset and stride.
		 */
		bool (*drawIndirect)(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Records drawCount indexed draws from argument records addressed by a byte offset and stride.
		 */
		bool (*drawIndexedIndirect)(
			void * impl,
			BufferHandle args,
			std::uint64_t offset,
			std::uint32_t drawCount,
			std::uint32_t stride,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Dispatches compute work using arguments at the given byte offset.
		 */
		bool (*dispatchIndirect)(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional indirect draw callbacks that read the draw count from a buffer.
	 */
	struct IndirectCountApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(IndirectCountApi),
			.version  = 1,
		};

		/**
		 * \brief Reads the draw count from count and records at most maxDrawCount indirect draws.
		 * \param impl Command list.
		 * \param args Draw arguments.
		 * \param argsOffset Byte offset of the first argument record.
		 * \param count Draw count buffer.
		 * \param countOffset Byte offset of the draw count.
		 * \param maxDrawCount Draw limit.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional error details.
		 */
		bool (*drawIndirectCount)(
			void * impl,
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride,
			Error * error
		) noexcept = nullptr;

		/**
		 * \brief Reads the draw count from count and records at most maxDrawCount indexed indirect draws.
		 * \param impl Command list.
		 * \param args Indexed draw arguments.
		 * \param argsOffset Byte offset of the first argument record.
		 * \param count Draw count buffer.
		 * \param countOffset Byte offset of the draw count.
		 * \param maxDrawCount Draw limit.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional error details.
		 */
		bool (*drawIndexedIndirectCount)(
			void * impl,
			BufferHandle args,
			std::uint64_t argsOffset,
			BufferHandle count,
			std::uint64_t countOffset,
			std::uint32_t maxDrawCount,
			std::uint32_t stride,
			Error * error
		) noexcept = nullptr;
	};

	/**
	 * \brief Optional callbacks that bracket native command list access.
	 */
	struct NativeEscapeApi final
	{
		InterfaceHeader header{
			.byteSize = sizeof(NativeEscapeApi),
			.version  = 1,
		};

		/**
		 * \brief Declares resources touched by native commands and their final states before native access.
		 */
		bool (*beginNativeMutation)(void * impl, GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends native access with the declared final resource states.
		 */
		bool (*endNativeMutation)(void * impl, const NativeMutationDesc & desc, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

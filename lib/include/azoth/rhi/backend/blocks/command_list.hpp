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

namespace azo::rhi
{

	/**
	 * \brief Command recording callbacks for rendering, compute, and resource transfers.
	 */
	struct RenderCommandApi final
	{
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(RenderCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Starts a new recording on the command list.
		 * \param impl Backend command list instance.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*begin)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Finishes recording so the command list can be submitted.
		 * \param impl Backend command list instance.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*end)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Records resource state transitions and memory dependencies.
		 * \param impl Backend command list instance.
		 * \param barriers Resource transitions and memory dependencies.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*barriers)(void * impl, const BarrierBatch & barriers, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins a rendering scope with the supplied attachments.
		 * \param impl Backend command list instance.
		 * \param desc Rendering attachments and render area.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*beginRendering)(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the current rendering scope.
		 * \param impl Backend command list instance.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*endRendering)(void * impl, Error * error) noexcept = nullptr;

		/**
		 * \brief Selects the graphics pipeline for subsequent draws.
		 * \param impl Backend command list instance.
		 * \param pipeline Graphics pipeline to bind.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setGraphicsPipeline)(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Selects the compute pipeline for subsequent dispatches.
		 * \param impl Backend command list instance.
		 * \param pipeline Compute pipeline to bind.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setComputePipeline)(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds a descriptor set with byte offsets for its dynamic buffer bindings.
		 * \param impl Backend command list instance.
		 * \param layout Pipeline layout containing the set.
		 * \param setIndex Set index in the pipeline layout.
		 * \param set Descriptor set to bind.
		 * \param dynamicOffsets Byte offsets for dynamic buffer bindings.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param layout Pipeline layout defining the constants.
		 * \param stages Shader stages receiving the constants.
		 * \param offset Byte offset of the constants.
		 * \param size Number of bytes to write.
		 * \param data Source bytes.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param viewport Viewport bounds and depth range.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setViewport)(void * impl, const Viewport & viewport, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the scissor rectangle in pixels.
		 * \param impl Backend command list instance.
		 * \param scissor Scissor rectangle in pixels.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setScissor)(void * impl, const Rect2D & scissor, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the RGBA constants used by constant blend factors.
		 * \param impl Backend command list instance.
		 * \param r Red blend constant.
		 * \param g Green blend constant.
		 * \param b Blue blend constant.
		 * \param a Alpha blend constant.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setBlendConstants)(void * impl, float r, float g, float b, float a, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets the reference value for stencil tests on both faces.
		 * \param impl Backend command list instance.
		 * \param reference Stencil reference value for both faces.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setStencilReference)(void * impl, std::uint32_t reference, Error * error) noexcept = nullptr;

		/**
		 * \brief Sets constant and slope depth bias with a clamp value.
		 * \param impl Backend command list instance.
		 * \param constantFactor Constant depth bias.
		 * \param clamp Depth bias clamp.
		 * \param slopeFactor Slope scaled depth bias.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setDepthBias)(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds a vertex buffer at a byte offset to the given slot.
		 * \param impl Backend command list instance.
		 * \param slot Vertex buffer binding slot.
		 * \param buffer Vertex buffer to bind.
		 * \param offset Byte offset into the buffer.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setVertexBuffer)(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept = nullptr;

		/**
		 * \brief Binds an index buffer at a byte offset with 32 bit indices when index32 is true and 16 bit otherwise.
		 * \param impl Backend command list instance.
		 * \param buffer Index buffer to bind.
		 * \param offset Byte offset into the buffer.
		 * \param index32 True for 32 bit indices, false for 16 bit.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setIndexBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept = nullptr;

		/**
		 * \brief Records an instanced draw starting at firstVertex and firstInstance.
		 * \param impl Backend command list instance.
		 * \param vertexCount Number of vertices per instance.
		 * \param instanceCount Number of instances to draw.
		 * \param firstVertex First vertex to draw.
		 * \param firstInstance First instance index.
		 * \param[out] error Optional output for failure details.
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
		 * \brief Records an indexed instanced draw with vertexOffset added to each index.
		 * \param impl Backend command list instance.
		 * \param indexCount Number of indices per instance.
		 * \param instanceCount Number of instances to draw.
		 * \param firstIndex First index in the bound index buffer.
		 * \param vertexOffset Offset added to each vertex index.
		 * \param firstInstance First instance index.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param groupCountX Workgroup count along the X axis.
		 * \param groupCountY Workgroup count along the Y axis.
		 * \param groupCountZ Workgroup count along the Z axis.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*dispatch)(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies size bytes from the source offset to the destination offset.
		 * \param impl Backend command list instance.
		 * \param dst Destination buffer.
		 * \param dstOffset Byte offset in the destination buffer.
		 * \param src Source buffer.
		 * \param srcOffset Byte offset in the source buffer.
		 * \param size Number of bytes to copy.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param dst Destination texture.
		 * \param src Source buffer.
		 * \param regions Source buffer regions and destination texture subresources.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*copyBufferToTexture)(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Copies texture subresources into buffer regions.
		 * \param impl Backend command list instance.
		 * \param dst Destination buffer.
		 * \param src Source texture.
		 * \param regions Source texture subresources and destination buffer regions.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*copyTextureToBuffer)(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Copies texture regions without scaling.
		 * \param impl Backend command list instance.
		 * \param dst Destination texture.
		 * \param src Source texture.
		 * \param regions Source and destination texture regions.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*copyTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept = nullptr;

		/**
		 * \brief Fills a byte range with a repeated 32 bit value.
		 * \param impl Backend command list instance.
		 * \param buffer Buffer to fill.
		 * \param offset Byte offset of the range.
		 * \param size Number of bytes to fill.
		 * \param value 32 bit value to repeat.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*clearBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept = nullptr;

		/**
		 * \brief Clears the selected texture subresources to the supplied color.
		 * \param impl Backend command list instance.
		 * \param texture Texture to clear.
		 * \param color Clear color.
		 * \param ranges Texture subresources to clear.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param dst Destination texture.
		 * \param src Multisampled source texture.
		 * \param regions Source and destination resolve regions.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*resolveTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies and scales texture regions with the selected filter.
		 * \param impl Backend command list instance.
		 * \param dst Destination texture.
		 * \param src Source texture.
		 * \param regions Source and destination texture regions.
		 * \param filter Filter used when scaling.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*blit)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept = nullptr;

		/**
		 * \brief Generates the lower mip levels from mip level zero.
		 * \param impl Backend command list instance.
		 * \param texture Texture whose mip levels are generated.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*generateMips)(void * impl, TextureHandle texture, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins a nested debug label for subsequent commands.
		 * \param impl Backend command list instance.
		 * \param name Debug label text.
		 * \param color Debug label color.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends the most recent debug label.
		 * \param impl Backend command list instance.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*endDebugLabel)(void * impl, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Command callbacks for resources that share memory.
	 */
	struct AliasingCommandApi final
	{
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(AliasingCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Orders access when resources reuse the same memory.
		 * \param impl Backend command list instance.
		 * \param barriers Resource handoffs for shared memory.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*aliasBarriers)(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional command callbacks for ray tracing and acceleration structures.
	 */
	struct RayTracingCommandApi final
	{
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(RayTracingCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Selects the ray tracing pipeline for subsequent ray dispatches.
		 * \param impl Backend command list instance.
		 * \param pipeline Ray tracing pipeline to bind.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*setRayTracingPipeline)(void * impl, RayTracingPipelineHandle pipeline, Error * error) noexcept = nullptr;

		/**
		 * \brief Records the supplied acceleration structure builds.
		 * \param impl Backend command list instance.
		 * \param builds Acceleration structure build descriptions.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*buildAccelerationStructures)(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies the source acceleration structure into the destination.
		 * \param impl Backend command list instance.
		 * \param dst Destination acceleration structure.
		 * \param src Source acceleration structure.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*copyAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies the source acceleration structure into the destination in compact form.
		 * \param impl Backend command list instance.
		 * \param dst Destination for the compacted acceleration structure.
		 * \param src Acceleration structure to compact.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*compactAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept = nullptr;

		/**
		 * \brief Dispatches rays over the given dimensions using the supplied shader binding table.
		 * \param impl Backend command list instance.
		 * \param sbt Shader binding table regions.
		 * \param width Ray dispatch width.
		 * \param height Ray dispatch height.
		 * \param depth Ray dispatch depth.
		 * \param[out] error Optional output for failure details.
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
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(QueryCommandApi),
			.version  = 1,
		};

		/**
		 * \brief Resets queryCount slots starting at firstQuery for reuse.
		 * \param impl Backend command list instance.
		 * \param pool Query pool to reset.
		 * \param firstQuery First query index.
		 * \param queryCount Number of queries to reset.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*resetQueryPool)(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept = nullptr;

		/**
		 * \brief Records a GPU timestamp in the selected query slot at a single pipeline stage.
		 * \param impl Backend command list instance.
		 * \param pool Timestamp query pool.
		 * \param query Destination query index.
		 * \param stage Single pipeline stage for the timestamp.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*writeTimestamp)(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept = nullptr;

		/**
		 * \brief Begins collecting results for the selected query slot.
		 * \param impl Backend command list instance.
		 * \param pool Query pool containing the query.
		 * \param query Query index to begin.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*beginQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends collection for the selected query slot.
		 * \param impl Backend command list instance.
		 * \param pool Query pool containing the query.
		 * \param query Query index to end.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*endQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept = nullptr;

		/**
		 * \brief Copies query results into the destination buffer at the given byte offset.
		 * \param impl Backend command list instance.
		 * \param pool Source query pool.
		 * \param firstQuery First query index to resolve.
		 * \param queryCount Number of queries to resolve.
		 * \param dst Destination buffer for query results.
		 * \param dstOffset Byte offset in the destination buffer.
		 * \param[out] error Optional output for failure details.
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
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(IndirectApi),
			.version  = 1,
		};

		/**
		 * \brief Records drawCount draws from argument records addressed by a byte offset and stride.
		 * \param impl Backend command list instance.
		 * \param args Buffer containing draw arguments.
		 * \param offset Byte offset of the first argument record.
		 * \param drawCount Number of draws to record.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*drawIndirect)(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept =
			nullptr;

		/**
		 * \brief Records drawCount indexed draws from argument records addressed by a byte offset and stride.
		 * \param impl Backend command list instance.
		 * \param args Buffer containing indexed draw arguments.
		 * \param offset Byte offset of the first argument record.
		 * \param drawCount Number of indexed draws to record.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param args Buffer containing dispatch arguments.
		 * \param offset Byte offset of the dispatch arguments.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*dispatchIndirect)(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional indirect draw callbacks that read the draw count from a buffer.
	 */
	struct IndirectCountApi final
	{
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(IndirectCountApi),
			.version  = 1,
		};

		/**
		 * \brief Reads the draw count from count and records at most maxDrawCount indirect draws.
		 * \param impl Backend command list instance.
		 * \param args Buffer containing draw arguments.
		 * \param argsOffset Byte offset of the first argument record.
		 * \param count Buffer containing the draw count.
		 * \param countOffset Byte offset of the draw count.
		 * \param maxDrawCount Maximum number of draws to record.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional output for failure details.
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
		 * \param impl Backend command list instance.
		 * \param args Buffer containing indexed draw arguments.
		 * \param argsOffset Byte offset of the first argument record.
		 * \param count Buffer containing the draw count.
		 * \param countOffset Byte offset of the draw count.
		 * \param maxDrawCount Maximum number of indexed draws to record.
		 * \param stride Byte distance between argument records.
		 * \param[out] error Optional output for failure details.
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
		/**
		 * \brief Interface size and version for block discovery.
		 */
		InterfaceHeader header{
			.byteSize = sizeof(NativeEscapeApi),
			.version  = 1,
		};

		/**
		 * \brief Begins native access for the selected graphics API and declared resources.
		 * \param impl Backend command list instance.
		 * \param api Graphics API used by the native commands.
		 * \param desc Resources touched and their final states.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*beginNativeMutation)(void * impl, GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Ends native access with the declared final resource states.
		 * \param impl Backend command list instance.
		 * \param desc Resources touched and their final states.
		 * \param[out] error Optional output for failure details.
		 */
		bool (*endNativeMutation)(void * impl, const NativeMutationDesc & desc, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

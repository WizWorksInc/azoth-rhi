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
 * \brief Command-recording ABI blocks published by backend command-list objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Required command-list recording table.
	 *
	 * This block is the minimum command surface needed for graphics, compute, copies, clears, resolves, dynamic rendering, and debug labels. Specialized
	 * command families such as ray tracing, queries, aliasing, and native mutation live in optional blocks.
	 */
	struct RenderCommandApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(RenderCommandApi), .version = 1 };

		/// Recording lifetime.

		bool (*begin)(void * impl, Error * error) noexcept = nullptr;
		bool (*end)(void * impl, Error * error) noexcept   = nullptr;

		/// Synchronization.

		bool (*barriers)(void * impl, const BarrierBatch & barriers, Error * error) noexcept = nullptr;

		/// Dynamic rendering.

		bool (*beginRendering)(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept = nullptr;
		bool (*endRendering)(void * impl, Error * error) noexcept									 = nullptr;

		/// Pipeline state.

		bool (*setGraphicsPipeline)(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept = nullptr;
		bool (*setComputePipeline)(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept	  = nullptr;

		/// Shader inputs.

		bool (*bindDescriptorSet)(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
			std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept = nullptr;
		bool (*pushConstants)(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data,
			Error * error) noexcept															 = nullptr;

		/// Dynamic fixed-function state.

		bool (*setViewport)(void * impl, const Viewport & viewport, Error * error) noexcept								= nullptr;
		bool (*setScissor)(void * impl, const Rect2D & scissor, Error * error) noexcept									= nullptr;
		bool (*setBlendConstants)(void * impl, float r, float g, float b, float a, Error * error) noexcept				= nullptr;
		bool (*setStencilReference)(void * impl, std::uint32_t reference, Error * error) noexcept						= nullptr;
		bool (*setDepthBias)(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept = nullptr;

		/// Vertex and index input.

		bool (*setVertexBuffer)(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept = nullptr;
		bool (*setIndexBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept		= nullptr;

		/// Direct draw and dispatch.

		bool (*draw)(void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance,
			Error * error) noexcept								 = nullptr;
		bool (*drawIndexed)(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, Error * error) noexcept = nullptr;

		bool (*dispatch)(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept = nullptr;

		/// Copy, clear, resolve, blit, and mip operations.

		bool (*copyBuffer)(void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size,
			Error * error) noexcept = nullptr;
		bool (*copyBufferToTexture)(
			void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept = nullptr;
		bool (*copyTextureToBuffer)(
			void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept					 = nullptr;
		bool (*copyTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept		 = nullptr;
		bool (*clearBuffer)(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept = nullptr;
		bool (*clearTexture)(
			void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept	 = nullptr;
		bool (*resolveTexture)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept	 = nullptr;
		bool (*blit)(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept = nullptr;
		bool (*generateMips)(void * impl, TextureHandle texture, Error * error) noexcept															 = nullptr;

		/// Debug labels.

		bool (*beginDebugLabel)(void * impl, CString name, std::uint32_t color, Error * error) noexcept = nullptr;
		bool (*endDebugLabel)(void * impl, Error * error) noexcept										= nullptr;
	};

	/**
	 * \brief Optional command table for backends that expose explicit resource aliasing barriers.
	 *
	 * Missing this block means aliasing barriers are not supported by the command-list facade, even if normal resource barriers are available.
	 */
	struct AliasingCommandApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(AliasingCommandApi), .version = 1 };

		bool (*aliasBarriers)(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional command table for backends that expose ray tracing pipeline, acceleration-structure, and ray-dispatch commands.
	 *
	 * \note Designed and unimplemented as of 2026-07-31, the recording half of RayTracingApi. Only the Null backend publishes it, so supportsRayTracing,
	 * which needs both halves, is false on every backend that talks to real hardware.
	 */
	struct RayTracingCommandApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(RayTracingCommandApi), .version = 1 };

		/// Ray tracing pipeline state.

		bool (*setRayTracingPipeline)(void * impl, RayTracingPipelineHandle pipeline, Error * error) noexcept = nullptr;

		/// Acceleration structures and ray dispatch.

		bool (*buildAccelerationStructures)(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept			= nullptr;
		bool (*copyAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept	= nullptr;
		bool (*compactAccelerationStructure)(void * impl, AccelerationStructureHandle dst, AccelerationStructureHandle src, Error * error) noexcept = nullptr;
		bool (*traceRays)(
			void * impl, const ShaderBindingTableDesc & sbt, std::uint32_t width, std::uint32_t height, std::uint32_t depth, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional command table for query-pool reset, query scope, timestamp, and query-result resolve commands.
	 */
	struct QueryCommandApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(QueryCommandApi), .version = 1 };

		bool (*resetQueryPool)(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept = nullptr;
		bool (*writeTimestamp)(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error * error) noexcept	  = nullptr;
		bool (*beginQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept									  = nullptr;
		bool (*endQuery)(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept									  = nullptr;
		bool (*resolveQueryData)(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
			std::uint64_t dstOffset, Error * error) noexcept																				  = nullptr;
	};

	/**
	 * \brief Optional command table for indirect draw and dispatch commands that read arguments from buffers.
	 */
	struct IndirectApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(IndirectApi), .version = 1 };

		bool (*drawIndirect)(
			void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept = nullptr;
		bool (*drawIndexedIndirect)(
			void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept = nullptr;
		bool (*dispatchIndirect)(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept							 = nullptr;
	};

	/**
	 * \brief Optional command table for indirect draws whose draw count is read from a separate count buffer.
	 */
	struct IndirectCountApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(IndirectCountApi), .version = 1 };

		bool (*drawIndirectCount)(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
			std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept = nullptr;
		bool (*drawIndexedIndirectCount)(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
			std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept = nullptr;
	};

	/**
	 * \brief Optional command table for backend-native command-list mutation.
	 *
	 * \attention beginNativeMutation and endNativeMutation bracket direct API access so validation can check the requested backend and reconcile declared
	 * resource states.
	 */
	struct NativeEscapeApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(NativeEscapeApi), .version = 1 };

		bool (*beginNativeMutation)(void * impl, GraphicsApiId api, const NativeMutationDesc & desc, Error * error) noexcept = nullptr;
		bool (*endNativeMutation)(void * impl, const NativeMutationDesc & desc, Error * error) noexcept						 = nullptr;
	};

} // namespace azo::rhi

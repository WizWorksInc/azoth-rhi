// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/core/build_config.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"
#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTL4Counters.hpp>
#include <Metal/MTL4RenderCommandEncoder.hpp>
#include <Metal/MTL4RenderPass.hpp>
#include <Metal/MTLArgument.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPass.hpp>
#include <Metal/MTLTexture.hpp>
#include <cstdint>

namespace azo::rhi::metal4
{
	bool metal4_cmd_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.beginRendering");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = recording_list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool		 = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		const NS::SharedPtr<MTL4::RenderPassDescriptor> pass = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());

		std::uint32_t colorIndex = 0;
		for (const RenderingAttachment & color : desc.colors)
		{
			MTL::Texture * view = resolve_texture_view(device, color.view);
			if (view == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering color attachment names an unknown texture view");
			}

			MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(colorIndex);
			attachment->setTexture(view);
			attachment->setLoadAction(metal_load_action(color.load));
			attachment->setStoreAction(metal_store_action(color.store));
			attachment->setClearColor(MTL::ClearColor::Make(color.clearColor.r, color.clearColor.g, color.clearColor.b, color.clearColor.a));
			++colorIndex;
		}

		if (desc.depthStencil != nullptr)
		{
			MTL::Texture * view = resolve_texture_view(device, desc.depthStencil->view);
			if (view == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment names an unknown texture view");
			}

			MTL::RenderPassDepthAttachmentDescriptor * depth = pass->depthAttachment();
			depth->setTexture(view);
			depth->setLoadAction(metal_load_action(desc.depthStencil->load));
			depth->setStoreAction(metal_store_action(desc.depthStencil->store));
			depth->setClearDepth(desc.depthStencil->clearDepthStencil.depth);
		}

		Metal4QueryPool * timestamps = nullptr;
		if (desc.timestamps != nullptr)
		{
			timestamps = resolve_query_pool(device, desc.timestamps->pool);
			if (timestamps == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "rendering timestamp writes name a query pool this device never created");
			}
			if ((desc.timestamps->beginQuery != kInvalidIndex && desc.timestamps->beginQuery >= timestamps->queryCount) ||
				(desc.timestamps->endQuery != kInvalidIndex && desc.timestamps->endQuery >= timestamps->queryCount))
			{
				return fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a query past the end of the pool");
			}
		}

		end_active_encoders(list);

		MTL4::RenderCommandEncoder * encoder = list->commandBuffer->renderCommandEncoder(pass.get());
		if (encoder == nullptr)
		{
			return fail(error, ErrorCode::eNativeApiError, "Metal 4 render command encoder creation failed");
		}

		encoder->setArgumentTable(list->argumentTable.get(), MTL::RenderStageVertex | MTL::RenderStageFragment);
		list->renderEncoder = NS::RetainPtr(encoder);
		++list->encoderEpoch;
		list->scopeDrew = false;

		flush_pending_barrier(list, encoder);

		if (timestamps != nullptr && desc.timestamps->beginQuery != kInvalidIndex && timestamps->heap.get() != nullptr)
		{
			encoder->writeTimestamp(MTL4::TimestampGranularityPrecise, MTL::RenderStageVertex, timestamps->heap.get(), desc.timestamps->beginQuery);
		}

		if (timestamps != nullptr && desc.timestamps->endQuery != kInvalidIndex && timestamps->heap.get() != nullptr)
		{
			list->pendingEndHeap  = timestamps->heap;
			list->pendingEndQuery = desc.timestamps->endQuery;
		}

		return succeed(error);
	}

	bool metal4_cmd_end_rendering(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = list_of(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return succeed(error);
		}

		if (list->pendingEndHeap.get() != nullptr)
		{
			const MTL::RenderStages endStage = list->scopeDrew ? MTL::RenderStageFragment : MTL::RenderStageVertex;
			list->renderEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, endStage, list->pendingEndHeap.get(), list->pendingEndQuery);
			list->pendingEndHeap.reset();
		}

		pop_encoder_debug_groups(list, list->renderEncoder.get());
		list->renderEncoder->endEncoding();
		list->renderEncoder.reset();
		return succeed(error);
	}

	bool metal4_cmd_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setGraphicsPipeline outside a rendering scope");
		}

		const auto * tracked = device->graphicsPipelines.resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr || tracked->state.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline names a pipeline this device never created");
		}

		MTL4::RenderCommandEncoder * encoder = list->renderEncoder.get();

		encoder->setRenderPipelineState(tracked->state.get());
		encoder->setCullMode(tracked->cull);
		encoder->setFrontFacingWinding(tracked->winding);
		encoder->setTriangleFillMode(tracked->fill);

		if (tracked->depthStencil.get() != nullptr)
		{
			encoder->setDepthStencilState(tracked->depthStencil.get());
		}
		if (tracked->depthBiasEnable)
		{
			encoder->setDepthBias(tracked->depthBiasConstant, tracked->depthBiasSlope, tracked->depthBiasClamp);
		}

		list->boundPrimitive = tracked->primitive;
		return succeed(error);
	}

	bool metal4_cmd_set_viewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setViewport outside a rendering scope");
		}

		double originY = viewport.y;
		double height  = viewport.height;
		if (get_clip_space() == ClipSpaceConvention::eYDown)
		{
			originY = static_cast<double>(viewport.y) + static_cast<double>(viewport.height);
			height	= -static_cast<double>(viewport.height);
		}

		list->renderEncoder->setViewport(MTL::Viewport{ .originX=viewport.x, .originY=originY, .width=viewport.width, .height=height, .znear=viewport.minDepth, .zfar=viewport.maxDepth });
		return succeed(error);
	}

	bool metal4_cmd_set_scissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setScissor outside a rendering scope");
		}

		list->renderEncoder->setScissorRect(MTL::ScissorRect{ .x=static_cast<NS::UInteger>(scissor.x),
			.y=static_cast<NS::UInteger>(scissor.y),
			.width=static_cast<NS::UInteger>(scissor.width),
			.height=static_cast<NS::UInteger>(scissor.height), });
		return succeed(error);
	}

	bool metal4_cmd_set_blend_constants(void * impl, const float r, const float g, const float b, const float a, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setBlendConstants outside a rendering scope");
		}

		list->renderEncoder->setBlendColor(r, g, b, a);
		return succeed(error);
	}

	bool metal4_cmd_set_stencil_reference(void * impl, const std::uint32_t reference, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setStencilReference outside a rendering scope");
		}

		list->renderEncoder->setStencilReferenceValue(reference);
		return succeed(error);
	}

	bool metal4_cmd_set_depth_bias(void * impl, const float constantFactor, const float clamp, const float slopeFactor, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setDepthBias outside a rendering scope");
		}

		list->renderEncoder->setDepthBias(constantFactor, slopeFactor, clamp);
		return succeed(error);
	}

	bool metal4_cmd_set_vertex_buffer(void * impl, const std::uint32_t slot, BufferHandle buffer, const std::uint64_t offset, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "setVertexBuffer outside a rendering scope");
		}

		MTL::Buffer * resolved = resolve_buffer(device, buffer);
		if (resolved == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer names a buffer this device never created");
		}

		list->argumentTable->setAddress(resolved->gpuAddress() + offset, kMetalVertexBufferBase + slot);
		return succeed(error);
	}

	bool metal4_cmd_set_index_buffer(void * impl, BufferHandle buffer, const std::uint64_t offset, const bool index32, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * resolved = resolve_buffer(device, buffer);
		if (resolved == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer names a buffer this device never created");
		}

		if (offset > resolved->length())
		{
			return fail(error, ErrorCode::eInvalidArgument, "setIndexBuffer offset is past the end of the buffer");
		}

		list->boundIndexBuffer = resolved->gpuAddress() + offset;
		list->boundIndexLength = resolved->length() - offset;
		list->boundIndexType   = index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
		return succeed(error);
	}

	bool metal4_cmd_draw(void * impl, const std::uint32_t vertexCount, const std::uint32_t instanceCount, const std::uint32_t firstVertex,
		const std::uint32_t firstInstance, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "draw outside a rendering scope");
		}

		list->renderEncoder->drawPrimitives(list->boundPrimitive, firstVertex, vertexCount, instanceCount, firstInstance);
		list->scopeDrew = true;
		return succeed(error);
	}

	bool metal4_cmd_draw_indexed(void * impl, const std::uint32_t indexCount, const std::uint32_t instanceCount, const std::uint32_t firstIndex,
		const std::int32_t vertexOffset, const std::uint32_t firstInstance, Error * error) noexcept
	{
		CmdList * list = list_of(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "drawIndexed outside a rendering scope");
		}
		if (list->boundIndexBuffer == 0)
		{
			return fail(error, ErrorCode::eInvalidState, "drawIndexed with no index buffer bound");
		}

		const std::uint64_t indexSize = list->boundIndexType == MTL::IndexTypeUInt32 ? 4 : 2;
		const std::uint64_t byteStart = static_cast<std::uint64_t>(firstIndex) * indexSize;

		if (byteStart + (static_cast<std::uint64_t>(indexCount) * indexSize) > list->boundIndexLength)
		{
			return fail(error, ErrorCode::eInvalidArgument, "drawIndexed reads past the end of the bound index buffer");
		}

		list->renderEncoder->drawIndexedPrimitives(list->boundPrimitive,
			indexCount,
			list->boundIndexType,
			list->boundIndexBuffer + byteStart,
			list->boundIndexLength - byteStart,
			instanceCount,
			vertexOffset,
			firstInstance);
		list->scopeDrew = true;
		return succeed(error);
	}

	bool metal4_cmd_draw_indirect(
		void * impl, BufferHandle args, const std::uint64_t offset, const std::uint32_t drawCount, const std::uint32_t stride, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "drawIndirect outside a rendering scope");
		}

		MTL::Buffer * buffer = resolve_buffer(device, args);
		if (buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndirect names a buffer this device never created");
		}

		const std::uint32_t step = stride != 0 ? stride : 16;
		for (std::uint32_t draw = 0; draw < drawCount; ++draw)
		{
			list->renderEncoder->drawPrimitives(list->boundPrimitive, buffer->gpuAddress() + offset + (static_cast<std::uint64_t>(draw) * step));
			list->scopeDrew = true;
		}

		return succeed(error);
	}

	bool metal4_cmd_draw_indexed_indirect(
		void * impl, BufferHandle args, const std::uint64_t offset, const std::uint32_t drawCount, const std::uint32_t stride, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect outside a rendering scope");
		}
		if (list->boundIndexBuffer == 0)
		{
			return fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect with no index buffer bound");
		}

		MTL::Buffer * buffer = resolve_buffer(device, args);
		if (buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirect names a buffer this device never created");
		}

		const std::uint32_t step = stride != 0 ? stride : 20;
		for (std::uint32_t draw = 0; draw < drawCount; ++draw)
		{
			list->renderEncoder->drawIndexedPrimitives(list->boundPrimitive,
				list->boundIndexType,
				list->boundIndexBuffer,
				list->boundIndexLength,
				buffer->gpuAddress() + offset + (static_cast<std::uint64_t>(draw) * step));
			list->scopeDrew = true;
		}

		return succeed(error);
	}

}

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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	/*
	 * Opening a rendering scope.
	 *
	 * The attachment descriptors did not fork, so everything below the descriptor type is the same code the other generation runs. What did change is the
	 * timestamps: Metal 3 has to choose between naming sample points on the pass descriptor and taking them by command, depending on what the adapter samples
	 * at, and this generation takes a timestamp on the encoder unconditionally. Both branches collapse into one write.
	 */
	bool CmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.beginRendering");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool		= NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		const NS::SharedPtr<MTL4::RenderPassDescriptor> pass = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());

		std::uint32_t colorIndex = 0;
		for (const RenderingAttachment & color : desc.colors)
		{
			MTL::Texture * view = ResolveTextureView(device, color.view);
			if (view == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering color attachment names an unknown texture view");
			}

			MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(colorIndex);
			attachment->setTexture(view);
			attachment->setLoadAction(MetalLoadAction(color.load));
			attachment->setStoreAction(MetalStoreAction(color.store));
			attachment->setClearColor(MTL::ClearColor::Make(color.clearColor.r, color.clearColor.g, color.clearColor.b, color.clearColor.a));
			++colorIndex;
		}

		if (desc.depthStencil != nullptr)
		{
			MTL::Texture * view = ResolveTextureView(device, desc.depthStencil->view);
			if (view == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering depth attachment names an unknown texture view");
			}

			MTL::RenderPassDepthAttachmentDescriptor * depth = pass->depthAttachment();
			depth->setTexture(view);
			depth->setLoadAction(MetalLoadAction(desc.depthStencil->load));
			depth->setStoreAction(MetalStoreAction(desc.depthStencil->store));
			depth->setClearDepth(desc.depthStencil->clearDepthStencil.depth);
		}

		Metal4QueryPool * timestamps = nullptr;
		if (desc.timestamps != nullptr)
		{
			timestamps = ResolveQueryPool(device, desc.timestamps->pool);
			if (timestamps == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering timestamp writes name a query pool this device never created");
			}
			if ((desc.timestamps->beginQuery != kInvalidIndex && desc.timestamps->beginQuery >= timestamps->queryCount) ||
				(desc.timestamps->endQuery != kInvalidIndex && desc.timestamps->endQuery >= timestamps->queryCount))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a query past the end of the pool");
			}
		}

		EndActiveEncoders(list);

		MTL4::RenderCommandEncoder * encoder = list->commandBuffer->renderCommandEncoder(pass.get());
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 render command encoder creation failed");
		}

		// Both stages see the table. A vertex-only binding would leave a fragment shader reading nothing.
		encoder->setArgumentTable(list->argumentTable.get(), MTL::RenderStageVertex | MTL::RenderStageFragment);
		list->renderEncoder = NS::RetainPtr(encoder);
		list->scopeDrew		= false;

		// A barrier recorded before this scope opened was meant to order this scope, so it goes on first.
		FlushPendingBarrier(list, encoder);

		if (timestamps != nullptr && desc.timestamps->beginQuery != kInvalidIndex && timestamps->heap.get() != nullptr)
		{
			/*
			 * Precise and not relaxed, since relaxed may sample only at encoder boundaries, which is the resolution the other generation is stuck with and
			 * the reason this one is worth having.
			 *
			 * Sampled after the vertex stage for the begin and after the fragment stage for the end, which are the earliest and latest points in a pass, so the
			 * pair brackets the whole of it.
			 */
			encoder->writeTimestamp(MTL4::TimestampGranularityPrecise, MTL::RenderStageVertex, timestamps->heap.get(), desc.timestamps->beginQuery);
		}

		// Held for EndRendering, which is the last point the encoder is still open.
		if (timestamps != nullptr && desc.timestamps->endQuery != kInvalidIndex && timestamps->heap.get() != nullptr)
		{
			list->pendingEndHeap  = timestamps->heap;
			list->pendingEndQuery = desc.timestamps->endQuery;
		}

		return Succeed(error);
	}

	bool CmdEndRendering(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Succeed(error);
		}

		if (list->pendingEndHeap.get() != nullptr)
		{
			/*
			 * Fragment where the scope drew, because that is the last stage a draw passes through, and vertex where it did not.
			 *
			 * One stage and not a mask: writeTimestamp takes an afterStage, not an afterStages, and Metal refuses a value with two bits set. So a scope that
			 * never drew cannot ask for both and cannot ask for fragment either, since a stage nothing reached takes no sample and leaves the slot reading as
			 * unwritten.
			 */
			const MTL::RenderStages endStage = list->scopeDrew ? MTL::RenderStageFragment : MTL::RenderStageVertex;
			list->renderEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, endStage, list->pendingEndHeap.get(), list->pendingEndQuery);
			list->pendingEndHeap.reset();
		}

		list->renderEncoder->endEncoding();
		list->renderEncoder.reset();
		return Succeed(error);
	}

	bool CmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setGraphicsPipeline outside a rendering scope");
		}

		const auto * tracked = device->graphicsPipelines.Resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr || tracked->state.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline names a pipeline this device never created");
		}

		MTL4::RenderCommandEncoder * encoder = list->renderEncoder.get();

		// The same MTLRenderPipelineState the other generation builds, and the same state Metal keeps on the encoder and not in the pipeline object.
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
		return Succeed(error);
	}

	bool CmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setViewport outside a rendering scope");
		}

		// Metal's own NDC runs Y up so eYUp needs nothing done. Presenting eYDown means flipping: origin to the bottom edge, height negated. Winding is left as
		// authored either way. The same flip the other generation makes, and without it this backend alone renders vertically mirrored.
		double originY = viewport.y;
		double height  = viewport.height;
		if (GetClipSpace() == ClipSpaceConvention::eYDown)
		{
			originY = static_cast<double>(viewport.y) + static_cast<double>(viewport.height);
			height	= -static_cast<double>(viewport.height);
		}

		list->renderEncoder->setViewport(MTL::Viewport{ viewport.x, originY, viewport.width, height, viewport.minDepth, viewport.maxDepth });
		return Succeed(error);
	}

	bool CmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setScissor outside a rendering scope");
		}

		list->renderEncoder->setScissorRect(MTL::ScissorRect{ static_cast<NS::UInteger>(scissor.x),
			static_cast<NS::UInteger>(scissor.y),
			static_cast<NS::UInteger>(scissor.width),
			static_cast<NS::UInteger>(scissor.height) });
		return Succeed(error);
	}

	bool CmdSetBlendConstants(void * impl, const float r, const float g, const float b, const float a, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setBlendConstants outside a rendering scope");
		}

		list->renderEncoder->setBlendColor(r, g, b, a);
		return Succeed(error);
	}

	bool CmdSetStencilReference(void * impl, const std::uint32_t reference, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setStencilReference outside a rendering scope");
		}

		list->renderEncoder->setStencilReferenceValue(reference);
		return Succeed(error);
	}

	bool CmdSetDepthBias(void * impl, const float constantFactor, const float clamp, const float slopeFactor, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setDepthBias outside a rendering scope");
		}

		list->renderEncoder->setDepthBias(constantFactor, slopeFactor, clamp);
		return Succeed(error);
	}

	/*
	 * A vertex buffer, which on this generation is an address in the argument table, not a call on the encoder. The index is the same one the other generation
	 * uses, so what a shader declares does not change with the path.
	 */
	bool CmdSetVertexBuffer(void * impl, const std::uint32_t slot, BufferHandle buffer, const std::uint64_t offset, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->argumentTable.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setVertexBuffer outside a rendering scope");
		}

		MTL::Buffer * resolved = ResolveBuffer(device, buffer);
		if (resolved == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer names a buffer this device never created");
		}

		list->argumentTable->setAddress(resolved->gpuAddress() + offset, kMetalVertexBufferBase + slot);
		return Succeed(error);
	}

	bool CmdSetIndexBuffer(void * impl, BufferHandle buffer, const std::uint64_t offset, const bool index32, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * resolved = ResolveBuffer(device, buffer);
		if (resolved == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer names a buffer this device never created");
		}

		/*
		 * The length is what bounds an indexed draw on this generation, which takes an address and a span where Metal 3 passes the buffer object and lets
		 * Metal bound it. An offset past the end has to be refused here because the subtraction below wraps, and the span it would produce covers most of the
		 * address space.
		 */
		if (offset > resolved->length())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "setIndexBuffer offset is past the end of the buffer");
		}

		// Held, not bound, a draw taking the index buffer as an argument on this generation.
		list->boundIndexBuffer = resolved->gpuAddress() + offset;
		list->boundIndexLength = resolved->length() - offset;
		list->boundIndexType   = index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
		return Succeed(error);
	}

	bool CmdDraw(void * impl, const std::uint32_t vertexCount, const std::uint32_t instanceCount, const std::uint32_t firstVertex,
		const std::uint32_t firstInstance, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "draw outside a rendering scope");
		}

		// vertexStart comes before vertexCount here, unlike drawIndexedPrimitives, which takes its count first.
		list->renderEncoder->drawPrimitives(list->boundPrimitive, firstVertex, vertexCount, instanceCount, firstInstance);
		list->scopeDrew = true;
		return Succeed(error);
	}

	bool CmdDrawIndexed(void * impl, const std::uint32_t indexCount, const std::uint32_t instanceCount, const std::uint32_t firstIndex,
		const std::int32_t vertexOffset, const std::uint32_t firstInstance, Error * error) noexcept
	{
		CmdList * list = ListOf(static_cast<Metal4Object *>(impl));
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexed outside a rendering scope");
		}
		if (list->boundIndexBuffer == 0)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexed with no index buffer bound");
		}

		// The first index is folded into the address, this generation taking no first-index argument.
		const std::uint64_t indexSize = list->boundIndexType == MTL::IndexTypeUInt32 ? 4 : 2;
		const std::uint64_t byteStart = static_cast<std::uint64_t>(firstIndex) * indexSize;

		// The same span rule as setIndexBuffer, applied to where this draw actually reads. Subtracting a first index past the end wraps, so the draw would be
		// handed a span reaching well beyond the allocation instead of a refusal.
		if (byteStart + (static_cast<std::uint64_t>(indexCount) * indexSize) > list->boundIndexLength)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "drawIndexed reads past the end of the bound index buffer");
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
		return Succeed(error);
	}

	/*
	 * Indirect draws, one command per entry.
	 *
	 * Metal takes one draw per indirect call on both generations, so a batch is issued as the documented one command per entry and not as a multi-draw,
	 * which is why supportsMultiDrawIndirect stays false.
	 */
	bool CmdDrawIndirect(
		void * impl, BufferHandle args, const std::uint64_t offset, const std::uint32_t drawCount, const std::uint32_t stride, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndirect outside a rendering scope");
		}

		MTL::Buffer * buffer = ResolveBuffer(device, args);
		if (buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndirect names a buffer this device never created");
		}

		const std::uint32_t step = stride != 0 ? stride : 16;
		for (std::uint32_t draw = 0; draw < drawCount; ++draw)
		{
			list->renderEncoder->drawPrimitives(list->boundPrimitive, buffer->gpuAddress() + offset + (static_cast<std::uint64_t>(draw) * step));
			list->scopeDrew = true;
		}

		return Succeed(error);
	}

	bool CmdDrawIndexedIndirect(
		void * impl, BufferHandle args, const std::uint64_t offset, const std::uint32_t drawCount, const std::uint32_t stride, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect outside a rendering scope");
		}
		if (list->boundIndexBuffer == 0)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect with no index buffer bound");
		}

		MTL::Buffer * buffer = ResolveBuffer(device, args);
		if (buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirect names a buffer this device never created");
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

		return Succeed(error);
	}

} // namespace azo::rhi::metal4

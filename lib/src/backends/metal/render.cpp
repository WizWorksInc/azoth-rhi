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

#include "azoth/rhi/core/build_config.hpp"

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	bool MetalBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.beginRendering");

		auto * object					   = static_cast<MetalObject *>(impl);
		MetalDevice * device			   = object->owner;
		MTL::CommandBuffer * commandBuffer = CmdBufferOf(object);
		if (commandBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool			 = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::RenderPassDescriptor * pass						 = MTL::RenderPassDescriptor::alloc()->init();
		const NS::SharedPtr<MTL::RenderPassDescriptor> passGuard = NS::TransferPtr(pass);

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

		/*
		 * The scope's timestamps, which is the portable way to time one because both sample points are where Metal fixes them anyway. Two paths, chosen by what
		 * the adapter samples at and not by preference.
		 *
		 * Where it samples at a stage boundary the points are named on the pass descriptor here and Metal writes them itself. Where it samples at a draw
		 * boundary the encoder takes each sample by command. The begin is recorded below and the end by MetalEndRendering.
		 */
		MetalQueryPool * timestamps = nullptr;
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

			if (device->samplesAtStageBoundary)
			{
				const NS::UInteger begin = desc.timestamps->beginQuery != kInvalidIndex ? desc.timestamps->beginQuery : MTL::CounterDontSample;
				const NS::UInteger end	 = desc.timestamps->endQuery != kInvalidIndex ? desc.timestamps->endQuery : MTL::CounterDontSample;

				/*
				 * The end is both stage ends aimed at one slot, which is deliberate and not a duplicate. A pass with no fragment work has no fragment stage
				 * boundary and end-of-fragment alone would leave the slot unwritten. Measured on an Apple part, a clear-and-store pass with no draws wrote both
				 * vertex boundaries and neither fragment one.
				 *
				 * Both ends together resolve to whichever the GPU reaches last. The slot always holds the end of the pass.
				 */
				MTL::RenderPassSampleBufferAttachmentDescriptor * attachment = pass->sampleBufferAttachments()->object(0);
				attachment->setSampleBuffer(timestamps->sampleBuffer.get());
				attachment->setStartOfVertexSampleIndex(begin);
				attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
				attachment->setEndOfVertexSampleIndex(end);
				attachment->setEndOfFragmentSampleIndex(end);
			}
		}

		EndActiveEncoders(object);
		MTL::RenderCommandEncoder * encoder = commandBuffer->renderCommandEncoder(pass);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal render command encoder creation failed");
		}
		ConsumeAliasWait(object->list, encoder);
		object->list->renderEncoder = NS::RetainPtr(encoder);

		if (timestamps != nullptr && !device->samplesAtStageBoundary && device->samplesAtDrawBoundary)
		{
			if (desc.timestamps->beginQuery != kInvalidIndex)
			{
				encoder->sampleCountersInBuffer(timestamps->sampleBuffer.get(), desc.timestamps->beginQuery, false);
			}
			if (desc.timestamps->endQuery != kInvalidIndex)
			{
				object->list->pendingEndTimestamp = timestamps->sampleBuffer;
				object->list->pendingEndQuery	  = desc.timestamps->endQuery;
			}
		}
		return Succeed(error);
	}

	bool MetalEndRendering(void * impl, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list != nullptr && object->list->renderEncoder.get() != nullptr)
		{
			// The scope's end timestamp on an adapter that samples by command, left here by MetalBeginRendering because this is the last point the encoder is
			// still open. The stage-boundary path has nothing to do, Metal having written the sample from the pass descriptor.
			if (object->list->pendingEndTimestamp.get() != nullptr)
			{
				object->list->renderEncoder->sampleCountersInBuffer(object->list->pendingEndTimestamp.get(), object->list->pendingEndQuery, false);
				object->list->pendingEndTimestamp.reset();
			}

			object->list->renderEncoder->endEncoding();
			object->list->renderEncoder.reset();
		}
		return Succeed(error);
	}

	bool MetalSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setGraphicsPipeline outside a rendering scope");
		}

		const auto * tracked = device->graphicsPipelines.Resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline names a pipeline this device never created");
		}

		const MetalGraphicsPipeline & pipe	= (*tracked);
		MTL::RenderCommandEncoder * encoder = object->list->renderEncoder.get();
		encoder->setRenderPipelineState(pipe.state.get());
		encoder->setCullMode(pipe.cull);
		encoder->setFrontFacingWinding(pipe.winding);
		encoder->setTriangleFillMode(pipe.fill);
		if (pipe.depthStencil.get() != nullptr)
		{
			encoder->setDepthStencilState(pipe.depthStencil.get());
		}
		if (pipe.depthBiasEnable)
		{
			encoder->setDepthBias(pipe.depthBiasConstant, pipe.depthBiasSlope, pipe.depthBiasClamp);
		}
		object->list->boundPrimitive = pipe.primitive;
		return Succeed(error);
	}

	bool MetalSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setViewport outside a rendering scope");
		}
		// Metal's own NDC runs Y up so eYUp needs nothing done. Presenting eYDown means flipping: origin to the bottom edge, height negated. Winding is
		// left as authored either way.
		double originY = viewport.y;
		double height  = viewport.height;
		if (GetClipSpace() == ClipSpaceConvention::eYDown)
		{
			originY = static_cast<double>(viewport.y) + static_cast<double>(viewport.height);
			height	= -static_cast<double>(viewport.height);
		}

		const MTL::Viewport vp{
			.originX = viewport.x, .originY = originY, .width = viewport.width, .height = height, .znear = viewport.minDepth, .zfar = viewport.maxDepth
		};
		object->list->renderEncoder->setViewport(vp);
		return Succeed(error);
	}

	bool MetalSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setScissor outside a rendering scope");
		}
		const MTL::ScissorRect rect{
			.x = static_cast<NS::UInteger>(scissor.x), .y = static_cast<NS::UInteger>(scissor.y), .width = scissor.width, .height = scissor.height
		};
		object->list->renderEncoder->setScissorRect(rect);
		return Succeed(error);
	}

	bool MetalSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setBlendConstants outside a rendering scope");
		}
		object->list->renderEncoder->setBlendColor(r, g, b, a);
		return Succeed(error);
	}

	bool MetalSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setStencilReference outside a rendering scope");
		}
		object->list->renderEncoder->setStencilReferenceValue(reference);
		return Succeed(error);
	}

	bool MetalSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setDepthBias outside a rendering scope");
		}
		object->list->renderEncoder->setDepthBias(constantFactor, slopeFactor, clamp);
		return Succeed(error);
	}

	bool MetalSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "setVertexBuffer outside a rendering scope");
		}
		MTL::Buffer * resolved = ResolveBuffer(device, buffer);
		if (resolved == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer names a buffer this device never created");
		}
		object->list->renderEncoder->setVertexBuffer(resolved, offset, kMetalVertexBufferBase + slot);
		return Succeed(error);
	}

	bool MetalSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept
	{
		auto * object		   = static_cast<MetalObject *>(impl);
		MetalDevice * device   = object->owner;
		MTL::Buffer * resolved = ResolveBuffer(device, buffer);
		if (resolved == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer names a buffer this device never created");
		}
		object->list->boundIndexBuffer = resolved;
		object->list->boundIndexOffset = offset;
		object->list->boundIndexType   = index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
		return Succeed(error);
	}

	bool MetalPushConstants(void * impl, [[maybe_unused]] const PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "pushConstants outside a recording scope");
		}

		/*
		 * setVertexBytes, setFragmentBytes and setBytes upload the whole block at buffer 0 with no destination offset so a partial update would write the
		 * sub-range to the start of the block and corrupt the earlier fields. Only a full-block push from offset 0 can be honored faithfully without a shadow
		 * copy.
		 */
		if (offset != 0)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "Metal pushConstants only supports a full update from offset 0");
		}

		// Slang lowers push constants to a small Metal buffer set inline, whichever kind of encoder is open. Buffer 0 is reserved for it, which is what
		// keeps MetalArgumentBufferIndexForSet from having to know whether a layout has one.
		constexpr std::uint32_t index = kMetalPushConstantIndex;
		if (MTL::RenderCommandEncoder * render = object->list->renderEncoder.get(); render != nullptr)
		{
			if (stages.Contains(ShaderStage::eVertex))
			{
				render->setVertexBytes(data, size, index);
			}
			if (stages.Contains(ShaderStage::eFragment))
			{
				render->setFragmentBytes(data, size, index);
			}
			return Succeed(error);
		}

		// Outside a rendering scope this is a compute push, which is the same block on the same index of the compute encoder. Opened here the way binding a
		// descriptor set opens one, so the order of the two against a dispatch does not matter.
		EnsureComputeEncoder(object);
		MTL::ComputeCommandEncoder * compute = object->list->computeEncoder.get();
		if (compute == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "pushConstants outside a render or compute scope");
		}

		compute->setBytes(data, size, index);
		return Succeed(error);
	}

	bool MetalDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * object = static_cast<MetalObject *>(impl);
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "draw outside a rendering scope");
		}
		object->list->renderEncoder->drawPrimitives(object->list->boundPrimitive, firstVertex, vertexCount, instanceCount, firstInstance);
		return Succeed(error);
	}

	bool MetalDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * object		= static_cast<MetalObject *>(impl);
		MetalCmdList * list = object->list;
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexed outside a rendering scope");
		}
		if (list->boundIndexBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexed without a bound index buffer");
		}
		const std::uint64_t indexSize	= (list->boundIndexType == MTL::IndexTypeUInt32) ? 4 : 2;
		const std::uint64_t indexOffset = list->boundIndexOffset + (static_cast<std::uint64_t>(firstIndex) * indexSize);
		list->renderEncoder->drawIndexedPrimitives(
			list->boundPrimitive, indexCount, list->boundIndexType, list->boundIndexBuffer, indexOffset, instanceCount, vertexOffset, firstInstance);
		return Succeed(error);
	}

	/*
	 * Metal takes one indirect draw per call so a batch is drawCount calls walking the buffer by stride. MTLDrawPrimitivesIndirectArguments holds the same four
	 * uints the RHI documents so the caller's buffer needs no fixup. A zero stride means tightly packed, since a buffer built for one draw is the common way to
	 * reach this.
	 */
	bool MetalDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		if (object->list == nullptr || object->list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndirect outside a rendering scope");
		}

		MTL::Buffer * indirect = ResolveBuffer(device, args);
		if (indirect == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndirect names a buffer this device never created");
		}

		constexpr std::uint32_t kIndirectArgsSize = 16; // four uints
		const std::uint64_t step				  = stride != 0 ? stride : kIndirectArgsSize;
		MTL::RenderCommandEncoder * encoder		  = object->list->renderEncoder.get();
		for (std::uint32_t draw = 0; draw < drawCount; ++draw)
		{
			encoder->drawPrimitives(object->list->boundPrimitive, indirect, offset + (static_cast<std::uint64_t>(draw) * step));
		}
		return Succeed(error);
	}

	bool MetalDrawIndexedIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		auto * object		 = static_cast<MetalObject *>(impl);
		MetalDevice * device = object->owner;
		MetalCmdList * list	 = object->list;
		if (list == nullptr || list->renderEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect outside a rendering scope");
		}
		if (list->boundIndexBuffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "drawIndexedIndirect without a bound index buffer");
		}

		MTL::Buffer * indirect = ResolveBuffer(device, args);
		if (indirect == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "drawIndexedIndirect names a buffer this device never created");
		}

		// MTLDrawIndexedPrimitivesIndirectArguments is five words: indexCount, instanceCount, indexStart, baseVertex, baseInstance.
		constexpr std::uint32_t kIndexedIndirectArgsSize = 20;
		const std::uint64_t step						 = stride != 0 ? stride : kIndexedIndirectArgsSize;
		for (std::uint32_t draw = 0; draw < drawCount; ++draw)
		{
			list->renderEncoder->drawIndexedPrimitives(list->boundPrimitive,
				list->boundIndexType,
				list->boundIndexBuffer,
				list->boundIndexOffset,
				indirect,
				offset + (static_cast<std::uint64_t>(draw) * step));
		}
		return Succeed(error);
	}

	// Lazily opens the compute encoder. bindDescriptorSet can run before setComputePipeline so both ensure the encoder exists first.

} // namespace azo::rhi::metal

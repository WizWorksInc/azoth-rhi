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
	 * The RHI's pipeline stages against Metal 4's.
	 *
	 * Coarser on this side than on Vulkan's, because Metal names the stages an encoder runs in and not the finer graph a Vulkan barrier can address. Anything
	 * with no distinct Metal stage folds into the nearest one that contains it, which widens a barrier instead of narrowing it, and a barrier that waits for
	 * more than it had to is slow where one that waits for less is wrong.
	 */
	MTL::Stages StagesFor(const Flags<PipelineStage> stages) noexcept
	{
		if (stages.Contains(PipelineStage::eAllCommands) || stages.Contains(PipelineStage::eAllGraphics))
		{
			return MTL::StageAll;
		}

		NS::UInteger out = 0;

		if (stages.Contains(PipelineStage::eVertexShader) || stages.Contains(PipelineStage::eVertexInput) ||
			stages.Contains(PipelineStage::eDrawIndirect) || stages.Contains(PipelineStage::eTessellationControlShader) ||
			stages.Contains(PipelineStage::eTessellationEvaluationShader) || stages.Contains(PipelineStage::eGeometryShader))
		{
			out |= MTL::StageVertex;
		}

		// Depth and stencil testing happen inside the fragment stage here, having no encoder stage of their own.
		if (stages.Contains(PipelineStage::eFragmentShader) || stages.Contains(PipelineStage::eColorOutput) ||
			stages.Contains(PipelineStage::eEarlyFragmentTests) || stages.Contains(PipelineStage::eLateFragmentTests))
		{
			out |= MTL::StageFragment;
		}

		if (stages.Contains(PipelineStage::eComputeShader))
		{
			out |= MTL::StageDispatch;
		}

		// Copies, clears and resolves all record on the compute encoder here, but Metal still counts them as the blit stage, so a barrier against a copy names
		// blit and not dispatch.
		if (stages.Contains(PipelineStage::eCopy) || stages.Contains(PipelineStage::eClear) || stages.Contains(PipelineStage::eResolve))
		{
			out |= MTL::StageBlit;
		}

		if (stages.Contains(PipelineStage::eAccelerationStructureBuild))
		{
			out |= MTL::StageAccelerationStructure;
		}

		// eHost is not a GPU stage. A barrier naming only it has nothing to order here, and StageAll is the answer that cannot be wrong.
		return out != 0 ? static_cast<MTL::Stages>(out) : MTL::StageAll;
	}

	namespace
	{
		/*
		 * What each encoder kind runs, and what an intra-pass barrier inside it may wait for.
		 *
		 * The two differ only on the render encoder: a tile based deferred renderer cannot hold a pass open waiting for its own fragment or tile work, so
		 * naming fragment there aborts in the validation layer and corrupts the command buffer's resource table without it. Tile, object and mesh stay out of
		 * the render masks because nothing here builds a pipeline that runs them.
		 */
		constexpr MTL::Stages kRenderEncoderStages	 = static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageFragment);
		constexpr MTL::Stages kRenderWaitableStages	 = MTL::StageVertex;
		constexpr MTL::Stages kComputeEncoderStages	 = static_cast<MTL::Stages>(MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure);
		constexpr MTL::Stages kComputeWaitableStages = kComputeEncoderStages;

		[[nodiscard]] constexpr MTL::Stages Intersect(const MTL::Stages stages, const MTL::Stages mask) noexcept
		{
			return static_cast<MTL::Stages>(static_cast<NS::UInteger>(stages) & static_cast<NS::UInteger>(mask));
		}

		/*
		 * A barrier recorded while an encoder is open, the mid-pass placement.
		 *
		 * barrierAfterStages is the producer form and carries the whole dependency: everything a later encoder runs waits for what is already recorded. Both
		 * masks take any stage, so neither side is narrowed.
		 *
		 * barrierAfterEncoderStages covers only the rest of this same encoder, and is skipped, not widened, when either side falls outside it. No gap: this
		 * generation will not wait on a fragment producer, and later encoders are covered above.
		 */
		template <typename EncoderT>
		void RecordBarrier(EncoderT * encoder, const MTL::Stages waitable, const MTL::Stages runnable, const MTL::Stages producer, const MTL::Stages consumer) noexcept
		{
			encoder->barrierAfterStages(producer, consumer, MTL4::VisibilityOptionDevice);

			const MTL::Stages after	 = Intersect(producer, waitable);
			const MTL::Stages before = Intersect(consumer, runnable);
			if (static_cast<NS::UInteger>(after) != 0 && static_cast<NS::UInteger>(before) != 0)
			{
				encoder->barrierAfterEncoderStages(after, before, MTL4::VisibilityOptionDevice);
			}
		}

		/*
		 * A barrier recorded with nothing open, replayed onto the encoder it was waiting for.
		 *
		 * barrierAfterQueueStages is the consumer form: this encoder waits for the ones before it, which is exactly where the barrier sat. Both of its masks
		 * take any stage too, so the producer arrives as it was written even though it names work a different kind of encoder ran.
		 */
		template <typename EncoderT>
		void FlushPending(CmdList * list, EncoderT * encoder) noexcept
		{
			if (list == nullptr || encoder == nullptr || static_cast<NS::UInteger>(list->pendingConsumer) == 0)
			{
				return;
			}

			encoder->barrierAfterQueueStages(list->pendingProducer, list->pendingConsumer, MTL4::VisibilityOptionDevice);
			list->pendingProducer = static_cast<MTL::Stages>(0);
			list->pendingConsumer = static_cast<MTL::Stages>(0);
		}
	} // namespace

	void FlushPendingBarrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept
	{
		FlushPending(list, encoder);
	}

	void FlushPendingBarrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept
	{
		FlushPending(list, encoder);
	}

	/*
	 * Closing whatever is open, and signing off on any timestamps it took.
	 *
	 * The fence update belongs here and not where the resolve is recorded, because the encoder that wrote a timestamp has usually closed by then. A frame that
	 * times a dispatch and then renders has no encoder open at all when it resolves, so a fence taken at that point would order nothing and the resolve would
	 * read slots the GPU had not written yet.
	 */
	void EndActiveEncoders(CmdList * list) noexcept
	{
		if (list->renderEncoder.get() != nullptr)
		{
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->renderEncoder->updateFence(list->timestampFence.get(), kRenderEncoderStages);
			}

			list->renderEncoder->endEncoding();
			list->renderEncoder.reset();
		}
		if (list->computeEncoder.get() != nullptr)
		{
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->computeEncoder->updateFence(list->timestampFence.get(), kComputeEncoderStages);
			}

			list->computeEncoder->endEncoding();
			list->computeEncoder.reset();
		}
	}

	/*
	 * The compute encoder, opened once and kept.
	 *
	 * Metal 3 opens a blit encoder per copy and closes it again, because a blit encoder is a different object from the compute one and every switch between
	 * them costs an encoder. Here they are the same encoder, so a run of copies and dispatches records into one and the only thing that closes it is a
	 * rendering scope or the end of the list.
	 */
	MTL4::ComputeCommandEncoder * BeginCompute(Metal4Object * object) noexcept
	{
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return nullptr;
		}

		if (list->computeEncoder.get() != nullptr)
		{
			return list->computeEncoder.get();
		}

		// A rendering scope and a compute scope cannot both be open, so opening this closes that.
		if (list->renderEncoder.get() != nullptr)
		{
			list->renderEncoder->endEncoding();
			list->renderEncoder.reset();
		}

		MTL4::ComputeCommandEncoder * encoder = list->commandBuffer->computeCommandEncoder();
		if (encoder == nullptr)
		{
			return nullptr;
		}

		encoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder = NS::RetainPtr(encoder);
		FlushPendingBarrier(list, encoder);
		return encoder;
	}

	/*
	 * Push constant bytes into memory a shader can reach.
	 *
	 * An argument table binds addresses, and this generation has no inline setBytes, so the bytes need a buffer. Written at a bump offset inside a block and a
	 * new block added when one fills, so a list pushing constants every draw still allocates only once. The blocks are dropped at the next Begin, by which
	 * point the caller has waited for the submission that read them.
	 */
	MTL::GPUAddress WritePushConstants(Metal4Device * device, CmdList * list, const void * data, const std::uint32_t size) noexcept
	{
		// Metal wants a buffer offset aligned, and 256 covers every constant alignment the ABI can ask for.
		constexpr std::uint64_t kAlignment = 256;
		constexpr std::uint64_t kBlockSize = 64 * 1024;

		const std::uint64_t aligned = (static_cast<std::uint64_t>(size) + kAlignment - 1) & ~(kAlignment - 1);

		const bool needsBlock = list->pushConstantBlocks.empty() || list->pushConstantOffset + aligned > kBlockSize;
		if (needsBlock)
		{
			if (aligned > kBlockSize)
			{
				return 0;
			}

			MTL::Buffer * block = device->device->newBuffer(kBlockSize, MTL::ResourceStorageModeShared);
			if (block == nullptr)
			{
				return 0;
			}

			NS::SharedPtr<MTL::Buffer> owned = NS::TransferPtr(block);
			if (!detail::TryPushBack(list->pushConstantBlocks, owned))
			{
				return 0;
			}

			// Bound by address like everything else here, so it has to be resident like everything else here.
			NoteListAllocation(list, owned.get());

			list->pushConstantOffset = 0;
		}

		MTL::Buffer * block = list->pushConstantBlocks.back().get();

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a mapped buffer is a flat run of bytes by construction.
		std::memcpy(static_cast<std::uint8_t *>(block->contents()) + list->pushConstantOffset, data, size);

		const MTL::GPUAddress address = block->gpuAddress() + list->pushConstantOffset;
		list->pushConstantOffset += aligned;
		return address;
	}

	/*
	 * Beginning a recording.
	 *
	 * Metal 3 makes a fresh command buffer per Begin because a Metal command buffer is single use. Here the command buffer is reused and the allocator behind
	 * it is reset, which is the same single-use rule expressed as recycling the memory and not the object. Resetting invalidates everything the allocator
	 * handed out before, which is why an allocator belongs to one list and is never shared.
	 */
	bool CmdBegin(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr || list->allocator.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		/*
		 * A Begin on a list that is already recording closes what it had first.
		 *
		 * An allocator holds one open command buffer, and beginning a second against it is refused by Metal, not treated as a restart. The RHI's own lifecycle
		 * check catches this in the validation modes that run it, so what reaches here is a caller in a mode that does not, and dropping the previous recording
		 * is what the other generation does by making a fresh command buffer.
		 */
		if (list->lifecycle == 1)
		{
			EndActiveEncoders(list);
			list->commandBuffer->endCommandBuffer();
		}

		list->allocator->reset();
		list->commandBuffer->beginCommandBuffer(list->allocator.get());

		// What this recording's own transients are held in, declared here so it does not have to be one of the thirty-two a queue will take.
		if (list->residency.get() != nullptr)
		{
			list->commandBuffer->useResidencySet(list->residency.get());
		}

		if (!list->debugName.empty())
		{
			list->commandBuffer->setLabel(NS::String::string(list->debugName.c_str(), NS::UTF8StringEncoding));
		}

		// The previous recording's transients belong to a submission the caller has already waited for.
		list->keepAlive.clear();
		list->wroteEncoderTimestamps = false;
		list->debugLabelScopes.clear();
		list->pendingProducer = static_cast<MTL::Stages>(0);
		list->pendingConsumer = static_cast<MTL::Stages>(0);

		/*
		 * The push constant blocks are kept and rewound, not dropped and remade.
		 *
		 * A list outlives its recordings once a pool takes them back, so a block rebuilt every Begin would leave the set below holding one dead buffer per
		 * frame. The caller has waited for the submission that read them, which is what a pool reset promises, so reuse cannot hand back a block the GPU is
		 * still reading.
		 */
		list->pushConstantOffset = 0;

		/*
		 * The residency set is rebuilt from what this recording will actually use. Removing everything and putting the kept blocks back leaves it holding this
		 * frame's transients and nothing else.
		 */
		if (list->residency.get() != nullptr)
		{
			list->residency->removeAllAllocations();
			for (const NS::SharedPtr<MTL::Buffer> & block : list->pushConstantBlocks)
			{
				list->residency->addAllocation(block.get());
			}
			list->residency->commit();
			list->residency->requestResidency();
		}

		list->lifecycle = 1;
		return Succeed(error);
	}

	bool CmdEnd(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		EndActiveEncoders(list);
		list->commandBuffer->endCommandBuffer();

		list->lifecycle = 2;
		return Succeed(error);
	}

	/*
	 * Barriers, which on this generation are real.
	 *
	 * Metal 3 tracks hazards inside one command buffer itself, so the batch lowers to nothing there. Metal 4 gives that up for explicit control, so a
	 * BarrierBatch reaches the driver and a caller that under-declares gets a race the other generation covered for.
	 *
	 * One barrier for the batch, not one per resource: Metal's barrier is between stage sets and names no resource, so the batch collapses to the union on each
	 * side.
	 */
	bool CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.barriers");

		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		Flags<PipelineStage> before;
		Flags<PipelineStage> after;

		for (const BufferBarrier & barrier : barriers.buffers)
		{
			before |= barrier.before.stages;
			after |= barrier.after.stages;
		}
		for (const TextureBarrier & barrier : barriers.textures)
		{
			before |= barrier.before.stages;
			after |= barrier.after.stages;
		}
		for (const MemoryBarrier & barrier : barriers.memory)
		{
			before |= barrier.before.stages;
			after |= barrier.after.stages;
		}

		if (before.Empty() && after.Empty())
		{
			return Succeed(error);
		}

		const MTL::Stages producer = StagesFor(before);
		const MTL::Stages consumer = StagesFor(after);

		if (list->renderEncoder.get() != nullptr)
		{
			RecordBarrier(list->renderEncoder.get(), kRenderWaitableStages, kRenderEncoderStages, producer, consumer);
		}
		else if (list->computeEncoder.get() != nullptr)
		{
			RecordBarrier(list->computeEncoder.get(), kComputeWaitableStages, kComputeEncoderStages, producer, consumer);
		}
		else
		{
			/*
			 * Nothing is open, so there is no encoder to record on and opening one to host a barrier is what this used to do. An encoder holding nothing but a
			 * barrier has no work on either side of it to order, so the barrier is held for the next encoder instead, which is the one it was recorded for.
			 */
			list->pendingProducer = static_cast<MTL::Stages>(static_cast<NS::UInteger>(list->pendingProducer) | static_cast<NS::UInteger>(producer));
			list->pendingConsumer = static_cast<MTL::Stages>(static_cast<NS::UInteger>(list->pendingConsumer) | static_cast<NS::UInteger>(consumer));
		}

		return Succeed(error);
	}

	bool CmdBeginDebugLabel(void * impl, CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (!object->owner->debugLabels)
		{
			return Succeed(error);
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		NS::String * label							  = NS::String::string(name != nullptr ? name : "", NS::UTF8StringEncoding);

		// Pushed on whatever is open, and remembered so the pop reaches the same object and not whatever is open by then.
		MTL4::CommandEncoder * scope = nullptr;
		if (list->renderEncoder.get() != nullptr)
		{
			scope = list->renderEncoder.get();
		}
		else if (list->computeEncoder.get() != nullptr)
		{
			scope = list->computeEncoder.get();
		}

		if (scope != nullptr)
		{
			scope->pushDebugGroup(label);
		}
		else
		{
			list->commandBuffer->pushDebugGroup(label);
		}

		if (!detail::TryPushBack(list->debugLabelScopes, scope))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "debug label tracking failed");
		}

		return Succeed(error);
	}

	bool CmdEndDebugLabel(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (!object->owner->debugLabels || list->debugLabelScopes.empty())
		{
			return Succeed(error);
		}

		MTL4::CommandEncoder * scope = list->debugLabelScopes.back();
		list->debugLabelScopes.pop_back();

		/*
		 * A label opened inside an encoder that has since closed went with it, so there is nothing to pop. Testing the pointer against what is open now is what
		 * tells those apart.
		 */
		if (scope != nullptr)
		{
			const bool stillOpen = scope == static_cast<MTL4::CommandEncoder *>(list->renderEncoder.get()) ||
								   scope == static_cast<MTL4::CommandEncoder *>(list->computeEncoder.get());
			if (stillOpen)
			{
				scope->popDebugGroup();
			}

			return Succeed(error);
		}

		list->commandBuffer->popDebugGroup();
		return Succeed(error);
	}

	bool CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);

		const auto * tracked = device->computePipelines.Resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr || tracked->state.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setComputePipeline names a pipeline this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		// The same MTLComputePipelineState the other generation builds. Pipelines did not fork.
		encoder->setComputePipelineState(tracked->state.get());
		list->boundThreadGroup = tracked->threadsPerThreadgroup;
		return Succeed(error);
	}

	bool CmdDispatch(void * impl, const std::uint32_t x, const std::uint32_t y, const std::uint32_t z, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatch without a bound compute pipeline");
		}

		// Re-set immediately before the dispatch, in case the encoder took a copy of the table when it was first handed one instead of at dispatch time.
		list->computeEncoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder->dispatchThreadgroups(MTL::Size::Make(x, y, z), list->boundThreadGroup);
		return Succeed(error);
	}

	bool CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatchIndirect without a bound compute pipeline");
		}

		MTL::Buffer * buffer = ResolveBuffer(device, args);
		if (buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect names a buffer this device never created");
		}

		// An address, not a buffer and an offset, which is how this generation takes indirect arguments.
		list->computeEncoder->dispatchThreadgroups(buffer->gpuAddress() + offset, list->boundThreadGroup);
		return Succeed(error);
	}

	bool CmdCopyBuffer(void * impl, BufferHandle dst, const std::uint64_t dstOffset, BufferHandle src, const std::uint64_t srcOffset,
		const std::uint64_t size, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBuffer");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Buffer * destination = ResolveBuffer(device, dst);
		MTL::Buffer * source	  = ResolveBuffer(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		encoder->copyFromBuffer(source, srcOffset, destination, dstOffset, size);
		return Succeed(error);
	}

	bool CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBufferToTexture");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * texture = ResolveTexture(device, dst);
		MTL::Buffer * buffer   = ResolveBuffer(device, src);
		if (texture == nullptr || buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture names a resource this device never created");
		}

		const Format format = ResolveTextureFormat(device, dst);
		if (!detail::HasLinearLayout(format))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "copyBufferToTexture on a combined depth-stencil format, whose aspects copy separately");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		for (const BufferTextureCopy & region : regions)
		{
			// Block arithmetic and not texel arithmetic, as on the other generation: a row length is in texels and converts through the block grid, and an
			// image height counts block rows.
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const NS::UInteger bytesPerRow	 = static_cast<NS::UInteger>(detail::TightRowPitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::BlockRows(format, imageRows);

			encoder->copyFromBuffer(buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage,
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)));
		}

		return Succeed(error);
	}

	bool CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTextureToBuffer");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Buffer * buffer   = ResolveBuffer(device, dst);
		MTL::Texture * texture = ResolveTexture(device, src);
		if (buffer == nullptr || texture == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer names a resource this device never created");
		}

		const Format format = ResolveTextureFormat(device, src);
		if (!detail::HasLinearLayout(format))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "copyTextureToBuffer on a combined depth-stencil format, whose aspects copy separately");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		for (const BufferTextureCopy & region : regions)
		{
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const NS::UInteger bytesPerRow	 = static_cast<NS::UInteger>(detail::TightRowPitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::BlockRows(format, imageRows);

			encoder->copyFromTexture(texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)),
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage);
		}

		return Succeed(error);
	}

	bool CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTexture");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * destination = ResolveTexture(device, dst);
		MTL::Texture * source	   = ResolveTexture(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTexture names a texture this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		for (const TextureCopy & region : regions)
		{
			encoder->copyFromTexture(source,
				region.srcSubresource.layer,
				region.srcSubresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.srcOffset.x),
					static_cast<NS::UInteger>(region.srcOffset.y),
					static_cast<NS::UInteger>(region.srcOffset.z)),
				MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth),
				destination,
				region.dstSubresource.layer,
				region.dstSubresource.mip,
				MTL::Origin::Make(static_cast<NS::UInteger>(region.dstOffset.x),
					static_cast<NS::UInteger>(region.dstOffset.y),
					static_cast<NS::UInteger>(region.dstOffset.z)));
		}

		return Succeed(error);
	}

	/*
	 * Filling a buffer, which Metal takes a byte value for and the RHI states as a word.
	 *
	 * Four equal bytes lower to a fill, which allocates nothing. Anything else is staged as a word pattern in a shared buffer and copied in, which is what the
	 * other generation does for every value. Clearing to zero is most of the calls, so keeping the fill for that case saves a staging allocation per clear.
	 */
	bool CmdClearBuffer(
		void * impl, BufferHandle buffer, const std::uint64_t offset, const std::uint64_t size, const std::uint32_t value, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearBuffer");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * destination = ResolveBuffer(device, buffer);
		if (destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		const auto byte = static_cast<std::uint8_t>(value & 0xFFu);
		if (value == (static_cast<std::uint32_t>(byte) * 0x01010101u))
		{
			encoder->fillBuffer(destination, NS::Range::Make(offset, size), byte);
			return Succeed(error);
		}

		NS::SharedPtr<MTL::Buffer> staging = NS::TransferPtr(device->device->newBuffer(size, MTL::ResourceStorageModeShared));
		if (staging.get() == nullptr)
		{
			return Fail(error, ErrorCode::eOutOfDeviceMemory, "Metal 4 clear staging buffer allocation failed");
		}

		auto * words				  = static_cast<std::uint32_t *>(staging->contents());
		const std::uint64_t wordCount = size / 4;
		for (std::uint64_t i = 0; i < wordCount; ++i)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a mapped buffer is a flat run of words.
			words[i] = value;
		}

		// Read by the copy below, so it has to be reachable like anything else the GPU touches here.
		NoteListAllocation(list, staging.get());

		encoder->copyFromBuffer(staging.get(), 0, destination, offset, wordCount * 4);

		// Held until the next Begin, by which point the caller has waited for the submission that read it.
		if (!detail::TryPushBack(list->keepAlive, staging))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "clear staging buffer tracking failed");
		}

		return Succeed(error);
	}

	/*
	 * Clearing a texture, which is a load action and not a command on either generation.
	 *
	 * One rendering scope per subresource, opened with a clear load action and closed with no draw in it, which is what performs the clear. The scope is the
	 * operation, so the compute encoder has to give way first.
	 */
	bool CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearTexture");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const Metal4TextureSlot * slot = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr || slot->texture.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearTexture names a texture this device never created");
		}

		MTL::Texture * tex = slot->texture.get();

		/*
		 * Metal clears a color texture by opening a render pass over it, which a texture created without MTLTextureUsageRenderTarget cannot be an attachment
		 * of. Refused here with a reason instead of left to the validation layer, which traps in a debug build and reads back nothing at all in a release one.
		 *
		 * The same refusal Direct3D 12 makes, for the same reason: it clears through a render target view.
		 */
		if (!slot->usage.Contains(TextureUsage::eColorAttachment))
		{
			return Fail(error,
				ErrorCode::eInvalidArgument,
				"clearTexture needs a texture usable as a color attachment, which is what Metal clears through");
		}

		EndActiveEncoders(list);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureSubresourceRange & range : ranges)
		{
			// The clear value is a color, and a depth or stencil slice needs a different attachment and a different clear value, so a range naming one is
			// refused, not cleared to something nobody asked for.
			if (range.aspects.Contains(TextureAspect::eDepth) || range.aspects.Contains(TextureAspect::eStencil))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal clearTexture clears color aspects only");
			}

			const auto textureMips	 = static_cast<std::uint32_t>(tex->mipmapLevelCount());
			const auto textureLayers = static_cast<std::uint32_t>(tex->arrayLength());
			if (range.baseMip >= textureMips || range.baseLayer >= textureLayers)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "clearTexture range starts past the end of the texture");
			}

			const std::uint32_t mipCount   = range.mipCount == kAllMips ? textureMips - range.baseMip : range.mipCount;
			const std::uint32_t layerCount = range.layerCount == kAllLayers ? textureLayers - range.baseLayer : range.layerCount;

			for (std::uint32_t mip = range.baseMip; mip < range.baseMip + mipCount; ++mip)
			{
				for (std::uint32_t layer = range.baseLayer; layer < range.baseLayer + layerCount; ++layer)
				{
					const NS::SharedPtr<MTL4::RenderPassDescriptor> pass = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());

					// The attachment descriptors did not fork: a Metal 4 render pass holds the same ones.
					MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(0);
					attachment->setTexture(tex);
					attachment->setLevel(mip);
					attachment->setSlice(layer);
					attachment->setLoadAction(MTL::LoadActionClear);
					attachment->setStoreAction(MTL::StoreActionStore);
					attachment->setClearColor(MTL::ClearColor::Make(color.r, color.g, color.b, color.a));

					MTL4::RenderCommandEncoder * encoder = list->commandBuffer->renderCommandEncoder(pass.get());
					if (encoder == nullptr)
					{
						return Fail(error, ErrorCode::eNativeApiError, "Metal 4 clear render command encoder creation failed");
					}

					encoder->endEncoding();
				}
			}
		}

		return Succeed(error);
	}

	bool CmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.generateMips");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * tex = ResolveTexture(device, texture);
		if (tex == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "generateMips names a texture this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object);
		if (encoder == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		encoder->generateMipmaps(tex);
		return Succeed(error);
	}

	/*
	 * Refused for the reason the other generation refuses it: Metal has no fixed function scaled blit, on either generation, and resampling belongs to the
	 * utility target which dispatches a compute shader for it.
	 */
	bool CmdBlit(void * impl, TextureHandle, TextureHandle, std::span<const TextureBlit>, Filter, Error * error) noexcept
	{
		static_cast<void>(impl);
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal has no scaled blit, so resampling goes through the utility target's compute path");
	}

	/*
	 * Resolving, which Metal performs through a store action and not a copy.
	 *
	 * The multisampled source is attached, the single sample destination is named as its resolve target, and ending the scope with no draw in it performs the
	 * resolve.
	 */
	bool CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.resolveTexture");

		auto * object		 = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		 = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * destination = ResolveTexture(device, dst);
		MTL::Texture * source	   = ResolveTexture(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveTexture names a texture this device never created");
		}

		EndActiveEncoders(list);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureResolve & region : regions)
		{
			const NS::SharedPtr<MTL4::RenderPassDescriptor> pass = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());

			MTL::RenderPassColorAttachmentDescriptor * attachment = pass->colorAttachments()->object(0);
			attachment->setTexture(source);
			attachment->setLevel(region.srcSubresource.mip);
			attachment->setSlice(region.srcSubresource.layer);
			attachment->setLoadAction(MTL::LoadActionLoad);
			attachment->setStoreAction(MTL::StoreActionMultisampleResolve);
			attachment->setResolveTexture(destination);
			attachment->setResolveLevel(region.dstSubresource.mip);
			attachment->setResolveSlice(region.dstSubresource.layer);

			MTL4::RenderCommandEncoder * encoder = list->commandBuffer->renderCommandEncoder(pass.get());
			if (encoder == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal 4 resolve render command encoder creation failed");
			}

			encoder->endEncoding();
		}

		return Succeed(error);
	}

} // namespace azo::rhi::metal4

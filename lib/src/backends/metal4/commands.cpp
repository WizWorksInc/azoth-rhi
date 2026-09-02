// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	constexpr MTL::Stages StagesFor(const Flags<Stage> stages) noexcept
	{
		if (stages.Contains(Stage::eAllCommands) || stages.Contains(Stage::eAllGraphics))
		{
			return MTL::StageAll;
		}

		NS::UInteger out = 0;

		if (stages.Contains(Stage::eVertexWork) || stages.Contains(Stage::eIndirectFetch))
		{
			out |= MTL::StageVertex;
		}

		if (stages.Contains(Stage::eCompute) || stages.Contains(Stage::eIndirectFetch))
		{
			out |= MTL::StageDispatch;
		}

		if (stages.Contains(Stage::eFragmentShading) || stages.Contains(Stage::eDepthStencil) || stages.Contains(Stage::eColorOutput))
		{
			out |= MTL::StageFragment;
		}

		if (stages.Contains(Stage::eCopy))
		{
			out |= MTL::StageBlit;
		}

		if (stages.Contains(Stage::eCopy) || stages.Contains(Stage::eResolve))
		{
			out |= MTL::StageFragment;
		}

		if (stages.Contains(Stage::eAccelBuild))
		{
			out |= MTL::StageAccelerationStructure;
		}

		if (stages.Contains(Stage::eRayTracing))
		{
			out |= MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch;
		}

		return out != 0 ? static_cast<MTL::Stages>(out) : MTL::StageAll;
	}

	namespace
	{
		[[nodiscard]] constexpr Flags<Stage> StagesOf(const ResourceState & state) noexcept
		{
			if (!state.stages.Empty())
			{
				return state.stages;
			}

			Flags<Stage> out{};

			if (state.use.Contains(ResourceUse::eIndirectArgs))
			{
				out |= Stage::eIndirectFetch;
			}
			if (state.use.Contains(ResourceUse::eVertexBuffer) || state.use.Contains(ResourceUse::eIndexBuffer))
			{
				out |= Stage::eVertexWork;
			}

			if (state.use.Contains(ResourceUse::eUniformRead) || state.use.Contains(ResourceUse::eSampledRead) ||
				state.use.Contains(ResourceUse::eStorageRead) || state.use.Contains(ResourceUse::eStorageWrite))
			{
				out |= Stage::eAllCommands;
			}

			if (state.use.Contains(ResourceUse::eColorTarget))
			{
				out |= Stage::eColorOutput;
			}
			if (state.use.Contains(ResourceUse::eDepthStencilTarget) || state.use.Contains(ResourceUse::eDepthStencilRead))
			{
				out |= Stage::eDepthStencil;
			}
			if (state.use.Contains(ResourceUse::eCopySrc) || state.use.Contains(ResourceUse::eCopyDst))
			{
				out |= Stage::eCopy;
			}
			if (state.use.Contains(ResourceUse::eResolveSrc) || state.use.Contains(ResourceUse::eResolveDst))
			{
				out |= Stage::eResolve;
			}
			if (state.use.Contains(ResourceUse::eHostRead) || state.use.Contains(ResourceUse::eHostWrite))
			{
				out |= Stage::eHost;
			}
			if (state.use.Contains(ResourceUse::eAccelBuildInput) || state.use.Contains(ResourceUse::eAccelWrite) ||
				state.use.Contains(ResourceUse::eAccelBuildScratch))
			{
				out |= Stage::eAccelBuild;
			}
			if (state.use.Contains(ResourceUse::eAccelRead))
			{
				out |= Flags<Stage>(Stage::eAccelBuild) | Stage::eRayTracing;
			}

			return out;
		}

		static_assert(StagesFor(Stage::eIndirectFetch) == static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageDispatch),
			"an indirect dispatch fetches its arguments on the dispatch stage, so naming vertex alone leaves the fetch unordered against the write that filled "
			"the argument buffer");

		static_assert(StagesFor(StagesOf(ResourceState{ .use = ResourceUse::eAccelRead })) ==
						  static_cast<MTL::Stages>(MTL::StageAccelerationStructure | MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch),
			"reading an acceleration structure happens in the shaders that trace as well as in a refit, so the build stage alone never orders the trace");

		static_assert((static_cast<NS::UInteger>(StagesFor(Stage::eRayTracing)) & MTL::StageAccelerationStructure) == 0,
			"Apple's acceleration structure stage is where a build runs and not where a tracing shader runs, which is what eAccelBuild names instead");

		static_assert(StagesFor(Flags<Stage>{}) == MTL::StageAll,
			"an empty mask has to widen to everything, since PlaceBarrier holds this value and FlushPending reads a zero consumer as nothing pending");

		static_assert(StagesFor(StagesOf(ResourceState{ .use = ResourceUse::eAccelBuildScratch })) == MTL::StageAccelerationStructure,
			"a build scratch is touched by the build and by nothing else here, so it names the one stage that runs a build and never widens to everything");

		constexpr MTL::Stages kRenderEncoderStages	 = static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageFragment);
		constexpr MTL::Stages kRenderWaitableStages	 = MTL::StageVertex;
		constexpr MTL::Stages kComputeEncoderStages	 = static_cast<MTL::Stages>(MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure);
		constexpr MTL::Stages kComputeWaitableStages = kComputeEncoderStages;

		[[nodiscard]] constexpr MTL::Stages Intersect(const MTL::Stages stages, const MTL::Stages mask) noexcept
		{
			return static_cast<MTL::Stages>(static_cast<NS::UInteger>(stages) & static_cast<NS::UInteger>(mask));
		}

		template <typename EncoderT>
		void RecordBarrier(EncoderT * encoder, const MTL::Stages waitable, const MTL::Stages runnable, const MTL::Stages producer, const MTL::Stages consumer,
			const MTL4::VisibilityOptions visibility) noexcept
		{
			encoder->barrierAfterStages(producer, consumer, visibility);

			const MTL::Stages after	 = Intersect(producer, waitable);
			const MTL::Stages before = Intersect(consumer, runnable);
			if (static_cast<NS::UInteger>(after) != 0 && static_cast<NS::UInteger>(before) != 0)
			{
				encoder->barrierAfterEncoderStages(after, before, visibility);
			}
		}

		template <typename EncoderT>
		void FlushPending(CmdList * list, EncoderT * encoder) noexcept
		{
			if (list == nullptr || encoder == nullptr || static_cast<NS::UInteger>(list->pendingConsumer) == 0)
			{
				return;
			}

			encoder->barrierAfterQueueStages(list->pendingProducer, list->pendingConsumer, list->pendingVisibility);
			list->pendingProducer	= static_cast<MTL::Stages>(0);
			list->pendingConsumer	= static_cast<MTL::Stages>(0);
			list->pendingVisibility = MTL4::VisibilityOptionNone;
		}

		void PlaceBarrier(CmdList * list, const MTL::Stages producer, const MTL::Stages consumer, const MTL4::VisibilityOptions visibility) noexcept
		{
			if (list->renderEncoder.get() != nullptr)
			{
				RecordBarrier(list->renderEncoder.get(), kRenderWaitableStages, kRenderEncoderStages, producer, consumer, visibility);
			}
			else if (list->computeEncoder.get() != nullptr)
			{
				RecordBarrier(list->computeEncoder.get(), kComputeWaitableStages, kComputeEncoderStages, producer, consumer, visibility);
			}
			else
			{
				list->pendingProducer = static_cast<MTL::Stages>(static_cast<NS::UInteger>(list->pendingProducer) | static_cast<NS::UInteger>(producer));
				list->pendingConsumer = static_cast<MTL::Stages>(static_cast<NS::UInteger>(list->pendingConsumer) | static_cast<NS::UInteger>(consumer));
				list->pendingVisibility =
					static_cast<MTL4::VisibilityOptions>(static_cast<NS::UInteger>(list->pendingVisibility) | static_cast<NS::UInteger>(visibility));
			}
		}
	}

	void FlushPendingBarrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept
	{
		FlushPending(list, encoder);
	}

	void FlushPendingBarrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept
	{
		FlushPending(list, encoder);
	}

	void PopEncoderDebugGroups(CmdList * list, MTL4::CommandEncoder * encoder) noexcept
	{
		if (encoder == nullptr)
		{
			return;
		}

		for (std::size_t index = list->debugLabelScopes.size(); index-- > 0;)
		{
			if (list->debugLabelScopes[index] != list->encoderEpoch)
			{
				break;
			}

			encoder->popDebugGroup();
			list->debugLabelScopes[index] = kDebugScopeClosed;
		}
	}

	void EndActiveEncoders(CmdList * list) noexcept
	{
		if (list->renderEncoder.get() != nullptr)
		{
			PopEncoderDebugGroups(list, list->renderEncoder.get());
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->renderEncoder->updateFence(list->timestampFence.get(), kRenderEncoderStages);
			}

			list->renderEncoder->endEncoding();
			list->renderEncoder.reset();
		}
		if (list->computeEncoder.get() != nullptr)
		{
			PopEncoderDebugGroups(list, list->computeEncoder.get());
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->computeEncoder->updateFence(list->timestampFence.get(), kComputeEncoderStages);
			}

			list->computeEncoder->endEncoding();
			list->computeEncoder.reset();
		}
	}

	MTL4::ComputeCommandEncoder * BeginCompute(Metal4Object * object, Error * error) noexcept
	{
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return FailValue<MTL4::ComputeCommandEncoder *>(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (list->computeEncoder.get() != nullptr)
		{
			return list->computeEncoder.get();
		}

		if (list->renderEncoder.get() != nullptr)
		{
			return FailValue<MTL4::ComputeCommandEncoder *>(
				error, ErrorCode::eInvalidState, "a transfer or compute command cannot be recorded inside a rendering scope, so record it between passes");
		}

		MTL4::ComputeCommandEncoder * encoder = list->commandBuffer->computeCommandEncoder();
		if (encoder == nullptr)
		{
			return FailValue<MTL4::ComputeCommandEncoder *>(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		encoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder = NS::RetainPtr(encoder);
		++list->encoderEpoch;
		FlushPendingBarrier(list, encoder);
		return encoder;
	}

	MTL::GPUAddress WritePushConstants(Metal4Device * device, CmdList * list, const void * data, const std::uint32_t size) noexcept
	{
		constexpr std::uint64_t kAlignment = 256;
		constexpr std::uint64_t kBlockSize = 64 * 1024;

		const std::uint64_t aligned = (static_cast<std::uint64_t>(size) + kAlignment - 1) & ~(kAlignment - 1);

		const bool blockIsFull = list->pushConstantBlocks.empty() || list->pushConstantOffset + aligned > kBlockSize;
		if (blockIsFull)
		{
			if (aligned > kBlockSize)
			{
				return 0;
			}

			const std::size_t next = list->pushConstantBlocks.empty() ? 0 : list->pushConstantBlock + 1;
			if (next < list->pushConstantBlocks.size())
			{
				list->pushConstantBlock = next;
			}
			else
			{
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

				NoteListAllocation(list, owned.get());
				list->pushConstantBlock = list->pushConstantBlocks.size() - 1;
			}

			list->pushConstantOffset = 0;
		}

		MTL::Buffer * block = list->pushConstantBlocks[list->pushConstantBlock].get();

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a mapped buffer is a flat run of bytes by construction.
		std::memcpy(static_cast<std::uint8_t *>(block->contents()) + list->pushConstantOffset, data, size);

		const MTL::GPUAddress address = block->gpuAddress() + list->pushConstantOffset;
		list->pushConstantOffset += aligned;
		return address;
	}

	bool Metal4CmdBegin(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr || list->allocator.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (list->lifecycle == 1)
		{
			EndActiveEncoders(list);
			list->commandBuffer->endCommandBuffer();
		}

		list->allocator->reset();
		list->commandBuffer->beginCommandBuffer(list->allocator.get());

		if (list->residency.get() != nullptr)
		{
			list->commandBuffer->useResidencySet(list->residency.get());
		}

		if (!list->debugName.empty())
		{
			list->commandBuffer->setLabel(NS::String::string(list->debugName.c_str(), NS::UTF8StringEncoding));
		}

		list->keepAlive.clear();
		list->wroteEncoderTimestamps = false;
		list->debugLabelScopes.clear();
		list->pendingProducer	= static_cast<MTL::Stages>(0);
		list->pendingConsumer	= static_cast<MTL::Stages>(0);
		list->pendingVisibility = MTL4::VisibilityOptionNone;

		list->pushConstantBlock	 = 0;
		list->pushConstantOffset = 0;

		list->boundIndexBuffer = 0;
		list->boundPrimitive   = MTL::PrimitiveTypeTriangle;
		list->boundThreadGroup = MTL::Size{ 1, 1, 1 };

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

	bool Metal4CmdEnd(void * impl, Error * error) noexcept
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

	bool Metal4CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.barriers");

		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		Flags<Stage> before;
		Flags<Stage> after;

		for (const BufferBarrier & barrier : barriers.buffers)
		{
			before |= StagesOf(barrier.before);
			after |= StagesOf(barrier.after);
		}
		for (const TextureBarrier & barrier : barriers.textures)
		{
			before |= StagesOf(barrier.before);
			after |= StagesOf(barrier.after);
		}
		for (const MemoryBarrier & barrier : barriers.memory)
		{
			before |= StagesOf(barrier.before);
			after |= StagesOf(barrier.after);
		}

		if (before.Empty() && after.Empty())
		{
			return Succeed(error);
		}

		PlaceBarrier(list, StagesFor(before), StagesFor(after), MTL4::VisibilityOptionDevice);
		return Succeed(error);
	}

	bool Metal4CmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.aliasBarriers");

		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = RecordingListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}
		if (barriers.empty())
		{
			return Succeed(error);
		}
		if (list->renderEncoder.get() != nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "aliasBarriers cannot be recorded inside a rendering scope, so record it between passes");
		}

		Metal4Device * device = object->owner;
		for (const AliasBarrier & barrier : barriers)
		{
			if ((barrier.beforeBuffer.IsValid() && device->buffers.Resolve(barrier.beforeBuffer, true) == nullptr) ||
				(barrier.afterBuffer.IsValid() && device->buffers.Resolve(barrier.afterBuffer, true) == nullptr) ||
				(barrier.beforeTexture.IsValid() && device->textures.Resolve(barrier.beforeTexture, true) == nullptr) ||
				(barrier.afterTexture.IsValid() && device->textures.Resolve(barrier.afterTexture, true) == nullptr))
			{
				return Fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
			}
		}

		constexpr auto visibility = static_cast<MTL4::VisibilityOptions>(MTL4::VisibilityOptionDevice | MTL4::VisibilityOptionResourceAlias);
		PlaceBarrier(list, MTL::StageAll, MTL::StageAll, visibility);
		return Succeed(error);
	}

	bool Metal4CmdBeginDebugLabel(void * impl, CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
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

		if (!detail::TryPushBack(list->debugLabelScopes, scope != nullptr ? list->encoderEpoch : kDebugScopeCommandBuffer))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "debug label tracking failed");
		}

		return Succeed(error);
	}

	bool Metal4CmdEndDebugLabel(void * impl, Error * error) noexcept
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

		const std::uint64_t opened = list->debugLabelScopes.back();
		list->debugLabelScopes.pop_back();

		if (opened == kDebugScopeClosed)
		{
			return Succeed(error);
		}

		if (opened != kDebugScopeCommandBuffer)
		{
			MTL4::CommandEncoder * live = list->renderEncoder.get() != nullptr ? static_cast<MTL4::CommandEncoder *>(list->renderEncoder.get())
																			   : static_cast<MTL4::CommandEncoder *>(list->computeEncoder.get());
			if (live != nullptr && opened == list->encoderEpoch)
			{
				live->popDebugGroup();
			}

			return Succeed(error);
		}

		list->commandBuffer->popDebugGroup();
		return Succeed(error);
	}

	bool Metal4CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);

		const auto * tracked = device->computePipelines.Resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr || tracked->state.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setComputePipeline names a pipeline this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->setComputePipelineState(tracked->state.get());
		list->boundThreadGroup = tracked->threadsPerThreadgroup;
		return Succeed(error);
	}

	bool Metal4CmdDispatch(void * impl, const std::uint32_t x, const std::uint32_t y, const std::uint32_t z, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = ListOf(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatch without a bound compute pipeline");
		}

		list->computeEncoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder->dispatchThreadgroups(MTL::Size::Make(x, y, z), list->boundThreadGroup);
		return Succeed(error);
	}

	bool Metal4CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "dispatchIndirect without a bound compute pipeline");
		}

		MTL::Buffer * buffer = ResolveBuffer(device, args);
		if (buffer == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect names a buffer this device never created");
		}

		list->computeEncoder->dispatchThreadgroups(buffer->gpuAddress() + offset, list->boundThreadGroup);
		return Succeed(error);
	}

	bool Metal4CmdCopyBuffer(void * impl, BufferHandle dst, const std::uint64_t dstOffset, BufferHandle src, const std::uint64_t srcOffset,
		const std::uint64_t size, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Buffer * destination = ResolveBuffer(device, dst);
		MTL::Buffer * source	  = ResolveBuffer(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->copyFromBuffer(source, srcOffset, destination, dstOffset, size);
		return Succeed(error);
	}

	bool Metal4CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBufferToTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
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

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		for (const BufferTextureCopy & region : regions)
		{
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

	bool Metal4CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTextureToBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
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

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
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

	bool Metal4CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * destination = ResolveTexture(device, dst);
		MTL::Texture * source	   = ResolveTexture(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "copyTexture names a texture this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
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

	bool Metal4CmdClearBuffer(
		void * impl, BufferHandle buffer, const std::uint64_t offset, const std::uint64_t size, const std::uint32_t value, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
		if (list == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * destination = ResolveBuffer(device, buffer);
		if (destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "clearBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
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

		NoteListAllocation(list, staging.get());

		encoder->copyFromBuffer(staging.get(), 0, destination, offset, wordCount * 4);

		if (!detail::TryPushBack(list->keepAlive, staging))
		{
			return Fail(error, ErrorCode::eOutOfHostMemory, "clear staging buffer tracking failed");
		}

		return Succeed(error);
	}

	bool Metal4CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
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

		if (!slot->usage.Contains(TextureUsage::eColorAttachment))
		{
			return Fail(error, ErrorCode::eInvalidArgument, "clearTexture needs a texture usable as a color attachment, which is what Metal clears through");
		}

		EndActiveEncoders(list);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureSubresourceRange & range : ranges)
		{
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

					FlushPendingBarrier(list, encoder);
					encoder->endEncoding();
				}
			}
		}

		return Succeed(error);
	}

	bool Metal4CmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.generateMips");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * tex = ResolveTexture(device, texture);
		if (tex == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "generateMips names a texture this device never created");
		}

		if (tex->mipmapLevelCount() <= 1)
		{
			return Succeed(error);
		}

		const Metal4TextureSlot * slot = device->textures.Resolve(texture, kHandleAlreadyChecked);
		if (slot != nullptr && (IsCompressedFormat(slot->format) || IsIntegerFormat(slot->format) || IsDepthFormat(slot->format)))
		{
			return Fail(
				error, ErrorCode::eUnsupportedFeature, "generateMips needs a linear-filterable, renderable format (not block-compressed, integer, or depth)");
		}

		MTL4::ComputeCommandEncoder * encoder = BeginCompute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->generateMipmaps(tex);
		return Succeed(error);
	}

	bool Metal4CmdBlit(void * impl, TextureHandle, TextureHandle, std::span<const TextureBlit>, Filter, Error * error) noexcept
	{
		static_cast<void>(impl);
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal has no scaled blit, so resampling goes through the utility target's compute path");
	}

	bool Metal4CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.resolveTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);
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
			const bool wholeSlice = region.srcOffset.x == 0 && region.srcOffset.y == 0 && region.srcOffset.z == 0 && region.dstOffset.x == 0 &&
									region.dstOffset.y == 0 && region.dstOffset.z == 0 &&
									region.extent.width == static_cast<std::uint32_t>(source->width() >> region.srcSubresource.mip) &&
									region.extent.height == static_cast<std::uint32_t>(source->height() >> region.srcSubresource.mip);
			if (!wholeSlice)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "Metal resolveTexture resolves a whole subresource, not a sub-rectangle");
			}

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

			FlushPendingBarrier(list, encoder);
			encoder->endEncoding();
		}

		return Succeed(error);
	}

}

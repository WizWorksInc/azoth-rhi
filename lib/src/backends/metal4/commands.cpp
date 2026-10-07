// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSRange.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTL4CommandEncoder.hpp>
#include <Metal/MTL4ComputeCommandEncoder.hpp>
#include <Metal/MTL4RenderCommandEncoder.hpp>
#include <Metal/MTL4RenderPass.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLGPUAddress.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPass.hpp>
#include <Metal/MTLResource.hpp>
#include <Metal/MTLTexture.hpp>
#include <Metal/MTLTypes.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace azo::rhi::metal4
{
	static constexpr MTL::Stages stages_for(const Flags<Stage> stages) noexcept
	{
		if (stages.contains(Stage::eAllCommands) || stages.contains(Stage::eAllGraphics))
		{
			return MTL::StageAll;
		}

		NS::UInteger out = 0;

		if (stages.contains(Stage::eVertexWork) || stages.contains(Stage::eIndirectFetch))
		{
			out |= MTL::StageVertex;
		}

		if (stages.contains(Stage::eCompute) || stages.contains(Stage::eIndirectFetch))
		{
			out |= MTL::StageDispatch;
		}

		if (stages.contains(Stage::eFragmentShading) || stages.contains(Stage::eDepthStencil) || stages.contains(Stage::eColorOutput))
		{
			out |= MTL::StageFragment;
		}

		if (stages.contains(Stage::eCopy))
		{
			out |= MTL::StageBlit;
		}

		if (stages.contains(Stage::eCopy) || stages.contains(Stage::eResolve))
		{
			out |= MTL::StageFragment;
		}

		if (stages.contains(Stage::eAccelBuild))
		{
			out |= MTL::StageAccelerationStructure;
		}

		if (stages.contains(Stage::eRayTracing))
		{
			out |= MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch;
		}

		return out != 0 ? static_cast<MTL::Stages>(out) : MTL::StageAll;
	}

	namespace
	{
		[[nodiscard]] constexpr Flags<Stage> stages_of(const ResourceState & state) noexcept
		{
			if (!state.stages.empty())
			{
				return state.stages;
			}

			Flags<Stage> out{};

			if (state.use.contains(ResourceUse::eIndirectArgs))
			{
				out |= Stage::eIndirectFetch;
			}
			if (state.use.contains(ResourceUse::eVertexBuffer) || state.use.contains(ResourceUse::eIndexBuffer))
			{
				out |= Stage::eVertexWork;
			}

			if (state.use.contains(ResourceUse::eUniformRead) || state.use.contains(ResourceUse::eSampledRead) ||
				state.use.contains(ResourceUse::eStorageRead) || state.use.contains(ResourceUse::eStorageWrite))
			{
				out |= Stage::eAllCommands;
			}

			if (state.use.contains(ResourceUse::eColorTarget))
			{
				out |= Stage::eColorOutput;
			}
			if (state.use.contains(ResourceUse::eDepthStencilTarget) || state.use.contains(ResourceUse::eDepthStencilRead))
			{
				out |= Stage::eDepthStencil;
			}
			if (state.use.contains(ResourceUse::eCopySrc) || state.use.contains(ResourceUse::eCopyDst))
			{
				out |= Stage::eCopy;
			}
			if (state.use.contains(ResourceUse::eResolveSrc) || state.use.contains(ResourceUse::eResolveDst))
			{
				out |= Stage::eResolve;
			}
			if (state.use.contains(ResourceUse::eHostRead) || state.use.contains(ResourceUse::eHostWrite))
			{
				out |= Stage::eHost;
			}
			if (state.use.contains(ResourceUse::eAccelBuildInput) || state.use.contains(ResourceUse::eAccelWrite) ||
				state.use.contains(ResourceUse::eAccelBuildScratch))
			{
				out |= Stage::eAccelBuild;
			}
			if (state.use.contains(ResourceUse::eAccelRead))
			{
				out |= Flags<Stage>(Stage::eAccelBuild) | Stage::eRayTracing;
			}

			return out;
		}

		static_assert(
			stages_for(Stage::eIndirectFetch) == static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageDispatch),
			"an indirect dispatch fetches its arguments on the dispatch stage, so naming vertex alone leaves the fetch unordered against the write that filled "
			"the argument buffer"
		);

		static_assert(
			stages_for(stages_of(ResourceState{ .use = ResourceUse::eAccelRead })) ==
				static_cast<MTL::Stages>(MTL::StageAccelerationStructure | MTL::StageVertex | MTL::StageFragment | MTL::StageDispatch),
			"reading an acceleration structure happens in the shaders that trace as well as in a refit, so the build stage alone never orders the trace"
		);

		static_assert(
			(static_cast<NS::UInteger>(stages_for(Stage::eRayTracing)) & MTL::StageAccelerationStructure) == 0,
			"Apple's acceleration structure stage is where a build runs and not where a tracing shader runs, which is what eAccelBuild names instead"
		);

		static_assert(
			stages_for(Flags<Stage>{}) == MTL::StageAll,
			"an empty mask has to widen to everything, since PlaceBarrier holds this value and FlushPending reads a zero consumer as nothing pending"
		);

		static_assert(
			stages_for(stages_of(ResourceState{ .use = ResourceUse::eAccelBuildScratch })) == MTL::StageAccelerationStructure,
			"a build scratch is touched by the build and by nothing else here, so it names the one stage that runs a build and never widens to everything"
		);

		constexpr MTL::Stages kRenderEncoderStages	 = static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageFragment);
		constexpr MTL::Stages kRenderWaitableStages	 = MTL::StageVertex;
		constexpr MTL::Stages kComputeEncoderStages	 = static_cast<MTL::Stages>(MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure);
		constexpr MTL::Stages kComputeWaitableStages = kComputeEncoderStages;

		[[nodiscard]] constexpr MTL::Stages intersect(const MTL::Stages stages, const MTL::Stages mask) noexcept
		{
			return static_cast<MTL::Stages>(static_cast<NS::UInteger>(stages) & static_cast<NS::UInteger>(mask));
		}

		template <typename EncoderT>
		void record_barrier(
			EncoderT * encoder,
			const MTL::Stages waitable,
			const MTL::Stages runnable,
			const MTL::Stages producer,
			const MTL::Stages consumer,
			const MTL4::VisibilityOptions visibility
		) noexcept
		{
			encoder->barrierAfterStages(producer, consumer, visibility);

			const MTL::Stages after	 = intersect(producer, waitable);
			const MTL::Stages before = intersect(consumer, runnable);
			if (static_cast<NS::UInteger>(after) != 0 && static_cast<NS::UInteger>(before) != 0)
			{
				encoder->barrierAfterEncoderStages(after, before, visibility);
			}
		}

		template <typename EncoderT>
		void flush_pending(CmdList * list, EncoderT * encoder) noexcept
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

		void place_barrier(CmdList * list, const MTL::Stages producer, const MTL::Stages consumer, const MTL4::VisibilityOptions visibility) noexcept
		{
			if (list->renderEncoder.get() != nullptr)
			{
				record_barrier(list->renderEncoder.get(), kRenderWaitableStages, kRenderEncoderStages, producer, consumer, visibility);
			}
			else if (list->computeEncoder.get() != nullptr)
			{
				record_barrier(list->computeEncoder.get(), kComputeWaitableStages, kComputeEncoderStages, producer, consumer, visibility);
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

	void flush_pending_barrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept
	{
		flush_pending(list, encoder);
	}

	void flush_pending_barrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept
	{
		flush_pending(list, encoder);
	}

	void pop_encoder_debug_groups(CmdList * list, MTL4::CommandEncoder * encoder) noexcept
	{
		if (encoder == nullptr)
		{
			return;
		}

		for (std::size_t index = list->debugLabelScopes.size(); index-- > 0;)
		{
			if (azo::rhi::detail::at(list->debugLabelScopes, index) != list->encoderEpoch)
			{
				break;
			}

			encoder->popDebugGroup();
			azo::rhi::detail::at(list->debugLabelScopes, index) = kDebugScopeClosed;
		}
	}

	void end_active_encoders(CmdList * list) noexcept
	{
		if (list->renderEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(list, list->renderEncoder.get());
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->renderEncoder->updateFence(list->timestampFence.get(), kRenderEncoderStages);
			}

			list->renderEncoder->endEncoding();
			list->renderEncoder.reset();
		}
		if (list->computeEncoder.get() != nullptr)
		{
			pop_encoder_debug_groups(list, list->computeEncoder.get());
			if (list->wroteEncoderTimestamps && list->timestampFence.get() != nullptr)
			{
				list->computeEncoder->updateFence(list->timestampFence.get(), kComputeEncoderStages);
			}

			list->computeEncoder->endEncoding();
			list->computeEncoder.reset();
		}
	}

	MTL4::ComputeCommandEncoder * begin_compute(Metal4Object * object, Error * error) noexcept
	{
		CmdList * list = recording_list_of(object);
		if (list == nullptr)
		{
			return fail_value<MTL4::ComputeCommandEncoder *>(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (list->computeEncoder.get() != nullptr)
		{
			return list->computeEncoder.get();
		}

		if (list->renderEncoder.get() != nullptr)
		{
			return fail_value<MTL4::ComputeCommandEncoder *>(
				error,
				ErrorCode::eInvalidState,
				"a transfer or compute command cannot be recorded inside a rendering scope, so record it between passes"
			);
		}

		MTL4::ComputeCommandEncoder * encoder = list->commandBuffer->computeCommandEncoder();
		if (encoder == nullptr)
		{
			return fail_value<MTL4::ComputeCommandEncoder *>(error, ErrorCode::eNativeApiError, "Metal 4 compute command encoder creation failed");
		}

		encoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder = NS::RetainPtr(encoder);
		++list->encoderEpoch;
		flush_pending_barrier(list, encoder);
		return encoder;
	}

	MTL::GPUAddress write_push_constants(Metal4Device * device, CmdList * list, const void * data, const std::uint32_t size) noexcept
	{
		constexpr std::uint64_t kAlignment = 256;
		constexpr std::uint64_t kBlockSize = static_cast<const std::uint64_t>(64 * 1024);

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
				if (!detail::try_push_back(list->pushConstantBlocks, owned))
				{
					return 0;
				}

				note_list_allocation(list, owned.get());
				list->pushConstantBlock = list->pushConstantBlocks.size() - 1;
			}

			list->pushConstantOffset = 0;
		}

		MTL::Buffer * block = azo::rhi::detail::at(list->pushConstantBlocks, list->pushConstantBlock).get();

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a mapped buffer is a flat run of bytes by construction.
		std::memcpy(static_cast<std::uint8_t *>(block->contents()) + list->pushConstantOffset, data, size);

		const MTL::GPUAddress address = block->gpuAddress() + list->pushConstantOffset;
		list->pushConstantOffset += aligned;
		return address;
	}

	bool metal4_cmd_begin(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = list_of(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr || list->allocator.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		if (list->lifecycle == ListLifecycle::eRecording)
		{
			end_active_encoders(list);
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

		list->lifecycle = ListLifecycle::eRecording;
		return succeed(error);
	}

	bool metal4_cmd_end(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = list_of(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		end_active_encoders(list);
		list->commandBuffer->endCommandBuffer();

		list->lifecycle = ListLifecycle::eEnded;
		return succeed(error);
	}

	bool metal4_cmd_barriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.barriers");

		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = recording_list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		Flags<Stage> before;
		Flags<Stage> after;

		for (const BufferBarrier & barrier : barriers.buffers)
		{
			before |= stages_of(barrier.before);
			after |= stages_of(barrier.after);
		}
		for (const TextureBarrier & barrier : barriers.textures)
		{
			before |= stages_of(barrier.before);
			after |= stages_of(barrier.after);
		}
		for (const MemoryBarrier & barrier : barriers.memory)
		{
			before |= stages_of(barrier.before);
			after |= stages_of(barrier.after);
		}

		if (before.empty() && after.empty())
		{
			return succeed(error);
		}

		place_barrier(list, stages_for(before), stages_for(after), MTL4::VisibilityOptionDevice);
		return succeed(error);
	}

	bool metal4_cmd_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.aliasBarriers");

		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = recording_list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}
		if (barriers.empty())
		{
			return succeed(error);
		}
		if (list->renderEncoder.get() != nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "aliasBarriers cannot be recorded inside a rendering scope, so record it between passes");
		}

		Metal4Device * device = object->owner;
		for (const AliasBarrier & barrier : barriers)
		{
			if ((barrier.beforeBuffer.is_valid() && device->buffers.resolve(barrier.beforeBuffer, true) == nullptr) ||
				(barrier.afterBuffer.is_valid() && device->buffers.resolve(barrier.afterBuffer, true) == nullptr) ||
				(barrier.beforeTexture.is_valid() && device->textures.resolve(barrier.beforeTexture, true) == nullptr) ||
				(barrier.afterTexture.is_valid() && device->textures.resolve(barrier.afterTexture, true) == nullptr))
			{
				return fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
			}
		}

		constexpr auto kVisibility = static_cast<MTL4::VisibilityOptions>(MTL4::VisibilityOptionDevice | MTL4::VisibilityOptionResourceAlias);
		place_barrier(list, MTL::StageAll, MTL::StageAll, kVisibility);
		return succeed(error);
	}

	bool metal4_cmd_begin_debug_label(void * impl, CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = recording_list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (!object->owner->debugLabels)
		{
			return succeed(error);
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

		if (!detail::try_push_back(list->debugLabelScopes, scope != nullptr ? list->encoderEpoch : kDebugScopeCommandBuffer))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "debug label tracking failed");
		}

		return succeed(error);
	}

	bool metal4_cmd_end_debug_label(void * impl, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = recording_list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command recorded on a list that is not open for recording");
		}

		if (!object->owner->debugLabels || list->debugLabelScopes.empty())
		{
			return succeed(error);
		}

		const std::uint64_t opened = list->debugLabelScopes.back();
		list->debugLabelScopes.pop_back();

		if (opened == kDebugScopeClosed)
		{
			return succeed(error);
		}

		if (opened != kDebugScopeCommandBuffer)
		{
			MTL4::CommandEncoder * live = list->renderEncoder.get() != nullptr ? static_cast<MTL4::CommandEncoder *>(list->renderEncoder.get())
																			   : static_cast<MTL4::CommandEncoder *>(list->computeEncoder.get());
			if (live != nullptr && opened == list->encoderEpoch)
			{
				live->popDebugGroup();
			}

			return succeed(error);
		}

		list->commandBuffer->popDebugGroup();
		return succeed(error);
	}

	bool metal4_cmd_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);

		const auto * tracked = device->computePipelines.resolve(pipeline, kHandleAlreadyChecked);
		if (tracked == nullptr || tracked->state.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "setComputePipeline names a pipeline this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->setComputePipelineState(tracked->state.get());
		list->boundThreadGroup = tracked->threadsPerThreadgroup;
		return succeed(error);
	}

	bool metal4_cmd_dispatch(void * impl, const std::uint32_t x, const std::uint32_t y, const std::uint32_t z, Error * error) noexcept
	{
		auto * object  = static_cast<Metal4Object *>(impl);
		CmdList * list = list_of(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "dispatch without a bound compute pipeline");
		}
		if (x == 0 || y == 0 || z == 0)
		{
			return succeed(error);
		}

		list->computeEncoder->setArgumentTable(list->argumentTable.get());
		list->computeEncoder->dispatchThreadgroups(MTL::Size::Make(x, y, z), list->boundThreadGroup);
		return succeed(error);
	}

	bool metal4_cmd_dispatch_indirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->computeEncoder.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "dispatchIndirect without a bound compute pipeline");
		}

		MTL::Buffer * buffer = resolve_buffer(device, args);
		if (buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "dispatchIndirect names a buffer this device never created");
		}

		list->computeEncoder->dispatchThreadgroups(buffer->gpuAddress() + offset, list->boundThreadGroup);
		return succeed(error);
	}

	bool metal4_cmd_copy_buffer(
		void * impl,
		BufferHandle dst,
		const std::uint64_t dstOffset,
		BufferHandle src,
		const std::uint64_t srcOffset,
		const std::uint64_t size,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Buffer * destination = resolve_buffer(device, dst);
		MTL::Buffer * source	  = resolve_buffer(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->copyFromBuffer(source, srcOffset, destination, dstOffset, size);
		return succeed(error);
	}

	bool metal4_cmd_copy_buffer_to_texture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyBufferToTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * texture = resolve_texture(device, dst);
		MTL::Buffer * buffer   = resolve_buffer(device, src);
		if (texture == nullptr || buffer == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyBufferToTexture names a resource this device never created");
		}

		const Format format = resolve_texture_format(device, dst);
		if (!detail::has_linear_layout(format))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "copyBufferToTexture on a combined depth-stencil format, whose aspects copy separately");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		for (const BufferTextureCopy & region : regions)
		{
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const auto bytesPerRow			 = static_cast<NS::UInteger>(detail::tight_row_pitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::block_rows(format, imageRows);

			encoder->copyFromBuffer(
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage,
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)
				)
			);
		}

		return succeed(error);
	}

	bool metal4_cmd_copy_texture_to_buffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTextureToBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Buffer * buffer   = resolve_buffer(device, dst);
		MTL::Texture * texture = resolve_texture(device, src);
		if (buffer == nullptr || texture == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTextureToBuffer names a resource this device never created");
		}

		const Format format = resolve_texture_format(device, src);
		if (!detail::has_linear_layout(format))
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "copyTextureToBuffer on a combined depth-stencil format, whose aspects copy separately");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		for (const BufferTextureCopy & region : regions)
		{
			const std::uint32_t rowTexels	 = region.bufferRowLength != 0 ? region.bufferRowLength : region.textureExtent.width;
			const std::uint32_t imageRows	 = region.bufferImageHeight != 0 ? region.bufferImageHeight : region.textureExtent.height;
			const auto bytesPerRow			 = static_cast<NS::UInteger>(detail::tight_row_pitch(format, rowTexels));
			const NS::UInteger bytesPerImage = bytesPerRow * detail::block_rows(format, imageRows);

			encoder->copyFromTexture(
				texture,
				region.subresource.layer,
				region.subresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.textureOffset.x),
					static_cast<NS::UInteger>(region.textureOffset.y),
					static_cast<NS::UInteger>(region.textureOffset.z)
				),
				MTL::Size::Make(region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth),
				buffer,
				region.bufferOffset,
				bytesPerRow,
				bytesPerImage
			);
		}

		return succeed(error);
	}

	bool metal4_cmd_copy_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.copyTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * destination = resolve_texture(device, dst);
		MTL::Texture * source	   = resolve_texture(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "copyTexture names a texture this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		for (const TextureCopy & region : regions)
		{
			encoder->copyFromTexture(
				source,
				region.srcSubresource.layer,
				region.srcSubresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.srcOffset.x),
					static_cast<NS::UInteger>(region.srcOffset.y),
					static_cast<NS::UInteger>(region.srcOffset.z)
				),
				MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth),
				destination,
				region.dstSubresource.layer,
				region.dstSubresource.mip,
				MTL::Origin::Make(
					static_cast<NS::UInteger>(region.dstOffset.x),
					static_cast<NS::UInteger>(region.dstOffset.y),
					static_cast<NS::UInteger>(region.dstOffset.z)
				)
			);
		}

		return succeed(error);
	}

	bool metal4_cmd_clear_buffer(
		void * impl,
		BufferHandle buffer,
		const std::uint64_t offset,
		const std::uint64_t size,
		const std::uint32_t value,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearBuffer");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Buffer * destination = resolve_buffer(device, buffer);
		if (destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearBuffer names a buffer this device never created");
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		const auto byte = static_cast<std::uint8_t>(value & 0xFFu);
		if (value == (static_cast<std::uint32_t>(byte) * 0x01010101u))
		{
			encoder->fillBuffer(destination, NS::Range::Make(offset, size), byte);
			return succeed(error);
		}

		NS::SharedPtr<MTL::Buffer> staging = NS::TransferPtr(device->device->newBuffer(size, MTL::ResourceStorageModeShared));
		if (staging.get() == nullptr)
		{
			return fail(error, ErrorCode::eOutOfDeviceMemory, "Metal 4 clear staging buffer allocation failed");
		}

		auto * words				  = static_cast<std::uint32_t *>(staging->contents());
		const std::uint64_t wordCount = size / 4;
		for (std::uint64_t i = 0; i < wordCount; ++i)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a mapped buffer is a flat run of words.
			words[i] = value;
		}

		note_list_allocation(list, staging.get());

		encoder->copyFromBuffer(staging.get(), 0, destination, offset, wordCount * 4);

		if (!detail::try_push_back(list->keepAlive, staging))
		{
			return fail(error, ErrorCode::eOutOfHostMemory, "clear staging buffer tracking failed");
		}

		return succeed(error);
	}

	bool metal4_cmd_clear_texture(
		void * impl,
		TextureHandle texture,
		const ClearColor & color,
		std::span<const TextureSubresourceRange> ranges,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.clearTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const Metal4TextureSlot * slot = device->textures.resolve(texture, kHandleAlreadyChecked);
		if (slot == nullptr || slot->texture.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "clearTexture names a texture this device never created");
		}

		MTL::Texture * tex = slot->texture.get();

		if (!slot->usage.contains(TextureUsage::eColorAttachment))
		{
			return fail(error, ErrorCode::eInvalidArgument, "clearTexture needs a texture usable as a color attachment, which is what Metal clears through");
		}

		end_active_encoders(list);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureSubresourceRange & range : ranges)
		{
			if (range.aspects.contains(TextureAspect::eDepth) || range.aspects.contains(TextureAspect::eStencil))
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "Metal clearTexture clears color aspects only");
			}

			const auto textureMips	 = static_cast<std::uint32_t>(tex->mipmapLevelCount());
			const auto textureLayers = static_cast<std::uint32_t>(tex->arrayLength());
			if (range.baseMip >= textureMips || range.baseLayer >= textureLayers)
			{
				return fail(error, ErrorCode::eInvalidArgument, "clearTexture range starts past the end of the texture");
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
						return fail(error, ErrorCode::eNativeApiError, "Metal 4 clear render command encoder creation failed");
					}

					flush_pending_barrier(list, encoder);
					encoder->endEncoding();
				}
			}
		}

		return succeed(error);
	}

	bool metal4_cmd_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.generateMips");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;

		MTL::Texture * tex = resolve_texture(device, texture);
		if (tex == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "generateMips names a texture this device never created");
		}

		if (tex->mipmapLevelCount() <= 1)
		{
			return succeed(error);
		}

		const Metal4TextureSlot * slot = device->textures.resolve(texture, kHandleAlreadyChecked);
		if (slot != nullptr && (is_compressed_format(slot->format) || is_integer_format(slot->format) || is_depth_format(slot->format)))
		{
			return fail(
				error,
				ErrorCode::eUnsupportedFeature,
				"generateMips needs a linear-filterable, renderable format (not block-compressed, integer, or depth)"
			);
		}

		MTL4::ComputeCommandEncoder * encoder = begin_compute(object, error);
		if (encoder == nullptr)
		{
			return false;
		}

		encoder->generateMipmaps(tex);
		return succeed(error);
	}

	bool metal4_cmd_blit(
		void * impl,
		TextureHandle /*unused*/,
		TextureHandle /*unused*/,
		std::span<const TextureBlit> /*unused*/,
		Filter /*unused*/,
		Error * error
	) noexcept
	{
		static_cast<void>(impl);
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal has no scaled blit, so resampling goes through the utility target's compute path");
	}

	bool metal4_cmd_resolve_texture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.resolveTexture");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MTL::Texture * destination = resolve_texture(device, dst);
		MTL::Texture * source	   = resolve_texture(device, src);
		if (destination == nullptr || source == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveTexture names a texture this device never created");
		}

		end_active_encoders(list);
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		for (const TextureResolve & region : regions)
		{
			const bool wholeSlice = region.srcOffset.x == 0 && region.srcOffset.y == 0 && region.srcOffset.z == 0 && region.dstOffset.x == 0 &&
									region.dstOffset.y == 0 && region.dstOffset.z == 0 &&
									region.extent.width == static_cast<std::uint32_t>(source->width() >> region.srcSubresource.mip) &&
									region.extent.height == static_cast<std::uint32_t>(source->height() >> region.srcSubresource.mip);
			if (!wholeSlice)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "Metal resolveTexture resolves a whole subresource, not a sub-rectangle");
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
				return fail(error, ErrorCode::eNativeApiError, "Metal 4 resolve render command encoder creation failed");
			}

			flush_pending_barrier(list, encoder);
			encoder->endEncoding();
		}

		return succeed(error);
	}

}

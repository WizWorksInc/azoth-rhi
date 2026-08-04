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
	[[nodiscard]] Metal4QueryPool * ResolveQueryPool(Metal4Device * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPools.Resolve(handle, kHandleAlreadyChecked);
	}

	/*
	 * A query pool, which on this generation is a counter heap.
	 *
	 * The same restriction as the other generation, for the same reason: Metal counts visible samples through a visibility result buffer named on the render
	 * pass, and pipeline statistics not at all, so both are refused, not approximated. What did change is that a heap needs no counter set probing, the
	 * type being named on the descriptor.
	 */
	QueryPoolHandle Metal4CreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createQueryPool");

		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.type != QueryType::eTimestamp)
		{
			return FailValue<QueryPoolHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp query pools only, and this pool asked for another type");
		}
		if (desc.queryCount == 0)
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool			  = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		const NS::SharedPtr<MTL4::CounterHeapDescriptor> heapDesc = NS::TransferPtr(MTL4::CounterHeapDescriptor::alloc()->init());
		heapDesc->setType(MTL4::CounterHeapTypeTimestamp);
		heapDesc->setCount(desc.queryCount);

		NS::Error * heapError	 = nullptr;
		MTL4::CounterHeap * heap = device->device->newCounterHeap(heapDesc.get(), &heapError);
		if (heap == nullptr)
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "Metal 4 counter heap creation failed");
		}

		NS::SharedPtr<MTL4::CounterHeap> owned = NS::TransferPtr(heap);
		if (desc.debugName != nullptr)
		{
			owned->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}

		const QueryPoolHandle handle = device->queryPools.Store(Metal4QueryPool{
			.heap		= std::move(owned),
			.type		= desc.type,
			.queryCount = desc.queryCount,
		});
		if (!handle.IsValid())
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 query pool tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool Metal4CalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "calibrateTimestamp needs somewhere to write the result");
		}
		if (!device->caps.supportsTimestampCalibration)
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "this Metal adapter samples no counters, so its clocks cannot be correlated");
		}

		MTL::Timestamp cpu = 0;
		MTL::Timestamp gpu = 0;
		device->device->sampleTimestamps(&cpu, &gpu);

		out->queueType				 = queueType;
		out->gpuTimestamp			 = cpu != 0 || gpu != 0 ? gpu : 0;
		out->cpuTimestampNanoseconds = cpu;
		out->gpuPeriodNanoseconds	 = device->caps.timestampPeriodNanoseconds;
		out->calibrated				 = cpu != 0 || gpu != 0;
		return Succeed(error);
	}

	/*
	 * Metal has no reset. A sample buffer slot is written by whatever samples into it and carries MTLCounterErrorValue until something does, so there is no
	 * stale-result window for a reset to close and nothing for this to lower to.
	 *
	 * The handle is still resolved, because succeeding on a pool this device never created would hide the caller's mistake until the resolve.
	 */
	/*
	 * Invalidating the range, which is how a counter heap says a slot holds nothing.
	 *
	 * Without it a slot never written this frame still reads whatever the last frame left, and a caller that resolves more slots than it wrote gets a stale
	 * time instead of a sentinel. Invalidate is exactly the "these hold nothing" the RHI's reset means.
	 */
	bool Metal4CmdResetQueryPool(void * impl, QueryPoolHandle pool, const std::uint32_t firstQuery, const std::uint32_t queryCount, Error * error) noexcept
	{
		auto * object			  = static_cast<Metal4Object *>(impl);
		Metal4QueryPool * tracked = ResolveQueryPool(object->owner, pool);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resetQueryPool names a query pool this device never created");
		}
		// Subtracted and not added, the sum of two counts a caller chooses being free to wrap and let an out-of-range range through.
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}

		tracked->heap->invalidateCounterRange(NS::Range::Make(firstQuery, queryCount));
		return Succeed(error);
	}

	/*
	 * A timestamp, written wherever the caller asked for one.
	 *
	 * This is the difference that made the generation worth adopting. Metal 3 refuses a write inside a dispatch scope on an adapter that samples only at stage
	 * boundaries, which is every Apple part measured so far, and has no way to close a compute encoder to get around it. Here the encoder takes the sample
	 * itself, so a dispatch can be bracketed as tightly as a caller wants.
	 */
	bool Metal4CmdWriteTimestamp(void * impl, QueryPoolHandle pool, const std::uint32_t query, Flags<PipelineStage>, Error * error) noexcept
	{
		auto * object			  = static_cast<Metal4Object *>(impl);
		CmdList * list			  = ListOf(object);
		Metal4QueryPool * tracked = ResolveQueryPool(object->owner, pool);
		if (tracked == nullptr || tracked->heap.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "writeTimestamp names a query pool this device never created");
		}
		if (query >= tracked->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		// Any encoder write has to be ordered against a later resolve, and the fence for that is made here so EndActiveEncoders has one to update whenever an
		// encoder closes.
		if (list->renderEncoder.get() != nullptr || list->computeEncoder.get() != nullptr)
		{
			if (list->timestampFence.get() == nullptr)
			{
				list->timestampFence = NS::TransferPtr(object->owner->device->newFence());
				if (list->timestampFence.get() == nullptr)
				{
					return Fail(error, ErrorCode::eNativeApiError, "Metal 4 timestamp fence allocation failed");
				}
			}

			list->wroteEncoderTimestamps = true;
		}

		// Precise, since relaxed may sample only at encoder boundaries, which is the resolution this exists to improve on.
		if (list->renderEncoder.get() != nullptr)
		{
			list->renderEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, MTL::RenderStageFragment, tracked->heap.get(), query);
			return Succeed(error);
		}
		if (list->computeEncoder.get() != nullptr)
		{
			list->computeEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, tracked->heap.get(), query);
			return Succeed(error);
		}

		// Between scopes the command buffer takes it, which captures the point every command before it finished.
		list->commandBuffer->writeTimestampIntoHeap(tracked->heap.get(), query);
		return Succeed(error);
	}

	/*
	 * Refused, as on the other generation. Metal counts visible samples through a visibility result buffer named on the render pass descriptor, which is a
	 * different object reached a different way, and a pool a caller brought here has nothing to do with it.
	 */
	bool Metal4CmdBeginQuery(void * impl, QueryPoolHandle, std::uint32_t, Error * error) noexcept
	{
		static_cast<void>(impl);
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and a scoped query is not one");
	}

	bool Metal4CmdEndQuery(void * impl, QueryPoolHandle, std::uint32_t, Error * error) noexcept
	{
		static_cast<void>(impl);
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and a scoped query is not one");
	}

	/*
	 * Resolving into a buffer the caller owns.
	 *
	 * The destination layout is one 64 bit value per query, packed, which is what the RHI documents and what a timestamp heap entry already is. The two fences
	 * are this generation's way of ordering the resolve against the writes it reads and against whatever reads the buffer afterwards, where the other
	 * generation gets that ordering from the blit encoder sitting in command order.
	 */
	bool Metal4CmdResolveQueryData(void * impl, QueryPoolHandle pool, const std::uint32_t firstQuery, const std::uint32_t queryCount, BufferHandle dst,
		const std::uint64_t dstOffset, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.resolveQueryData");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = ListOf(object);

		Metal4QueryPool * tracked = ResolveQueryPool(device, pool);
		MTL::Buffer * destination = ResolveBuffer(device, dst);
		if (tracked == nullptr || tracked->heap.get() == nullptr || destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveQueryData names a handle this device never created");
		}
		// Subtracted and not added, the sum of two counts a caller chooses being free to wrap and let an out-of-range range through.
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		/*
		 * The resolve has to be ordered against the writes it reads, and command order does not do that here: a resolve runs on the blit stage while the writes
		 * happened on a render or compute encoder, and Metal 4 orders those only through a fence.
		 *
		 * So whatever encoder is still open updates the fence before it closes, and the resolve waits on it. A list with nothing open needs no fence, which is
		 * why this is conditional.
		 */
		// Whatever is still open signs off on its timestamps as it closes, which is where the fence gets updated.
		EndActiveEncoders(list);

		// Waited on only when an encoder actually took a sample. A list whose writes all went to the command buffer is already ordered by sitting in command
		// order and needs nothing.
		MTL::Fence * waitFence = list->wroteEncoderTimestamps ? list->timestampFence.get() : nullptr;

		const std::uint64_t entrySize = device->device->sizeOfCounterHeapEntry(MTL4::CounterHeapTypeTimestamp);
		const std::uint64_t bytes	  = entrySize * queryCount;

		/*
		 * Bounded here because a resolve names a raw address and a length on this generation, where the other hands over the buffer object and lets Metal bound
		 * it. Nothing else stands between a destination offset the caller chose and a GPU write past the end of the allocation.
		 */
		if (dstOffset > destination->length() || bytes > destination->length() - dstOffset)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resolveQueryData writes past the end of the destination buffer");
		}

		const MTL4::BufferRange range = MTL4::BufferRange::Make(destination->gpuAddress() + dstOffset, bytes);

		// No update fence: what reads the destination afterwards is ordered by the caller's own barrier, which is the same contract the other generation works
		// under.
		list->commandBuffer->resolveCounterHeap(tracked->heap.get(), NS::Range::Make(firstQuery, queryCount), range, waitFence, nullptr);
		return Succeed(error);
	}

} // namespace azo::rhi::metal4

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

#include "azoth/rhi/resources/query.hpp"

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSRange.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Metal/MTL4Counters.hpp>
#include <Metal/MTLAccelerationStructureTypes.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLFence.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>

#include <cstdint> // NOLINT
#include <utility>

namespace azo::rhi::metal4
{
	[[nodiscard]] Metal4QueryPool * resolve_query_pool(Metal4Device * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPools.resolve(handle, kHandleAlreadyChecked);
	}

	QueryPoolHandle metal4_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createQueryPool");

		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.type != QueryType::eTimestamp)
		{
			return fail_value<QueryPoolHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal implements timestamp query pools only, and this pool asked for another type"
			);
		}
		if (desc.queryCount == 0)
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool			  = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		const NS::SharedPtr<MTL4::CounterHeapDescriptor> heapDesc = NS::TransferPtr(MTL4::CounterHeapDescriptor::alloc()->init());
		heapDesc->setType(MTL4::CounterHeapTypeTimestamp);
		heapDesc->setCount(desc.queryCount);

		NS::Error * heapError	 = nullptr;
		MTL4::CounterHeap * heap = device->device->newCounterHeap(heapDesc.get(), &heapError);
		if (heap == nullptr)
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "Metal 4 counter heap creation failed");
		}

		NS::SharedPtr<MTL4::CounterHeap> owned = NS::TransferPtr(heap);
		if (desc.debugName != nullptr)
		{
			owned->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}

		const QueryPoolHandle handle = device->queryPools.store(
			Metal4QueryPool{
				.heap		= std::move(owned),
				.type		= desc.type,
				.queryCount = desc.queryCount,
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 query pool tracking failed");
		}

		return return_value(handle, error);
	}

	bool metal4_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
	{
		auto * device = static_cast<Metal4Device *>(impl);
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "calibrateTimestamp needs somewhere to write the result");
		}
		if (!device->caps.supportsTimestampCalibration)
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "this Metal adapter samples no counters, so its clocks cannot be correlated");
		}

		MTL::Timestamp cpu = 0;
		MTL::Timestamp gpu = 0;
		device->device->sampleTimestamps(&cpu, &gpu);

		out->queueType				 = queueType;
		out->gpuTimestamp			 = cpu != 0 || gpu != 0 ? gpu : 0;
		out->cpuTimestampNanoseconds = cpu;
		out->gpuPeriodNanoseconds	 = device->caps.timestampPeriodNanoseconds;
		out->calibrated				 = cpu != 0 || gpu != 0;
		return succeed(error);
	}

	bool metal4_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, const std::uint32_t firstQuery, const std::uint32_t queryCount, Error * error) noexcept
	{
		auto * object			  = static_cast<Metal4Object *>(impl);
		Metal4QueryPool * tracked = resolve_query_pool(object->owner, pool);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resetQueryPool names a query pool this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}

		tracked->heap->invalidateCounterRange(NS::Range::Make(firstQuery, queryCount));
		return succeed(error);
	}

	namespace
	{
		[[nodiscard]] MTL::RenderStages render_stage_for(const Flags<Stage> stages) noexcept
		{
			const Flags<Stage> beyondVertex = stages & ~(Flags<Stage>(Stage::eIndirectFetch) | Stage::eVertexWork);
			return !stages.empty() && beyondVertex.empty() ? MTL::RenderStageVertex : MTL::RenderStageFragment;
		}
	} // namespace

	bool metal4_cmd_write_timestamp(void * impl, QueryPoolHandle pool, const std::uint32_t query, const Flags<Stage> stage, Error * error) noexcept
	{
		auto * object			  = static_cast<Metal4Object *>(impl);
		CmdList * list			  = list_of(object);
		Metal4QueryPool * tracked = resolve_query_pool(object->owner, pool);
		if (tracked == nullptr || tracked->heap.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "writeTimestamp names a query pool this device never created");
		}
		if (query >= tracked->queryCount)
		{
			return fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		if (list->renderEncoder.get() != nullptr || list->computeEncoder.get() != nullptr)
		{
			if (list->timestampFence.get() == nullptr)
			{
				list->timestampFence = NS::TransferPtr(object->owner->device->newFence());
				if (list->timestampFence.get() == nullptr)
				{
					return fail(error, ErrorCode::eNativeApiError, "Metal 4 timestamp fence allocation failed");
				}
			}

			list->wroteEncoderTimestamps = true;
		}

		if (list->renderEncoder.get() != nullptr)
		{
			list->renderEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, render_stage_for(stage), tracked->heap.get(), query);
			return succeed(error);
		}
		if (list->computeEncoder.get() != nullptr)
		{
			list->computeEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, tracked->heap.get(), query);
			return succeed(error);
		}

		list->commandBuffer->writeTimestampIntoHeap(tracked->heap.get(), query);
		return succeed(error);
	}

	bool metal4_cmd_begin_query(void * impl, QueryPoolHandle /*unused*/, std::uint32_t /*unused*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and a scoped query is not one");
	}

	bool metal4_cmd_end_query(void * impl, QueryPoolHandle /*unused*/, std::uint32_t /*unused*/, Error * error) noexcept
	{
		static_cast<void>(impl);
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and a scoped query is not one");
	}

	bool metal4_cmd_resolve_query_data(
		void * impl,
		QueryPoolHandle pool,
		const std::uint32_t firstQuery,
		const std::uint32_t queryCount,
		BufferHandle dst,
		const std::uint64_t dstOffset,
		Error * error
	) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.resolveQueryData");

		auto * object		  = static_cast<Metal4Object *>(impl);
		Metal4Device * device = object->owner;
		CmdList * list		  = list_of(object);

		Metal4QueryPool * tracked = resolve_query_pool(device, pool);
		MTL::Buffer * destination = resolve_buffer(device, dst);
		if (tracked == nullptr || tracked->heap.get() == nullptr || destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveQueryData names a handle this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}
		if (list == nullptr || list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		end_active_encoders(list);

		MTL::Fence * waitFence = list->wroteEncoderTimestamps ? list->timestampFence.get() : nullptr;

		const std::uint64_t entrySize = device->device->sizeOfCounterHeapEntry(MTL4::CounterHeapTypeTimestamp);
		const std::uint64_t bytes	  = entrySize * queryCount;

		if (dstOffset > destination->length() || bytes > destination->length() - dstOffset)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resolveQueryData writes past the end of the destination buffer");
		}

		const MTL4::BufferRange range = MTL4::BufferRange::Make(destination->gpuAddress() + dstOffset, bytes);

		list->commandBuffer->resolveCounterHeap(tracked->heap.get(), NS::Range::Make(firstQuery, queryCount), range, waitFence, nullptr);
		return succeed(error);
	}

} // namespace azo::rhi::metal4

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSRange.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Metal/MTLBlitCommandEncoder.hpp>
#include <Metal/MTLBlitPass.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLCounters.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLResource.hpp>

#include <cstdint>
#include <utility>

namespace azo::rhi::metal
{
	[[nodiscard]] MetalQueryPool * resolve_query_pool(MetalDevice * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPools.resolve(handle, kHandleAlreadyChecked);
	}

	QueryPoolHandle metal_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createQueryPool");

		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.type != QueryType::eTimestamp)
		{
			return fail_value<QueryPoolHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal implements timestamp query pools only, and this pool asked for another type"
			);
		}
		if (device->timestampCounterSet.get() == nullptr)
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eUnsupportedFeature, "this Metal adapter exposes no timestamp counter set");
		}
		if (desc.queryCount == 0)
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool				= NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		const NS::SharedPtr<MTL::CounterSampleBufferDescriptor> sbd = NS::TransferPtr(MTL::CounterSampleBufferDescriptor::alloc()->init());
		sbd->setCounterSet(device->timestampCounterSet.get());
		sbd->setSampleCount(desc.queryCount);
		sbd->setStorageMode(MTL::StorageModePrivate);
		if (desc.debugName != nullptr)
		{
			sbd->setLabel(NS::String::string(desc.debugName, NS::UTF8StringEncoding));
		}

		NS::Error * nativeError						= nullptr;
		NS::SharedPtr<MTL::CounterSampleBuffer> buf = NS::TransferPtr(device->device->newCounterSampleBuffer(sbd.get(), &nativeError));
		if (buf.get() == nullptr)
		{
			return fail_value<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "MTLDevice::newCounterSampleBuffer failed");
		}

		return return_value(
			device->queryPools.store(
				MetalQueryPool{
					.sampleBuffer = std::move(buf),
					.type		  = desc.type,
					.queryCount	  = desc.queryCount,
				}
			),
			error
		);
	}

	bool metal_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
	{
		auto * device = static_cast<MetalDevice *>(impl);
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

	bool metal_cmd_reset_query_pool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept
	{
		auto * object			 = static_cast<MetalObject *>(impl);
		MetalQueryPool * tracked = resolve_query_pool(object->owner, pool);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resetQueryPool names a query pool this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}
		return succeed(error);
	}

	bool metal_cmd_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, [[maybe_unused]] Flags<Stage> stage, Error * error) noexcept
	{
		auto * object			 = static_cast<MetalObject *>(impl);
		MetalDevice * device	 = object->owner;
		MetalQueryPool * tracked = resolve_query_pool(device, pool);
		if (tracked == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "writeTimestamp names a query pool this device never created");
		}
		if (query >= tracked->queryCount)
		{
			return fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MetalCmdList * rec = object->list;
		if (rec->renderEncoder.get() != nullptr)
		{
			if (!device->samplesAtDrawBoundary)
			{
				return fail(
					error,
					ErrorCode::eUnsupportedFeature,
					"this Metal adapter samples counters at stage boundaries only, so a timestamp cannot be written inside a rendering scope. Time the "
					"scope with BeginRenderingDesc::timestamps instead"
				);
			}
			rec->renderEncoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			return succeed(error);
		}

		if (rec->computeEncoder.get() != nullptr)
		{
			if (!device->samplesAtDispatchBoundary)
			{
				return fail(
					error,
					ErrorCode::eUnsupportedFeature,
					"this Metal adapter samples counters at stage boundaries only, so a timestamp cannot be written inside a dispatch scope. Write it "
					"before the scope's first binding or after the work that closes the scope"
				);
			}
			rec->computeEncoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			return succeed(error);
		}

		const NS::SharedPtr<NS::AutoreleasePool> autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		if (device->samplesAtStageBoundary)
		{
			if (rec->timestampFence.get() == nullptr)
			{
				rec->timestampFence = NS::TransferPtr(device->device->newFence());
				if (rec->timestampFence.get() == nullptr)
				{
					return fail(error, ErrorCode::eNativeApiError, "Metal timestamp fence allocation failed");
				}
			}

			MTL::BlitPassDescriptor * pass = MTL::BlitPassDescriptor::alloc()->init();

			const NS::SharedPtr<MTL::BlitPassDescriptor> passGuard	   = NS::TransferPtr(pass);
			MTL::BlitPassSampleBufferAttachmentDescriptor * attachment = pass->sampleBufferAttachments()->object(0);
			attachment->setSampleBuffer(tracked->sampleBuffer.get());
			attachment->setStartOfEncoderSampleIndex(query);
			attachment->setEndOfEncoderSampleIndex(MTL::CounterDontSample);

			MTL::BlitCommandEncoder * encoder = rec->commandBuffer->blitCommandEncoder(pass);
			if (encoder == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			consume_alias_wait(rec, encoder);
			encoder->updateFence(rec->timestampFence.get());
			encoder->endEncoding();
			return succeed(error);
		}

		if (device->samplesAtBlitBoundary)
		{
			MTL::BlitCommandEncoder * encoder = rec->commandBuffer->blitCommandEncoder();
			if (encoder == nullptr)
			{
				return fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			consume_alias_wait(rec, encoder);
			encoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			encoder->endEncoding();
			return succeed(error);
		}

		return fail(error, ErrorCode::eUnsupportedFeature, "this Metal adapter samples counters at no point a timestamp write can reach");
	}

	bool metal_cmd_begin_query(
		[[maybe_unused]] void * impl,
		[[maybe_unused]] QueryPoolHandle pool,
		[[maybe_unused]] std::uint32_t query,
		Error * error
	) noexcept
	{
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and beginQuery serves the counting kinds");
	}

	bool metal_cmd_end_query([[maybe_unused]] void * impl, [[maybe_unused]] QueryPoolHandle pool, [[maybe_unused]] std::uint32_t query, Error * error) noexcept
	{
		return fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and endQuery serves the counting kinds");
	}

	bool metal_cmd_resolve_query_data(
		void * impl,
		QueryPoolHandle pool,
		std::uint32_t firstQuery,
		std::uint32_t queryCount,
		BufferHandle dst,
		std::uint64_t dstOffset,
		Error * error
	) noexcept
	{
		auto * object			  = static_cast<MetalObject *>(impl);
		MetalDevice * device	  = object->owner;
		MetalQueryPool * tracked  = resolve_query_pool(device, pool);
		MTL::Buffer * destination = resolve_buffer(device, dst);
		if (tracked == nullptr || destination == nullptr)
		{
			return fail(error, ErrorCode::eInvalidHandle, "resolveQueryData names a handle this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder						 = begin_blit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		encoder->resolveCounters(tracked->sampleBuffer.get(), NS::Range::Make(firstQuery, queryCount), destination, dstOffset);
		encoder->endEncoding();
		return succeed(error);
	}

}

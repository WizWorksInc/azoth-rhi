// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	[[nodiscard]] MetalQueryPool * ResolveQueryPool(MetalDevice * device, QueryPoolHandle handle) noexcept
	{
		return device->queryPools.Resolve(handle, kHandleAlreadyChecked);
	}

	QueryPoolHandle MetalCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createQueryPool");

		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.type != QueryType::eTimestamp)
		{
			return FailValue<QueryPoolHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp query pools only, and this pool asked for another type");
		}
		if (device->timestampCounterSet.get() == nullptr)
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eUnsupportedFeature, "this Metal adapter exposes no timestamp counter set");
		}
		if (desc.queryCount == 0)
		{
			return FailValue<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
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
			return FailValue<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "MTLDevice::newCounterSampleBuffer failed");
		}

		return ReturnValue(device->queryPools.Store(MetalQueryPool{
							   .sampleBuffer = std::move(buf),
							   .type		 = desc.type,
							   .queryCount	 = desc.queryCount,
						   }),
			error);
	}

	bool MetalCalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
	{
		auto * device = static_cast<MetalDevice *>(impl);
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

	bool MetalCmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept
	{
		auto * object			 = static_cast<MetalObject *>(impl);
		MetalQueryPool * tracked = ResolveQueryPool(object->owner, pool);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resetQueryPool names a query pool this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}
		return Succeed(error);
	}

	bool MetalCmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, [[maybe_unused]] Flags<Stage> stage, Error * error) noexcept
	{
		auto * object			 = static_cast<MetalObject *>(impl);
		MetalDevice * device	 = object->owner;
		MetalQueryPool * tracked = ResolveQueryPool(device, pool);
		if (tracked == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "writeTimestamp names a query pool this device never created");
		}
		if (query >= tracked->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		MetalCmdList * rec = object->list;
		if (rec->renderEncoder.get() != nullptr)
		{
			if (!device->samplesAtDrawBoundary)
			{
				return Fail(error,
					ErrorCode::eUnsupportedFeature,
					"this Metal adapter samples counters at stage boundaries only, so a timestamp cannot be written inside a rendering scope. Time the "
					"scope with BeginRenderingDesc::timestamps instead");
			}
			rec->renderEncoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			return Succeed(error);
		}

		if (rec->computeEncoder.get() != nullptr)
		{
			if (!device->samplesAtDispatchBoundary)
			{
				return Fail(error,
					ErrorCode::eUnsupportedFeature,
					"this Metal adapter samples counters at stage boundaries only, so a timestamp cannot be written inside a dispatch scope. Write it "
					"before the scope's first binding or after the work that closes the scope");
			}
			rec->computeEncoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			return Succeed(error);
		}

		const NS::SharedPtr<NS::AutoreleasePool> autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		if (device->samplesAtStageBoundary)
		{
			if (rec->timestampFence.get() == nullptr)
			{
				rec->timestampFence = NS::TransferPtr(device->device->newFence());
				if (rec->timestampFence.get() == nullptr)
				{
					return Fail(error, ErrorCode::eNativeApiError, "Metal timestamp fence allocation failed");
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
				return Fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			ConsumeAliasWait(rec, encoder);
			encoder->updateFence(rec->timestampFence.get());
			encoder->endEncoding();
			return Succeed(error);
		}

		if (device->samplesAtBlitBoundary)
		{
			MTL::BlitCommandEncoder * encoder = rec->commandBuffer->blitCommandEncoder();
			if (encoder == nullptr)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Metal blit command encoder creation failed");
			}
			ConsumeAliasWait(rec, encoder);
			encoder->sampleCountersInBuffer(tracked->sampleBuffer.get(), query, false);
			encoder->endEncoding();
			return Succeed(error);
		}

		return Fail(error, ErrorCode::eUnsupportedFeature, "this Metal adapter samples counters at no point a timestamp write can reach");
	}

	bool MetalCmdBeginQuery([[maybe_unused]] void * impl, [[maybe_unused]] QueryPoolHandle pool, [[maybe_unused]] std::uint32_t query, Error * error) noexcept
	{
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and beginQuery serves the counting kinds");
	}

	bool MetalCmdEndQuery([[maybe_unused]] void * impl, [[maybe_unused]] QueryPoolHandle pool, [[maybe_unused]] std::uint32_t query, Error * error) noexcept
	{
		return Fail(error, ErrorCode::eUnsupportedFeature, "Metal implements timestamp queries only, and endQuery serves the counting kinds");
	}

	bool MetalCmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept
	{
		auto * object			  = static_cast<MetalObject *>(impl);
		MetalDevice * device	  = object->owner;
		MetalQueryPool * tracked  = ResolveQueryPool(device, pool);
		MTL::Buffer * destination = ResolveBuffer(device, dst);
		if (tracked == nullptr || destination == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveQueryData names a handle this device never created");
		}
		if (firstQuery > tracked->queryCount || queryCount > tracked->queryCount - firstQuery)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}
		if (object->list == nullptr || object->list->commandBuffer.get() == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidState, "command list has no command buffer");
		}

		const NS::SharedPtr<NS::AutoreleasePool> autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::BlitCommandEncoder * encoder						 = BeginBlit(object, error);
		if (encoder == nullptr)
		{
			return false;
		}
		encoder->resolveCounters(tracked->sampleBuffer.get(), NS::Range::Make(firstQuery, queryCount), destination, dstOffset);
		encoder->endEncoding();
		return Succeed(error);
	}

}

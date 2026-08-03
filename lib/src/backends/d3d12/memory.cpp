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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	bool D3D12CalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "timestamp calibration output is null");
		}
		auto * device						  = static_cast<D3D12Device *>(impl);
		detail::HostVector<D3D12Queue> & pool = device->QueuesForType(queueType);
		if (pool.empty() || !pool.front().queue)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "requested queue type is not available");
		}
		D3D12Queue * queue = &pool.front();

		UINT64 gpu = 0;
		UINT64 cpu = 0;
		if (FAILED(queue->queue->GetClockCalibration(&gpu, &cpu)))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12CommandQueue::GetClockCalibration failed");
		}

		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		out->queueType	  = queueType;
		out->gpuTimestamp = gpu;
		out->cpuTimestampNanoseconds =
			frequency.QuadPart != 0 ? static_cast<std::uint64_t>(static_cast<double>(cpu) / static_cast<double>(frequency.QuadPart) * 1.0e9) : 0;
		// Timestamp frequency differs per engine, the copy queue especially so derive the period from the queue being calibrated and not the cached graphics one.
		// Falls back to the caps value when this queue reports no frequency.
		UINT64 gpuFrequency		  = 0;
		out->gpuPeriodNanoseconds = (SUCCEEDED(queue->queue->GetTimestampFrequency(&gpuFrequency)) && gpuFrequency != 0)
										? 1.0e9f / static_cast<float>(gpuFrequency)
										: device->caps.timestampPeriodNanoseconds;
		out->calibrated			  = true;
		return Succeed(error);
	}

	bool D3D12QueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "memory budget output is null");
		}
		auto * device = static_cast<D3D12Device *>(impl);

		// GPU-local and transient heaps sit in the local segment, upload and readback in the non-local system one.
		const DXGI_MEMORY_SEGMENT_GROUP group =
			(heap == HeapType::eGpuLocal || heap == HeapType::eTransient) ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		if (FAILED(device->adapter->QueryVideoMemoryInfo(0, group, &info)))
		{
			return Fail(error, ErrorCode::eNativeApiError, "IDXGIAdapter3::QueryVideoMemoryInfo failed");
		}

		out->heap						  = heap;
		out->budgetBytes				  = info.Budget;
		out->usageBytes					  = info.CurrentUsage;
		out->availableForReservationBytes = info.AvailableForReservation;
		out->budgetIsPrecise			  = true;
		return Succeed(error);
	}

	[[nodiscard]] D3D12_RESIDENCY_PRIORITY MapResidencyPriority(ResidencyPriority priority) noexcept
	{
		switch (priority)
		{
		case ResidencyPriority::eMinimum:  return D3D12_RESIDENCY_PRIORITY_MINIMUM;
		case ResidencyPriority::eLow:	   return D3D12_RESIDENCY_PRIORITY_LOW;
		case ResidencyPriority::eNormal:   return D3D12_RESIDENCY_PRIORITY_NORMAL;
		case ResidencyPriority::eHigh:	   return D3D12_RESIDENCY_PRIORITY_HIGH;
		case ResidencyPriority::eCritical: return D3D12_RESIDENCY_PRIORITY_MAXIMUM;
		}
		return D3D12_RESIDENCY_PRIORITY_NORMAL;
	}

	bool D3D12SetResidencyPriority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept
	{
		auto * device = static_cast<D3D12Device *>(impl);
		ComPtr<ID3D12Device1> device1;
		if (FAILED(device->device.As(&device1)))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "setResidencyPriority requires ID3D12Device1");
		}

		detail::HostVector<ID3D12Pageable *> objects;
		detail::HostVector<D3D12_RESIDENCY_PRIORITY> values;
		objects.reserve(priorities.size());
		values.reserve(priorities.size());
		for (const ResidencyPriorityDesc & desc : priorities)
		{
			ID3D12Resource * resource = nullptr;
			if (desc.buffer.IsValid())
			{
				BufferSlot * slot = ResolveBuffer(device, desc.buffer);
				if (slot == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid buffer handle");
				}
				resource = slot->resource.Get();
			}
			else if (desc.texture.IsValid())
			{
				TextureSlot * slot = ResolveTexture(device, desc.texture);
				if (slot == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid texture handle");
				}
				resource = slot->resource.Get();
			}
			if (resource != nullptr)
			{
				objects.push_back(resource);
				values.push_back(MapResidencyPriority(desc.priority));
			}
		}

		if (!objects.empty() && FAILED(device1->SetResidencyPriority(static_cast<UINT>(objects.size()), objects.data(), values.data())))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12Device1::SetResidencyPriority failed");
		}
		return Succeed(error);
	}

	/*
	 * The backend releases native resources immediately on Destroy so no kind has a deferred queue to collect. Resetting the counter is all this does, which is
	 * what gives a profiler its per-collect destroy count.
	 *
	 * That counter spans the device and not one kind so the first kind the sweep reaches resets it and the other fifteen have nothing to do.
	 */
	bool D3D12CollectGarbage(void * impl, ResourceType type, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.collectGarbage");

		if (type == ResourceType::eBuffer)
		{
			static_cast<D3D12Device *>(impl)->pendingRetire.store(0, std::memory_order_relaxed);
			AZO_RHI_PROFILE_PLOT("rhi.d3d12.pendingRetire", static_cast<std::int64_t>(0));
		}
		return Succeed(error);
	}

	bool D3D12CollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept
	{
		return D3D12CollectGarbage(impl, type, error);
	}

	// Adoption. An adopted resource is borrowed: the slot AddRefs the caller's resource and destroy leaves it alone so ownership never moves.

	// Wraps an externally created ID3D12Resource as an RHI buffer. The caller keeps ownership of the resource.

} // namespace azo::rhi::d3d12

#endif // _WIN32

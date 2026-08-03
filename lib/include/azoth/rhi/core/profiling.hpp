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

#pragma once

/**
 * \file
 * \brief Profiling instrumentation macros.
 */

#include "azoth/rhi/host/profiler.hpp"

#define AZO_RHI_PROFILE_CONCAT_INNER(first, second) first##second
#define AZO_RHI_PROFILE_CONCAT(first, second)		AZO_RHI_PROFILE_CONCAT_INNER(first, second)

/**
 * \def AZO_RHI_PROFILE_ZONE
 * \brief Begins a CPU profiling zone for the enclosing scope.
 *
 * \param zoneName Static or otherwise long-lived zone name passed to the active profiler.
 */

/**
 * \def AZO_RHI_PROFILE_ZONE_C
 * \brief Begins a CPU profiling zone for the enclosing scope with an explicit color.
 *
 * \param zoneName Static or otherwise long-lived zone name passed to the active profiler.
 * \param zoneColor Packed profiler-specific color value. Zero requests the sink default.
 */

/**
 * \def AZO_RHI_PROFILE_PLOT
 * \brief Emits a signed integer plot sample to the active profiler.
 *
 * \param plotName Static or otherwise long-lived plot name passed to the active profiler.
 * \param plotValue Numeric value converted to std::int64_t before emission.
 */

/**
 * \def AZO_RHI_PROFILE_ALLOC
 * \brief Reports a GPU allocation to the active profiler.
 *
 * \param allocAddress Profiler-visible allocation address or handle.
 * \param allocSize Allocation size in bytes.
 * \param allocPool Profiler-visible allocation pool name or handle.
 */

/**
 * \def AZO_RHI_PROFILE_FREE
 * \brief Reports a GPU allocation release to the active profiler.
 *
 * \param freeAddress Profiler-visible allocation address or handle.
 * \param freePool Profiler-visible allocation pool name or handle.
 */

/**
 * \def AZO_RHI_PROFILE_GPU_ZONE
 * \brief Begins a GPU profiling zone for the enclosing scope.
 *
 * \param cmdList Command list that receives the GPU timestamp commands.
 * \param zoneName Static or otherwise long-lived zone name passed to the active profiler.
 */

/**
 * \def AZO_RHI_PROFILE_GPU_COLLECT
 * \brief Flushes resolved GPU timestamps from a command list about to be submitted.
 *
 * Call once per frame on a command list that will execute on the queue used for GPU profiling.
 */

/**
 * \def AZO_RHI_PROFILE_FIBER_SUSPENSION
 * \brief Marks the rest of the enclosing scope as a wait that may suspend the current fiber.
 *
 * \param syncOps SyncOps for the device doing the wait. Fiber identity comes from that host scheduler.
 * \param deviceProfiler Profiler instance associated with the waiting device.
 */

#ifdef AZOTH_RHI_ENABLE_PROFILING

	// One constant-initialized ZoneLocation per call site lets profiler sinks intern call sites by address.
	#define AZO_RHI_PROFILE_ZONE_IMPL(zoneName, zoneColor)                                                                                                     \
		static constexpr ::azo::rhi::ZoneLocation AZO_RHI_PROFILE_CONCAT(                                                                                      \
			azoRhiZoneLoc_, __LINE__){ .name = (zoneName), .file = __FILE__, .line = __LINE__, .color = (zoneColor) };                                         \
		const ::azo::rhi::detail::ScopedZone AZO_RHI_PROFILE_CONCAT(azoRhiZone_, __LINE__)                                                                     \
		{                                                                                                                                                      \
			AZO_RHI_PROFILE_CONCAT(azoRhiZoneLoc_, __LINE__)                                                                                                   \
		}

	#define AZO_RHI_PROFILE_ZONE(zoneName) AZO_RHI_PROFILE_ZONE_IMPL(zoneName, 0u)

	#define AZO_RHI_PROFILE_ZONE_C(zoneName, zoneColor) AZO_RHI_PROFILE_ZONE_IMPL(zoneName, zoneColor)

	#define AZO_RHI_PROFILE_PLOT(plotName, plotValue)                                                                                                          \
		do                                                                                                                                                     \
		{                                                                                                                                                      \
			if (::azo::rhi::Profiler * azoRhiSink = ::azo::rhi::GetProfiler(); azoRhiSink != nullptr)                                                          \
			{                                                                                                                                                  \
				azoRhiSink->Plot((plotName), static_cast<std::int64_t>(plotValue));                                                                            \
			}                                                                                                                                                  \
		} while (false)

	#define AZO_RHI_PROFILE_ALLOC(allocAddress, allocSize, allocPool)                                                                                          \
		do                                                                                                                                                     \
		{                                                                                                                                                      \
			if (::azo::rhi::Profiler * azoRhiSink = ::azo::rhi::GetProfiler(); azoRhiSink != nullptr)                                                          \
			{                                                                                                                                                  \
				azoRhiSink->GpuAllocate((allocAddress), static_cast<std::uint64_t>(allocSize), (allocPool));                                                   \
			}                                                                                                                                                  \
		} while (false)

	#define AZO_RHI_PROFILE_FREE(freeAddress, freePool)                                                                                                        \
		do                                                                                                                                                     \
		{                                                                                                                                                      \
			if (::azo::rhi::Profiler * azoRhiSink = ::azo::rhi::GetProfiler(); azoRhiSink != nullptr)                                                          \
			{                                                                                                                                                  \
				azoRhiSink->GpuFree((freeAddress), (freePool));                                                                                                \
			}                                                                                                                                                  \
		} while (false)

	#define AZO_RHI_PROFILE_GPU_ZONE(cmdList, zoneName)                                                                                                        \
		static constexpr ::azo::rhi::ZoneLocation AZO_RHI_PROFILE_CONCAT(                                                                                      \
			azoRhiGpuLoc_, __LINE__){ .name = (zoneName), .file = __FILE__, .line = __LINE__, .color = 0u };                                                   \
		const ::azo::rhi::detail::ScopedGpuZone AZO_RHI_PROFILE_CONCAT(azoRhiGpuZone_, __LINE__)                                                               \
		{                                                                                                                                                      \
			(cmdList), AZO_RHI_PROFILE_CONCAT(azoRhiGpuLoc_, __LINE__)                                                                                         \
		}

	#define AZO_RHI_PROFILE_GPU_COLLECT(cmdList)                                                                                                               \
		do                                                                                                                                                     \
		{                                                                                                                                                      \
			if (::azo::rhi::Profiler * azoRhiSink = ::azo::rhi::GetProfiler(); azoRhiSink != nullptr)                                                          \
			{                                                                                                                                                  \
				azoRhiSink->CollectGpu(cmdList);                                                                                                               \
			}                                                                                                                                                  \
		} while (false)

	#define AZO_RHI_PROFILE_FIBER_SUSPENSION(syncOps, deviceProfiler)                                                                                          \
		const ::azo::rhi::detail::FiberSuspension AZO_RHI_PROFILE_CONCAT(azoRhiFiber_, __LINE__)                                                               \
		{                                                                                                                                                      \
			(syncOps), (deviceProfiler)                                                                                                                        \
		}

#else
	// Unevaluated sink keeps disabled profiling arguments type-checked without generating runtime work.
	#define AZO_RHI_PROFILE_SINK(expr) static_cast<void>(sizeof(expr))

	#define AZO_RHI_PROFILE_ZONE(zoneName)							  AZO_RHI_PROFILE_SINK(zoneName)
	#define AZO_RHI_PROFILE_ZONE_C(zoneName, zoneColor)				  AZO_RHI_PROFILE_SINK((zoneName, zoneColor))
	#define AZO_RHI_PROFILE_PLOT(plotName, plotValue)				  AZO_RHI_PROFILE_SINK((plotName, plotValue))
	#define AZO_RHI_PROFILE_ALLOC(allocAddress, allocSize, allocPool) AZO_RHI_PROFILE_SINK((allocAddress, allocSize, allocPool))
	#define AZO_RHI_PROFILE_FREE(freeAddress, freePool)				  AZO_RHI_PROFILE_SINK((freeAddress, freePool))
	#define AZO_RHI_PROFILE_GPU_ZONE(cmdList, zoneName)				  AZO_RHI_PROFILE_SINK((cmdList, zoneName))
	#define AZO_RHI_PROFILE_GPU_COLLECT(cmdList)					  AZO_RHI_PROFILE_SINK(cmdList)
	#define AZO_RHI_PROFILE_FIBER_SUSPENSION(syncOps, deviceProfiler) AZO_RHI_PROFILE_SINK((syncOps, deviceProfiler))
#endif

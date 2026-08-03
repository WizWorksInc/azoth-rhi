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
 * \brief Query pool creation, query readback, and timestamp calibration descriptors.
 */

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	/**
	 * \brief Query payload produced by a query pool.
	 */
	enum class QueryType : std::uint8_t
	{
		/**
		 * \brief GPU timestamp query for measuring command execution time.
		 */
		eTimestamp,

		/**
		 * \brief Visibility query reporting samples or fragments that passed depth and stencil tests.
		 */
		eOcclusion,

		/**
		 * \brief Backend-supported graphics or compute statistics.
		 */
		ePipelineStatistics,
	};

	/**
	 * \brief Pipeline statistic counters requested from a statistics query pool.
	 *
	 * Bitflags. Only request counters supported by the selected device.
	 */
	enum class PipelineStatistic : std::uint32_t // NOLINT(performance-enum-size)
	{
		eInputAssemblyVertices = 1u << 0u,

		/**
		 * \brief Assembled primitives before clipping, culling, or rasterization effects.
		 */
		eInputAssemblyPrimitives = 1u << 1u,

		/**
		 * \brief Vertex shader invocations, which may differ from submitted vertex count.
		 */
		eVertexShaderInvocations = 1u << 2u,

		eClippingInvocations = 1u << 3u,

		/**
		 * \brief Primitives emitted by clipping after rejection or splitting.
		 */
		eClippingPrimitives = 1u << 4u,

		/**
		 * \brief Fragment shader work, not final visible pixels.
		 */
		eFragmentShaderInvocations = 1u << 5u,

		/**
		 * \brief Compute shader invocations, not dispatch calls or workgroups.
		 */
		eComputeShaderInvocations = 1u << 6u,
	};

	/**
	 * \brief Query pool creation request.
	 *
	 * statistics is used only when type is ePipelineStatistics. debugName is borrowed for the duration of creation.
	 */
	struct QueryPoolDesc final
	{
		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
		Flags<PipelineStatistic> statistics;
		const char * debugName = nullptr;
	};

	/**
	 * \brief Query result readback request.
	 *
	 * Query memory must not be read before the GPU resolve or copy that writes it has completed.
	 */
	struct QueryResultDesc final
	{
		QueryPoolHandle pool{};
		std::uint32_t firstQuery = 0;
		std::uint32_t queryCount = 0;

		/**
		 * \brief Waits for query completion during readback when the backend supports it.
		 */
		bool wait = false;

		/**
		 * \brief Writes availability data alongside each query result.
		 */
		bool withAvailability = true;
	};

	/**
	 * \brief CPU and GPU timestamp correlation result.
	 *
	 * Cross-queue or CPU-to-GPU timing needs calibration instead of subtracting unrelated timestamp domains.
	 */
	struct TimestampCalibration final
	{
		QueueType queueType					  = QueueType::eGraphics;
		std::uint64_t gpuTimestamp			  = 0;
		std::uint64_t cpuTimestampNanoseconds = 0;
		float gpuPeriodNanoseconds			  = 1.0f;

		/**
		 * \brief True when the timestamps came from a calibrated pair.
		 */
		bool calibrated = false;
	};

} // namespace azo::rhi

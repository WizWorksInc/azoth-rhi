// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <cstdint>

namespace azo::rhi
{

	enum class QueryType : std::uint8_t
	{
		eTimestamp,

		eOcclusion,

		ePipelineStatistics,
	};

	enum class PipelineStatistic : std::uint32_t // NOLINT(performance-enum-size)
	{
		eInputAssemblyVertices = 1u << 0u,

		eInputAssemblyPrimitives = 1u << 1u,

		eVertexShaderInvocations = 1u << 2u,

		eClippingInvocations = 1u << 3u,

		eClippingPrimitives = 1u << 4u,

		eFragmentShaderInvocations = 1u << 5u,

		eComputeShaderInvocations = 1u << 6u,
	};

	struct QueryPoolDesc final
	{
		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
		Flags<PipelineStatistic> statistics;
		const char * debugName = nullptr;
	};

	struct QueryResultDesc final
	{
		QueryPoolHandle pool{};
		std::uint32_t firstQuery = 0;
		std::uint32_t queryCount = 0;

		bool wait = false;

		bool withAvailability = true;
	};

	struct TimestampCalibration final
	{
		QueueType queueType					  = QueueType::eGraphics;
		std::uint64_t gpuTimestamp			  = 0;
		std::uint64_t cpuTimestampNanoseconds = 0;
		float gpuPeriodNanoseconds			  = 1.0f;

		bool calibrated = false;
	};

}

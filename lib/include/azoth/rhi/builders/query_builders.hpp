// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/resources/query.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace azo::rhi
{
	class QueryPoolBuilder final
	{
	public:
		QueryPoolBuilder & Type(QueryType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		QueryPoolBuilder & Timestamp() noexcept
		{
			return Type(QueryType::eTimestamp);
		}

		QueryPoolBuilder & Occlusion() noexcept
		{
			return Type(QueryType::eOcclusion);
		}

		QueryPoolBuilder & PipelineStatistics(Flags<PipelineStatistic> statistics) noexcept
		{
			m_desc.type		  = QueryType::ePipelineStatistics;
			m_desc.statistics = statistics;
			return *this;
		}

		QueryPoolBuilder & QueryCount(std::uint32_t count) noexcept
		{
			m_desc.queryCount = count;
			return *this;
		}

		QueryPoolBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] QueryPoolDesc Build() const noexcept
		{
			QueryPoolDesc desc = m_desc;
			desc.debugName	   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		QueryPoolDesc m_desc{};
		std::string m_debugName;
	};

	class QueryResultBuilder final
	{
	public:
		QueryResultBuilder & Pool(QueryPoolHandle pool) noexcept
		{
			m_desc.pool = pool;
			return *this;
		}

		QueryResultBuilder & Range(std::uint32_t firstQuery, std::uint32_t queryCount) noexcept
		{
			m_desc.firstQuery = firstQuery;
			m_desc.queryCount = queryCount;
			return *this;
		}

		QueryResultBuilder & Wait(bool enabled = true) noexcept
		{
			m_desc.wait = enabled;
			return *this;
		}

		QueryResultBuilder & Availability(bool enabled = true) noexcept
		{
			m_desc.withAvailability = enabled;
			return *this;
		}

		[[nodiscard]] constexpr QueryResultDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		QueryResultDesc m_desc{};
	};
}

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
 * \brief Builders for query-pool and query-result descriptions.
 */

#include "azoth/rhi/resources/query.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <string>
#include <string_view>

namespace azo::rhi
{
	/**
	 * \brief Builds query-pool descriptions backed by owned debug-name storage.
	 */
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

		/**
		 * \brief Selects a pipeline-statistics query pool and records the statistics to collect.
		 */
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

		/**
		 * \brief Builds a query-pool description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
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

		/**
		 * \brief Selects the contiguous query range to read from the pool.
		 *
		 * \param firstQuery First query index in the pool.
		 * \param queryCount Number of query slots to read.
		 */
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

		/**
		 * \brief Requests per-query availability data in the result payload.
		 */
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
} // namespace azo::rhi

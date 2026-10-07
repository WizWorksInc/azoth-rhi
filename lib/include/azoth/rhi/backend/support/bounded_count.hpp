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

#include <atomic>
#include <cstdint>
#include <limits>

namespace azo::rhi
{
	enum class ListLifecycle : std::uint8_t
	{
		eFresh,
		eRecording,
		eEnded,
		eSubmitted,
	};

	class BoundedCount final
	{
	public:
		BoundedCount() = default;

		BoundedCount(const BoundedCount & other) noexcept : m_count(other.load()) {}

		BoundedCount & operator=(const BoundedCount & other) noexcept
		{
			m_count.store(other.load(), std::memory_order_relaxed);
			return *this;
		}

		~BoundedCount() = default;

		[[nodiscard]] bool try_acquire(const std::uint32_t bound) noexcept
		{
			std::uint32_t held = m_count.load(std::memory_order_relaxed);
			while (held < bound)
			{
				if (m_count.compare_exchange_weak(held, held + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
				{
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] bool try_acquire() noexcept
		{
			return try_acquire(std::numeric_limits<std::uint32_t>::max());
		}

		[[nodiscard]] bool try_release() noexcept
		{
			std::uint32_t held = m_count.load(std::memory_order_relaxed);
			while (held > 0)
			{
				if (m_count.compare_exchange_weak(held, held - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
				{
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] std::uint32_t exchange(const std::uint32_t value) noexcept
		{
			return m_count.exchange(value, std::memory_order_acq_rel);
		}

		[[nodiscard]] std::uint32_t load() const noexcept
		{
			return m_count.load(std::memory_order_acquire);
		}

	private:
		std::atomic<std::uint32_t> m_count{ 0 };
	};

	// The bound here is the counter's own range, so this fires only where one more map would wrap the count rather than at any useful limit.
	inline constexpr const char * kMapCountWouldOverflow = "map of a buffer whose outstanding map count would overflow";

	inline constexpr const char * kSubmitOfRecordingList = "submit of a command list that is still recording, so End was never called on it";

	inline constexpr const char * kSubmitOfNeverBegunList = "submit of a command list that was never begun, so it carries no recorded work";

	inline constexpr const char * kSubmitOfSubmittedList = "submit of a command list that was already submitted, so begin it again before submitting it again";

	inline constexpr const char * kResetOfPoolWithRunningList =
		"reset of a command pool holding a command list that is still executing, so wait for that work to complete first";

	inline constexpr const char * kSubmitOfPendingList =
		"submit of a command list whose earlier submission is still executing, so wait for that submission to complete first";

	// A fresh or recording list is refused everywhere, and a submitted one only where the backend or the pending work forbids it.
	[[nodiscard]] constexpr const char * submit_refusal_for(
		const ListLifecycle lifecycle, const bool backendResubmits, const bool earlierSubmitPending) noexcept
	{
		switch (lifecycle)
		{
		case ListLifecycle::eFresh:		return kSubmitOfNeverBegunList;
		case ListLifecycle::eRecording: return kSubmitOfRecordingList;
		case ListLifecycle::eSubmitted:
			if (!backendResubmits)
			{
				return kSubmitOfSubmittedList;
			}

			return earlierSubmitPending ? kSubmitOfPendingList : nullptr;
		case ListLifecycle::eEnded: break;
		}

		return nullptr;
	}

	// The whole pre-submit sweep, so the loop, the null skip and the unwrap are not copied once per backend alongside the message table.
	template <typename Lists, typename RecordOf, typename Pending>
	[[nodiscard]] const char * submit_refusal_for_lists(const Lists & lists, const bool backendResubmits, RecordOf recordOf, Pending pending)
	{
		for (const auto * list : lists)
		{
			if (list == nullptr)
			{
				continue;
			}

			const auto * record = recordOf(*list);
			if (record == nullptr)
			{
				continue;
			}

			if (const char * refusal = submit_refusal_for(record->lifecycle, backendResubmits, pending(*record)); refusal != nullptr)
			{
				return refusal;
			}
		}

		return nullptr;
	}

	// Every backend refuses a Begin past the budget in the same words, so the message lives here rather than in five copies.
	inline constexpr const char * kOpenListBudgetExhausted =
		"too many command lists are begun and not yet submitted on this queue, so submit or reset one before beginning another";

	class OpenListBudget final
	{
	public:
		void set_bound(const std::uint32_t bound) noexcept
		{
			m_bound = bound;
		}

		[[nodiscard]] bool try_open(const QueueType type) noexcept
		{
			return CountFor(type).try_acquire(m_bound);
		}

		void close(const QueueType type) noexcept
		{
			static_cast<void>(CountFor(type).try_release());
		}

		[[nodiscard]] std::uint32_t open(const QueueType type) const noexcept
		{
			return CountFor(type).load();
		}

	private:
		[[nodiscard]] BoundedCount & CountFor(const QueueType type) noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return m_compute;
			case QueueType::eCopy:	   return m_copy;
			case QueueType::eGraphics: break;
			}

			return m_graphics;
		}

		[[nodiscard]] const BoundedCount & CountFor(const QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return m_compute;
			case QueueType::eCopy:	   return m_copy;
			case QueueType::eGraphics: break;
			}

			return m_graphics;
		}

		std::uint32_t m_bound = 0;
		BoundedCount m_graphics;
		BoundedCount m_compute;
		BoundedCount m_copy;
	};
}

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

#include "azoth/rhi/core/enums.hpp"

#include <atomic>
#include <cstdint> // NOLINT
#include <limits>

namespace azo::rhi
{
	/**
	 * \brief Recording and submission state of a command list.
	 */
	enum class ListLifecycle : std::uint8_t
	{
		eFresh,
		eRecording,
		eEnded,
		eSubmitted,
	};

	/**
	 * \brief Atomic counter whose copies and moves snapshot the value without resetting the source.
	 */
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

		BoundedCount(BoundedCount && other) noexcept : m_count(other.load()) {}

		BoundedCount & operator=(BoundedCount && other) noexcept
		{
			return *this = other;
		}

		~BoundedCount() = default;

		/**
		 * \brief Increments the count if it is below the supplied bound.
		 */
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

		/**
		 * \brief Increments the count unless it would overflow.
		 */
		[[nodiscard]] bool try_acquire() noexcept
		{
			return try_acquire(std::numeric_limits<std::uint32_t>::max());
		}

		/**
		 * \brief Decrements the count if it is nonzero.
		 */
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

		/**
		 * \brief Replaces the count and returns its previous value.
		 */
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

	inline constexpr auto kMapCountWouldOverflow = "map of a buffer whose outstanding map count would overflow";

	inline constexpr auto kSubmitOfRecordingList = "submit of a command list that is still recording, so End was never called on it";

	inline constexpr auto kSubmitOfNeverBegunList = "submit of a command list that was never begun, so it carries no recorded work";

	inline constexpr auto kSubmitOfSubmittedList = "submit of a command list that was already submitted, so begin it again before submitting it again";

	inline constexpr auto kResetOfPoolWithRunningList =
		"reset of a command pool holding a command list that is still executing, so wait for that work to complete first";

	inline constexpr auto kSubmitOfPendingList =
		"submit of a command list whose earlier submission is still executing, so wait for that submission to complete first";

	/**
	 * \brief Checks command list lifecycle and resubmission rules.
	 * \return Refusal message, or nullptr if these rules permit submission.
	 */
	[[nodiscard]] constexpr const char * submit_refusal_for(
		const ListLifecycle lifecycle,
		const bool backendResubmits,
		const bool earlierSubmitPending
	) noexcept
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

	/**
	 * \brief Returns the first lifecycle refusal, skipping null lists and unresolved records.
	 * \param recordOf Maps a command list reference to its backend record pointer.
	 * \param pending Reports whether a backend record's earlier submission is still executing.
	 * \return nullptr if no checked list is refused.
	 */
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

	inline constexpr auto kOpenListBudgetExhausted =
		"too many command lists are begun and not yet submitted on this queue, so submit or reset one before beginning another";

	/**
	 * \brief Limits open command list counts separately for each queue type.
	 */
	class OpenListBudget final
	{
	public:
		/**
		 * \brief Sets the limit for each queue type before concurrent use.
		 */
		void set_bound(const std::uint32_t bound) noexcept
		{
			m_bound = bound;
		}

		/**
		 * \brief Reserves one open command list slot for a queue type.
		 */
		[[nodiscard]] bool try_open(const QueueType type) noexcept
		{
			return CountFor(type).try_acquire(m_bound);
		}

		/**
		 * \brief Releases one open slot without decrementing below zero.
		 */
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
} // namespace azo::rhi

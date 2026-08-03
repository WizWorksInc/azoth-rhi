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
 * \brief Process-wide device-tag pool for handle ownership checks.
 */

#include "azoth/rhi/core/handle.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <mutex>

namespace azo::rhi::detail
{
	/**
	 * \brief Allocates live-device tags used to reject handles created by another device.
	 *
	 * Tags are encoded into handle indices before slot-map lookup. Tag zero is reserved for invalid and default-constructed handles.
	 * \attention Each acquired tag must be released at most once. Duplicate releases can recycle the same tag more than once.
	 */
	class DeviceTagPool final
	{
	public:
		/**
		 * \brief Returns either a recycled tag or the next unused tag.
		 *
		 * \param outTag Receives a nonzero tag only when this function returns true.
		 */
		[[nodiscard]] bool Acquire(std::uint32_t & outTag) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_freeCount != 0)
			{
				--m_freeCount;

				// m_freeCount was checked before decrementing, so the resulting index is inside the fixed free-list storage.
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				outTag = m_free[m_freeCount];
				return true;
			}

			if (m_next > kMaxDeviceTag)
			{
				return false;
			}

			outTag = m_next;
			++m_next;
			return true;
		}

		/**
		 * \brief Returns a live-device tag to the fixed free list.
		 *
		 * \param tag Acquired tag to recycle. Zero is ignored because it is the invalid-handle tag.
		 */
		void Release(std::uint32_t tag) noexcept
		{
			if (tag == 0)
			{
				return;
			}

			const std::scoped_lock lock(m_mutex);
			if (m_freeCount < m_free.size())
			{
				// Fixed storage keeps release allocation-free and noexcept during device teardown.
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				m_free[m_freeCount] = tag;
				++m_freeCount;
			}
		}

	private:
		std::mutex m_mutex;
		std::array<std::uint32_t, kMaxDeviceTag> m_free{};
		std::size_t m_freeCount = 0;
		std::uint32_t m_next	= 1;
	};

	/**
	 * \brief Returns the process-wide tag pool without depending on static initialization order across translation units.
	 */
	[[nodiscard]] inline DeviceTagPool & DeviceTags() noexcept
	{
		static DeviceTagPool pool;
		return pool;
	}
} // namespace azo::rhi::detail

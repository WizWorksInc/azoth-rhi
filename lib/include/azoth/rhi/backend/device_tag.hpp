// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/handle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace azo::rhi::detail
{
	class DeviceTagPool final
	{
	public:
		[[nodiscard]] bool Acquire(std::uint32_t & outTag) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_freeCount != 0)
			{
				--m_freeCount;

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

		void Release(std::uint32_t tag) noexcept
		{
			if (tag == 0)
			{
				return;
			}

			const std::scoped_lock lock(m_mutex);
			if (m_freeCount < m_free.size())
			{
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

	[[nodiscard]] inline DeviceTagPool & DeviceTags() noexcept
	{
		static DeviceTagPool pool;
		return pool;
	}
}

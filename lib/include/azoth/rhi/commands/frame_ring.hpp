// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/ownership/unique.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace azo::rhi
{

	inline constexpr std::uint32_t kMaxFramesInFlight = 4;

	struct FrameRingDesc final
	{
		std::uint32_t framesInFlight = 2;
		QueueType queueType			 = QueueType::eGraphics;
		const char * debugName		 = nullptr;
	};

	class FrameRing final
	{
	public:
		FrameRing() = default;

		FrameRing(const FrameRing &)				 = delete;
		FrameRing & operator=(const FrameRing &)	 = delete;
		FrameRing(FrameRing &&) noexcept			 = default;
		FrameRing & operator=(FrameRing &&) noexcept = default;
		~FrameRing()								 = default;

		[[nodiscard]] static FrameRing Create(Device dev, Queue queue, const FrameRingDesc & desc, Error & error) noexcept
		{
			error = {};

			if (desc.framesInFlight == 0 || desc.framesInFlight > kMaxFramesInFlight)
			{
				error = Error{ .code = ErrorCode::eInvalidArgument, .message = "framesInFlight must be between one and kMaxFramesInFlight" };
				return {};
			}

			FrameRing ring;
			ring.m_queue	 = queue;
			ring.m_depth	 = desc.framesInFlight;
			ring.m_debugName = desc.debugName;
			ring.m_timeline	 = UniqueTimeline{ dev, dev.CreateTimeline(TimelineDesc{ .debugName = desc.debugName }, error) };
			if (!ring.m_timeline.IsValid())
			{
				return {};
			}

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			for (std::uint32_t slot = 0; slot < desc.framesInFlight; ++slot)
			{
				ring.m_pools[slot] = dev.CreateCommandPool(CommandPoolDesc{ .queueType = desc.queueType, .debugName = desc.debugName }, error);
				if (!ring.m_pools[slot].IsValid())
				{
					return {};
				}
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			return ring;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_timeline.IsValid() && m_depth != 0;
		}

		[[nodiscard]] CommandList Begin(Error & error) noexcept
		{
			return Begin(std::numeric_limits<std::uint64_t>::max(), error);
		}

		[[nodiscard]] CommandList Begin(const std::uint64_t timeoutNanoseconds, Error & error) noexcept
		{
			error = {};

			if (!IsValid())
			{
				error = Error{ .code = ErrorCode::eInvalidState, .message = "Begin on a frame ring that was never created or was moved from" };
				return {};
			}

			const std::uint64_t next	 = m_frame + 1;
			const std::uint64_t retiring = next > m_depth ? next - m_depth : 0;

			if (retiring != 0 && !m_queue.Wait(m_timeline.Get(), retiring, timeoutNanoseconds, error))
			{
				return {};
			}

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			CommandPool & pool = m_pools[next % m_depth];
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (!pool.Reset(RetirePoint{ .timeline = m_timeline.Get(), .value = retiring }, error))
			{
				return {};
			}

			CommandList list = pool.Allocate(m_debugName, error);
			if (!list.IsValid())
			{
				return {};
			}

			m_frame = next;
			return list;
		}

		[[nodiscard]] TimelinePoint Signal() const noexcept
		{
			return TimelinePoint{ .timeline = m_timeline.Get(), .value = m_frame };
		}

		[[nodiscard]] RetirePoint Retire() const noexcept
		{
			return RetirePoint{ .timeline = m_timeline.Get(), .value = m_frame };
		}

		[[nodiscard]] TimelineHandle Timeline() const noexcept
		{
			return m_timeline.Get();
		}

		[[nodiscard]] std::uint64_t FrameIndex() const noexcept
		{
			return m_frame;
		}

		[[nodiscard]] std::uint32_t SlotIndex() const noexcept
		{
			return m_depth != 0 ? static_cast<std::uint32_t>(m_frame % m_depth) : 0;
		}

		[[nodiscard]] std::uint32_t FramesInFlight() const noexcept
		{
			return m_depth;
		}

	private:
		Queue m_queue;
		UniqueTimeline m_timeline;
		std::array<CommandPool, kMaxFramesInFlight> m_pools{};
		std::uint64_t m_frame	 = 0;
		std::uint32_t m_depth	 = 0;
		const char * m_debugName = nullptr;
	};

}

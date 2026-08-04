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
 * \brief Pool ring and timeline for a frame loop that keeps the CPU ahead of the GPU.
 */

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

	/**
	 * \brief How many frames a ring keeps in flight at once, which is also how many command pools it owns.
	 *
	 * Two overlaps recording with execution. Three adds slack for a frame that runs long. Past that the added latency costs more than the smoothing buys, so the
	 * ceiling is here, not left to the caller.
	 */
	inline constexpr std::uint32_t kMaxFramesInFlight = 4;

	/**
	 * \brief Frame-ring creation request.
	 */
	struct FrameRingDesc final
	{
		std::uint32_t framesInFlight = 2;
		QueueType queueType			 = QueueType::eGraphics;
		const char * debugName		 = nullptr;
	};

	/**
	 * \brief One command pool per frame in flight, a timeline counting frames and the wait that ties them together.
	 *
	 * It exists because the explicit form of this loop is the same four lines everywhere, one of which is arithmetic that fails silently: too small a value stalls
	 * and too large a one resets a pool the GPU is still reading.
	 *
	 * The ring owns the timeline and destroys it. The pools have no individual destroy call and stay with the device.
	 */
	class FrameRing final
	{
	public:
		FrameRing() = default;

		/*
		 * Movable so Create can hand one back. Not copyable, because a copy would be a second ring over one set of pools and one timeline. Both would reset the same
		 * pool and signal the same value with nothing reporting it. The defaulted move clears the source timeline, which is what IsValid and Begin refuse on.
		 */
		FrameRing(const FrameRing &)				 = delete;
		FrameRing & operator=(const FrameRing &)	 = delete;
		FrameRing(FrameRing &&) noexcept			 = default;
		FrameRing & operator=(FrameRing &&) noexcept = default;
		~FrameRing()								 = default;

		/**
		 * \brief Creates the timeline and the per-frame pools.
		 *
		 * The queue is copied and is the one Begin waits on, so it has to be the queue the recorded work is submitted to. An invalid ring leaves the reason in error.
		 */
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

			// The depth was refused above unless it fits the pool array, so slot is inside it for the whole loop.
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

		/**
		 * \brief Opens the next frame and returns a command list to record into.
		 *
		 * Waits until the frame that last held this slot has retired, resets that slot's pool and allocates from it. The returned list has not been begun, since a
		 * caller may want a debug label or a native mutation opened first. An invalid list leaves the reason in error.
		 */
		[[nodiscard]] CommandList Begin(Error & error) noexcept
		{
			return Begin(std::numeric_limits<std::uint64_t>::max(), error);
		}

		/**
		 * \brief Opens the next frame, giving up on the retire wait after timeoutNanoseconds.
		 *
		 * \attention Every frame Begin hands out has to reach a submission that signals Signal(). A frame begun and then dropped, which is what an early return
		 * on error does, leaves its value unsignalled and the Begin one lap later waits forever. The default is an infinite wait. Pass a bound to get eTimeout back
		 * instead.
		 */
		[[nodiscard]] CommandList Begin(const std::uint64_t timeoutNanoseconds, Error & error) noexcept
		{
			error = {};

			// A ring that was never created, or was moved out of, has a depth of zero and the slot arithmetic below divides by it.
			if (!IsValid())
			{
				error = Error{ .code = ErrorCode::eInvalidState, .message = "Begin on a frame ring that was never created or was moved from" };
				return {};
			}

			// The frame this slot last carried, which is the one whose work has to be done before its pool can be recycled. Frames before the ring has filled have
			// nothing to wait for, and the timeline starts at zero so waiting on them would pass anyway.
			const std::uint64_t next	 = m_frame + 1;
			const std::uint64_t retiring = next > m_depth ? next - m_depth : 0;

			if (retiring != 0 && !m_queue.Wait(m_timeline.Get(), retiring, timeoutNanoseconds, error))
			{
				return {};
			}

			// Create refuses a depth outside the pool array and Begin refuses a ring whose depth is zero.
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

		/**
		 * \brief What the open frame's submission signals. Pass this in SubmitDesc::signals.
		 */
		[[nodiscard]] TimelinePoint Signal() const noexcept
		{
			return TimelinePoint{ .timeline = m_timeline.Get(), .value = m_frame };
		}

		/**
		 * \brief The point the open frame's resources can be released after, for DestroyPolicy::eDeferUntilSafe.
		 */
		[[nodiscard]] RetirePoint Retire() const noexcept
		{
			return RetirePoint{ .timeline = m_timeline.Get(), .value = m_frame };
		}

		[[nodiscard]] TimelineHandle Timeline() const noexcept
		{
			return m_timeline.Get();
		}

		/**
		 * \brief The open frame's number, counting from one. Zero before the first Begin.
		 */
		[[nodiscard]] std::uint64_t FrameIndex() const noexcept
		{
			return m_frame;
		}

		/**
		 * \brief Which slot the open frame occupies, below FramesInFlight.
		 *
		 * Anything a frame writes and the next frame must not touch is indexed by this: a depth target, a per-frame uniform buffer, a query pool. One of those shared
		 * across slots is the hazard going deeper than a single frame in flight actually introduces, and it corrupts quietly without failing, so the index a caller
		 * needs to avoid it comes from here and not from arithmetic they repeat.
		 */
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

} // namespace azo::rhi

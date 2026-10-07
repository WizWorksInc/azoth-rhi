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

#include "azoth/rhi/commands/sync.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace azo::rhi
{
	class TimelineBuilder final
	{
	public:
		TimelineBuilder & initial_value(std::uint64_t value) noexcept
		{
			m_desc.initialValue = value;
			return *this;
		}

		TimelineBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] TimelineDesc build() const noexcept
		{
			TimelineDesc desc = m_desc;
			desc.debugName	  = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		TimelineDesc m_desc{};
		std::string m_debugName;
	};

	class BinarySemaphoreBuilder final
	{
	public:
		BinarySemaphoreBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] BinarySemaphoreDesc build() const noexcept
		{
			BinarySemaphoreDesc desc{};
			desc.debugName = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		std::string m_debugName;
	};

	class TimelinePointBuilder final
	{
	public:
		TimelinePointBuilder & timeline(TimelineHandle timeline) noexcept
		{
			m_desc.timeline = timeline;
			return *this;
		}

		TimelinePointBuilder & value(std::uint64_t value) noexcept
		{
			m_desc.value = value;
			return *this;
		}

		TimelinePointBuilder & wait_stages(Flags<Stage> stages) noexcept
		{
			m_desc.waitStages = stages;
			return *this;
		}

		[[nodiscard]] constexpr TimelinePoint build() const noexcept
		{
			return m_desc;
		}

	private:
		TimelinePoint m_desc{};
	};

	class SwapchainSyncBuilder final
	{
	public:
		SwapchainSyncBuilder & acquired(BinarySemaphoreHandle semaphore) noexcept
		{
			m_desc.acquired = semaphore;
			return *this;
		}

		SwapchainSyncBuilder & render_finished(BinarySemaphoreHandle semaphore) noexcept
		{
			m_desc.renderFinished = semaphore;
			return *this;
		}

		SwapchainSyncBuilder & wait_stages(Flags<Stage> stages) noexcept
		{
			m_desc.waitStages = stages;
			return *this;
		}

		[[nodiscard]] constexpr SwapchainSync build() const noexcept
		{
			return m_desc;
		}

	private:
		SwapchainSync m_desc{};
	};

	class ResourceStateBuilder final
	{
	public:
		ResourceStateBuilder & use(Flags<ResourceUse> use) noexcept
		{
			m_desc.use = use;
			return *this;
		}

		ResourceStateBuilder & stages(Flags<Stage> stages) noexcept
		{
			m_desc.stages = stages;
			return *this;
		}

		[[nodiscard]] constexpr ResourceState build() const noexcept
		{
			return m_desc;
		}

	private:
		ResourceState m_desc{};
	};

	class BarrierBatchBuilder final
	{
	public:
		BarrierBatchBuilder & memory(MemoryBarrier barrier)
		{
			m_memory.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & buffer(BufferBarrier barrier)
		{
			m_buffers.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & texture(TextureBarrier barrier)
		{
			m_textures.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & clear() noexcept
		{
			m_memory.clear();
			m_buffers.clear();
			m_textures.clear();
			return *this;
		}

		[[nodiscard]] BarrierBatch build() const noexcept
		{
			return BarrierBatch{
				.memory	  = std::span<const MemoryBarrier>{ m_memory.data(), m_memory.size() },
				.buffers  = std::span<const BufferBarrier>{ m_buffers.data(), m_buffers.size() },
				.textures = std::span<const TextureBarrier>{ m_textures.data(), m_textures.size() },
			};
		}

	private:
		std::vector<MemoryBarrier> m_memory;
		std::vector<BufferBarrier> m_buffers;
		std::vector<TextureBarrier> m_textures;
	};

	class SubmitBuilder final
	{
	public:
		SubmitBuilder & add_command_list(const CommandList & commandList)
		{
			m_commandLists.push_back(&commandList);
			return *this;
		}

		SubmitBuilder & command_lists(std::span<const CommandList *> commandLists)
		{
			m_commandLists.assign(commandLists.begin(), commandLists.end());
			return *this;
		}

		SubmitBuilder & add_wait(TimelinePoint wait)
		{
			m_waits.push_back(wait);
			return *this;
		}

		SubmitBuilder & add_signal(TimelinePoint signal)
		{
			m_signals.push_back(signal);
			return *this;
		}

		SubmitBuilder & add_swapchain(SwapchainSync sync)
		{
			m_swapchains.push_back(sync);
			return *this;
		}

		SubmitBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] SubmitDesc build() noexcept
		{
			return SubmitDesc{
				.commandLists = std::span{ m_commandLists.data(), m_commandLists.size() },
				.waits		  = std::span<const TimelinePoint>{ m_waits.data(), m_waits.size() },
				.signals	  = std::span<const TimelinePoint>{ m_signals.data(), m_signals.size() },
				.swapchains	  = std::span<const SwapchainSync>{ m_swapchains.data(), m_swapchains.size() },
				.debugName	  = m_debugName.empty() ? nullptr : m_debugName.c_str(),
			};
		}

	private:
		std::vector<const CommandList *> m_commandLists;
		std::vector<TimelinePoint> m_waits;
		std::vector<TimelinePoint> m_signals;
		std::vector<SwapchainSync> m_swapchains;
		std::string m_debugName;
	};
}

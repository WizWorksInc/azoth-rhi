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
 * \brief Builders for synchronization and submission descriptions.
 */

#include "azoth/rhi/commands/sync.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace azo::rhi
{
	/**
	 * \brief Builds timeline descriptions backed by owned debug-name storage.
	 */
	class TimelineBuilder final
	{
	public:
		TimelineBuilder & InitialValue(std::uint64_t value) noexcept
		{
			m_desc.initialValue = value;
			return *this;
		}

		TimelineBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a timeline description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] TimelineDesc Build() const noexcept
		{
			TimelineDesc desc = m_desc;
			desc.debugName	  = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		TimelineDesc m_desc{};
		std::string m_debugName;
	};

	/**
	 * \brief Builds binary-semaphore descriptions backed by owned debug-name storage.
	 */
	class BinarySemaphoreBuilder final
	{
	public:
		BinarySemaphoreBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a binary-semaphore description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] BinarySemaphoreDesc Build() const noexcept
		{
			BinarySemaphoreDesc desc{};
			desc.debugName = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		std::string m_debugName;
	};

	/**
	 * \brief Builds timeline points used by submit waits and signals.
	 *
	 * \note waitStages is consumed only for waits. Timeline signals use only timeline and value.
	 */
	class TimelinePointBuilder final
	{
	public:
		TimelinePointBuilder & Timeline(TimelineHandle timeline) noexcept
		{
			m_desc.timeline = timeline;
			return *this;
		}

		TimelinePointBuilder & Value(std::uint64_t value) noexcept
		{
			m_desc.value = value;
			return *this;
		}

		/**
		 * \brief Sets the pipeline stages blocked by this timeline point when it is used as a submit wait.
		 */
		TimelinePointBuilder & WaitStages(Flags<PipelineStage> stages) noexcept
		{
			m_desc.waitStages = stages;
			return *this;
		}

		[[nodiscard]] constexpr TimelinePoint Build() const noexcept
		{
			return m_desc;
		}

	private:
		TimelinePoint m_desc{};
	};

	/**
	 * \brief Builds the acquire and present semaphore pair one surface contributes to a submission.
	 */
	class SwapchainSyncBuilder final
	{
	public:
		SwapchainSyncBuilder & Acquired(BinarySemaphoreHandle semaphore) noexcept
		{
			m_desc.acquired = semaphore;
			return *this;
		}

		SwapchainSyncBuilder & RenderFinished(BinarySemaphoreHandle semaphore) noexcept
		{
			m_desc.renderFinished = semaphore;
			return *this;
		}

		/**
		 * \brief Sets the pipeline stages blocked by the acquire wait. Defaults to the color-attachment stage.
		 */
		SwapchainSyncBuilder & WaitStages(Flags<PipelineStage> stages) noexcept
		{
			m_desc.waitStages = stages;
			return *this;
		}

		[[nodiscard]] constexpr SwapchainSync Build() const noexcept
		{
			return m_desc;
		}

	private:
		SwapchainSync m_desc{};
	};

	/**
	 * \brief Builds resource states used by barriers and rendering attachments.
	 */
	class ResourceStateBuilder final
	{
	public:
		ResourceStateBuilder & Stages(Flags<PipelineStage> stages) noexcept
		{
			m_desc.stages = stages;
			return *this;
		}

		ResourceStateBuilder & Access(Flags<Access> access) noexcept
		{
			m_desc.access = access;
			return *this;
		}

		ResourceStateBuilder & Layout(TextureLayout layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		[[nodiscard]] constexpr ResourceState Build() const noexcept
		{
			return m_desc;
		}

	private:
		ResourceState m_desc{};
	};

	/**
	 * \brief Builds barrier batches backed by owned barrier-list storage.
	 */
	class BarrierBatchBuilder final
	{
	public:
		BarrierBatchBuilder & Memory(MemoryBarrier barrier)
		{
			m_memory.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & Buffer(BufferBarrier barrier)
		{
			m_buffers.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & Texture(TextureBarrier barrier)
		{
			m_textures.push_back(barrier);
			return *this;
		}

		BarrierBatchBuilder & Clear() noexcept
		{
			m_memory.clear();
			m_buffers.clear();
			m_textures.clear();
			return *this;
		}

		/**
		 * \brief Builds a barrier batch that borrows this builder's barrier-list storage.
		 *
		 * \attention The returned spans stay valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] BarrierBatch Build() const noexcept
		{
			return BarrierBatch{ .memory = std::span<const MemoryBarrier>{ m_memory.data(), m_memory.size() },
				.buffers				 = std::span<const BufferBarrier>{ m_buffers.data(), m_buffers.size() },
				.textures				 = std::span<const TextureBarrier>{ m_textures.data(), m_textures.size() } };
		}

	private:
		std::vector<MemoryBarrier> m_memory;
		std::vector<BufferBarrier> m_buffers;
		std::vector<TextureBarrier> m_textures;
	};

	/**
	 * \brief Builds submit descriptions backed by owned dependency, signal, command-list pointer, and debug-name storage.
	 *
	 * \attention Command-list pointers are borrowed. The pointed command lists must remain valid through queue submission.
	 */
	class SubmitBuilder final
	{
	public:
		SubmitBuilder & AddCommandList(const CommandList & commandList)
		{
			m_commandLists.push_back(&commandList);
			return *this;
		}

		/**
		 * \brief Copies command-list pointers into the submit list.
		 *
		 * \attention The pointed command lists are borrowed and must remain valid through queue submission.
		 */
		SubmitBuilder & CommandLists(std::span<const CommandList *> commandLists)
		{
			m_commandLists.assign(commandLists.begin(), commandLists.end());
			return *this;
		}

		SubmitBuilder & AddWait(TimelinePoint wait)
		{
			m_waits.push_back(wait);
			return *this;
		}

		/**
		 * \brief Adds a timeline signal. The signal point uses timeline and value, not waitStages.
		 */
		SubmitBuilder & AddSignal(TimelinePoint signal)
		{
			m_signals.push_back(signal);
			return *this;
		}

		/**
		 * \brief Adds the acquire and present semaphores of one surface this submission renders to.
		 */
		SubmitBuilder & AddSwapchain(SwapchainSync sync)
		{
			m_swapchains.push_back(sync);
			return *this;
		}

		SubmitBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Builds a submit description that borrows this builder's storage.
		 *
		 * \attention The returned spans and debugName pointer stay valid only until this builder is modified or destroyed.
		 */
		[[nodiscard]] SubmitDesc Build() noexcept
		{
			return SubmitDesc{ .commandLists = std::span{ m_commandLists.data(), m_commandLists.size() },
				.waits						 = std::span<const TimelinePoint>{ m_waits.data(), m_waits.size() },
				.signals					 = std::span<const TimelinePoint>{ m_signals.data(), m_signals.size() },
				.swapchains					 = std::span<const SwapchainSync>{ m_swapchains.data(), m_swapchains.size() },
				.debugName					 = m_debugName.empty() ? nullptr : m_debugName.c_str() };
		}

	private:
		std::vector<const CommandList *> m_commandLists;
		std::vector<TimelinePoint> m_waits;
		std::vector<TimelinePoint> m_signals;
		std::vector<SwapchainSync> m_swapchains;
		std::string m_debugName;
	};
} // namespace azo::rhi

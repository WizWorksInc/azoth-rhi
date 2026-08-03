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

#include <azoth/rhi/core/flags.hpp>
#include <azoth/rhi/core/resource_handles.hpp>
#include <azoth/rhi/device/device.hpp>
#include <azoth/rhi/resources/resources.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fw::render
{
	/**
	 * \brief A slice of a pooled buffer, which is what one frame's worth of one thing was written into.
	 *
	 * Holds no ownership. It stays usable until the pool it came from is reset, and the caller is the one who knows the GPU is finished with it.
	 */
	struct BufferAllocation final
	{
		azo::rhi::BufferHandle buffer;

		/**
		 * \brief Where the slice starts, which is what a dynamic offset or a bind offset is given.
		 */
		std::uint64_t offset = 0;

		std::uint64_t size = 0;

		/**
		 * \brief Where to write, already advanced to the offset.
		 */
		std::uint8_t * data = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return data != nullptr && buffer.IsValid();
		}

		/**
		 * \brief Copies one object in, which is the whole of what a per-draw or per-frame block usually needs.
		 *
		 * \return False when the object does not fit, which means the allocation was made for something smaller.
		 */
		template <typename T>
		bool Write(const T & value) const noexcept
		{
			return WriteBytes(std::span{ reinterpret_cast<const std::uint8_t *>(&value), sizeof(T) }); // NOLINT(*-pro-type-reinterpret-cast)
		}

		[[nodiscard]] bool WriteBytes(std::span<const std::uint8_t> bytes) const noexcept;
	};

	/**
	 * \brief Buffers a frame's transient writes are cut out of, handed back all at once.
	 *
	 * A buffer for each small thing a frame writes is the wrong shape twice: creation is not free, and each has to be destroyed at a point the GPU has passed.
	 * So slices are cut off one big buffer and Reset moves the cut back.
	 *
	 * \attention Reset frees every slice at once, so it belongs after the wait for the frame that took them.
	 */
	class BufferPool final
	{
	public:
		/**
		 * \brief A pool that cuts its slices out of blocks allocated from device.
		 *
		 * \param blockBytes How big a block is. An allocation larger than this gets a block of its own sized to fit.
		 * \param usage What the buffers are bound as, which also decides the alignment slices are cut at.
		 */
		BufferPool(azo::rhi::Device device, std::uint64_t blockBytes, azo::rhi::Flags<azo::rhi::BufferUsage> usage);

		BufferPool(const BufferPool &)			   = delete;
		BufferPool & operator=(const BufferPool &) = delete;
		BufferPool(BufferPool &&)				   = delete;
		BufferPool & operator=(BufferPool &&)	   = delete;

		~BufferPool();

		/**
		 * \brief Cuts a slice, creating another block when what is in hand has no room.
		 *
		 * \return The slice, or an invalid one when a block could not be created.
		 */
		[[nodiscard]] BufferAllocation Allocate(std::uint64_t bytes);

		/**
		 * \brief Cuts a slice sized for one object and copies it in.
		 */
		template <typename T>
		[[nodiscard]] BufferAllocation AllocateFor(const T & value)
		{
			const BufferAllocation allocation = Allocate(sizeof(T));
			if (allocation.IsValid())
			{
				static_cast<void>(allocation.Write(value));
			}

			return allocation;
		}

		/**
		 * \brief Makes every slice handed out since the last reset available again, keeping the blocks themselves.
		 */
		void Reset() noexcept;

		/**
		 * \brief How many blocks the pool has settled at, which is what says a block size was chosen too small.
		 */
		[[nodiscard]] std::size_t GetBlockCount() const noexcept
		{
			return m_blocks.size();
		}

		/**
		 * \brief Bytes across every block, which is what the pool has actually taken from the device.
		 */
		[[nodiscard]] std::uint64_t GetCapacity() const noexcept;

	private:
		// One buffer, mapped for its lifetime, with a cut that only ever moves forward until the pool is reset.
		struct Block final
		{
			azo::rhi::BufferHandle buffer;
			std::uint8_t * data		= nullptr;
			std::uint64_t sizeBytes = 0;
			std::uint64_t offset	= 0;
			bool coherent			= false;
		};

		[[nodiscard]] Block * CreateBlock(std::uint64_t bytes);

		azo::rhi::Device m_device;
		std::uint64_t m_blockBytes = 0;
		azo::rhi::Flags<azo::rhi::BufferUsage> m_usage;
		std::uint64_t m_alignment = 1;

		// Held by pointer so a slice's data stays put when the vector grows.
		std::vector<std::unique_ptr<Block>> m_blocks;
	};
} // namespace fw::render

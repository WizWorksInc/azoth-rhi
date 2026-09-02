// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
	struct BufferAllocation final
	{
		azo::rhi::BufferHandle buffer;

		std::uint64_t offset = 0;

		std::uint64_t size = 0;

		std::uint8_t * data = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return data != nullptr && buffer.IsValid();
		}

		template <typename T>
		bool Write(const T & value) const noexcept
		{
			return WriteBytes(std::span{ reinterpret_cast<const std::uint8_t *>(&value), sizeof(T) }); // NOLINT(*-pro-type-reinterpret-cast)
		}

		[[nodiscard]] bool WriteBytes(std::span<const std::uint8_t> bytes) const noexcept;
	};

	class BufferPool final
	{
	public:
		BufferPool(azo::rhi::Device device, std::uint64_t blockBytes, azo::rhi::Flags<azo::rhi::BufferUsage> usage);

		BufferPool(const BufferPool &)			   = delete;
		BufferPool & operator=(const BufferPool &) = delete;
		BufferPool(BufferPool &&)				   = delete;
		BufferPool & operator=(BufferPool &&)	   = delete;

		~BufferPool();

		[[nodiscard]] BufferAllocation Allocate(std::uint64_t bytes);

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

		void Reset() noexcept;

		[[nodiscard]] std::size_t GetBlockCount() const noexcept
		{
			return m_blocks.size();
		}

		[[nodiscard]] std::uint64_t GetCapacity() const noexcept;

	private:
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

		std::vector<std::unique_ptr<Block>> m_blocks;
	};
}

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

#include "FW/render/BufferPool.hpp"

#include "FW/utility/Log.hpp"

#include <algorithm>
#include <cstring>

namespace fw::render
{
	namespace
	{
		[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment) noexcept
		{
			return alignment <= 1 ? value : ((value + alignment - 1) & ~(alignment - 1));
		}

		[[nodiscard]] std::uint64_t AlignmentFor(const azo::rhi::Device & device, const azo::rhi::Flags<azo::rhi::BufferUsage> usage) noexcept
		{
			const azo::rhi::DeviceCaps & caps = device.get_caps();

			std::uint64_t alignment = 4;
			if (usage.contains(azo::rhi::BufferUsage::eUniform))
			{
				alignment = std::max(alignment, caps.minUniformBufferOffsetAlignment);
			}

			if (usage.contains(azo::rhi::BufferUsage::eStorage))
			{
				alignment = std::max(alignment, caps.minStorageBufferOffsetAlignment);
			}

			return std::max<std::uint64_t>(alignment, 1);
		}
	}

	bool BufferAllocation::WriteBytes(const std::span<const std::uint8_t> bytes) const noexcept
	{
		if (data == nullptr || bytes.size() > size)
		{
			return false;
		}

		if (!bytes.empty())
		{
			std::memcpy(data, bytes.data(), bytes.size());
		}

		return true;
	}

	BufferPool::BufferPool(const azo::rhi::Device device, const std::uint64_t blockBytes, const azo::rhi::Flags<azo::rhi::BufferUsage> usage)
		: m_device(device),
		  m_blockBytes(std::max<std::uint64_t>(blockBytes, 1)),
		  m_usage(usage),
		  m_alignment(device.is_valid() ? AlignmentFor(device, usage) : 1)
	{
	}

	BufferPool::~BufferPool()
	{
		for (const std::unique_ptr<Block> & block : m_blocks)
		{
			if (block->data != nullptr)
			{
				static_cast<void>(m_device.unmap(block->buffer));
			}

			static_cast<void>(m_device.destroy(block->buffer));
		}
	}

	BufferAllocation BufferPool::Allocate(const std::uint64_t bytes)
	{
		if (!m_device.is_valid() || bytes == 0)
		{
			return {};
		}

		for (const std::unique_ptr<Block> & block : m_blocks)
		{
			const std::uint64_t start = AlignUp(block->offset, m_alignment);
			if (start <= block->sizeBytes && bytes <= block->sizeBytes - start)
			{
				block->offset = start + bytes;

				return BufferAllocation{
					.buffer = block->buffer,
					.offset = start,
					.size	= bytes,
					.data	= block->data + start, // NOLINT(*-pro-bounds-pointer-arithmetic)
				};
			}
		}

		Block * fresh = CreateBlock(std::max(m_blockBytes, AlignUp(bytes, m_alignment)));
		if (fresh == nullptr)
		{
			return {};
		}

		fresh->offset = bytes;

		return BufferAllocation{
			.buffer = fresh->buffer,
			.offset = 0,
			.size	= bytes,
			.data	= fresh->data,
		};
	}

	void BufferPool::Reset() noexcept
	{
		for (const std::unique_ptr<Block> & block : m_blocks)
		{
			block->offset = 0;
		}
	}

	std::uint64_t BufferPool::GetCapacity() const noexcept
	{
		std::uint64_t total = 0;
		for (const std::unique_ptr<Block> & block : m_blocks)
		{
			total += block->sizeBytes;
		}

		return total;
	}

	BufferPool::Block * BufferPool::CreateBlock(const std::uint64_t bytes)
	{
		azo::rhi::Error error{};

		auto block		 = std::make_unique<Block>();
		block->sizeBytes = bytes;
		block->buffer	 = m_device.create_buffer(
			azo::rhi::BufferDesc{
				.size	   = bytes,
				.usage	   = m_usage,
				.memory	   = azo::rhi::MemoryUsage::eCpuToGpu,
				.debugName = "fw.render.bufferPool",
			},
			error);

		if (!block->buffer.is_valid())
		{
			LOG_ERROR(
				fw::Log(), "buffer pool: a block of {} bytes could not be created: {}", bytes, error.message != nullptr ? error.message : "no diagnostic");
			return nullptr;
		}

		const azo::rhi::MappedMemory mapped = m_device.map(block->buffer, azo::rhi::MapDesc{ .mode = azo::rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			LOG_ERROR(fw::Log(), "buffer pool: a block could not be mapped: {}", error.message != nullptr ? error.message : "no diagnostic");
			static_cast<void>(m_device.destroy(block->buffer));
			return nullptr;
		}

		block->data		= static_cast<std::uint8_t *>(mapped.data);
		block->coherent = mapped.coherent;

		if (!block->coherent)
		{
			LOG_WARNING(fw::Log(), "buffer pool: this device's upload memory is not coherent, so writes need a flush the pool does not do");
		}

		m_blocks.push_back(std::move(block));

		return m_blocks.back().get();
	}
}

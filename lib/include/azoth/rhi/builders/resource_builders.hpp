// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/resources/resources.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace azo::rhi
{
	class BufferBuilder final
	{
	public:
		BufferBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		BufferBuilder & Stride(std::uint64_t stride) noexcept
		{
			m_desc.stride = stride;
			return *this;
		}

		BufferBuilder & Usage(Flags<BufferUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		BufferBuilder & AddUsage(BufferUsage usage) noexcept
		{
			m_desc.usage = m_desc.usage | usage;
			return *this;
		}

		BufferBuilder & Memory(MemoryUsage memory) noexcept
		{
			m_desc.memory = memory;
			return *this;
		}

		BufferBuilder & GpuOnly() noexcept
		{
			return Memory(MemoryUsage::eGpuOnly);
		}

		BufferBuilder & CpuUpload() noexcept
		{
			return Memory(MemoryUsage::eCpuUpload);
		}

		BufferBuilder & CpuReadback() noexcept
		{
			return Memory(MemoryUsage::eCpuReadback);
		}

		BufferBuilder & Aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		BufferBuilder & SparseBinding(bool enabled = true) noexcept
		{
			m_desc.allowSparseBinding = enabled;
			return *this;
		}

		BufferBuilder & PersistentMap(bool enabled = true) noexcept
		{
			m_desc.persistentMap = enabled;
			return *this;
		}

		BufferBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] BufferDesc Build() const noexcept
		{
			BufferDesc desc = m_desc;
			desc.debugName	= m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		BufferDesc m_desc{};
		std::string m_debugName;
	};

	class TextureBuilder final
	{
	public:
		TextureBuilder & Type(TextureType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		TextureBuilder & Format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		TextureBuilder & Extent(std::uint32_t width, std::uint32_t height = 1, std::uint32_t depth = 1) noexcept
		{
			m_desc.width  = width;
			m_desc.height = height;
			m_desc.depth  = depth;
			return *this;
		}

		TextureBuilder & Mips(std::uint32_t mipLevels) noexcept
		{
			m_desc.mipLevels = mipLevels;
			return *this;
		}

		TextureBuilder & Layers(std::uint32_t arrayLayers) noexcept
		{
			m_desc.arrayLayers = arrayLayers;
			return *this;
		}

		TextureBuilder & Samples(SampleCount samples) noexcept
		{
			m_desc.samples = samples;
			return *this;
		}

		TextureBuilder & Usage(Flags<TextureUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		TextureBuilder & AddUsage(TextureUsage usage) noexcept
		{
			m_desc.usage = m_desc.usage | usage;
			return *this;
		}

		TextureBuilder & Memory(MemoryUsage memory) noexcept
		{
			m_desc.memory = memory;
			return *this;
		}

		TextureBuilder & Aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		TextureBuilder & SparseBinding(bool enabled = true) noexcept
		{
			m_desc.allowSparseBinding = enabled;
			return *this;
		}

		TextureBuilder & FormatViews(bool enabled = true) noexcept
		{
			m_desc.allowFormatViews = enabled;
			return *this;
		}

		TextureBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] TextureDesc Build() const noexcept
		{
			TextureDesc desc = m_desc;
			desc.debugName	 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		TextureDesc m_desc{};
		std::string m_debugName;
	};

	class MapBuilder final
	{
	public:
		MapBuilder & Mode(MapMode mode) noexcept
		{
			m_desc.mode = mode;
			return *this;
		}

		MapBuilder & Read() noexcept
		{
			return Mode(MapMode::eRead);
		}

		MapBuilder & Write() noexcept
		{
			return Mode(MapMode::eWrite);
		}

		MapBuilder & ReadWrite() noexcept
		{
			return Mode(MapMode::eReadWrite);
		}

		MapBuilder & Offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		MapBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		MapBuilder & WholeBuffer() noexcept
		{
			m_desc.offset = 0;
			m_desc.size	  = std::numeric_limits<std::uint64_t>::max();
			return *this;
		}

		[[nodiscard]] constexpr MapDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		MapDesc m_desc{};
	};

	class HeapBuilder final
	{
	public:
		HeapBuilder & Type(HeapType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		HeapBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		HeapBuilder & Alignment(std::uint64_t alignment) noexcept
		{
			m_desc.alignment = alignment;
			return *this;
		}

		HeapBuilder & AllowBuffers(bool enabled = true) noexcept
		{
			m_desc.allowBuffers = enabled;
			return *this;
		}

		HeapBuilder & AllowTextures(bool enabled = true) noexcept
		{
			m_desc.allowTextures = enabled;
			return *this;
		}

		HeapBuilder & Aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		HeapBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] HeapDesc Build() const noexcept
		{
			HeapDesc desc  = m_desc;
			desc.debugName = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		HeapDesc m_desc{};
		std::string m_debugName;
	};

	class PlacedBufferBuilder final
	{
	public:
		PlacedBufferBuilder & Buffer(const BufferDesc & buffer) noexcept
		{
			m_desc.buffer = buffer;
			return *this;
		}

		PlacedBufferBuilder & Heap(HeapHandle heap) noexcept
		{
			m_desc.heap = heap;
			return *this;
		}

		PlacedBufferBuilder & Offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr PlacedBufferDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		PlacedBufferDesc m_desc{};
	};

	class PlacedTextureBuilder final
	{
	public:
		PlacedTextureBuilder & Texture(const TextureDesc & texture) noexcept
		{
			m_desc.texture = texture;
			return *this;
		}

		PlacedTextureBuilder & Heap(HeapHandle heap) noexcept
		{
			m_desc.heap = heap;
			return *this;
		}

		PlacedTextureBuilder & Offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr PlacedTextureDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		PlacedTextureDesc m_desc{};
	};

	class ResidencyPriorityBuilder final
	{
	public:
		ResidencyPriorityBuilder & Buffer(BufferHandle buffer) noexcept
		{
			m_desc.buffer  = buffer;
			m_desc.texture = {};
			return *this;
		}

		ResidencyPriorityBuilder & Texture(TextureHandle texture) noexcept
		{
			m_desc.texture = texture;
			m_desc.buffer  = {};
			return *this;
		}

		ResidencyPriorityBuilder & Priority(ResidencyPriority priority) noexcept
		{
			m_desc.priority = priority;
			return *this;
		}

		[[nodiscard]] constexpr ResidencyPriorityDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		ResidencyPriorityDesc m_desc{};
	};
}

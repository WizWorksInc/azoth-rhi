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
		BufferBuilder & size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		BufferBuilder & stride(std::uint64_t stride) noexcept
		{
			m_desc.stride = stride;
			return *this;
		}

		BufferBuilder & usage(Flags<BufferUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		BufferBuilder & add_usage(BufferUsage usage) noexcept
		{
			m_desc.usage = m_desc.usage | usage;
			return *this;
		}

		BufferBuilder & memory(MemoryUsage memory) noexcept
		{
			m_desc.memory = memory;
			return *this;
		}

		BufferBuilder & gpu_only() noexcept
		{
			return memory(MemoryUsage::eGpuOnly);
		}

		BufferBuilder & cpu_upload() noexcept
		{
			return memory(MemoryUsage::eCpuUpload);
		}

		BufferBuilder & cpu_readback() noexcept
		{
			return memory(MemoryUsage::eCpuReadback);
		}

		BufferBuilder & aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		BufferBuilder & sparse_binding(bool enabled = true) noexcept
		{
			m_desc.allowSparseBinding = enabled;
			return *this;
		}

		BufferBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] BufferDesc build() const noexcept
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
		TextureBuilder & type(TextureType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		TextureBuilder & format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		TextureBuilder & extent(std::uint32_t width, std::uint32_t height = 1, std::uint32_t depth = 1) noexcept
		{
			m_desc.width  = width;
			m_desc.height = height;
			m_desc.depth  = depth;
			return *this;
		}

		TextureBuilder & mips(std::uint32_t mipLevels) noexcept
		{
			m_desc.mipLevels = mipLevels;
			return *this;
		}

		TextureBuilder & layers(std::uint32_t arrayLayers) noexcept
		{
			m_desc.arrayLayers = arrayLayers;
			return *this;
		}

		TextureBuilder & samples(SampleCount samples) noexcept
		{
			m_desc.samples = samples;
			return *this;
		}

		TextureBuilder & usage(Flags<TextureUsage> usage) noexcept
		{
			m_desc.usage = usage;
			return *this;
		}

		TextureBuilder & add_usage(TextureUsage usage) noexcept
		{
			m_desc.usage = m_desc.usage | usage;
			return *this;
		}

		TextureBuilder & memory(MemoryUsage memory) noexcept
		{
			m_desc.memory = memory;
			return *this;
		}

		TextureBuilder & aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		TextureBuilder & sparse_binding(bool enabled = true) noexcept
		{
			m_desc.allowSparseBinding = enabled;
			return *this;
		}

		TextureBuilder & format_views(bool enabled = true) noexcept
		{
			m_desc.allowFormatViews = enabled;
			return *this;
		}

		TextureBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] TextureDesc build() const noexcept
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
		MapBuilder & mode(MapMode mode) noexcept
		{
			m_desc.mode = mode;
			return *this;
		}

		MapBuilder & read() noexcept
		{
			return mode(MapMode::eRead);
		}

		MapBuilder & write() noexcept
		{
			return mode(MapMode::eWrite);
		}

		MapBuilder & read_write() noexcept
		{
			return mode(MapMode::eReadWrite);
		}

		MapBuilder & offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		MapBuilder & size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		MapBuilder & whole_buffer() noexcept
		{
			m_desc.offset = 0;
			m_desc.size	  = std::numeric_limits<std::uint64_t>::max();
			return *this;
		}

		[[nodiscard]] constexpr MapDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		MapDesc m_desc{};
	};

	class HeapBuilder final
	{
	public:
		HeapBuilder & type(HeapType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		HeapBuilder & size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		HeapBuilder & alignment(std::uint64_t alignment) noexcept
		{
			m_desc.alignment = alignment;
			return *this;
		}

		HeapBuilder & allow_buffers(bool enabled = true) noexcept
		{
			m_desc.allowBuffers = enabled;
			return *this;
		}

		HeapBuilder & allow_textures(bool enabled = true) noexcept
		{
			m_desc.allowTextures = enabled;
			return *this;
		}

		HeapBuilder & aliasing(bool enabled = true) noexcept
		{
			m_desc.allowAliasing = enabled;
			return *this;
		}

		HeapBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] HeapDesc build() const noexcept
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
		PlacedBufferBuilder & buffer(const BufferDesc & buffer) noexcept
		{
			m_desc.buffer = buffer;
			return *this;
		}

		PlacedBufferBuilder & heap(HeapHandle heap) noexcept
		{
			m_desc.heap = heap;
			return *this;
		}

		PlacedBufferBuilder & offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr PlacedBufferDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		PlacedBufferDesc m_desc{};
	};

	class PlacedTextureBuilder final
	{
	public:
		PlacedTextureBuilder & texture(const TextureDesc & texture) noexcept
		{
			m_desc.texture = texture;
			return *this;
		}

		PlacedTextureBuilder & heap(HeapHandle heap) noexcept
		{
			m_desc.heap = heap;
			return *this;
		}

		PlacedTextureBuilder & offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr PlacedTextureDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		PlacedTextureDesc m_desc{};
	};

	class ResidencyPriorityBuilder final
	{
	public:
		ResidencyPriorityBuilder & buffer(BufferHandle buffer) noexcept
		{
			m_desc.buffer  = buffer;
			m_desc.texture = {};
			return *this;
		}

		ResidencyPriorityBuilder & texture(TextureHandle texture) noexcept
		{
			m_desc.texture = texture;
			m_desc.buffer  = {};
			return *this;
		}

		ResidencyPriorityBuilder & priority(ResidencyPriority priority) noexcept
		{
			m_desc.priority = priority;
			return *this;
		}

		[[nodiscard]] constexpr ResidencyPriorityDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		ResidencyPriorityDesc m_desc{};
	};
}

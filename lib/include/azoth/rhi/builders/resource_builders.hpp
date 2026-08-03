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
 * \brief Builders for resource, mapping, heap, placement, and residency descriptions.
 */

#include "azoth/rhi/resources/resources.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace azo::rhi
{
	/**
	 * \brief Builds buffer descriptions backed by owned debug-name storage.
	 */
	class BufferBuilder final
	{
	public:
		/**
		 * \brief Sets the buffer size in bytes.
		 */
		BufferBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		/**
		 * \brief Sets the byte stride for structured or element-addressed buffer usage.
		 */
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

		/**
		 * \brief Builds a buffer description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
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

	/**
	 * \brief Builds texture descriptions backed by owned debug-name storage.
	 */
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

		/**
		 * \brief Sets the texture extent in texels.
		 *
		 * \param width Texture width in texels.
		 * \param height Texture height in texels.
		 * \param depth Texture depth in texels.
		 */
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

		TextureBuilder & InitialLayout(TextureLayout layout) noexcept
		{
			m_desc.initialLayout = layout;
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

		/**
		 * \brief Builds a texture description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
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

	/**
	 * \brief Builds map descriptions.
	 */
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

		/**
		 * \brief Sets the byte offset where the mapped range begins.
		 */
		MapBuilder & Offset(std::uint64_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		/**
		 * \brief Sets the byte size of the mapped range.
		 */
		MapBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		/**
		 * \brief Maps the whole buffer by using the resource API's max-size sentinel.
		 */
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

	/**
	 * \brief Builds heap descriptions backed by owned debug-name storage.
	 */
	class HeapBuilder final
	{
	public:
		HeapBuilder & Type(HeapType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		/**
		 * \brief Sets the heap size in bytes.
		 */
		HeapBuilder & Size(std::uint64_t size) noexcept
		{
			m_desc.size = size;
			return *this;
		}

		/**
		 * \brief Sets the heap alignment in bytes.
		 */
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

		/**
		 * \brief Builds a heap description that borrows this builder's debug-name storage.
		 *
		 * \attention The returned debugName pointer stays valid only until this builder is modified or destroyed.
		 */
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

	/**
	 * \brief Builds placed-buffer descriptions by copying the supplied BufferDesc exactly.
	 *
	 * \attention If the nested BufferDesc contains borrowed pointers, their original lifetime requirements still apply.
	 */
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

		/**
		 * \brief Sets the byte offset of the placed buffer inside the heap.
		 */
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

	/**
	 * \brief Builds placed-texture descriptions by copying the supplied TextureDesc exactly.
	 *
	 * \attention If the nested TextureDesc contains borrowed pointers, their original lifetime requirements still apply.
	 */
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

		/**
		 * \brief Sets the byte offset of the placed texture inside the heap.
		 */
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

	/**
	 * \brief Builds residency-priority descriptions for either one buffer or one texture.
	 */
	class ResidencyPriorityBuilder final
	{
	public:
		/**
		 * \brief Selects a buffer target and clears any previously selected texture target.
		 */
		ResidencyPriorityBuilder & Buffer(BufferHandle buffer) noexcept
		{
			m_desc.buffer  = buffer;
			m_desc.texture = {};
			return *this;
		}

		/**
		 * \brief Selects a texture target and clears any previously selected buffer target.
		 */
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
} // namespace azo::rhi

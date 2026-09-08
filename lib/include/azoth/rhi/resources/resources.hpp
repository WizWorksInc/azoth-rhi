// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{

	enum class BufferUsage : std::uint32_t // NOLINT(performance-enum-size)
	{
		eCopySrc = 1u << 0u,
		eCopyDst = 1u << 1u,
		eVertex	 = 1u << 2u,
		eIndex	 = 1u << 3u,

		eUniform = 1u << 4u,

		eStorage = 1u << 5u,

		eIndirect = 1u << 6u,

		eShaderBindingTable = 1u << 7u,

		eAccelerationStructureInput = 1u << 8u,

		eAccelerationStructureStorage = 1u << 9u,

		eSparseBinding = 1u << 10u,
	};

	enum class TextureUsage : std::uint32_t // NOLINT(performance-enum-size)
	{
		eCopySrc = 1u << 0u,
		eCopyDst = 1u << 1u,

		eSampled = 1u << 2u,

		eStorage = 1u << 3u,

		eColorAttachment		= 1u << 4u,
		eDepthStencilAttachment = 1u << 5u,

		eTransientAttachment = 1u << 6u,

		ePresent = 1u << 7u,

		eSparseBinding = 1u << 8u,
	};

	enum class MemoryUsage : std::uint8_t
	{
		eGpuOnly,

		eCpuUpload,

		eCpuReadback,

		eCpuToGpu,

		eGpuToCpu,

		eTransient,

		eReserved,
	};

	enum class MemoryAccess : std::uint8_t
	{
		eGpuOnly,

		eCpuVisible,

		eCpuVisibleCoherent,

		eCpuVisibleCached,
	};

	struct BufferDesc final
	{
		std::uint64_t size	 = 0;
		std::uint64_t stride = 0;
		Flags<BufferUsage> usage;
		MemoryUsage memory = MemoryUsage::eGpuOnly;

		bool allowAliasing = false;

		bool allowSparseBinding = false;

		bool persistentMap = false;

		const char * debugName = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct TextureDesc final
	{
		TextureType type		  = TextureType::eTex2D;
		Format format			  = Format::eUndefined;
		std::uint32_t width		  = 1;
		std::uint32_t height	  = 1;
		std::uint32_t depth		  = 1;
		std::uint32_t mipLevels	  = 1;
		std::uint32_t arrayLayers = 1;
		SampleCount samples		  = SampleCount::e1;
		Flags<TextureUsage> usage;
		MemoryUsage memory = MemoryUsage::eGpuOnly;

		bool allowAliasing = false;

		bool allowSparseBinding = false;

		bool allowFormatViews = false;

		const char * debugName = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct ExternalBufferImportDesc final
	{
		ExternalHandle handle{};
		BufferDesc desc{};
	};

	struct ExternalTextureImportDesc final
	{
		ExternalHandle handle{};
		TextureDesc desc{};
	};

	struct BufferInfo final
	{
		BufferDesc desc{};
		std::uint64_t allocationSize = 0;
		MemoryAccess memoryAccess	 = MemoryAccess::eGpuOnly;
	};

	struct TextureInfo final
	{
		TextureDesc desc{};
		std::uint64_t allocationSize = 0;
	};

	enum class MapMode : std::uint8_t
	{
		eRead,
		eWrite,
		eReadWrite,
	};

	struct MapDesc final
	{
		MapMode mode		 = MapMode::eWrite;
		std::uint64_t offset = 0;
		std::uint64_t size	 = std::numeric_limits<std::uint64_t>::max();
	};

	struct MappedMemory final
	{
		void * data		   = nullptr;
		std::uint64_t size = 0;
		bool coherent	   = false;
	};

	enum class HeapType : std::uint8_t
	{
		eGpuLocal,
		eCpuUpload,
		eCpuReadback,

		eTransient,
	};

	struct HeapDesc final
	{
		HeapType type			= HeapType::eGpuLocal;
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;

		bool allowBuffers = true;

		bool allowTextures = true;

		bool allowAliasing = false;

		const char * debugName = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct ExternalHeapImportDesc final
	{
		ExternalHandle handle{};
		HeapDesc desc{};
	};

	struct PlacedBufferDesc final
	{
		BufferDesc buffer{};
		HeapHandle heap{};
		std::uint64_t offset = 0;
	};

	struct PlacedTextureDesc final
	{
		TextureDesc texture{};
		HeapHandle heap{};
		std::uint64_t offset = 0;
	};

	struct MemoryInfo final
	{
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;
	};

	struct AliasBarrier final
	{
		BufferHandle beforeBuffer{};
		TextureHandle beforeTexture{};
		BufferHandle afterBuffer{};
		TextureHandle afterTexture{};
	};

	struct MemoryBudgetInfo final
	{
		HeapType heap							   = HeapType::eGpuLocal;
		std::uint64_t budgetBytes				   = 0;
		std::uint64_t usageBytes				   = 0;
		std::uint64_t availableForReservationBytes = 0;

		bool budgetIsPrecise = false;
	};

	enum class ResidencyPriority : std::uint8_t
	{
		eMinimum,
		eLow,
		eNormal,
		eHigh,
		eCritical,
	};

	struct ResidencyPriorityDesc final
	{
		BufferHandle buffer{};
		TextureHandle texture{};
		ResidencyPriority priority = ResidencyPriority::eNormal;
	};

	struct SparseMemoryPage final
	{
		HeapHandle heap{};
		std::uint64_t heapOffset = 0;
		std::uint64_t size		 = 0;
	};

	struct SparseBufferBind final
	{
		BufferHandle buffer{};
		std::uint64_t resourceOffset = 0;
		SparseMemoryPage page{};
	};

	struct SparseTextureBind final
	{
		TextureHandle texture{};
		TextureSubresource subresource{};
		Offset3D offset{};
		Extent3D extent{};
		SparseMemoryPage page{};
	};

	struct SparseBindDesc final
	{
		std::span<const SparseBufferBind> buffers;
		std::span<const SparseTextureBind> textures;
		std::span<const TimelinePoint> timelineWaits;
		std::span<const TimelinePoint> timelineSignals;
		const char * debugName = nullptr;
	};

}

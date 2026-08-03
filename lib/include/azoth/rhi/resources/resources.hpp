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
 * \brief Buffer, texture, heap, mapping, aliasing, residency, and sparse binding descriptors.
 */

#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Buffer usage flags.
	 *
	 * Usages may combine where the backend allows it. Prefer the narrowest set that covers every real pipeline use.
	 */
	enum class BufferUsage : std::uint32_t // NOLINT(performance-enum-size)
	{
		eCopySrc = 1u << 0u,
		eCopyDst = 1u << 1u,
		eVertex	 = 1u << 2u,
		eIndex	 = 1u << 3u,

		/**
		 * \brief Small read-only shader parameters, subject to backend alignment limits.
		 */
		eUniform = 1u << 4u,

		/**
		 * \brief Storage or read-write shader buffer binding.
		 */
		eStorage = 1u << 5u,

		/**
		 * \brief Indirect draw, dispatch, or trace argument buffer.
		 */
		eIndirect = 1u << 6u,

		/**
		 * \brief Ray tracing shader binding table.
		 *
		 * May need stricter alignment and addressability than ordinary storage buffers.
		 */
		eShaderBindingTable = 1u << 7u,

		/**
		 * \brief Geometry, instance, or build input data for acceleration-structure construction.
		 */
		eAccelerationStructureInput = 1u << 8u,

		/**
		 * \brief Backing storage for an acceleration structure.
		 */
		eAccelerationStructureStorage = 1u << 9u,

		/**
		 * \brief Sparse or reserved-resource buffer binding.
		 */
		eSparseBinding = 1u << 10u,
	};

	/**
	 * \brief Texture usage flags.
	 *
	 * Usage affects native image creation, valid views, allowed layouts, and synchronization rules.
	 */
	enum class TextureUsage : std::uint32_t // NOLINT(performance-enum-size)
	{
		eCopySrc = 1u << 0u,
		eCopyDst = 1u << 1u,

		/**
		 * \brief Read-only sampled access from shaders.
		 */
		eSampled = 1u << 2u,

		/**
		 * \brief Shader load-store access.
		 *
		 * Format support may differ from sampled support.
		 */
		eStorage = 1u << 3u,

		eColorAttachment		= 1u << 4u,
		eDepthStencilAttachment = 1u << 5u,

		/**
		 * \brief Contents need not survive beyond the render scope that uses them.
		 */
		eTransientAttachment = 1u << 6u,

		/**
		 * \brief Owned by or compatible with presentation.
		 */
		ePresent = 1u << 7u,

		/**
		 * \brief Sparse or reserved-resource texture binding.
		 */
		eSparseBinding = 1u << 8u,
	};

	/**
	 * \brief Memory placement hint used by backend allocation policy.
	 *
	 * This is not a strict heap contract. Query BufferInfo, TextureInfo, or MemoryInfo for what the backend actually chose.
	 */
	enum class MemoryUsage : std::uint8_t
	{
		/**
		 * \brief Device-local memory for resources only the GPU touches.
		 */
		eGpuOnly,

		/**
		 * \brief CPU-visible upload memory for CPU writes and GPU reads.
		 */
		eCpuUpload,

		/**
		 * \brief CPU-visible readback memory for GPU writes and CPU reads.
		 */
		eCpuReadback,

		/**
		 * \brief CPU-visible memory for resources the CPU updates and the GPU uses.
		 */
		eCpuToGpu,

		/**
		 * \brief CPU-visible memory for resources the GPU produces and the CPU reads.
		 */
		eGpuToCpu,

		/**
		 * \brief Short-lived memory for frame-local or pass-local resources.
		 */
		eTransient,

		/**
		 * \brief Reserved virtual memory with physical pages bound later.
		 */
		eReserved,
	};

	/**
	 * \brief Host visibility and cache behavior of resource memory.
	 */
	enum class MemoryAccess : std::uint8_t
	{
		eGpuOnly,

		/**
		 * \brief Mappable memory with backend-defined cache behavior.
		 */
		eCpuVisible,

		/**
		 * \brief Mappable memory where host writes reach the GPU without an explicit flush.
		 */
		eCpuVisibleCoherent,

		/**
		 * \brief Mappable memory that is host cached, for CPU reads.
		 */
		eCpuVisibleCached,
	};

	/**
	 * \brief Buffer creation request.
	 *
	 * usage must contain every intended pipeline use. stride is optional element metadata for structured access, diagnostics, and validation.
	 */
	struct BufferDesc final
	{
		std::uint64_t size	 = 0;
		std::uint64_t stride = 0;
		Flags<BufferUsage> usage;
		MemoryUsage memory = MemoryUsage::eGpuOnly;

		/**
		 * \brief Allows this buffer to share heap memory with another placed resource.
		 */
		bool allowAliasing = false;

		/**
		 * \brief Allows sparse page binding for this buffer.
		 */
		bool allowSparseBinding = false;

		/**
		 * \brief Requests a persistent CPU mapping when the selected memory allows it.
		 */
		bool persistentMap = false;

		const char * debugName = nullptr;

		/**
		 * \brief Handle types this buffer may be exported to, empty when it is not exportable.
		 *
		 * Declared at creation because both target APIs require it there: Vulkan chains an export structure into the create call and Direct3D 12 takes a shared
		 * heap flag at the same point. A buffer created without this cannot be exported afterwards. Creation refuses a type the adapter cannot export, which
		 * Instance::QueryExternalHandleSupport answers ahead of the attempt.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/**
	 * \brief Texture creation request.
	 *
	 * Dimensions describe the base mip. mipLevels and arrayLayers describe the whole allocated subresource range. usage must contain every intended pipeline
	 * use.
	 */
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
		MemoryUsage memory			= MemoryUsage::eGpuOnly;
		TextureLayout initialLayout = TextureLayout::eUndefined;

		/**
		 * \brief Allows this texture to share heap memory with another placed resource.
		 */
		bool allowAliasing = false;

		/**
		 * \brief Allows sparse page or tile binding for this texture.
		 */
		bool allowSparseBinding = false;

		/**
		 * \brief Allows texture views whose format differs from this texture's own format.
		 *
		 * Declare this at texture creation because some backends need different native resource flags or typeless formats.
		 */
		bool allowFormatViews = false;

		const char * debugName = nullptr;

		/**
		 * \brief Handle types this texture may be exported to, empty when it is not exportable.
		 *
		 * Declared at creation because both target APIs require it there: Vulkan chains an export structure into the create call and Direct3D 12 takes a shared
		 * heap flag at the same point. A texture created without this cannot be exported afterwards. Creation refuses a type the adapter cannot export, which
		 * Instance::QueryExternalHandleSupport answers ahead of the attempt.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/**
	 * \brief An external buffer handle and the buffer to build over the memory it names.
	 *
	 * desc describes the buffer being created, exactly as an ordinary creation would, because importing memory does not import a description with it.
	 * The importing side has to state the same size and usage the exporting side used, and a mismatch is undefined, not reported.
	 */
	struct ExternalBufferImportDesc final
	{
		ExternalHandle handle{};
		BufferDesc desc{};
	};

	/**
	 * \brief An external texture handle and the texture to build over the memory it names.
	 *
	 * The same rule as the buffer form: every field of desc has to match what the exporter created, since none of it travels with the handle.
	 */
	struct ExternalTextureImportDesc final
	{
		ExternalHandle handle{};
		TextureDesc desc{};
	};

	/**
	 * \brief Runtime allocation details for a buffer.
	 *
	 * allocationSize may include alignment padding, and is zero when the buffer owns no allocation of its own. A placed or adopted buffer reports zero
	 * because its memory is the heap's or the caller's to account for, and so does a backend that allocates nothing at all.
	 */
	struct BufferInfo final
	{
		BufferDesc desc{};
		std::uint64_t allocationSize = 0;
		MemoryAccess memoryAccess	 = MemoryAccess::eGpuOnly;
	};

	/**
	 * \brief Runtime allocation details for a texture.
	 *
	 * allocationSize may include tiling, metadata, or alignment overhead, and is zero when the texture owns no allocation of its own. A placed, adopted
	 * or sparse texture reports zero because its memory is the heap's or the caller's to account for, and so does a backend that allocates nothing.
	 */
	struct TextureInfo final
	{
		TextureDesc desc{};
		std::uint64_t allocationSize = 0;
	};

	/**
	 * \brief CPU mapping intent for host-visible memory.
	 */
	enum class MapMode : std::uint8_t
	{
		eRead,
		eWrite,
		eReadWrite,
	};

	/**
	 * \brief Requested byte range for mapping a buffer.
	 *
	 * The default size maps from offset to the end of the resource.
	 */
	struct MapDesc final
	{
		MapMode mode		 = MapMode::eWrite;
		std::uint64_t offset = 0;
		std::uint64_t size	 = std::numeric_limits<std::uint64_t>::max();
	};

	/**
	 * \brief Mapped buffer range.
	 *
	 * Non-coherent memory needs an explicit flush after CPU writes and an invalidate before CPU reads.
	 */
	struct MappedMemory final
	{
		void * data		   = nullptr;
		std::uint64_t size = 0;
		bool coherent	   = false;
	};

	/**
	 * \brief Heap class for placed resources and explicit allocation policy.
	 */
	enum class HeapType : std::uint8_t
	{
		eGpuLocal,
		eCpuUpload,
		eCpuReadback,

		/**
		 * \brief Short-lived heap for frame-local or aliasable temporaries.
		 */
		eTransient,
	};

	/**
	 * \brief Explicit memory heap allocation request.
	 *
	 * Placed resources bind into this heap when the type is allowed and the offset satisfies backend alignment requirements.
	 */
	struct HeapDesc final
	{
		HeapType type			= HeapType::eGpuLocal;
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;

		/**
		 * \brief Allows buffers to be placed in this heap.
		 */
		bool allowBuffers = true;

		/**
		 * \brief Allows textures to be placed in this heap.
		 */
		bool allowTextures = true;

		/**
		 * \brief Allows placed resources in this heap to alias each other.
		 */
		bool allowAliasing = false;

		const char * debugName = nullptr;

		/**
		 * \brief Handle types this heap may be exported to, empty when it is not exportable.
		 *
		 * Declared here as well as on resources because the memory is what crosses the boundary. A caller placing its own resources shares the heap once rather
		 * than each resource. Both target APIs name this granularity. Direct3D 12 takes a shared heap flag at heap creation and Vulkan and CUDA import a
		 * Direct3D 12 heap directly. Creation refuses a type the adapter cannot export.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/**
	 * \brief An external memory handle and the heap to build over it.
	 *
	 * The importer decides what to place in the memory, so this carries no resource description. desc states the size and the kinds of resource the heap
	 * will accept, which has to match what the exporting side allocated.
	 */
	struct ExternalHeapImportDesc final
	{
		ExternalHandle handle{};
		HeapDesc desc{};
	};

	/**
	 * \brief Buffer placed in an explicit heap.
	 *
	 * offset must satisfy both the heap alignment and the backend's buffer placement alignment.
	 */
	struct PlacedBufferDesc final
	{
		BufferDesc buffer{};
		HeapHandle heap{};
		std::uint64_t offset = 0;
	};

	/**
	 * \brief Texture placed in an explicit heap.
	 *
	 * offset must satisfy both the heap alignment and the backend's texture placement alignment.
	 */
	struct PlacedTextureDesc final
	{
		TextureDesc texture{};
		HeapHandle heap{};
		std::uint64_t offset = 0;
	};

	/**
	 * \brief Heap footprint a resource needs before creation.
	 *
	 * Transient and aliasing allocators use this to size a heap and choose legal placed offsets.
	 */
	struct MemoryInfo final
	{
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;
	};

	/**
	 * \brief Aliasing transition between placed resources sharing heap memory.
	 *
	 * Developer validation uses this to prove the old resource lifetime ended before the new one begins.
	 */
	struct AliasBarrier final
	{
		BufferHandle beforeBuffer{};
		TextureHandle beforeTexture{};
		BufferHandle afterBuffer{};
		TextureHandle afterTexture{};
	};

	/**
	 * \brief Memory budget and usage for one heap class.
	 *
	 * Check budgetIsPrecise before treating availableForReservationBytes as an exact limit.
	 */
	struct MemoryBudgetInfo final
	{
		HeapType heap							   = HeapType::eGpuLocal;
		std::uint64_t budgetBytes				   = 0;
		std::uint64_t usageBytes				   = 0;
		std::uint64_t availableForReservationBytes = 0;

		/**
		 * \brief True when the backend reports a precise budget instead of an estimate.
		 */
		bool budgetIsPrecise = false;
	};

	/**
	 * \brief Backend residency preference for resources that can be paged or evicted.
	 */
	enum class ResidencyPriority : std::uint8_t
	{
		eMinimum,
		eLow,
		eNormal,
		eHigh,
		eCritical,
	};

	/**
	 * \brief Residency-priority update for one buffer or texture.
	 *
	 * Exactly one resource handle should be set.
	 */
	struct ResidencyPriorityDesc final
	{
		BufferHandle buffer{};
		TextureHandle texture{};
		ResidencyPriority priority = ResidencyPriority::eNormal;
	};

	/**
	 * \brief Binding for one sparse-resource memory page.
	 *
	 * Byte values must satisfy backend page alignment.
	 */
	struct SparseMemoryPage final
	{
		HeapHandle heap{};
		std::uint64_t heapOffset = 0;
		std::uint64_t size		 = 0;
	};

	/**
	 * \brief Sparse buffer memory bind.
	 *
	 * An invalid heap unbinds the range.
	 */
	struct SparseBufferBind final
	{
		BufferHandle buffer{};
		std::uint64_t resourceOffset = 0;
		SparseMemoryPage page{};
	};

	/**
	 * \brief Sparse texture memory bind.
	 *
	 * An invalid heap unbinds the tile range.
	 */
	struct SparseTextureBind final
	{
		TextureHandle texture{};
		TextureSubresource subresource{};
		Offset3D offset{};
		Extent3D extent{};
		SparseMemoryPage page{};
	};

	/**
	 * \brief Sparse bind batch submitted with timeline ordering.
	 *
	 * Span members and debugName are borrowed for the duration of the bind call.
	 */
	struct SparseBindDesc final
	{
		std::span<const SparseBufferBind> buffers;
		std::span<const SparseTextureBind> textures;
		std::span<const TimelinePoint> timelineWaits;
		std::span<const TimelinePoint> timelineSignals;
		const char * debugName = nullptr;
	};

} // namespace azo::rhi

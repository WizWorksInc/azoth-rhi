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
 * \brief Per-resource-kind slot tables for backend resource handles.
 */

#include "azoth/rhi/backend/blocks/common.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <utility>

namespace azo::rhi::detail
{

	/**
	 * \brief Maps a typed resource handle to the ResourceType partition that owns its slot map.
	 *
	 * \note There is intentionally no usable primary mapping. Unsupported handle types fail at compile time when kValue is requested.
	 */
	template <class HandleT>
	struct ResourceTypeOf;

#define AZO_RHI_MAP_RESOURCE_TYPE(handleType, resourceType)                                                                                                    \
	template <>                                                                                                                                                \
	struct ResourceTypeOf<handleType> final                                                                                                                    \
	{                                                                                                                                                          \
		static constexpr ResourceType kValue = ResourceType::resourceType;                                                                                     \
	}

	AZO_RHI_MAP_RESOURCE_TYPE(BufferHandle, eBuffer);
	AZO_RHI_MAP_RESOURCE_TYPE(TextureHandle, eTexture);
	AZO_RHI_MAP_RESOURCE_TYPE(TextureViewHandle, eTextureView);
	AZO_RHI_MAP_RESOURCE_TYPE(SamplerHandle, eSampler);
	AZO_RHI_MAP_RESOURCE_TYPE(HeapHandle, eHeap);
	AZO_RHI_MAP_RESOURCE_TYPE(DescriptorSetLayoutHandle, eDescriptorSetLayout);
	AZO_RHI_MAP_RESOURCE_TYPE(DescriptorSetHandle, eDescriptorSet);
	AZO_RHI_MAP_RESOURCE_TYPE(PipelineLayoutHandle, ePipelineLayout);
	AZO_RHI_MAP_RESOURCE_TYPE(GraphicsPipelineHandle, eGraphicsPipeline);
	AZO_RHI_MAP_RESOURCE_TYPE(ComputePipelineHandle, eComputePipeline);
	AZO_RHI_MAP_RESOURCE_TYPE(RayTracingPipelineHandle, eRayTracingPipeline);
	AZO_RHI_MAP_RESOURCE_TYPE(PipelineCacheHandle, ePipelineCache);
	AZO_RHI_MAP_RESOURCE_TYPE(AccelerationStructureHandle, eAccelerationStructure);
	AZO_RHI_MAP_RESOURCE_TYPE(QueryPoolHandle, eQueryPool);
	AZO_RHI_MAP_RESOURCE_TYPE(TimelineHandle, eTimeline);
	AZO_RHI_MAP_RESOURCE_TYPE(BinarySemaphoreHandle, eBinarySemaphore);

#undef AZO_RHI_MAP_RESOURCE_TYPE

	/**
	 * \brief Slot-map set split by resource kind.
	 *
	 * Each resource kind has its own SlotMap because RHI synchronization is keyed by resource kind. Resolve remains lock-free through SlotMap stable chunks.
	 * \attention Store, Retire, Reset, and direct table mutation are writer-side operations. The caller must hold the matching resource-kind guard.
	 */
	template <class Tag, class Payload>
	class ResourceTables final
	{
	public:
		ResourceTables() = default;

		ResourceTables(const ResourceTables &)			   = delete;
		ResourceTables & operator=(const ResourceTables &) = delete;
		ResourceTables(ResourceTables &&)				   = delete;
		ResourceTables & operator=(ResourceTables &&)	   = delete;
		~ResourceTables()								   = default;

		/**
		 * \brief Clears every resource table and encodes deviceTag into future handles.
		 */
		void Rebind(const std::uint32_t deviceTag) noexcept
		{
			for (SlotMap<Tag, Payload> & table : m_tables)
			{
				table.Rebind(deviceTag);
			}
		}

		/**
		 * \brief Returns the slot map for one resource kind.
		 *
		 * \attention type must be a valid ResourceType enumerator.
		 */
		[[nodiscard]] SlotMap<Tag, Payload> & For(const ResourceType type) noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		/**
		 * \brief Returns the slot map for one resource kind.
		 *
		 * \attention type must be a valid ResourceType enumerator.
		 */
		[[nodiscard]] const SlotMap<Tag, Payload> & For(const ResourceType type) const noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		/**
		 * \brief Stores a payload in the resource partition selected by HandleT.
		 *
		 * \note Returns an invalid handle when the selected slot map cannot allocate or has exhausted its slot space.
		 */
		template <class HandleT>
		[[nodiscard]] HandleT Store(Payload payload)
		{
			const Handle<Tag> slot = For(ResourceTypeOf<HandleT>::kValue).Store(std::move(payload));
			return HandleT{ .index = slot.index, .generation = slot.generation };
		}

		/**
		 * \brief Resolves a typed resource handle in its compile-time resource partition.
		 *
		 * \param validate When false, SlotMap still checks the device tag and slot bounds but skips live-generation validation.
		 */
		template <class HandleT>
		[[nodiscard]] Payload * Resolve(const HandleT handle, const bool validate) noexcept
		{
			return For(ResourceTypeOf<HandleT>::kValue).Resolve(Erase<HandleT>(handle), validate);
		}

		/**
		 * \brief Resolves a type-erased resource handle in the resource partition supplied by type.
		 *
		 * \attention type must match the original typed handle kind. Passing the wrong type queries the wrong slot map.
		 * \param validate When false, SlotMap still checks the device tag and slot bounds but skips live-generation validation.
		 */
		[[nodiscard]] Payload * Resolve(const ResourceType type, const RawHandle handle, const bool validate) noexcept
		{
			return For(type).Resolve(Handle<Tag>{ .index = handle.index, .generation = handle.generation }, validate);
		}

		/**
		 * \brief Retires a type-erased resource handle in the resource partition supplied by type.
		 *
		 * \attention type must match the original typed handle kind. Passing the wrong type cannot retire the intended slot.
		 * \param validate When false, SlotMap still checks the device tag and slot bounds but skips live-generation validation.
		 */
		[[nodiscard]] bool Retire(const ResourceType type, const RawHandle handle, const bool validate) noexcept
		{
			return For(type).Retire(Handle<Tag>{ .index = handle.index, .generation = handle.generation }, validate);
		}

		/**
		 * \brief Resets every resource table and releases all slot-map chunks.
		 */
		void Reset() noexcept
		{
			for (SlotMap<Tag, Payload> & table : m_tables)
			{
				table.Reset();
			}
		}

		/**
		 * \brief Counts live entries across all resource tables.
		 *
		 * \note The returned count is diagnostic only when writers may be mutating tables concurrently.
		 */
		[[nodiscard]] std::size_t LiveCount() const noexcept
		{
			std::size_t live = 0;
			for (const SlotMap<Tag, Payload> & table : m_tables)
			{
				live += table.LiveCount();
			}

			return live;
		}

	private:
		template <class HandleT>
		[[nodiscard]] static Handle<Tag> Erase(const HandleT handle) noexcept
		{
			return Handle<Tag>{ .index = handle.index, .generation = handle.generation };
		}

		std::array<SlotMap<Tag, Payload>, kResourceTypeCount> m_tables;
	};

} // namespace azo::rhi::detail

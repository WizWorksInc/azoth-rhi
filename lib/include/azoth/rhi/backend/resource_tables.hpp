// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/blocks/common.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace azo::rhi::detail
{

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

		void Rebind(const std::uint32_t deviceTag) noexcept
		{
			for (SlotMap<Tag, Payload> & table : m_tables)
			{
				table.Rebind(deviceTag);
			}
		}

		[[nodiscard]] SlotMap<Tag, Payload> & For(const ResourceType type) noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		[[nodiscard]] const SlotMap<Tag, Payload> & For(const ResourceType type) const noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		template <class HandleT>
		[[nodiscard]] HandleT Store(Payload payload)
		{
			const Handle<Tag> slot = For(ResourceTypeOf<HandleT>::kValue).Store(std::move(payload));
			return HandleT{ .index = slot.index, .generation = slot.generation };
		}

		template <class HandleT>
		[[nodiscard]] Payload * Resolve(const HandleT handle, const bool validate) noexcept
		{
			return For(ResourceTypeOf<HandleT>::kValue).Resolve(Erase<HandleT>(handle), validate);
		}

		[[nodiscard]] Payload * Resolve(const ResourceType type, const RawHandle handle, const bool validate) noexcept
		{
			return For(type).Resolve(Handle<Tag>{ .index = handle.index, .generation = handle.generation }, validate);
		}

		[[nodiscard]] bool Retire(const ResourceType type, const RawHandle handle, const bool validate) noexcept
		{
			return For(type).Retire(Handle<Tag>{ .index = handle.index, .generation = handle.generation }, validate);
		}

		void Reset() noexcept
		{
			for (SlotMap<Tag, Payload> & table : m_tables)
			{
				table.Reset();
			}
		}

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

}

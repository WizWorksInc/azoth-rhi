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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/ownership/unique.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include <concepts>
#include <cstdint> // NOLINT
#include <span>
#include <utility>

namespace azo::rhi::raii
{

	using Buffer				= Unique<BufferHandle>;
	using Texture				= Unique<TextureHandle>;
	using TextureView			= Unique<TextureViewHandle>;
	using Sampler				= Unique<SamplerHandle>;
	using Heap					= Unique<HeapHandle>;
	using DescriptorSetLayout	= Unique<DescriptorSetLayoutHandle>;
	using PipelineLayout		= Unique<PipelineLayoutHandle>;
	using GraphicsPipeline		= Unique<GraphicsPipelineHandle>;
	using ComputePipeline		= Unique<ComputePipelineHandle>;
	using RayTracingPipeline	= Unique<RayTracingPipelineHandle>;
	using PipelineCache			= Unique<PipelineCacheHandle>;
	using AccelerationStructure = Unique<AccelerationStructureHandle>;
	using QueryPool				= Unique<QueryPoolHandle>;
	using Timeline				= Unique<TimelineHandle>;
	using BinarySemaphore		= Unique<BinarySemaphoreHandle>;

	using rhi::CommandList;
	using DescriptorSet = DescriptorSetHandle;

	using rhi::CommandPool;
	using rhi::DescriptorArena;
	using rhi::Queue;
	using rhi::Swapchain;

	class Device final
	{
	public:
		Device() = default;

		explicit Device(UniqueDevice owner) noexcept : m_owner(std::move(owner)) {}

		Device(const Device &)				   = delete;
		Device & operator=(const Device &)	   = delete;
		Device(Device &&) noexcept			   = default;
		Device & operator=(Device &&) noexcept = default;

		~Device() = default;

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_owner.is_valid();
		}

		[[nodiscard]] rhi::Device get() const noexcept
		{
			return m_owner.get();
		}

		[[nodiscard]] Result<Buffer> create_buffer(const BufferDesc & desc) noexcept
		{
			return Own<Buffer>(m_owner.get().create_buffer_with_result(desc));
		}

		[[nodiscard]] Result<Buffer> create_placed_buffer(const PlacedBufferDesc & desc) noexcept
		{
			return Own<Buffer>(m_owner.get().create_placed_buffer_with_result(desc));
		}

		[[nodiscard]] Result<Texture> create_texture(const TextureDesc & desc) noexcept
		{
			return Own<Texture>(m_owner.get().create_texture_with_result(desc));
		}

		[[nodiscard]] Result<Texture> create_placed_texture(const PlacedTextureDesc & desc) noexcept
		{
			return Own<Texture>(m_owner.get().create_placed_texture_with_result(desc));
		}

		[[nodiscard]] Result<TextureView> create_texture_view(const TextureHandle texture, const TextureViewDesc & desc) noexcept
		{
			return Own<TextureView>(m_owner.get().create_texture_view_with_result(texture, desc));
		}

		[[nodiscard]] Result<Sampler> create_sampler(const SamplerDesc & desc) noexcept
		{
			return Own<Sampler>(m_owner.get().create_sampler_with_result(desc));
		}

		[[nodiscard]] Result<Heap> create_heap(const HeapDesc & desc) noexcept
		{
			return Own<Heap>(m_owner.get().create_heap_with_result(desc));
		}

		[[nodiscard]] Result<DescriptorSetLayout> create_descriptor_set_layout(const DescriptorSetLayoutDesc & desc) noexcept
		{
			return Own<DescriptorSetLayout>(m_owner.get().create_descriptor_set_layout_with_result(desc));
		}

		[[nodiscard]] Result<PipelineLayout> create_pipeline_layout(const PipelineLayoutDesc & desc) noexcept
		{
			return Own<PipelineLayout>(m_owner.get().create_pipeline_layout_with_result(desc));
		}

		[[nodiscard]] Result<GraphicsPipeline> create_graphics_pipeline(const GraphicsPipelineDesc & desc) noexcept
		{
			return Own<GraphicsPipeline>(m_owner.get().create_graphics_pipeline_with_result(desc));
		}

		[[nodiscard]] Result<ComputePipeline> create_compute_pipeline(const ComputePipelineDesc & desc) noexcept
		{
			return Own<ComputePipeline>(m_owner.get().create_compute_pipeline_with_result(desc));
		}

		[[nodiscard]] Result<RayTracingPipeline> create_ray_tracing_pipeline(const RayTracingPipelineDesc & desc) noexcept
		{
			return Own<RayTracingPipeline>(m_owner.get().create_ray_tracing_pipeline_with_result(desc));
		}

		[[nodiscard]] Result<PipelineCache> create_pipeline_cache(const PipelineCacheDesc & desc) noexcept
		{
			return Own<PipelineCache>(m_owner.get().create_pipeline_cache_with_result(desc));
		}

		[[nodiscard]] Result<AccelerationStructure> create_acceleration_structure(const AccelerationStructureDesc & desc) noexcept
		{
			return Own<AccelerationStructure>(m_owner.get().create_acceleration_structure_with_result(desc));
		}

		[[nodiscard]] Result<QueryPool> create_query_pool(const QueryPoolDesc & desc) noexcept
		{
			return Own<QueryPool>(m_owner.get().create_query_pool_with_result(desc));
		}

		[[nodiscard]] Result<Timeline> create_timeline(const TimelineDesc & desc) noexcept
		{
			return Own<Timeline>(m_owner.get().create_timeline_with_result(desc));
		}

		[[nodiscard]] Result<BinarySemaphore> create_binary_semaphore(const BinarySemaphoreDesc & desc) noexcept
		{
			return Own<BinarySemaphore>(m_owner.get().create_binary_semaphore_with_result(desc));
		}

		[[nodiscard]] Result<CommandPool> create_command_pool(const CommandPoolDesc & desc) noexcept
		{
			return m_owner.get().create_command_pool_with_result(desc);
		}

		[[nodiscard]] Result<DescriptorArena> create_descriptor_arena(const DescriptorArenaDesc & desc) noexcept
		{
			return m_owner.get().create_descriptor_arena_with_result(desc);
		}

		[[nodiscard]] Result<Swapchain> create_swapchain(const SwapchainDesc & desc) noexcept
		{
			return m_owner.get().create_swapchain_with_result(desc);
		}

	private:
		template <class OwnerT, class HandleT>
		[[nodiscard]] Result<OwnerT> Own(const Result<HandleT> & made) noexcept
		{
			if (!made)
			{
				return made.get_error();
			}

			return OwnerT{ m_owner.get(), made.value() };
		}

		UniqueDevice m_owner;
	};

	class Instance final
	{
	public:
		Instance() = default;

		explicit Instance(UniqueInstance owner) noexcept : m_owner(std::move(owner)) {}

		Instance(const Instance &)				   = delete;
		Instance & operator=(const Instance &)	   = delete;
		Instance(Instance &&) noexcept			   = default;
		Instance & operator=(Instance &&) noexcept = default;

		~Instance() = default;

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_owner.is_valid();
		}

		[[nodiscard]] rhi::Instance get() const noexcept
		{
			return m_owner.get();
		}

		[[nodiscard]] GraphicsApiId get_graphics_api_id() const noexcept
		{
			return m_owner.get().get_graphics_api_id();
		}

		[[nodiscard]] Result<std::uint32_t> enumerate_adapters(const std::span<AdapterInfo> adapters) const noexcept
		{
			return m_owner.get().enumerate_adapters_with_result(adapters);
		}

	private:
		UniqueInstance m_owner;
	};

	class Selection final
	{
	public:
		Selection() = default;

		explicit Selection(const BackendPreference & preference) : m_backends(preference) {}

		Selection(const Selection &)				 = delete;
		Selection & operator=(const Selection &)	 = delete;
		Selection(Selection &&) noexcept			 = default;
		Selection & operator=(Selection &&) noexcept = default;

		~Selection() = default;

		[[nodiscard]] BackendSelection & get() noexcept
		{
			return m_backends;
		}

		[[nodiscard]] const BackendSelection & get() const noexcept
		{
			return m_backends;
		}

		[[nodiscard]] Result<Instance> create_instance(const InstanceDesc & desc = {})
		{
			Result<UniqueInstance> made = m_backends.create_instance(desc);
			if (!made)
			{
				return made.get_error();
			}

			return Instance{ std::move(made.value()) };
		}

		[[nodiscard]] Result<Device> create_device(const DeviceDesc & desc = {})
		{
			Result<UniqueDevice> made = m_backends.create_device(desc);
			if (!made)
			{
				return made.get_error();
			}

			return Device{ std::move(made.value()) };
		}

	private:
		BackendSelection m_backends;
	};

	namespace detail
	{

		template <class T>
		inline constexpr bool kOwns = false;

		template <class HandleT>
		inline constexpr bool kOwns<Unique<HandleT>> = true;

		template <class T>
		inline constexpr bool kBorrows = !kOwns<T>;

		template <class HandleT>
		concept DeviceDestroyable = requires(rhi::Device device, HandleT handle) {
			{ device.destroy(handle, DestroyDesc{}) } -> std::same_as<bool>;
		};

	} // namespace detail

} // namespace azo::rhi::raii

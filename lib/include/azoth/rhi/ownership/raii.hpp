// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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
#include <cstdint>
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

	using CommandList	= rhi::CommandList;
	using DescriptorSet = DescriptorSetHandle;

	using Queue			  = rhi::Queue;
	using CommandPool	  = rhi::CommandPool;
	using DescriptorArena = rhi::DescriptorArena;
	using Swapchain		  = rhi::Swapchain;

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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_owner.IsValid();
		}

		[[nodiscard]] rhi::Device Get() const noexcept
		{
			return m_owner.Get();
		}

		[[nodiscard]] Result<Buffer> CreateBuffer(const BufferDesc & desc) noexcept
		{
			return Own<Buffer>(m_owner.Get().CreateBufferWithResult(desc));
		}

		[[nodiscard]] Result<Buffer> CreatePlacedBuffer(const PlacedBufferDesc & desc) noexcept
		{
			return Own<Buffer>(m_owner.Get().CreatePlacedBufferWithResult(desc));
		}

		[[nodiscard]] Result<Texture> CreateTexture(const TextureDesc & desc) noexcept
		{
			return Own<Texture>(m_owner.Get().CreateTextureWithResult(desc));
		}

		[[nodiscard]] Result<Texture> CreatePlacedTexture(const PlacedTextureDesc & desc) noexcept
		{
			return Own<Texture>(m_owner.Get().CreatePlacedTextureWithResult(desc));
		}

		[[nodiscard]] Result<TextureView> CreateTextureView(const TextureHandle texture, const TextureViewDesc & desc) noexcept
		{
			return Own<TextureView>(m_owner.Get().CreateTextureViewWithResult(texture, desc));
		}

		[[nodiscard]] Result<Sampler> CreateSampler(const SamplerDesc & desc) noexcept
		{
			return Own<Sampler>(m_owner.Get().CreateSamplerWithResult(desc));
		}

		[[nodiscard]] Result<Heap> CreateHeap(const HeapDesc & desc) noexcept
		{
			return Own<Heap>(m_owner.Get().CreateHeapWithResult(desc));
		}

		[[nodiscard]] Result<DescriptorSetLayout> CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc) noexcept
		{
			return Own<DescriptorSetLayout>(m_owner.Get().CreateDescriptorSetLayoutWithResult(desc));
		}

		[[nodiscard]] Result<PipelineLayout> CreatePipelineLayout(const PipelineLayoutDesc & desc) noexcept
		{
			return Own<PipelineLayout>(m_owner.Get().CreatePipelineLayoutWithResult(desc));
		}

		[[nodiscard]] Result<GraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc & desc) noexcept
		{
			return Own<GraphicsPipeline>(m_owner.Get().CreateGraphicsPipelineWithResult(desc));
		}

		[[nodiscard]] Result<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc & desc) noexcept
		{
			return Own<ComputePipeline>(m_owner.Get().CreateComputePipelineWithResult(desc));
		}

		[[nodiscard]] Result<RayTracingPipeline> CreateRayTracingPipeline(const RayTracingPipelineDesc & desc) noexcept
		{
			return Own<RayTracingPipeline>(m_owner.Get().CreateRayTracingPipelineWithResult(desc));
		}

		[[nodiscard]] Result<PipelineCache> CreatePipelineCache(const PipelineCacheDesc & desc) noexcept
		{
			return Own<PipelineCache>(m_owner.Get().CreatePipelineCacheWithResult(desc));
		}

		[[nodiscard]] Result<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureDesc & desc) noexcept
		{
			return Own<AccelerationStructure>(m_owner.Get().CreateAccelerationStructureWithResult(desc));
		}

		[[nodiscard]] Result<QueryPool> CreateQueryPool(const QueryPoolDesc & desc) noexcept
		{
			return Own<QueryPool>(m_owner.Get().CreateQueryPoolWithResult(desc));
		}

		[[nodiscard]] Result<Timeline> CreateTimeline(const TimelineDesc & desc) noexcept
		{
			return Own<Timeline>(m_owner.Get().CreateTimelineWithResult(desc));
		}

		[[nodiscard]] Result<BinarySemaphore> CreateBinarySemaphore(const BinarySemaphoreDesc & desc) noexcept
		{
			return Own<BinarySemaphore>(m_owner.Get().CreateBinarySemaphoreWithResult(desc));
		}

		[[nodiscard]] Result<CommandPool> CreateCommandPool(const CommandPoolDesc & desc) noexcept
		{
			return m_owner.Get().CreateCommandPoolWithResult(desc);
		}

		[[nodiscard]] Result<DescriptorArena> CreateDescriptorArena(const DescriptorArenaDesc & desc) noexcept
		{
			return m_owner.Get().CreateDescriptorArenaWithResult(desc);
		}

		[[nodiscard]] Result<Swapchain> CreateSwapchain(const SwapchainDesc & desc) noexcept
		{
			return m_owner.Get().CreateSwapchainWithResult(desc);
		}

	private:
		template <class OwnerT, class HandleT>
		[[nodiscard]] Result<OwnerT> Own(const Result<HandleT> & made) noexcept
		{
			if (!made)
			{
				return made.GetError();
			}

			return OwnerT{ m_owner.Get(), made.Value() };
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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_owner.IsValid();
		}

		[[nodiscard]] rhi::Instance Get() const noexcept
		{
			return m_owner.Get();
		}

		[[nodiscard]] GraphicsApiId GetGraphicsApiId() const noexcept
		{
			return m_owner.Get().GetGraphicsApiId();
		}

		[[nodiscard]] Result<std::uint32_t> EnumerateAdapters(const std::span<AdapterInfo> adapters) const noexcept
		{
			return m_owner.Get().EnumerateAdaptersWithResult(adapters);
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

		[[nodiscard]] BackendSelection & Get() noexcept
		{
			return m_backends;
		}

		[[nodiscard]] const BackendSelection & Get() const noexcept
		{
			return m_backends;
		}

		[[nodiscard]] Result<Instance> CreateInstance(const InstanceDesc & desc = {})
		{
			Result<UniqueInstance> made = m_backends.CreateInstance(desc);
			if (!made)
			{
				return made.GetError();
			}

			return Instance{ std::move(made.Value()) };
		}

		[[nodiscard]] Result<Device> CreateDevice(const DeviceDesc & desc = {})
		{
			Result<UniqueDevice> made = m_backends.CreateDevice(desc);
			if (!made)
			{
				return made.GetError();
			}

			return Device{ std::move(made.Value()) };
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
			{ device.Destroy(handle, DestroyDesc{}) } -> std::same_as<bool>;
		};

	}

}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

namespace azo::rhi
{

	template <class HandleT>
	class Unique final
	{
	public:
		Unique() = default;

		Unique(Device device, HandleT handle, const DestroyDesc & destroy = {}) noexcept : m_device(device), m_handle(handle), m_destroy(destroy) {}

		Unique(const Unique &)			   = delete;
		Unique & operator=(const Unique &) = delete;

		Unique(Unique && other) noexcept : m_device(other.m_device), m_handle(other.m_handle), m_destroy(other.m_destroy)
		{
			other.m_handle = {};
		}

		Unique & operator=(Unique && other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_device	   = other.m_device;
				m_handle	   = other.m_handle;
				m_destroy	   = other.m_destroy;
				other.m_handle = {};
			}

			return *this;
		}

		~Unique()
		{
			Reset();
		}

		[[nodiscard]] HandleT Get() const noexcept
		{
			return m_handle;
		}

		[[nodiscard]] HandleT operator*() const noexcept
		{
			return m_handle;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_handle.IsValid();
		}

		explicit operator bool() const noexcept
		{
			return IsValid();
		}

		[[nodiscard]] Device Owner() const noexcept
		{
			return m_device;
		}

		void SetDestroyDesc(const DestroyDesc & destroy) noexcept
		{
			m_destroy = destroy;
		}

		[[nodiscard]] const DestroyDesc & GetDestroyDesc() const noexcept
		{
			return m_destroy;
		}

		[[nodiscard]] HandleT Release() noexcept
		{
			const HandleT released = m_handle;
			m_handle			   = {};
			return released;
		}

		void Reset() noexcept
		{
			if (m_handle.IsValid() && m_device.IsValid())
			{
				static_cast<void>(m_device.Destroy(m_handle, m_destroy));
			}

			m_handle = {};
		}

		bool Reset(Error & error) noexcept
		{
			error = {};
			if (!m_handle.IsValid() || !m_device.IsValid())
			{
				m_handle = {};
				return true;
			}

			const bool destroyed = m_device.Destroy(m_handle, m_destroy, error);
			m_handle			 = {};
			return destroyed;
		}

	private:
		Device m_device;
		HandleT m_handle{};
		DestroyDesc m_destroy{};
	};

	using UniqueBuffer				  = Unique<BufferHandle>;
	using UniqueTexture				  = Unique<TextureHandle>;
	using UniqueTextureView			  = Unique<TextureViewHandle>;
	using UniqueSampler				  = Unique<SamplerHandle>;
	using UniqueHeap				  = Unique<HeapHandle>;
	using UniqueDescriptorSetLayout	  = Unique<DescriptorSetLayoutHandle>;
	using UniquePipelineLayout		  = Unique<PipelineLayoutHandle>;
	using UniqueGraphicsPipeline	  = Unique<GraphicsPipelineHandle>;
	using UniqueComputePipeline		  = Unique<ComputePipelineHandle>;
	using UniqueRayTracingPipeline	  = Unique<RayTracingPipelineHandle>;
	using UniquePipelineCache		  = Unique<PipelineCacheHandle>;
	using UniqueAccelerationStructure = Unique<AccelerationStructureHandle>;
	using UniqueQueryPool			  = Unique<QueryPoolHandle>;
	using UniqueTimeline			  = Unique<TimelineHandle>;
	using UniqueBinarySemaphore		  = Unique<BinarySemaphoreHandle>;

}

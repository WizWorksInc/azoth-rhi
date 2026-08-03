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
 * \brief First-tier unique owners for device-destroyed resource handles.
 */

#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"

namespace azo::rhi
{

	/**
	 * \brief Move-only owner for one resource handle destroyed through a borrowed Device view.
	 *
	 * This tier owns lifetime only. The flat handle API remains the whole API and the owning wrapper just removes the manual Destroy call.
	 *
	 * \attention The Device view is borrowed. The device must outlive every Unique created from it.
	 */
	template <class HandleT>
	class Unique final
	{
	public:
		Unique() = default;

		/**
		 * \brief Takes ownership of handle and destroys it with destroy policy.
		 */
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

		/**
		 * \brief Returns true when this owner currently holds a non-invalid handle.
		 *
		 * This does not prove the borrowed device is still alive or that the device still knows the handle.
		 */
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

		/**
		 * \brief Replaces the destroy policy used by Reset and the destructor.
		 *
		 * Use this when a tighter retire point is learned after creation.
		 */
		void SetDestroyDesc(const DestroyDesc & destroy) noexcept
		{
			m_destroy = destroy;
		}

		[[nodiscard]] const DestroyDesc & GetDestroyDesc() const noexcept
		{
			return m_destroy;
		}

		/**
		 * \brief Releases ownership and returns the handle to the caller.
		 *
		 * The caller becomes responsible for destroying the handle.
		 */
		[[nodiscard]] HandleT Release() noexcept
		{
			const HandleT released = m_handle;
			m_handle			   = {};
			return released;
		}

		/**
		 * \brief Destroys the held handle now and clears this owner.
		 *
		 * Destroy failures are swallowed because the destructor uses this path. Use Reset(Error&) when the result matters.
		 */
		void Reset() noexcept
		{
			if (m_handle.IsValid() && m_device.IsValid())
			{
				static_cast<void>(m_device.Destroy(m_handle, m_destroy));
			}

			m_handle = {};
		}

		/**
		 * \brief Destroys the held handle now, clears this owner, and reports the device result.
		 *
		 * The handle is released from this owner even when destruction fails because there is no safe retry state to preserve here.
		 */
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

	/**
	 * \name Unique owners for individually destroyed resource handles
	 *
	 * DescriptorArenaHandle is absent because arenas are device-owned. DescriptorSetHandle is absent because sets are reclaimed by DescriptorArena. \{
	 */

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

	/** \} */

} // namespace azo::rhi

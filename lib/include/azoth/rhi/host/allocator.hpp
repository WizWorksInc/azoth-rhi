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
 * \brief Host CPU allocation hooks and host-supplied device memory allocation.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <atomic>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace azo::rhi
{

	/**
	 * \brief Sink for CPU allocations routed through HostAllocate and HostAllocatorAdapter.
	 *
	 * Free receives the original size and alignment so an implementation can be a pool or arena instead of a general heap.
	 *
	 * \attention Must be thread-safe. Install before the first routed allocation and keep installed while any allocation it returned is still live.
	 */
	class HostAllocator
	{
	public:
		HostAllocator()									 = default;
		HostAllocator(const HostAllocator &)			 = delete;
		HostAllocator & operator=(const HostAllocator &) = delete;
		HostAllocator(HostAllocator &&)					 = delete;
		HostAllocator & operator=(HostAllocator &&)		 = delete;
		virtual ~HostAllocator()						 = default;

		/**
		 * \brief Allocates storage with the requested size and alignment, or null when refused.
		 */
		[[nodiscard]] virtual void * Allocate(std::size_t size, std::size_t alignment) = 0;

		/**
		 * \brief Frees storage returned by Allocate with the same size and alignment.
		 */
		virtual void Free(void * memory, std::size_t size, std::size_t alignment) = 0;
	};

	namespace detail
	{
		/*
		 * The slot itself lives in the library and not in this header.
		 *
		 * An inline variable is one object per binary once a shared build is in play, so a host that installed an allocator would be storing into its own copy
		 * while the library kept reading another. Reaching it through a call leaves one object for both sides. Windows has no spelling that would let the
		 * variable stay here either.
		 */
		[[nodiscard]] AZO_RHI_API std::atomic<HostAllocator *> & HostAllocatorSlot() noexcept;
	} // namespace detail

	/**
	 * \brief Installs the process-wide host allocator or clears it with nullptr.
	 *
	 * \attention Install once before routed allocations begin. Frees go to the allocator installed at free time.
	 */
	inline void SetHostAllocator(HostAllocator * allocator) noexcept
	{
		detail::HostAllocatorSlot().store(allocator, std::memory_order_release);
	}

	[[nodiscard]] inline HostAllocator * GetHostAllocator() noexcept
	{
		return detail::HostAllocatorSlot().load(std::memory_order_acquire);
	}

	/**
	 * \brief Allocates through the installed host allocator or aligned operator new.
	 */
	[[nodiscard]] inline void * HostAllocate(std::size_t size, std::size_t alignment)
	{
		if (HostAllocator * allocator = GetHostAllocator(); allocator != nullptr)
		{
			return allocator->Allocate(size, alignment);
		}
		return ::operator new(size, std::align_val_t{ alignment });
	}

	/**
	 * \brief Frees through the installed host allocator or aligned operator delete.
	 *
	 * size and alignment must match the allocation that produced memory.
	 */
	inline void HostFree(void * memory, std::size_t size, std::size_t alignment) noexcept
	{
		if (HostAllocator * allocator = GetHostAllocator(); allocator != nullptr)
		{
			allocator->Free(memory, size, alignment);
			return;
		}
		::operator delete(memory, std::align_val_t{ alignment });
	}

	/**
	 * \brief Deleter for objects constructed by HostNew.
	 *
	 * The deleter stores the allocation's exact size and alignment so base-typed owners can free derived-object storage correctly.
	 */
	struct HostDeleter final
	{
		std::size_t size	  = 0;
		std::size_t alignment = 0;

		template <class T>
		void operator()(T * object) const noexcept
		{
			if (object != nullptr)
			{
				std::destroy_at(object);
				HostFree(object, size, alignment);
			}
		}
	};

	/**
	 * \brief Unique owner for an object stored in host-allocator memory.
	 *
	 * Converting to a base-typed owner still requires the usual virtual-destructor rule for deletion through the base type.
	 */
	template <class T>
	using HostUniquePtr = std::unique_ptr<T, HostDeleter>;

	/**
	 * \brief Constructs an object in memory from the installed host allocator.
	 *
	 * Returns null when the allocator refuses storage. With exceptions enabled, constructor failure frees the raw storage before rethrowing.
	 */
	template <class T, class... Args>
	[[nodiscard]] HostUniquePtr<T> HostNew(Args &&... args)
	{
		void * storage = HostAllocate(sizeof(T), alignof(T));
		if (storage == nullptr)
		{
			return HostUniquePtr<T>{};
		}

#ifdef AZOTH_RHI_NO_EXCEPTIONS
		// Construction cannot leave in this build, so storage has no unowned failure window.
		return HostUniquePtr<T>{ std::construct_at(static_cast<T *>(storage), std::forward<Args>(args)...),
			HostDeleter{ .size = sizeof(T), .alignment = alignof(T) } };
#else
		try
		{
			return HostUniquePtr<T>{ std::construct_at(static_cast<T *>(storage), std::forward<Args>(args)...),
				HostDeleter{ .size = sizeof(T), .alignment = alignof(T) } };
		}
		catch (...)
		{
			// No owner exists until construct_at returns, so failed construction frees by hand.
			HostFree(storage, sizeof(T), alignof(T));
			throw;
		}
#endif
	}

	/**
	 * \brief Standard-library allocator adapter over the installed host allocator.
	 *
	 * Stateless instances compare equal, so containers can move storage between adapter instances.
	 */
	template <class T>
	class HostAllocatorAdapter
	{
	public:
		using value_type = T;

		HostAllocatorAdapter() noexcept = default;

		template <class U>
		explicit constexpr HostAllocatorAdapter(const HostAllocatorAdapter<U> &) noexcept
		{
		}

		/**
		 * \brief Reports an allocation refusal through the build's allocation-failure policy.
		 *
		 * Throws std::bad_alloc by default. Aborts when exceptions are disabled because standard containers have no error-code channel.
		 */
		[[noreturn]] static void RefuseAllocation()
		{
#ifdef AZOTH_RHI_NO_EXCEPTIONS
			std::abort();
#else
			throw std::bad_alloc();
#endif
		}

		[[nodiscard]] T * allocate(std::size_t count)
		{
			if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
			{
				RefuseAllocation();
			}
			void * memory = HostAllocate(count * sizeof(T), alignof(T));
			if (memory == nullptr)
			{
				RefuseAllocation();
			}
			return static_cast<T *>(memory);
		}

		void deallocate(T * memory, std::size_t count) noexcept
		{
			// Spelled out because several instantiations use pointer T, where implicit decay hides one level.
			HostFree(static_cast<void *>(memory), count * sizeof(T), alignof(T));
		}

		template <class U>
		friend constexpr bool operator==(const HostAllocatorAdapter &, const HostAllocatorAdapter<U> &) noexcept
		{
			return true;
		}
	};

	/**
	 * \brief Device-memory allocation request for one placed buffer or texture.
	 *
	 * size and alignment are backend-adjusted values from GetBufferMemoryInfo or GetTextureMemoryInfo.
	 */
	struct MemoryRequest final
	{
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;

		/**
		 * \brief Heap pool required by the resource's MemoryUsage.
		 */
		HeapType heapType = HeapType::eGpuLocal;

		/**
		 * \brief Whether the span will hold a buffer resource.
		 */
		bool forBuffer = false;

		/**
		 * \brief Whether the span will hold a texture resource.
		 */
		bool forTexture = false;

		bool allowAliasing = false;
		CString debugName  = nullptr;
	};

	/**
	 * \brief Granted range of device memory.
	 *
	 * The heap is an ordinary RHI handle. Allocators create heaps through Device::CreateHeap and hand back offsets into them.
	 */
	struct MemorySpan final
	{
		HeapHandle heap{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = 0;

		/**
		 * \brief Allocator-owned payload returned unchanged to Free.
		 */
		void * userData = nullptr;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return heap.IsValid();
		}
	};

	/**
	 * \brief Coarse allocator accounting.
	 *
	 * Byte fields are bytes. heapCount and liveAllocations are counts.
	 */
	struct MemoryStats final
	{
		std::uint64_t heapCount		  = 0;
		std::uint64_t reservedBytes	  = 0;
		std::uint64_t allocatedBytes  = 0;
		std::uint64_t liveAllocations = 0;
	};

	/**
	 * \brief Host-supplied allocator for buffer and texture heap memory.
	 *
	 * When installed on a device, buffers and textures are placed into spans returned here instead of using the backend's own allocator.
	 *
	 * \attention Must be thread-safe. Allocate and Free may call back into the device to create and destroy heaps while RHI resource guards are held.
	 */
	class DeviceMemoryAllocator
	{
	public:
		DeviceMemoryAllocator()											 = default;
		DeviceMemoryAllocator(const DeviceMemoryAllocator &)			 = delete;
		DeviceMemoryAllocator & operator=(const DeviceMemoryAllocator &) = delete;
		DeviceMemoryAllocator(DeviceMemoryAllocator &&)					 = delete;
		DeviceMemoryAllocator & operator=(DeviceMemoryAllocator &&)		 = delete;
		virtual ~DeviceMemoryAllocator()								 = default;

		/**
		 * \brief Grants a span satisfying request, or returns false to fail the resource create.
		 */
		[[nodiscard]] virtual bool Allocate(Device device, const MemoryRequest & request, MemorySpan & out) = 0;

		/**
		 * \brief Returns a span granted earlier.
		 *
		 * The RHI has already destroyed the resource that occupied the span.
		 */
		virtual void Free(Device device, const MemorySpan & span) = 0;

		[[nodiscard]] virtual MemoryStats Stats() const
		{
			return {};
		}
	};

	namespace detail
	{
		// One object for both sides, for the reason HostAllocatorSlot above gives.
		[[nodiscard]] AZO_RHI_API std::atomic<DeviceMemoryAllocator *> & DeviceAllocatorSlot() noexcept;
	} // namespace detail

	/**
	 * \brief Installs the process-wide device memory allocator or clears it with nullptr.
	 *
	 * \attention Install once before resource creation. Freed spans go to the allocator installed at free time.
	 */
	inline void SetDeviceMemoryAllocator(DeviceMemoryAllocator * allocator) noexcept
	{
		detail::DeviceAllocatorSlot().store(allocator, std::memory_order_release);
	}

	[[nodiscard]] inline DeviceMemoryAllocator * GetDeviceMemoryAllocator() noexcept
	{
		return detail::DeviceAllocatorSlot().load(std::memory_order_acquire);
	}

	/**
	 * \brief Maps a resource MemoryUsage to the heap class it requires.
	 */
	[[nodiscard]] constexpr HeapType HeapTypeForUsage(MemoryUsage usage) noexcept
	{
		switch (usage)
		{
		case MemoryUsage::eCpuUpload:
		case MemoryUsage::eCpuToGpu:	return HeapType::eCpuUpload;
		case MemoryUsage::eCpuReadback: return HeapType::eCpuReadback;
		default:						return HeapType::eGpuLocal;
		}
	}

} // namespace azo::rhi

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/resources/resources.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace azo::rhi
{

	class Device;

	class HostAllocator
	{
	public:
		HostAllocator()									 = default;
		HostAllocator(const HostAllocator &)			 = delete;
		HostAllocator & operator=(const HostAllocator &) = delete;
		HostAllocator(HostAllocator &&)					 = delete;
		HostAllocator & operator=(HostAllocator &&)		 = delete;
		virtual ~HostAllocator()						 = default;

		[[nodiscard]] virtual void * Allocate(std::size_t size, std::size_t alignment) = 0;

		virtual void Free(void * memory, std::size_t size, std::size_t alignment) = 0;
	};

	namespace detail
	{
		[[nodiscard]] AZO_RHI_API std::atomic<HostAllocator *> & HostAllocatorSlot() noexcept;
	}

	inline void SetHostAllocator(HostAllocator * allocator) noexcept
	{
		detail::HostAllocatorSlot().store(allocator, std::memory_order_release);
	}

	[[nodiscard]] inline HostAllocator * GetHostAllocator() noexcept
	{
		return detail::HostAllocatorSlot().load(std::memory_order_acquire);
	}

	[[nodiscard]] inline void * HostAllocate(std::size_t size, std::size_t alignment)
	{
		if (HostAllocator * allocator = GetHostAllocator(); allocator != nullptr)
		{
			return allocator->Allocate(size, alignment);
		}
		return ::operator new(size, std::align_val_t{ alignment });
	}

	inline void HostFree(void * memory, std::size_t size, std::size_t alignment) noexcept
	{
		if (HostAllocator * allocator = GetHostAllocator(); allocator != nullptr)
		{
			allocator->Free(memory, size, alignment);
			return;
		}
		::operator delete(memory, std::align_val_t{ alignment });
	}

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

	template <class T>
	using HostUniquePtr = std::unique_ptr<T, HostDeleter>;

	template <class T, class... Args>
	[[nodiscard]] HostUniquePtr<T> HostNew(Args &&... args)
	{
		void * storage = HostAllocate(sizeof(T), alignof(T));
		if (storage == nullptr)
		{
			return HostUniquePtr<T>{};
		}

#ifdef AZOTH_RHI_NO_EXCEPTIONS
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
			HostFree(storage, sizeof(T), alignof(T));
			throw;
		}
#endif
	}

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
			HostFree(static_cast<void *>(memory), count * sizeof(T), alignof(T));
		}

		template <class U>
		friend constexpr bool operator==(const HostAllocatorAdapter &, const HostAllocatorAdapter<U> &) noexcept
		{
			return true;
		}
	};

	struct MemoryRequest final
	{
		std::uint64_t size		= 0;
		std::uint64_t alignment = 0;

		HeapType heapType = HeapType::eGpuLocal;

		bool forBuffer = false;

		bool forTexture = false;

		bool allowAliasing = false;
		CString debugName  = nullptr;
	};

	struct MemorySpan final
	{
		HeapHandle heap{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = 0;

		void * userData = nullptr;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return heap.IsValid();
		}
	};

	struct MemoryStats final
	{
		std::uint64_t heapCount		  = 0;
		std::uint64_t reservedBytes	  = 0;
		std::uint64_t allocatedBytes  = 0;
		std::uint64_t liveAllocations = 0;
	};

	class DeviceMemoryAllocator
	{
	public:
		DeviceMemoryAllocator()											 = default;
		DeviceMemoryAllocator(const DeviceMemoryAllocator &)			 = delete;
		DeviceMemoryAllocator & operator=(const DeviceMemoryAllocator &) = delete;
		DeviceMemoryAllocator(DeviceMemoryAllocator &&)					 = delete;
		DeviceMemoryAllocator & operator=(DeviceMemoryAllocator &&)		 = delete;
		virtual ~DeviceMemoryAllocator()								 = default;

		[[nodiscard]] virtual bool Allocate(Device device, const MemoryRequest & request, MemorySpan & out) = 0;

		virtual void Free(Device device, const MemorySpan & span) = 0;

		[[nodiscard]] virtual MemoryStats Stats() const
		{
			return {};
		}
	};

	namespace detail
	{
		[[nodiscard]] AZO_RHI_API std::atomic<DeviceMemoryAllocator *> & DeviceAllocatorSlot() noexcept;
	}

	inline void SetDeviceMemoryAllocator(DeviceMemoryAllocator * allocator) noexcept
	{
		detail::DeviceAllocatorSlot().store(allocator, std::memory_order_release);
	}

	[[nodiscard]] inline DeviceMemoryAllocator * GetDeviceMemoryAllocator() noexcept
	{
		return detail::DeviceAllocatorSlot().load(std::memory_order_acquire);
	}

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

}

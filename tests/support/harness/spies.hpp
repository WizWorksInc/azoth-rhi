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

#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/profiler.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <semaphore>
#include <string>
#include <unordered_map>
#include <vector>

namespace azo::rhi::test
{

	class CountingHostAllocator final : public HostAllocator
	{
	public:
		void * Allocate(const std::size_t size, const std::size_t alignment) override
		{
			void * memory = ::operator new(size, std::align_val_t{ alignment });

			allocateCalls.fetch_add(1, std::memory_order_relaxed);
			liveBytes.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed);

			const std::scoped_lock lock(m_mutex);
			m_live.emplace(memory,
				Block{
					.size	   = size,
					.alignment = alignment,
				});
			return memory;
		}

		void Free(void * memory, const std::size_t size, const std::size_t alignment) override
		{
			freeCalls.fetch_add(1, std::memory_order_relaxed);
			liveBytes.fetch_sub(static_cast<std::int64_t>(size), std::memory_order_relaxed);

			{
				const std::scoped_lock lock(m_mutex);
				const auto found = m_live.find(memory);
				if (found == m_live.end())
				{
					++freesOfUnknownPointers;
				}
				else
				{
					if (found->second.size != size || found->second.alignment != alignment)
					{
						++mismatchedFrees;
					}
					m_live.erase(found);
				}
			}

			::operator delete(memory, std::align_val_t{ alignment });
		}

		[[nodiscard]] std::size_t LiveBlocks() const
		{
			const std::scoped_lock lock(m_mutex);
			return m_live.size();
		}

		std::atomic<std::uint64_t> allocateCalls{ 0 };
		std::atomic<std::uint64_t> freeCalls{ 0 };
		std::atomic<std::int64_t> liveBytes{ 0 };

		std::uint64_t mismatchedFrees		 = 0;
		std::uint64_t freesOfUnknownPointers = 0;

	private:
		struct Block final
		{
			std::size_t size	  = 0;
			std::size_t alignment = 0;
		};

		mutable std::mutex m_mutex;
		std::unordered_map<void *, Block> m_live;
	};

	class ScopedHostAllocator final
	{
	public:
		explicit ScopedHostAllocator(HostAllocator * allocator) noexcept : m_previous(GetHostAllocator())
		{
			SetHostAllocator(allocator);
		}

		ScopedHostAllocator(const ScopedHostAllocator &)			 = delete;
		ScopedHostAllocator & operator=(const ScopedHostAllocator &) = delete;
		ScopedHostAllocator(ScopedHostAllocator &&)					 = delete;
		ScopedHostAllocator & operator=(ScopedHostAllocator &&)		 = delete;

		~ScopedHostAllocator()
		{
			SetHostAllocator(m_previous);
		}

	private:
		HostAllocator * m_previous = nullptr;
	};

	struct Scheduler final
	{
		std::atomic<std::size_t> created{ 0 };
		std::atomic<std::size_t> destroyed{ 0 };
		std::atomic<std::size_t> acquired{ 0 };

		std::atomic<std::size_t> held{ 0 };
		std::atomic<std::size_t> peakHeld{ 0 };

		std::atomic<std::uint64_t> fiber{ 0 };

		[[nodiscard]] SyncOps Ops() noexcept
		{
			return SyncOps{
				.context = this,
				.create =
					[](void * context) noexcept
				{
					static_cast<Scheduler *>(context)->created.fetch_add(1, std::memory_order_relaxed);
					return static_cast<void *>(new std::binary_semaphore(1));
				},
				.destroy =
					[](void * context, void * lock) noexcept
				{
					static_cast<Scheduler *>(context)->destroyed.fetch_add(1, std::memory_order_relaxed);
					delete static_cast<std::binary_semaphore *>(lock);
				},
				.acquire =
					[](void * context, void * lock) noexcept
				{
					auto * self = static_cast<Scheduler *>(context);
					self->acquired.fetch_add(1, std::memory_order_relaxed);
					static_cast<std::binary_semaphore *>(lock)->acquire();

					const std::size_t now = self->held.fetch_add(1, std::memory_order_relaxed) + 1;
					std::size_t peak	  = self->peakHeld.load(std::memory_order_relaxed);
					while (now > peak && !self->peakHeld.compare_exchange_weak(peak, now, std::memory_order_relaxed))
					{
					}
				},
				.tryAcquire =
					[](void *, void * lock) noexcept
				{
					return static_cast<std::binary_semaphore *>(lock)->try_acquire();
				},
				.release =
					[](void * context, void * lock) noexcept
				{
					static_cast<Scheduler *>(context)->held.fetch_sub(1, std::memory_order_relaxed);
					static_cast<std::binary_semaphore *>(lock)->release();
				},
				.currentFiber =
					[](void * context) noexcept
				{
					return FiberId{ static_cast<Scheduler *>(context)->fiber.load(std::memory_order_relaxed) };
				},
			};
		}
	};

	class RecordingDeviceAllocator final : public DeviceMemoryAllocator
	{
	public:
		bool Allocate(Device device, const MemoryRequest & request, MemorySpan & out) override
		{
			const std::scoped_lock lock(m_mutex);

			requests.push_back(request);
			if (failNextAllocate)
			{
				failNextAllocate = false;
				++refusedAllocations;
				return false;
			}

			HeapDesc heap{};
			heap.type		   = request.heapType;
			heap.size		   = request.size + request.alignment;
			heap.alignment	   = request.alignment;
			heap.allowBuffers  = request.forBuffer;
			heap.allowTextures = request.forTexture;
			heap.allowAliasing = request.allowAliasing;
			heap.debugName	   = "azoth.rhi.test.allocatorHeap";

			Error error{};
			const HeapHandle handle = device.CreateHeap(heap, error);
			if (!handle.IsValid())
			{
				++failedHeapCreations;
				return false;
			}

			m_heaps.push_back(handle);
			out = MemorySpan{
				.heap	  = handle,
				.offset	  = 0,
				.size	  = request.size,
				.userData = nullptr,
			};
			++grantedSpans;
			return true;
		}

		void Free(Device device, const MemorySpan & span) override
		{
			const std::scoped_lock lock(m_mutex);
			++returnedSpans;
			device.Destroy(span.heap);
			std::erase(m_heaps, span.heap);
		}

		[[nodiscard]] MemoryStats Stats() const override
		{
			const std::scoped_lock lock(m_mutex);
			return MemoryStats{
				.heapCount		 = m_heaps.size(),
				.reservedBytes	 = 0,
				.allocatedBytes	 = 0,
				.liveAllocations = grantedSpans - returnedSpans,
			};
		}

		[[nodiscard]] std::size_t OutstandingSpans() const
		{
			const std::scoped_lock lock(m_mutex);
			return grantedSpans - returnedSpans;
		}

		[[nodiscard]] std::vector<MemoryRequest> SeenRequests() const
		{
			const std::scoped_lock lock(m_mutex);
			return requests;
		}

		bool failNextAllocate = false;

		std::vector<MemoryRequest> requests;
		std::uint64_t grantedSpans		  = 0;
		std::uint64_t returnedSpans		  = 0;
		std::uint64_t refusedAllocations  = 0;
		std::uint64_t failedHeapCreations = 0;

	private:
		mutable std::mutex m_mutex;
		std::vector<HeapHandle> m_heaps;
	};

	class ScopedDeviceAllocator final
	{
	public:
		explicit ScopedDeviceAllocator(DeviceMemoryAllocator * allocator) noexcept : m_previous(GetDeviceMemoryAllocator())
		{
			SetDeviceMemoryAllocator(allocator);
		}

		ScopedDeviceAllocator(const ScopedDeviceAllocator &)			 = delete;
		ScopedDeviceAllocator & operator=(const ScopedDeviceAllocator &) = delete;
		ScopedDeviceAllocator(ScopedDeviceAllocator &&)					 = delete;
		ScopedDeviceAllocator & operator=(ScopedDeviceAllocator &&)		 = delete;

		~ScopedDeviceAllocator()
		{
			SetDeviceMemoryAllocator(m_previous);
		}

	private:
		DeviceMemoryAllocator * m_previous = nullptr;
	};

	class RecordingProfiler final : public Profiler
	{
	public:
		void BeginZone(const ZoneLocation & location) override
		{
			const std::scoped_lock lock(m_mutex);
			++beginZoneCalls;
			++depth;
			maxDepth = depth > maxDepth ? depth : maxDepth;

			if (location.name != nullptr)
			{
				zoneNames.emplace_back(location.name);
			}
			everySiteHadFileAndLine = everySiteHadFileAndLine && location.file != nullptr && location.line != 0;
		}

		void EndZone() override
		{
			const std::scoped_lock lock(m_mutex);
			++endZoneCalls;
			--depth;
		}

		void Plot(const CString name, const std::int64_t value) override
		{
			const std::scoped_lock lock(m_mutex);
			plots.emplace_back(name != nullptr ? name : "", value);
		}

		void GpuAllocate(const void * address, const std::uint64_t size, const CString pool) override
		{
			const std::scoped_lock lock(m_mutex);
			gpuAllocations.push_back(GpuEvent{
				.address = address,
				.size	 = size,
				.pool	 = pool != nullptr ? pool : "",
			});
		}

		void GpuFree(const void * address, const CString pool) override
		{
			const std::scoped_lock lock(m_mutex);
			gpuFrees.push_back(GpuEvent{
				.address = address,
				.size	 = 0,
				.pool	 = pool != nullptr ? pool : "",
			});
		}

		void EnterFiber(const FiberId fiber, const CString name) override
		{
			const std::scoped_lock lock(m_mutex);
			fiberEvents.push_back(FiberEvent{
				.fiber	 = fiber,
				.name	 = name != nullptr ? name : "",
				.named	 = name != nullptr,
				.entered = true,
			});
		}

		void LeaveFiber(const FiberId fiber) override
		{
			const std::scoped_lock lock(m_mutex);
			fiberEvents.push_back(FiberEvent{
				.fiber	 = fiber,
				.entered = false,
			});
		}

		struct GpuEvent final
		{
			const void * address = nullptr;
			std::uint64_t size	 = 0;
			std::string pool;
		};

		struct FiberEvent final
		{
			FiberId fiber{};
			std::string name;
			bool named	 = false;
			bool entered = false;
		};

		std::mutex m_mutex;
		std::vector<std::string> zoneNames;
		std::vector<std::pair<std::string, std::int64_t>> plots;
		std::vector<GpuEvent> gpuAllocations;
		std::vector<GpuEvent> gpuFrees;
		std::vector<FiberEvent> fiberEvents;
		int beginZoneCalls			 = 0;
		int endZoneCalls			 = 0;
		int depth					 = 0;
		int maxDepth				 = 0;
		bool everySiteHadFileAndLine = true;
	};

	using ScopedProfiler = ::azo::rhi::ScopedProfiler;

} // namespace azo::rhi::test

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
 * \brief Backend interface discovery, block caching, and synchronization helpers.
 */

#include "azoth/rhi/backend/allocation_tracker.hpp"
#include "azoth/rhi/backend/blocks/command_list.hpp"	 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/command_pool.hpp"	 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/common.hpp"			 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/descriptor_arena.hpp" // IWYU pragma: export
#include "azoth/rhi/backend/blocks/device.hpp"			 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/instance.hpp"		 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/native_object.hpp"	 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/queue.hpp"			 // IWYU pragma: export
#include "azoth/rhi/backend/blocks/swapchain.hpp"		 // IWYU pragma: export
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <mutex>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Resolve validation flag used after the validation layer has already checked live state and generation.
	 *
	 * Device-tag and slot-bounds checks still run. This only skips the live-generation check already done in front of the backend.
	 */
	inline constexpr bool kHandleAlreadyChecked = false;

	/**
	 * \brief Queue counts and dedication requirements requested from a backend.
	 */
	struct QueuePlan final
	{
		std::uint32_t graphicsCount = 0;
		std::uint32_t computeCount	= 0;
		std::uint32_t copyCount		= 0;
		bool computeDedicated		= false;
		bool copyDedicated			= false;
	};

	/**
	 * \brief Resolves device queue requests into per-type queue counts and dedicated-queue requirements.
	 *
	 * \param queues Requested queues. An empty span requests one graphics, one compute, and one copy queue when available.
	 * \note Duplicate entries keep the largest requested count and preserve any dedicated-queue requirement.
	 */
	[[nodiscard]] inline QueuePlan PlanQueues(std::span<const QueueRequest> queues) noexcept
	{
		if (queues.empty())
		{
			return QueuePlan{
				.graphicsCount = 1,
				.computeCount  = 1,
				.copyCount	   = 1,
			};
		}

		QueuePlan plan{};
		for (const QueueRequest & request : queues)
		{
			const std::uint32_t count = request.minCount == 0 ? 1u : request.minCount;
			switch (request.type)
			{
			case QueueType::eGraphics: plan.graphicsCount = std::max(plan.graphicsCount, count); break;
			case QueueType::eCompute:
				plan.computeCount	  = std::max(plan.computeCount, count);
				plan.computeDedicated = plan.computeDedicated || request.requireDedicatedQueue;
				break;
			case QueueType::eCopy:
				plan.copyCount	   = std::max(plan.copyCount, count);
				plan.copyDedicated = plan.copyDedicated || request.requireDedicatedQueue;
				break;
			}
		}

		return plan;
	}

	/**
	 * \brief Returns the queue count exposed for one queue type in a capabilities block.
	 */
	[[nodiscard]] inline std::uint32_t QueueCountForType(const DeviceCaps & caps, QueueType type) noexcept
	{
		switch (type)
		{
		case QueueType::eGraphics: return caps.graphicsQueueCount;
		case QueueType::eCompute:  return caps.computeQueueCount;
		case QueueType::eCopy:	   return caps.copyQueueCount;
		}

		return 0;
	}

	/**
	 * \brief Interface id and accepted layout version for one ABI block type.
	 */
	template <typename Block>
	struct InterfaceTraits;

	template <>
	struct InterfaceTraits<NativeObjectApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.nativeObject");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<InstanceApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.instance");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<CoreDeviceApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.core");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PresentApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.present");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PlacedMemoryApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.placedMemory");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RayTracingApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.rayTracing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueryApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.query");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PipelineCacheApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.pipelineCache");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ResidencyApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.residency");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ResourceIntrospectionApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.resourceIntrospection");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<AdoptionApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.adoption");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ExternalSharingApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.externalSharing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ExternalCapabilityApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.externalCapability");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueueApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.queue");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<SparseApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.sparse");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<CommandPoolApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.commandPool");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<DescriptorArenaApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.descriptorArena");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RenderCommandApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.render");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<AliasingCommandApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.aliasing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RayTracingCommandApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.rayTracingCommand");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueryCommandApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.queryCommand");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<IndirectApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.indirect");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<IndirectCountApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.indirectCount");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<NativeEscapeApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.nativeEscape");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<SwapchainApi> final
	{
		static constexpr InterfaceId kId		= MakeInterfaceId("azoth.rhi.block.swapchain");
		static constexpr std::uint32_t kVersion = 1;
	};

	/**
	 * \brief Published ABI block entry used by backend objects.
	 *
	 * Backends name supported blocks by listing Published entries in PublishingObject. Missing entries mean unsupported blocks, not stubbed blocks.
	 */
	template <typename Block, const Block & (*Table)() noexcept>
	struct Published final
	{
		/**
		 * \brief Returns the block table when id and version match.
		 *
		 * \param minVersion Minimum ABI version accepted by the caller.
		 */
		[[nodiscard]] static const void * Match(const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			if (id != InterfaceTraits<Block>::kId || minVersion > InterfaceTraits<Block>::kVersion)
			{
				return nullptr;
			}

			return &Table();
		}
	};

	/**
	 * \brief Queries a pack of published block entries and returns the first matching table.
	 *
	 * \param minVersion Minimum ABI version accepted by the caller.
	 */
	template <class... Blocks>
	[[nodiscard]] const void * QueryPublished(void *, const InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		const void * found = nullptr;
		((found = found != nullptr ? found : Blocks::Match(id, minVersion)), ...);
		return found;
	}

	/**
	 * \brief Returns a static BackendObject header for a backend object that publishes Blocks.
	 *
	 * The returned object is suitable for the first word of a backend object.
	 */
	template <class... Blocks>
	[[nodiscard]] const BackendObject * PublishingObject() noexcept
	{
		static constexpr BackendObject object{ .queryInterface = &QueryPublished<Blocks...> };
		return &object;
	}

	namespace detail
	{

		/*
		 * Both counters are defined in the library, not here.
		 *
		 * A static inside an inline function is one object per binary in a shared build, which would have the guard a backend takes and the violation a host reads
		 * counting in different places. Same reason detail::HostAllocatorSlot in host/allocator.hpp is reached through a call.
		 */
		[[nodiscard]] AZO_RHI_API int & GuardsHeld() noexcept;

		[[nodiscard]] AZO_RHI_API std::atomic<std::uint64_t> & ReentrancyViolationCount() noexcept;

		/**
		 * \brief Returns the number of detected attempts to call reentrant host code while an RHI guard was held.
		 *
		 * \note The library never resets this counter, so tests should compare deltas.
		 */
		[[nodiscard]] inline std::uint64_t ReentrancyViolations() noexcept
		{
			return ReentrancyViolationCount().load(std::memory_order_relaxed);
		}

		/**
		 * \brief Records a violation when a host callback could reenter the RHI while a guard is held by the current thread.
		 */
		inline void CheckNoGuardHeld() noexcept
		{
			if (GuardsHeld() == 0)
			{
				return;
			}

			ReentrancyViolationCount().fetch_add(1, std::memory_order_relaxed);
			AZO_RHI_DEBUG_BREAK();
		}

		/**
		 * \brief Process-wide guard used before a device-specific synchronization policy exists.
		 *
		 * Instance and device creation run before any device threading mode can apply, so this guard uses the RHI spin lock instead of device SyncOps.
		 */
		[[nodiscard]] inline SpinLock & LifetimeGuard() noexcept
		{
			static SpinLock guard;
			return guard;
		}

		/**
		 * \brief Scoped lock for backend instance and device lifetime entries.
		 *
		 * \note Counts as an RHI guard so reentrancy checks cover lifetime entries and device entries under the same rule.
		 */
		class LifetimeLock final
		{
		public:
			LifetimeLock() noexcept
			{
				++GuardsHeld();
				LifetimeGuard().lock();
			}

			~LifetimeLock() noexcept
			{
				LifetimeGuard().unlock();
				--GuardsHeld();
			}

			LifetimeLock(const LifetimeLock &)			   = delete;
			LifetimeLock & operator=(const LifetimeLock &) = delete;
			LifetimeLock(LifetimeLock &&)				   = delete;
			LifetimeLock & operator=(LifetimeLock &&)	   = delete;
		};

	} // namespace detail

	/**
	 * \brief Device-scoped lock selected by the device threading mode.
	 *
	 * Single-threaded mode counts the guard but takes no primitive. Threaded mode uses SpinLock. Cooperative mode uses SyncOps so fiber schedulers can suspend.
	 */
	class DeviceLock final
	{
	public:
		DeviceLock() = default;

		DeviceLock(const DeviceLock &)			   = delete;
		DeviceLock & operator=(const DeviceLock &) = delete;
		DeviceLock(DeviceLock &&)				   = delete;
		DeviceLock & operator=(DeviceLock &&)	   = delete;
		~DeviceLock()							   = default;

		/**
		 * \brief Binds the lock to a device threading policy.
		 *
		 * \param sync Required only in cooperative mode. It must remain valid until Release.
		 * \param deviceProfiler Optional pointer to the owning device profiler pointer for fiber-suspension profiling.
		 */
		[[nodiscard]] bool Bind(const ThreadingMode mode, const SyncOps * sync, Profiler * const * deviceProfiler) noexcept
		{
			m_mode			 = mode;
			m_deviceProfiler = deviceProfiler;
			if (mode != ThreadingMode::eCooperative)
			{
				return true;
			}

			m_sync = sync;
			m_host = sync->create(sync->context);
			return m_host != nullptr;
		}

		/**
		 * \brief Releases cooperative host synchronization storage owned by this lock.
		 */
		void Release() noexcept
		{
			if (m_host != nullptr)
			{
				m_sync->destroy(m_sync->context, m_host);
				m_host = nullptr;
			}
		}

		void lock() noexcept
		{
			++detail::GuardsHeld();

			switch (m_mode)
			{
			case ThreadingMode::eSingleThreaded: return;
			case ThreadingMode::eThreads:		 m_spin.lock(); return;
			case ThreadingMode::eCooperative:
			{
				// This is the only RHI lock path that may suspend a fiber.
				AZO_RHI_PROFILE_FIBER_SUSPENSION(*m_sync, m_deviceProfiler != nullptr ? *m_deviceProfiler : nullptr);
				m_sync->acquire(m_sync->context, m_host);
				return;
			}
			}
		}

		void unlock() noexcept
		{
			--detail::GuardsHeld();

			switch (m_mode)
			{
			case ThreadingMode::eSingleThreaded: return;
			case ThreadingMode::eThreads:		 m_spin.unlock(); return;
			case ThreadingMode::eCooperative:	 m_sync->release(m_sync->context, m_host); return;
			}
		}

	private:
		ThreadingMode m_mode				= ThreadingMode::eThreads;
		const SyncOps * m_sync				= nullptr;
		Profiler * const * m_deviceProfiler = nullptr;
		void * m_host						= nullptr;
		SpinLock m_spin;
	};

	/**
	 * \brief Device-level ABI blocks published by a backend.
	 *
	 * Null optional blocks mean the backend declined that capability.
	 */
	struct DeviceBlocks final
	{
		const CoreDeviceApi * core				   = nullptr;
		const PresentApi * present				   = nullptr;
		const PlacedMemoryApi * placedMemory	   = nullptr;
		const RayTracingApi * rayTracing		   = nullptr;
		const QueryApi * query					   = nullptr;
		const PipelineCacheApi * pipelineCache	   = nullptr;
		const ResidencyApi * residency			   = nullptr;
		const ResourceIntrospectionApi * introspection = nullptr;
		const AdoptionApi * adoption			   = nullptr;
		const ExternalSharingApi * externalSharing = nullptr;
	};

	/**
	 * \brief Queue ABI blocks published by a backend queue.
	 *
	 * core is required. sparse is optional.
	 */
	struct QueueBlocks final
	{
		const QueueApi * core	 = nullptr;
		const SparseApi * sparse = nullptr;
	};

	/**
	 * \brief Command-list ABI blocks published by a backend command list.
	 *
	 * render is required. Every other block is optional.
	 */
	struct CommandListBlocks final
	{
		const RenderCommandApi * render			= nullptr;
		const AliasingCommandApi * aliasing		= nullptr;
		const RayTracingCommandApi * rayTracing = nullptr;
		const QueryCommandApi * query			= nullptr;
		const IndirectApi * indirect			= nullptr;
		const IndirectCountApi * indirectCount	= nullptr;
		const NativeEscapeApi * nativeEscape	= nullptr;
	};

	namespace detail
	{

		/**
		 * \brief Reads the BackendObject header stored in the first word of a backend object.
		 *
		 * \attention impl must point to a valid backend object whose first word is a BackendObject pointer.
		 */
		[[nodiscard]] inline const BackendObject * ObjectOf(void * impl) noexcept
		{
			return static_cast<const BackendObject *>(*static_cast<const void * const *>(impl));
		}

		/**
		 * \brief Queries a backend object for one ABI block and rejects tables shorter than this build expects.
		 *
		 * \attention impl must point to a valid backend object. Null impl is not accepted here.
		 */
		template <typename Block>
		[[nodiscard]] inline const Block * QueryBlock(void * impl) noexcept
		{
			const BackendObject * object = ObjectOf(impl);
			if (object == nullptr || object->queryInterface == nullptr)
			{
				return nullptr;
			}

			const auto * block = static_cast<const Block *>(object->queryInterface(impl, InterfaceTraits<Block>::kId, InterfaceTraits<Block>::kVersion));
			if (block == nullptr || block->header.byteSize < sizeof(Block))
			{
				return nullptr;
			}

			return block;
		}

		/**
		 * \brief Returns the backend object behind decorator layers.
		 *
		 * Native-handle accessors use this instead of facade impl pointers so validation wrappers are not cast as backend objects.
		 * \note Unwrapping is bounded to eight layers to prevent decorator cycles from looping forever.
		 */
		[[nodiscard]] inline void * NativeImplOf(void * impl) noexcept
		{
			constexpr int kMaxLayers = 8;

			for (int layer = 0; layer < kMaxLayers && impl != nullptr; ++layer)
			{
				const NativeObjectApi * block = QueryBlock<NativeObjectApi>(impl);
				if (block == nullptr || block->inner == nullptr)
				{
					return impl;
				}

				void * inner = block->inner(impl);
				if (inner == nullptr || inner == impl)
				{
					return impl;
				}

				impl = inner;
			}

			return impl;
		}

		/**
		 * \brief Returns the unwrapped backend object only when it still publishes the expected block table.
		 *
		 * \param expected Block table pointer that identifies the expected backend object kind.
		 */
		template <typename Block>
		[[nodiscard]] inline void * NativeImplOf(void * impl, const Block & expected) noexcept
		{
			void * native = NativeImplOf(impl);
			return native != nullptr && QueryBlock<Block>(native) == &expected ? native : nullptr;
		}

	} // namespace detail

	/**
	 * \brief Cached ABI blocks and per-device support state.
	 *
	 * Device blocks are resolved at device creation. Queue and command-list blocks are resolved from the first child object of each kind and shared by later
	 * facades.
	 * \attention This assumes all queues from one device publish compatible queue blocks and all command lists from one device publish compatible command-list
	 * blocks.
	 */
	class BackendBlockSet final
	{
	public:
		/**
		 * \brief Resolves device blocks, binds per-resource locks, probes child block families, and derives device capabilities.
		 */
		BackendBlockSet(void * deviceImpl, const DeviceDesc & desc) noexcept
			: m_threading(desc.threading),
			  m_sync(desc.sync),
			  // Capture the allocator selected at device creation.
			  m_allocator(desc.allocator != nullptr ? desc.allocator : GetDeviceMemoryAllocator()),
			  m_profiler(desc.profiler)
		{
			m_device.core			 = detail::QueryBlock<CoreDeviceApi>(deviceImpl);
			m_device.present		 = detail::QueryBlock<PresentApi>(deviceImpl);
			m_device.placedMemory	 = detail::QueryBlock<PlacedMemoryApi>(deviceImpl);
			m_device.rayTracing		 = detail::QueryBlock<RayTracingApi>(deviceImpl);
			m_device.query			 = detail::QueryBlock<QueryApi>(deviceImpl);
			m_device.pipelineCache	 = detail::QueryBlock<PipelineCacheApi>(deviceImpl);
			m_device.residency		 = detail::QueryBlock<ResidencyApi>(deviceImpl);
			m_device.introspection	 = detail::QueryBlock<ResourceIntrospectionApi>(deviceImpl);
			m_device.adoption		 = detail::QueryBlock<AdoptionApi>(deviceImpl);
			m_device.externalSharing = detail::QueryBlock<ExternalSharingApi>(deviceImpl);

			BindLocks();
			ProbeChildren(deviceImpl);
			DeriveCaps(deviceImpl, desc);
		}

		~BackendBlockSet()
		{
			for (DeviceLock & lock : m_locks)
			{
				lock.Release();
			}
		}

		BackendBlockSet(const BackendBlockSet &)			 = delete;
		BackendBlockSet & operator=(const BackendBlockSet &) = delete;
		BackendBlockSet(BackendBlockSet &&)					 = delete;
		BackendBlockSet & operator=(BackendBlockSet &&)		 = delete;

		[[nodiscard]] const DeviceBlocks & Device() const noexcept
		{
			return m_device;
		}

		/**
		 * \brief Returns cached queue blocks, resolving them from queueImpl on first use.
		 *
		 * \attention queueImpl must belong to this device. The first successful resolution defines the cached optional queue blocks for later queues.
		 */
		[[nodiscard]] const QueueBlocks * Queue(void * queueImpl) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_queue.core == nullptr)
			{
				m_queue.core   = detail::QueryBlock<QueueApi>(queueImpl);
				m_queue.sparse = detail::QueryBlock<SparseApi>(queueImpl);
			}

			return m_queue.core != nullptr ? &m_queue : nullptr;
		}

		/**
		 * \brief Returns cached command-list blocks, resolving them from listImpl on first use.
		 *
		 * \attention listImpl must belong to this device. The first successful resolution defines the cached optional command-list blocks for later lists.
		 */
		[[nodiscard]] const CommandListBlocks * CommandList(void * listImpl) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_commandList.render == nullptr)
			{
				m_commandList.render		= detail::QueryBlock<RenderCommandApi>(listImpl);
				m_commandList.aliasing		= detail::QueryBlock<AliasingCommandApi>(listImpl);
				m_commandList.rayTracing	= detail::QueryBlock<RayTracingCommandApi>(listImpl);
				m_commandList.query			= detail::QueryBlock<QueryCommandApi>(listImpl);
				m_commandList.indirect		= detail::QueryBlock<IndirectApi>(listImpl);
				m_commandList.indirectCount = detail::QueryBlock<IndirectCountApi>(listImpl);
				m_commandList.nativeEscape	= detail::QueryBlock<NativeEscapeApi>(listImpl);
			}

			return m_commandList.render != nullptr ? &m_commandList : nullptr;
		}

		[[nodiscard]] const DeviceCaps & Caps() const noexcept
		{
			return m_caps;
		}

		[[nodiscard]] ThreadingMode Threading() const noexcept
		{
			return m_threading;
		}

		[[nodiscard]] const SyncOps & Sync() const noexcept
		{
			return m_sync;
		}

		[[nodiscard]] DeviceMemoryAllocator * Allocator() const noexcept
		{
			return m_allocator;
		}

		[[nodiscard]] Profiler * Profiling() const noexcept
		{
			return m_profiler;
		}

		/**
		 * \brief Returns true when resource creation should route through the installed allocator and placed-resource backend block.
		 */
		[[nodiscard]] bool AllocatesPlaced() const noexcept
		{
			return m_allocator != nullptr && m_device.placedMemory != nullptr;
		}

		[[nodiscard]] detail::AllocationTracker & Tracker() noexcept
		{
			return m_tracker;
		}

		/**
		 * \brief Returns the guard assigned to one resource kind.
		 */
		[[nodiscard]] DeviceLock & Guard(const ResourceType type) noexcept
		{
			return m_locks[static_cast<std::size_t>(type)];
		}

		/**
		 * \brief Returns the guard for backend child objects that are not partitioned by ResourceType.
		 */
		[[nodiscard]] DeviceLock & ObjectGuard() noexcept
		{
			return m_locks[kObjectLock];
		}

		[[nodiscard]] bool Unguarded() const noexcept
		{
			return m_threading == ThreadingMode::eSingleThreaded;
		}

	private:
		/**
		 * \brief Binds every per-device lock to the selected threading policy.
		 */
		void BindLocks() noexcept
		{
			for (DeviceLock & lock : m_locks)
			{
				static_cast<void>(lock.Bind(m_threading, &m_sync, &m_profiler));
			}
		}

		/**
		 * \brief Resolves queue and command-list block sets from representative child objects.
		 */
		void ProbeChildren(void * deviceImpl) noexcept
		{
			if (m_device.core == nullptr)
			{
				return;
			}

			Error ignored{};

			if (void * queueImpl = m_device.core->getQueue(deviceImpl, QueueType::eGraphics, 0, &ignored); queueImpl != nullptr)
			{
				static_cast<void>(Queue(queueImpl));
			}

			void * poolImpl = m_device.core->createCommandPool(deviceImpl, CommandPoolDesc{}, &ignored);
			if (poolImpl == nullptr)
			{
				return;
			}

			const CommandPoolApi * pool = detail::QueryBlock<CommandPoolApi>(poolImpl);
			if (pool == nullptr || pool->allocate == nullptr)
			{
				return;
			}

			if (void * listImpl = pool->allocate(poolImpl, nullptr, &ignored); listImpl != nullptr)
			{
				static_cast<void>(CommandList(listImpl));
			}
		}

		/**
		 * \brief Lowers every optional feature the caller did not declare so DeviceCaps reports what was granted.
		 *
		 * A device does not grant an optional feature nobody asked for, even where the adapter has it. That is why a program that runs on one adapter runs on any
		 * adapter meeting its declaration.
		 *
		 * Done here and not in each backend so a backend added later cannot forget it. Backends still decide what to actually enable.
		 */
		void GrantDeclaredFeatures(const DeviceDesc & desc) noexcept
		{
			const auto declared = [&desc](const DeviceFeature feature) noexcept
			{
				return std::ranges::contains(desc.requiredFeatures, feature) || std::ranges::contains(desc.preferredFeatures, feature);
			};

			m_caps.supportsTimestampQueries			 = m_caps.supportsTimestampQueries && declared(DeviceFeature::eTimestampQueries);
			m_caps.supportsAnisotropy				 = m_caps.supportsAnisotropy && declared(DeviceFeature::eSamplerAnisotropy);
			m_caps.supportsIndependentBlend			 = m_caps.supportsIndependentBlend && declared(DeviceFeature::eIndependentBlend);
			m_caps.supportsDepthBounds				 = m_caps.supportsDepthBounds && declared(DeviceFeature::eDepthBounds);
			m_caps.supportsPipelineStatisticsQueries = m_caps.supportsPipelineStatisticsQueries && declared(DeviceFeature::ePipelineStatisticsQueries);
			m_caps.supportsMultiDrawIndirect		 = m_caps.supportsMultiDrawIndirect && declared(DeviceFeature::eMultiDrawIndirect);
			m_caps.supportsDrawIndirectFirstInstance = m_caps.supportsDrawIndirectFirstInstance && declared(DeviceFeature::eDrawIndirectFirstInstance);
			m_caps.supportsShaderDrawParameters		 = m_caps.supportsShaderDrawParameters && declared(DeviceFeature::eShaderDrawParameters);
			m_caps.supportsTextureViewSwizzle		 = m_caps.supportsTextureViewSwizzle && declared(DeviceFeature::eTextureViewSwizzle);
			m_caps.supportsMultiPlanarFormats		 = m_caps.supportsMultiPlanarFormats && declared(DeviceFeature::eMultiPlanarFormats);
			m_caps.supportsSamplerYcbcrConversion	 = m_caps.supportsSamplerYcbcrConversion && declared(DeviceFeature::eSamplerYcbcrConversion);

			/*
			 * Sparse is a ladder and not a boolean, so it is lowered to the highest rung the caller named instead of cleared. Each token names the rung it unlocks, and
			 * a caller wanting volume residency has to say so even having asked for texture residency.
			 */
			SparseTier granted = SparseTier::eNone;
			if (declared(DeviceFeature::eSparseResources) || declared(DeviceFeature::eSparseBuffers))
			{
				granted = SparseTier::eBuffers;
			}
			if (declared(DeviceFeature::eSparseTextures))
			{
				granted = SparseTier::eResidentTextures;
			}
			if (declared(DeviceFeature::eSparseVolumes))
			{
				granted = SparseTier::eResidentVolumes;
			}

			m_caps.sparseTier = std::min(m_caps.sparseTier, granted);

			if (m_caps.sparseTier == SparseTier::eNone)
			{
				m_caps.sparseTileSizeBytes = 0;
			}

			// Where a timestamp may be written says nothing once no timestamp may be written at all, so it follows the feature it qualifies down.
			m_caps.supportsTimestampWritesInScope = m_caps.supportsTimestampWritesInScope && m_caps.supportsTimestampQueries;
		}

		/**
		 * \brief Combines backend-reported capabilities with support implied by discovered ABI blocks.
		 */
		void DeriveCaps(void * deviceImpl, const DeviceDesc & desc) noexcept
		{
			if (m_device.core == nullptr)
			{
				return;
			}

			m_caps = m_device.core->getCaps(deviceImpl);

			m_caps.supportsSurfaces			 = m_device.present != nullptr;
			m_caps.supportsPlacedResources	 = m_device.placedMemory != nullptr;
			m_caps.supportsResourceAdoption	 = m_device.adoption != nullptr;
			m_caps.supportsPipelineCache	 = m_device.pipelineCache != nullptr;
			m_caps.supportsMemoryBudget		 = m_device.residency != nullptr;
			m_caps.supportsMultiDrawIndirect = m_commandList.indirect != nullptr && m_caps.supportsMultiDrawIndirect;
			m_caps.supportsIndirectCount	 = m_commandList.indirectCount != nullptr;

			// The queue block is the only thing that binds sparse memory, so a backend declining it has no sparse whatever its device features say. Clamping without
			// assigning keeps the tier the backend reported when the block is there.
			if (m_queue.sparse == nullptr)
			{
				m_caps.sparseTier		   = SparseTier::eNone;
				m_caps.sparseTileSizeBytes = 0;
			}

			// Both the device and command-list halves are required.
			m_caps.supportsRayTracing		= m_device.rayTracing != nullptr && m_commandList.rayTracing != nullptr;
			m_caps.supportsTimestampQueries = m_device.query != nullptr && m_commandList.query != nullptr;

			// Last, so it lowers the answer everything above it settled on without being undone by one of them.
			GrantDeclaredFeatures(desc);
		}

		DeviceBlocks m_device{};
		QueueBlocks m_queue{};
		CommandListBlocks m_commandList{};
		DeviceCaps m_caps{};

		DeviceMemoryAllocator * m_allocator = nullptr;
		Profiler * m_profiler				= nullptr;
		detail::AllocationTracker m_tracker;

		ThreadingMode m_threading = ThreadingMode::eThreads;
		SyncOps m_sync{};

		static constexpr std::size_t kObjectLock = kResourceTypeCount;
		static constexpr std::size_t kLockCount	 = kObjectLock + 1;

		std::array<DeviceLock, kLockCount> m_locks;

		// Only first child-block discovery takes this lock.
		SpinLock m_mutex;
	};

	namespace detail
	{

		/**
		 * \brief Constructs public facades from backend objects and resolved block sets.
		 */
		struct FacadeBuilder final
		{
			[[nodiscard]] static Instance MakeInstance(void * impl, const InstanceApi * dispatch)
			{
				return Instance{ impl, dispatch };
			}

			[[nodiscard]] static UniqueInstance MakeUniqueInstance(void * impl, const InstanceApi * dispatch)
			{
				return UniqueInstance{ impl, dispatch };
			}

			[[nodiscard]] static Device MakeDevice(void * impl, BackendBlockSet * blocks)
			{
				return Device{ impl, blocks };
			}

			[[nodiscard]] static UniqueDevice MakeUniqueDevice(void * impl, BackendBlockSet * blocks)
			{
				return UniqueDevice{ impl, blocks };
			}

			[[nodiscard]] static Queue MakeQueue(void * impl, const QueueBlocks * blocks)
			{
				return Queue{ impl, blocks };
			}

			[[nodiscard]] static CommandPool MakeCommandPool(void * impl, const CommandPoolApi * dispatch, BackendBlockSet * blocks)
			{
				return CommandPool{ impl, dispatch, blocks };
			}

			[[nodiscard]] static CommandList MakeCommandList(void * impl, const CommandListBlocks * blocks)
			{
				return CommandList{ impl, blocks };
			}

			[[nodiscard]] static Swapchain MakeSwapchain(void * impl, const SwapchainApi * dispatch)
			{
				return Swapchain{ impl, dispatch };
			}

			[[nodiscard]] static DescriptorArena MakeDescriptorArena(void * impl, const DescriptorArenaApi * dispatch, BackendBlockSet * blocks)
			{
				return DescriptorArena{ impl, dispatch, blocks };
			}

			[[nodiscard]] static void * ImplOf(const Queue & queue) noexcept
			{
				return queue.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const Instance & instance) noexcept
			{
				return instance.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const Device & device) noexcept
			{
				return device.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const Swapchain & swapchain) noexcept
			{
				return swapchain.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const CommandList & commandList) noexcept
			{
				return commandList.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const CommandPool & commandPool) noexcept
			{
				return commandPool.m_impl;
			}

			[[nodiscard]] static void * ImplOf(const DescriptorArena & descriptorArena) noexcept
			{
				return descriptorArena.m_impl;
			}

			[[nodiscard]] static BackendBlockSet * BlocksOf(const Device & device) noexcept
			{
				return device.m_blocks;
			}
		};

		/**
		 * \brief Reads backend registry internals for backend selection.
		 */
		struct RegistryAccess final
		{
			[[nodiscard]] static const BackendCreateInfo * Find(const GraphicsApiRegistry & registry, GraphicsApiId id) noexcept
			{
				for (const BackendCreateInfo & entry : registry.m_entries)
				{
					if (entry.info.id == id)
					{
						return &entry;
					}
				}

				return nullptr;
			}
		};

		/**
		 * \brief Returns a facade impl already known to be unwrapped.
		 *
		 * \attention Use only after validation has rebuilt the facade from the backend object behind its wrapper.
		 */
		template <class Facade>
		[[nodiscard]] inline void * UnwrappedImplOf(const Facade & facade) noexcept
		{
			return FacadeBuilder::ImplOf(facade);
		}

	} // namespace detail

} // namespace azo::rhi

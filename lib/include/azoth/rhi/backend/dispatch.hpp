// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

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
#include <cstdint>
#include <mutex>
#include <span>

namespace azo::rhi
{

	inline constexpr bool kHandleAlreadyChecked = false;

	struct QueuePlan final
	{
		std::uint32_t graphicsCount = 0;
		std::uint32_t computeCount	= 0;
		std::uint32_t copyCount		= 0;
		bool computeDedicated		= false;
		bool copyDedicated			= false;
	};

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

	template <typename Block, const Block & (*Table)() noexcept>
	struct Published final
	{
		[[nodiscard]] static const void * Match(const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			if (id != InterfaceTraits<Block>::kId || minVersion > InterfaceTraits<Block>::kVersion)
			{
				return nullptr;
			}

			return &Table();
		}
	};

	template <class... Blocks>
	[[nodiscard]] const void * QueryPublished(void *, const InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		const void * found = nullptr;
		((found = found != nullptr ? found : Blocks::Match(id, minVersion)), ...);
		return found;
	}

	template <class... Blocks>
	[[nodiscard]] const BackendObject * PublishingObject() noexcept
	{
		static constexpr BackendObject object{ .queryInterface = &QueryPublished<Blocks...> };
		return &object;
	}

	namespace detail
	{

		[[nodiscard]] AZO_RHI_API int & GuardsHeld() noexcept;

		[[nodiscard]] AZO_RHI_API std::atomic<std::uint64_t> & ReentrancyViolationCount() noexcept;

		[[nodiscard]] inline std::uint64_t ReentrancyViolations() noexcept
		{
			return ReentrancyViolationCount().load(std::memory_order_relaxed);
		}

		inline void CheckNoGuardHeld() noexcept
		{
			if (GuardsHeld() == 0)
			{
				return;
			}

			ReentrancyViolationCount().fetch_add(1, std::memory_order_relaxed);
			AZO_RHI_DEBUG_BREAK();
		}

		[[nodiscard]] inline SpinLock & LifetimeGuard() noexcept
		{
			static SpinLock guard;
			return guard;
		}

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

	}

	class DeviceLock final
	{
	public:
		DeviceLock() = default;

		DeviceLock(const DeviceLock &)			   = delete;
		DeviceLock & operator=(const DeviceLock &) = delete;
		DeviceLock(DeviceLock &&)				   = delete;
		DeviceLock & operator=(DeviceLock &&)	   = delete;
		~DeviceLock()							   = default;

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

	struct DeviceBlocks final
	{
		const CoreDeviceApi * core					   = nullptr;
		const PresentApi * present					   = nullptr;
		const PlacedMemoryApi * placedMemory		   = nullptr;
		const RayTracingApi * rayTracing			   = nullptr;
		const QueryApi * query						   = nullptr;
		const PipelineCacheApi * pipelineCache		   = nullptr;
		const ResidencyApi * residency				   = nullptr;
		const ResourceIntrospectionApi * introspection = nullptr;
		const AdoptionApi * adoption				   = nullptr;
		const ExternalSharingApi * externalSharing	   = nullptr;
	};

	struct QueueBlocks final
	{
		const QueueApi * core	 = nullptr;
		const SparseApi * sparse = nullptr;
	};

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

		[[nodiscard]] inline const BackendObject * ObjectOf(void * impl) noexcept
		{
			return static_cast<const BackendObject *>(*static_cast<const void * const *>(impl));
		}

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

		template <typename Block>
		[[nodiscard]] inline void * NativeImplOf(void * impl, const Block & expected) noexcept
		{
			void * native = NativeImplOf(impl);
			return native != nullptr && QueryBlock<Block>(native) == &expected ? native : nullptr;
		}

	}

	class BackendBlockSet final
	{
	public:
		BackendBlockSet(void * deviceImpl, const DeviceDesc & desc) noexcept
			: m_threading(desc.threading),
			  m_sync(desc.sync),
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

		[[nodiscard]] bool AllocatesPlaced() const noexcept
		{
			return m_allocator != nullptr && m_device.placedMemory != nullptr;
		}

		[[nodiscard]] detail::AllocationTracker & Tracker() noexcept
		{
			return m_tracker;
		}

		[[nodiscard]] DeviceLock & Guard(const ResourceType type) noexcept
		{
			return m_locks[static_cast<std::size_t>(type)];
		}

		[[nodiscard]] DeviceLock & ObjectGuard() noexcept
		{
			return m_locks[kObjectLock];
		}

		[[nodiscard]] bool Unguarded() const noexcept
		{
			return m_threading == ThreadingMode::eSingleThreaded;
		}

	private:
		void BindLocks() noexcept
		{
			for (DeviceLock & lock : m_locks)
			{
				static_cast<void>(lock.Bind(m_threading, &m_sync, &m_profiler));
			}
		}

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

			m_caps.supportsTimestampWritesInScope = m_caps.supportsTimestampWritesInScope && m_caps.supportsTimestampQueries;
		}

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

			if (m_queue.sparse == nullptr)
			{
				m_caps.sparseTier		   = SparseTier::eNone;
				m_caps.sparseTileSizeBytes = 0;
			}

			m_caps.supportsRayTracing		= m_device.rayTracing != nullptr && m_commandList.rayTracing != nullptr;
			m_caps.supportsTimestampQueries = m_device.query != nullptr && m_commandList.query != nullptr;

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

		SpinLock m_mutex;
	};

	namespace detail
	{

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

		template <class Facade>
		[[nodiscard]] inline void * UnwrappedImplOf(const Facade & facade) noexcept
		{
			return FacadeBuilder::ImplOf(facade);
		}

	}

}

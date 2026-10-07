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
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/debug_break.hpp"
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

	inline constexpr bool kHandleAlreadyChecked = false;

	struct QueuePlan final
	{
		std::uint32_t graphicsCount = 0;
		std::uint32_t computeCount	= 0;
		std::uint32_t copyCount		= 0;
		bool computeDedicated		= false;
		bool copyDedicated			= false;
	};

	[[nodiscard]] inline QueuePlan plan_queues(std::span<const QueueRequest> queues) noexcept
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

	[[nodiscard]] inline std::uint32_t queue_count_for_type(const DeviceCaps & caps, QueueType type) noexcept
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
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.nativeObject");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<InstanceApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.instance");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<CoreDeviceApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.core");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PresentApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.present");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PlacedMemoryApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.placedMemory");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RayTracingApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.rayTracing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueryApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.query");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<PipelineCacheApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.pipelineCache");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ResidencyApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.residency");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ResourceIntrospectionApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.resourceIntrospection");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<AdoptionApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.adoption");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ExternalSharingApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.externalSharing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<ExternalCapabilityApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.externalCapability");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueueApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.queue");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<SparseApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.sparse");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<CommandPoolApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.commandPool");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<DescriptorArenaApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.descriptorArena");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RenderCommandApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.render");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<AliasingCommandApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.aliasing");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<RayTracingCommandApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.rayTracingCommand");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<QueryCommandApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.queryCommand");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<IndirectApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.indirect");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<IndirectCountApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.indirectCount");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<NativeEscapeApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.nativeEscape");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <>
	struct InterfaceTraits<SwapchainApi> final
	{
		static constexpr InterfaceId kId		= make_interface_id("azoth.rhi.block.swapchain");
		static constexpr std::uint32_t kVersion = 1;
	};

	template <typename Block, const Block & (*Table)() noexcept>
	struct Published final
	{
		[[nodiscard]] static const void * match(const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			if (id != InterfaceTraits<Block>::kId || minVersion > InterfaceTraits<Block>::kVersion)
			{
				return nullptr;
			}

			return &Table();
		}
	};

	template <class... Blocks>
	[[nodiscard]] const void * query_published(void * /*unused*/, const InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		const void * found = nullptr;
		((found = found != nullptr ? found : Blocks::match(id, minVersion)), ...);
		return found;
	}

	template <class... Blocks>
	[[nodiscard]] const BackendObject * publishing_object() noexcept
	{
		static constexpr BackendObject kObject{ .queryInterface = &query_published<Blocks...> };
		return &kObject;
	}

	namespace detail
	{

		[[nodiscard]] AZO_RHI_API int & guards_held() noexcept;

		[[nodiscard]] AZO_RHI_API std::atomic<std::uint64_t> & reentrancy_violation_count() noexcept;

		[[nodiscard]] inline std::uint64_t reentrancy_violations() noexcept
		{
			return reentrancy_violation_count().load(std::memory_order_relaxed);
		}

		inline void check_no_guard_held() noexcept
		{
			if (guards_held() == 0)
			{
				return;
			}

			reentrancy_violation_count().fetch_add(1, std::memory_order_relaxed);
			AZO_RHI_DEBUG_BREAK();
		}

		[[nodiscard]] inline SpinLock & lifetime_guard() noexcept
		{
			static SpinLock s_Guard;
			return s_Guard;
		}

		class LifetimeLock final
		{
		public:
			LifetimeLock() noexcept
			{
				++guards_held();
				lifetime_guard().lock();
			}

			~LifetimeLock() noexcept
			{
				lifetime_guard().unlock();
				--guards_held();
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

		[[nodiscard]] bool bind(const ThreadingMode mode, const SyncOps * sync, Profiler * const * deviceProfiler) noexcept
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

		void release() noexcept
		{
			if (m_host != nullptr)
			{
				m_sync->destroy(m_sync->context, m_host);
				m_host = nullptr;
			}
		}

		void lock() noexcept
		{
			// A cooperative holder is a fiber that may release on another thread, which a thread-local count cannot follow.
			if (m_mode != ThreadingMode::eCooperative)
			{
				++detail::guards_held();
			}

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
			if (m_mode != ThreadingMode::eCooperative)
			{
				--detail::guards_held();
			}

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

		[[nodiscard]] inline const BackendObject * object_of(void * impl) noexcept
		{
			return static_cast<const BackendObject *>(*static_cast<const void * const *>(impl));
		}

		template <typename Block>
		[[nodiscard]] const Block * query_block(void * impl) noexcept
		{
			const BackendObject * object = object_of(impl);
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

		[[nodiscard]] inline void * native_impl_of(void * impl) noexcept
		{
			constexpr int kMaxLayers = 8;

			for (int layer = 0; layer < kMaxLayers && impl != nullptr; ++layer)
			{
				const auto * block = query_block<NativeObjectApi>(impl);
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
		[[nodiscard]] void * native_impl_of(void * impl, const Block & expected) noexcept
		{
			void * native = native_impl_of(impl);
			return native != nullptr && query_block<Block>(native) == &expected ? native : nullptr;
		}

	} // namespace detail

	class BackendBlockSet final
	{
	public:
		BackendBlockSet(void * deviceImpl, const DeviceDesc & desc) noexcept
			: m_threading(desc.threading),
			  m_sync(desc.sync),
			  m_allocator(desc.allocator != nullptr ? desc.allocator : get_device_memory_allocator()),
			  m_profiler(desc.profiler)
		{
			m_device.core			 = detail::query_block<CoreDeviceApi>(deviceImpl);
			m_device.present		 = detail::query_block<PresentApi>(deviceImpl);
			m_device.placedMemory	 = detail::query_block<PlacedMemoryApi>(deviceImpl);
			m_device.rayTracing		 = detail::query_block<RayTracingApi>(deviceImpl);
			m_device.query			 = detail::query_block<QueryApi>(deviceImpl);
			m_device.pipelineCache	 = detail::query_block<PipelineCacheApi>(deviceImpl);
			m_device.residency		 = detail::query_block<ResidencyApi>(deviceImpl);
			m_device.introspection	 = detail::query_block<ResourceIntrospectionApi>(deviceImpl);
			m_device.adoption		 = detail::query_block<AdoptionApi>(deviceImpl);
			m_device.externalSharing = detail::query_block<ExternalSharingApi>(deviceImpl);

			BindLocks();
			ProbeChildren(deviceImpl);
			DeriveCaps(deviceImpl, desc);
		}

		~BackendBlockSet()
		{
			for (DeviceLock & lock : m_locks)
			{
				lock.release();
			}
		}

		BackendBlockSet(const BackendBlockSet &)			 = delete;
		BackendBlockSet & operator=(const BackendBlockSet &) = delete;
		BackendBlockSet(BackendBlockSet &&)					 = delete;
		BackendBlockSet & operator=(BackendBlockSet &&)		 = delete;

		[[nodiscard]] const DeviceBlocks & device() const noexcept
		{
			return m_device;
		}

		[[nodiscard]] const QueueBlocks * queue(void * queueImpl) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_queue.core == nullptr)
			{
				m_queue.core   = detail::query_block<QueueApi>(queueImpl);
				m_queue.sparse = detail::query_block<SparseApi>(queueImpl);
			}

			return m_queue.core != nullptr ? &m_queue : nullptr;
		}

		[[nodiscard]] const CommandListBlocks * command_list(void * listImpl) noexcept
		{
			const std::scoped_lock lock(m_mutex);
			if (m_commandList.render == nullptr)
			{
				m_commandList.render		= detail::query_block<RenderCommandApi>(listImpl);
				m_commandList.aliasing		= detail::query_block<AliasingCommandApi>(listImpl);
				m_commandList.rayTracing	= detail::query_block<RayTracingCommandApi>(listImpl);
				m_commandList.query			= detail::query_block<QueryCommandApi>(listImpl);
				m_commandList.indirect		= detail::query_block<IndirectApi>(listImpl);
				m_commandList.indirectCount = detail::query_block<IndirectCountApi>(listImpl);
				m_commandList.nativeEscape	= detail::query_block<NativeEscapeApi>(listImpl);
			}

			return m_commandList.render != nullptr ? &m_commandList : nullptr;
		}

		[[nodiscard]] const DeviceCaps & caps() const noexcept
		{
			return m_caps;
		}

		[[nodiscard]] ThreadingMode threading() const noexcept
		{
			return m_threading;
		}

		[[nodiscard]] const SyncOps & sync() const noexcept
		{
			return m_sync;
		}

		[[nodiscard]] DeviceMemoryAllocator * allocator() const noexcept
		{
			return m_allocator;
		}

		[[nodiscard]] Profiler * profiling() const noexcept
		{
			return m_profiler;
		}

		[[nodiscard]] bool allocates_placed() const noexcept
		{
			return m_allocator != nullptr && m_device.placedMemory != nullptr;
		}

		[[nodiscard]] detail::AllocationTracker & tracker() noexcept
		{
			return m_tracker;
		}

		[[nodiscard]] DeviceLock & guard(const ResourceType type) noexcept
		{
			return azo::rhi::detail::at(m_locks, static_cast<std::size_t>(type));
		}

		[[nodiscard]] DeviceLock & object_guard() noexcept
		{
			return azo::rhi::detail::at(m_locks, kObjectLock);
		}

		[[nodiscard]] bool unguarded() const noexcept
		{
			return m_threading == ThreadingMode::eSingleThreaded;
		}

	private:
		void BindLocks() noexcept
		{
			for (DeviceLock & lock : m_locks)
			{
				static_cast<void>(lock.bind(m_threading, &m_sync, &m_profiler));
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
				static_cast<void>(queue(queueImpl));
			}

			void * poolImpl = m_device.core->createCommandPool(deviceImpl, CommandPoolDesc{}, &ignored);
			if (poolImpl == nullptr)
			{
				return;
			}

			const auto * pool = detail::query_block<CommandPoolApi>(poolImpl);
			if (pool == nullptr || pool->allocate == nullptr)
			{
				return;
			}

			if (void * listImpl = pool->allocate(poolImpl, nullptr, &ignored); listImpl != nullptr)
			{
				static_cast<void>(command_list(listImpl));
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

			auto granted = SparseTier::eNone;
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

			// All three follow the queries they describe: an ordering, a clock correlation and a mid scope write say nothing once the queries are masked off.
			m_caps.supportsTimestampWritesInScope = m_caps.supportsTimestampWritesInScope && m_caps.supportsTimestampQueries;
			m_caps.supportsTimestampCalibration	  = m_caps.supportsTimestampCalibration && m_caps.supportsTimestampQueries;
			m_caps.supportsOrderedTimestamps	  = m_caps.supportsOrderedTimestamps && m_caps.supportsTimestampQueries;
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
			[[nodiscard]] static Instance make_instance(void * impl, const InstanceApi * dispatch)
			{
				return Instance{ impl, dispatch };
			}

			[[nodiscard]] static UniqueInstance make_unique_instance(void * impl, const InstanceApi * dispatch)
			{
				return UniqueInstance{ impl, dispatch };
			}

			[[nodiscard]] static Device make_device(void * impl, BackendBlockSet * blocks)
			{
				return Device{ impl, blocks };
			}

			[[nodiscard]] static UniqueDevice make_unique_device(void * impl, BackendBlockSet * blocks)
			{
				return UniqueDevice{ impl, blocks };
			}

			[[nodiscard]] static Queue make_queue(void * impl, const QueueBlocks * blocks)
			{
				return Queue{ impl, blocks };
			}

			[[nodiscard]] static CommandPool make_command_pool(void * impl, const CommandPoolApi * dispatch, BackendBlockSet * blocks)
			{
				return CommandPool{ impl, dispatch, blocks };
			}

			[[nodiscard]] static CommandList make_command_list(void * impl, const CommandListBlocks * blocks)
			{
				return CommandList{ impl, blocks };
			}

			[[nodiscard]] static Swapchain make_swapchain(void * impl, const SwapchainApi * dispatch)
			{
				return Swapchain{ impl, dispatch };
			}

			[[nodiscard]] static DescriptorArena make_descriptor_arena(void * impl, const DescriptorArenaApi * dispatch, BackendBlockSet * blocks)
			{
				return DescriptorArena{ impl, dispatch, blocks };
			}

			[[nodiscard]] static void * impl_of(const Queue & queue) noexcept
			{
				return queue.m_impl;
			}

			[[nodiscard]] static void * impl_of(const Instance & instance) noexcept
			{
				return instance.m_impl;
			}

			[[nodiscard]] static void * impl_of(const Device & device) noexcept
			{
				return device.m_impl;
			}

			[[nodiscard]] static void * impl_of(const Swapchain & swapchain) noexcept
			{
				return swapchain.m_impl;
			}

			[[nodiscard]] static void * impl_of(const CommandList & commandList) noexcept
			{
				return commandList.m_impl;
			}

			[[nodiscard]] static void * impl_of(const CommandPool & commandPool) noexcept
			{
				return commandPool.m_impl;
			}

			[[nodiscard]] static void * impl_of(const DescriptorArena & descriptorArena) noexcept
			{
				return descriptorArena.m_impl;
			}

			[[nodiscard]] static BackendBlockSet * blocks_of(const Device & device) noexcept
			{
				return device.m_blocks;
			}
		};

		struct RegistryAccess final
		{
			[[nodiscard]] static const BackendCreateInfo * find(const GraphicsApiRegistry & registry, GraphicsApiId id) noexcept
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
		[[nodiscard]] void * unwrapped_impl_of(const Facade & facade) noexcept
		{
			return FacadeBuilder::impl_of(facade);
		}

	} // namespace detail

} // namespace azo::rhi

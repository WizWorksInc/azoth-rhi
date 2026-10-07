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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/validation/registry.hpp"

#include "../../include/azoth/rhi/backend/details/resource_tables.hpp"

#include <atomic>
#include <cstdint> // NOLINT
#include <limits>
#include <span>
#include <thread>
#include <type_traits>

namespace azo::rhi::validation
{

	class DeviceValidator;

	struct WrappedDevice;

	struct WrappedObject
	{
		const BackendObject * object = nullptr;
		void * inner				 = nullptr;

		WrappedDevice * device		= nullptr;
		DeviceValidator * validator = nullptr;

		WrappedObject * nextChild					   = nullptr;
		void (*releaseChild)(WrappedObject *) noexcept = nullptr;
	};

	class DeviceValidator final
	{
	public:
		explicit DeviceValidator(const ValidationMode mode) noexcept : m_mode(mode) {}

		DeviceValidator(const DeviceValidator &)			 = delete;
		DeviceValidator & operator=(const DeviceValidator &) = delete;
		DeviceValidator(DeviceValidator &&)					 = delete;
		DeviceValidator & operator=(DeviceValidator &&)		 = delete;
		~DeviceValidator()									 = default;

		[[nodiscard]] HandleRegistry & Handles() noexcept
		{
			return m_handles;
		}

		[[nodiscard]] ValidationMode mode() const noexcept
		{
			return m_mode;
		}

		void set_mode(const ValidationMode mode) noexcept
		{
			m_mode = mode;
		}

		[[nodiscard]] bool checks_state() const noexcept
		{
			return m_mode == ValidationMode::eDeveloper || m_mode == ValidationMode::eCapture;
		}

		bool fail(Error * error, const char * message) noexcept
		{
			m_failures.fetch_add(1, std::memory_order_relaxed);
			if (error != nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eValidationFailed,
					.message = message,
				};
			}

			return false;
		}

		template <class T>
		[[nodiscard]] T fail_value(Error * error, const char * message) noexcept
		{
			static_cast<void>(fail(error, message));
			return T{};
		}

		[[nodiscard]] std::uint64_t failures() const noexcept
		{
			return m_failures.load(std::memory_order_relaxed);
		}

	private:
		HandleRegistry m_handles;
		std::atomic<std::uint64_t> m_failures{ 0 };
		ValidationMode m_mode = ValidationMode::eOff;
	};

	struct WrappedDevice final : WrappedObject
	{
		DeviceBlocks blocks{};

		DeviceValidator ownedValidator{ ValidationMode::eOff };

		std::atomic<WrappedObject *> children{ nullptr };

		std::atomic<std::uint64_t> nextArenaId{ 1 };
	};

	struct WrappedQueue final : WrappedObject
	{
		QueueBlocks blocks{};

		QueueType type = QueueType::eGraphics;
	};

	struct WrappedCommandList;

	struct WrappedCommandPool final : WrappedObject
	{
		const CommandPoolApi * blocks = nullptr;

		QueueType queueType = QueueType::eGraphics;

		detail::HostMap<void *, WrappedCommandList *> lists;
	};

	struct TrackedSubrange final
	{
		RegisteredHandle resource{};
		std::uint32_t aspects	 = 0;
		std::uint32_t mipBegin	 = 0;
		std::uint32_t mipEnd	 = 0;
		std::uint32_t layerBegin = 0;
		std::uint32_t layerEnd	 = 0;
		std::uint64_t byteBegin	 = 0;
		std::uint64_t byteEnd	 = std::numeric_limits<std::uint64_t>::max();
		std::uint32_t state		 = 0;
	};

	struct PendingOwnership final
	{
		RegisteredHandle resource{};

		OwnershipOp firstOp		   = OwnershipOp::eNone;
		QueueType firstCounterpart = QueueType::eGraphics;
		QueueType recordedOn	   = QueueType::eGraphics;

		std::uint8_t owner = 0;
		bool owned		   = false;
	};

	struct PendingArrival final
	{
		RegisteredHandle resource{};
		std::uint32_t use = 0;
		bool known		  = false;
	};

	struct WrappedCommandList final : WrappedObject
	{
		bool checksThread = false;

		std::thread::id recordingThread;

		CommandListBlocks blocks{};

		bool recording		 = false;
		bool rendering		 = false;
		bool graphicsBound	 = false;
		bool computeBound	 = false;
		bool rayTracingBound = false;

		QueueType queueType = QueueType::eGraphics;

		detail::HostVector<TrackedSubrange> recordedStates;
		detail::HostVector<TrackedSubrange> requiredStates;

		detail::HostVector<PendingOwnership> pendingOwnership;

		detail::HostVector<PendingArrival> pendingArrivals;
	};

	struct WrappedDescriptorArena final : WrappedObject
	{
		const DescriptorArenaApi * blocks = nullptr;

		std::uint64_t id = 0;
	};

	struct WrappedSwapchain final : WrappedObject
	{
		const SwapchainApi * blocks = nullptr;
	};

	template <class Block>
	[[nodiscard]] const Block * inner_block(WrappedObject * self) noexcept;

	template <auto Member>
	struct Forward;

	template <class T>
	inline constexpr bool kIsHandle = false;

	template <class Tag>
	inline constexpr bool kIsHandle<Handle<Tag>> = true;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Forward<Member>
	{
		static_assert(
			!kIsHandle<R>,
			"an entry returning a Handle must use Vending, which records it with the validation registry. "
			"Forward passes the handle straight through, so nothing has heard of it and the first use is "
			"refused as stale."
		);

		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			return (inner_block<Block>(self)->*Member)(self->inner, args...);
		}
	};

	[[nodiscard]] inline Error * PickError(Error * found, Error * candidate) noexcept
	{
		return candidate != nullptr ? candidate : found;
	}

	template <class T>
	[[nodiscard]] Error * PickError(Error * found, const T & /*unused*/) noexcept
	{
		return found;
	}

	template <class T>
	[[nodiscard]] bool argument_is_usable(DeviceValidator & /*unused*/, const T & /*unused*/) noexcept
	{
		return true;
	}

	template <class Tag>
	requires requires { detail::ResourceTypeOf<Handle<Tag>>::kValue; }
	[[nodiscard]] bool argument_is_usable(DeviceValidator & validator, const Handle<Tag> handle) noexcept
	{
		if (!handle.is_valid())
		{
			return true;
		}

		return validator.Handles().lookup(
				   RegisteredHandle{
					   .type	   = detail::ResourceTypeOf<Handle<Tag>>::kValue,
					   .index	   = handle.index,
					   .generation = handle.generation,
				   }
			   ) != nullptr;
	}

	template <class... Handles>
	[[nodiscard]] bool all_usable(DeviceValidator & validator, const Handles &... handles) noexcept
	{
		bool usable = true;
		((usable = usable && argument_is_usable(validator, handles)), ...);
		return usable;
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const PlacedBufferDesc & desc) noexcept
	{
		return all_usable(validator, desc.heap);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const PlacedTextureDesc & desc) noexcept
	{
		return all_usable(validator, desc.heap);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const PipelineLayoutDesc & desc) noexcept
	{
		for (const DescriptorSetLayoutHandle layout : desc.sets)
		{
			if (!all_usable(validator, layout))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const GraphicsPipelineDesc & desc) noexcept
	{
		return all_usable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const ComputePipelineDesc & desc) noexcept
	{
		return all_usable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const RayTracingPipelineDesc & desc) noexcept
	{
		return all_usable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const AccelerationStructureDesc & desc) noexcept
	{
		return all_usable(validator, desc.storage);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorSetAllocDesc & desc) noexcept
	{
		return all_usable(validator, desc.layout);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const RetirePoint & point) noexcept
	{
		return all_usable(validator, point.timeline);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const TimelinePoint & point) noexcept
	{
		return all_usable(validator, point.timeline);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DestroyDesc & desc) noexcept
	{
		return all_usable(validator, desc.safeAfter);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const RenderingAttachment & attachment) noexcept
	{
		return all_usable(validator, attachment.view);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const BufferBarrier & barrier) noexcept
	{
		return all_usable(validator, barrier.buffer);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const TextureBarrier & barrier) noexcept
	{
		return all_usable(validator, barrier.texture);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const AliasBarrier & barrier) noexcept
	{
		return all_usable(validator, barrier.beforeBuffer, barrier.beforeTexture, barrier.afterBuffer, barrier.afterTexture);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const NativeTouchedBuffer & touched) noexcept
	{
		return all_usable(validator, touched.buffer);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const NativeTouchedTexture & touched) noexcept
	{
		return all_usable(validator, touched.texture);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const ResidencyPriorityDesc & desc) noexcept
	{
		return all_usable(validator, desc.buffer, desc.texture);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const SparseBufferBind & bind) noexcept
	{
		return all_usable(validator, bind.buffer, bind.page.heap);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const SparseTextureBind & bind) noexcept
	{
		return all_usable(validator, bind.texture, bind.page.heap);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const ShaderBindingTableDesc & sbt) noexcept
	{
		return all_usable(validator, sbt.rayGeneration.buffer, sbt.miss.buffer, sbt.hit.buffer, sbt.callable.buffer);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const AccelerationStructureBuildDesc & build) noexcept
	{
		if (!all_usable(validator, build.dst, build.src, build.instanceBuffer, build.scratchBuffer))
		{
			return false;
		}

		for (const AccelerationStructureGeometryDesc & geometry : build.geometries)
		{
			if (!all_usable(validator, geometry.vertexBuffer, geometry.indexBuffer))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorWriteBuffer & write) noexcept
	{
		return all_usable(validator, write.set, write.buffer);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorWriteTexture & write) noexcept
	{
		return all_usable(validator, write.set, write.view, write.sampler);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorWriteSampler & write) noexcept
	{
		return all_usable(validator, write.set, write.sampler);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorWriteAccelerationStructure & write) noexcept
	{
		return all_usable(validator, write.set, write.accelerationStructure);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const DescriptorSetLayoutDesc & desc) noexcept
	{
		for (const DescriptorBinding & binding : desc.bindings)
		{
			for (const SamplerHandle sampler : binding.immutableSamplers)
			{
				if (!all_usable(validator, sampler))
				{
					return false;
				}
			}
		}

		return true;
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const SubmitDesc & desc) noexcept
	{
		for (const TimelinePoint & point : desc.waits)
		{
			if (!argument_is_usable(validator, point))
			{
				return false;
			}
		}

		for (const TimelinePoint & point : desc.signals)
		{
			if (!argument_is_usable(validator, point))
			{
				return false;
			}
		}

		return true;
	}

	template <class T>
	[[nodiscard]] bool argument_is_usable(DeviceValidator & validator, const std::span<const T> items) noexcept
	{
		for (const T & item : items)
		{
			if (!argument_is_usable(validator, item))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const BeginRenderingDesc & desc) noexcept
	{
		if (!argument_is_usable(validator, desc.colors))
		{
			return false;
		}

		if (desc.timestamps != nullptr && !all_usable(validator, desc.timestamps->pool))
		{
			return false;
		}

		return desc.depthStencil == nullptr || argument_is_usable(validator, *desc.depthStencil);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const BarrierBatch & batch) noexcept
	{
		return argument_is_usable(validator, batch.buffers) && argument_is_usable(validator, batch.textures);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const NativeMutationDesc & desc) noexcept
	{
		return argument_is_usable(validator, desc.buffers) && argument_is_usable(validator, desc.textures);
	}

	[[nodiscard]] inline bool argument_is_usable(DeviceValidator & validator, const SparseBindDesc & desc) noexcept
	{
		return argument_is_usable(validator, desc.buffers) && argument_is_usable(validator, desc.textures) &&
			   argument_is_usable(validator, desc.timelineWaits) && argument_is_usable(validator, desc.timelineSignals);
	}

	template <auto Member>
	struct Checked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Checked<Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			bool usable = true;
			((usable = usable && argument_is_usable(*self->validator, args)), ...);
			if (!usable)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<R>(error, "an operation names a handle this device has already taken back");
			}

			return (inner_block<Block>(self)->*Member)(self->inner, args...);
		}
	};

	[[nodiscard]] inline bool on_its_recording_thread(WrappedCommandList * self) noexcept
	{
		return !self->checksThread || std::this_thread::get_id() == self->recordingThread;
	}

	[[nodiscard]] inline bool recorded_on_its_own_thread(WrappedCommandList * self, Error * error) noexcept
	{
		return on_its_recording_thread(self) ? true : self->validator->fail(error, "a command recorded on a thread other than the one that began the list");
	}

	[[nodiscard]] inline bool recorded_into_an_open_list(WrappedCommandList * self, Error * error) noexcept
	{
		return self->recording ? true : self->validator->fail(error, "a command recorded on a list that is not between Begin and End");
	}

	[[nodiscard]] inline bool recorded_legally(WrappedCommandList * self, Error * error) noexcept
	{
		return recorded_on_its_own_thread(self, error) && recorded_into_an_open_list(self, error);
	}

	template <auto Member>
	struct Recorded;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Recorded<Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!on_its_recording_thread(self) || !self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				static_cast<void>(recorded_legally(self, error));
				return R{};
			}

			return (inner_block<Block>(self)->*Member)(self->inner, args...);
		}
	};

	template <auto Member>
	struct RecordedChecked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct RecordedChecked<Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!on_its_recording_thread(self) || !self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				static_cast<void>(recorded_legally(self, error));
				return R{};
			}

			return Checked<Member>::call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	using RecordedEntry = std::conditional_t<ChecksThread, Recorded<Member>, Forward<Member>>;

	template <bool ChecksThread, auto Member>
	using RecordedCheckedEntry = std::conditional_t<ChecksThread, RecordedChecked<Member>, Checked<Member>>;

	template <bool ChecksThread, auto Member>
	struct OutsideRendering;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct OutsideRendering<ChecksThread, Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!recorded_legally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->checks_state() && self->rendering) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<R>(
					error,
					"a transfer or dispatch recorded inside a rendering scope, which has to be recorded between passes"
				);
			}

			return RecordedCheckedEntry<ChecksThread, Member>::call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct InsideRenderingWithGraphics;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct InsideRenderingWithGraphics<ChecksThread, Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!recorded_legally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->checks_state())
			{
				if (!self->rendering) [[unlikely]]
				{
					Error * error = nullptr;
					((error = PickError(error, args)), ...);
					return self->validator->fail_value<R>(error, "a draw recorded outside a rendering scope");
				}

				if (!self->graphicsBound) [[unlikely]]
				{
					Error * error = nullptr;
					((error = PickError(error, args)), ...);
					return self->validator->fail_value<R>(error, "a draw recorded with no graphics pipeline bound");
				}
			}

			return RecordedCheckedEntry<ChecksThread, Member>::call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct OutsideRenderingWithCompute;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct OutsideRenderingWithCompute<ChecksThread, Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!recorded_legally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->checks_state() && !self->computeBound) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<R>(error, "a dispatch recorded with no compute pipeline bound");
			}

			return OutsideRendering<ChecksThread, Member>::call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct OutsideRenderingWithRayTracing;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct OutsideRenderingWithRayTracing<ChecksThread, Member>
	{
		static R call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!recorded_legally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->checks_state() && !self->rayTracingBound) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<R>(error, "a trace recorded with no ray tracing pipeline bound");
			}

			return OutsideRendering<ChecksThread, Member>::call(impl, args...);
		}
	};

	inline constexpr std::uint64_t kUsageDeclared = 1ull << 63u;

	template <class T>
	[[nodiscard]] std::uint64_t PickUsage(const std::uint64_t found, const T & /*unused*/) noexcept
	{
		return found;
	}

	inline constexpr std::uint64_t kExtentsDeclared = 1ull << 62u;
	inline constexpr unsigned kSizeShift			= 32u;

	inline constexpr std::uint64_t kSizeMax = 0x3fffffffull;

	static_assert((kUsageDeclared & kExtentsDeclared) == 0, "the declared-usage and declared-extents flags share a bit, so each reads as the other");
	static_assert(((kSizeMax << kSizeShift) & (kUsageDeclared | kExtentsDeclared)) == 0, "the buffer size field reaches the flags above it");

	[[nodiscard]] inline std::uint64_t PickUsage([[maybe_unused]] std::uint64_t found, const BufferDesc & desc) noexcept
	{
		const std::uint64_t usage = kUsageDeclared | desc.usage.bits();

		if (desc.size == 0 || desc.size > kSizeMax)
		{
			return usage;
		}

		return usage | kExtentsDeclared | (desc.size << kSizeShift);
	}

	[[nodiscard]] inline std::uint64_t declared_size_from(const std::uint64_t detail) noexcept
	{
		return (detail & kExtentsDeclared) != 0 ? (detail >> kSizeShift) & kSizeMax : 0;
	}

	inline constexpr unsigned kMipCountShift	  = 32u;
	inline constexpr unsigned kLayerCountShift	  = 40u;
	inline constexpr unsigned kAspectShift		  = 56u;
	inline constexpr std::uint64_t kMipCountMax	  = 0xffull;
	inline constexpr std::uint64_t kLayerCountMax = 0xffffull;
	inline constexpr std::uint64_t kAspectMax	  = 0x3full;

	[[nodiscard]] inline std::uint64_t aspects_of_format(const Format format) noexcept
	{
		if (plane_count_of(format) > 1)
		{
			const std::uint64_t planes = static_cast<std::uint64_t>(TextureAspect::ePlane0) | static_cast<std::uint64_t>(TextureAspect::ePlane1);
			return plane_count_of(format) > 2 ? planes | static_cast<std::uint64_t>(TextureAspect::ePlane2) : planes;
		}

		if (is_depth_format(format))
		{
			const auto depth   = static_cast<std::uint64_t>(TextureAspect::eDepth);
			const bool stencil = format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
			return stencil ? depth | static_cast<std::uint64_t>(TextureAspect::eStencil) : depth;
		}

		return static_cast<std::uint64_t>(TextureAspect::eColor);
	}

	[[nodiscard]] inline std::uint64_t PickUsage([[maybe_unused]] std::uint64_t found, const TextureDesc & desc) noexcept
	{
		static_assert(
			std::numeric_limits<std::underlying_type_t<TextureUsage>>::digits <= kMipCountShift,
			"a texture usage bit reaches the mip count, so the two would overwrite each other"
		);
		static_assert((kAspectMax << kAspectShift) < kExtentsDeclared, "the aspect field runs into the declared-extents and declared-usage flags above it");
		static_assert(kAllAspects.bits() <= kAspectMax, "an aspect enumerator reaches past the field, so it would be read as one of the flags above it");

		const std::uint64_t usage = kUsageDeclared | desc.usage.bits();

		if (desc.mipLevels == 0 || desc.mipLevels > kMipCountMax || desc.arrayLayers == 0 || desc.arrayLayers > kLayerCountMax)
		{
			return usage;
		}

		return usage | kExtentsDeclared | (static_cast<std::uint64_t>(desc.mipLevels) << kMipCountShift) |
			   (static_cast<std::uint64_t>(desc.arrayLayers) << kLayerCountShift) | (aspects_of_format(desc.format) << kAspectShift);
	}

	[[nodiscard]] inline std::uint64_t PickUsage(const std::uint64_t found, const PlacedBufferDesc & desc) noexcept
	{
		return PickUsage(found, desc.buffer);
	}

	[[nodiscard]] inline std::uint64_t PickUsage(const std::uint64_t found, const PlacedTextureDesc & desc) noexcept
	{
		return PickUsage(found, desc.texture);
	}

	template <class T>
	[[nodiscard]] Format pick_format(const Format found, const T & /*unused*/) noexcept
	{
		return found;
	}

	[[nodiscard]] inline Format pick_format([[maybe_unused]] const Format found, const TextureDesc & desc) noexcept
	{
		return desc.format;
	}

	[[nodiscard]] inline Format pick_format(const Format found, const PlacedTextureDesc & desc) noexcept
	{
		return pick_format(found, desc.texture);
	}

	struct DeclaredExtents final
	{
		std::uint32_t mips	  = 0;
		std::uint32_t layers  = 0;
		std::uint32_t aspects = 0;
		std::uint64_t bytes	  = 0;
	};

	[[nodiscard]] inline DeclaredExtents extents_from(const ResourceType type, const std::uint64_t detail) noexcept
	{
		DeclaredExtents extents{};

		if ((detail & kExtentsDeclared) == 0)
		{
			return extents;
		}

		if (type == ResourceType::eBuffer)
		{
			extents.bytes = declared_size_from(detail);
			return extents;
		}

		extents.mips	= static_cast<std::uint32_t>((detail >> kMipCountShift) & kMipCountMax);
		extents.layers	= static_cast<std::uint32_t>((detail >> kLayerCountShift) & kLayerCountMax);
		extents.aspects = static_cast<std::uint32_t>((detail >> kAspectShift) & kAspectMax);
		return extents;
	}

	template <ResourceType Type, auto Member>
	struct Recording;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct Recording<Type, Member>
	{
		static Produced call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			if (!all_usable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<Produced>(error, "a create names a handle this device has already taken back");
			}

			const Produced handle = (inner_block<Block>(self)->*Member)(self->inner, args...);
			if (handle.is_valid())
			{
				const RegisteredHandle registered{
					.type		= Type,
					.index		= handle.index,
					.generation = handle.generation,
				};
				if (self->validator->Handles().record(registered))
				{
					std::uint64_t usage = 0;
					((usage = PickUsage(usage, args)), ...);
					if (usage != 0)
					{
						self->validator->Handles().lookup(registered)->detail.store(usage, std::memory_order_relaxed);
					}

					Format format = Format::eUndefined;
					((format = pick_format(format, args)), ...);
					if (format != Format::eUndefined)
					{
						self->validator->Handles().lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
					}
				}
			}

			return handle;
		}
	};

	struct AdoptedSeed final
	{
		ResourceState state{};
		QueueOwnership ownership{};
		bool present = false;
	};

	template <class T>
	[[nodiscard]] AdoptedSeed PickAdoptedSeed(AdoptedSeed found, const T & /*unused*/) noexcept
	{
		return found;
	}

	[[nodiscard]] inline AdoptedSeed PickAdoptedSeed([[maybe_unused]] AdoptedSeed found, const AdoptedBufferDesc & desc) noexcept
	{
		return AdoptedSeed{ .state = desc.initialState, .ownership = desc.initialOwnership, .present = true };
	}

	[[nodiscard]] inline AdoptedSeed PickAdoptedSeed([[maybe_unused]] AdoptedSeed found, const AdoptedTextureDesc & desc) noexcept
	{
		return AdoptedSeed{ .state = desc.initialState, .ownership = desc.initialOwnership, .present = true };
	}

	template <ResourceType Type, auto Member>
	struct RecordingAdopted;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct RecordingAdopted<Type, Member>
	{
		static Produced call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			if (!all_usable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->fail_value<Produced>(error, "an adopt names a handle this device has already taken back");
			}

			const Produced handle = (inner_block<Block>(self)->*Member)(self->inner, args...);
			if (!handle.is_valid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (!self->validator->Handles().record(registered))
			{
				return handle;
			}

			AdoptedSeed seed{};
			((seed = PickAdoptedSeed(seed, args)), ...);
			if (!seed.present)
			{
				return handle;
			}

			if (ResourceRecord * record = self->validator->Handles().lookup(registered))
			{
				record->use.store(seed.state.use.bits(), std::memory_order_relaxed);
				record->useKnown.store(true, std::memory_order_relaxed);

				if (seed.ownership.op == OwnershipOp::eRelease || seed.ownership.op == OwnershipOp::eAcquire)
				{
					record->owner.store(static_cast<std::uint8_t>(seed.ownership.counterpart), std::memory_order_relaxed);
					record->owned.store(true, std::memory_order_relaxed);
				}
			}

			return handle;
		}
	};

	template <ResourceType Type, auto Member>
	struct Vending;

	template <ResourceType Type, class Block, class Produced, class... Args, Produced (*Block::*Member)(void *, Args...) noexcept>
	struct Vending<Type, Member>
	{
		static Produced call(void * impl, Args... args) noexcept
		{
			auto * self			  = static_cast<WrappedObject *>(impl);
			const Produced handle = (inner_block<Block>(self)->*Member)(self->inner, args...);
			if (!handle.is_valid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (self->validator->Handles().lookup(registered) == nullptr)
			{
				static_cast<void>(self->validator->Handles().record(registered));
			}

			return handle;
		}
	};

	[[nodiscard]] void * wrap_device(void * deviceImpl, ValidationMode mode) noexcept;

	[[nodiscard]] AZO_RHI_API DeviceValidator * validator_of(void * deviceImpl) noexcept;

} // namespace azo::rhi::validation

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/validation/registry.hpp"

#include <atomic>
#include <cstdint>
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

		[[nodiscard]] ValidationMode Mode() const noexcept
		{
			return m_mode;
		}

		void SetMode(const ValidationMode mode) noexcept
		{
			m_mode = mode;
		}

		[[nodiscard]] bool ChecksState() const noexcept
		{
			return m_mode == ValidationMode::eDeveloper || m_mode == ValidationMode::eCapture;
		}

		bool Fail(Error * error, const char * message) noexcept
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
		[[nodiscard]] T FailValue(Error * error, const char * message) noexcept
		{
			static_cast<void>(Fail(error, message));
			return T{};
		}

		[[nodiscard]] std::uint64_t Failures() const noexcept
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

		bool recording	   = false;
		bool rendering	   = false;
		bool graphicsBound	 = false;
		bool computeBound	 = false;
		bool rayTracingBound = false;

		QueueType queueType = QueueType::eGraphics;

		detail::HostVector<TrackedSubrange> recordedStates;

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
	[[nodiscard]] const Block * InnerBlock(WrappedObject * self) noexcept;

	template <auto Member>
	struct Forward;

	template <class T>
	inline constexpr bool kIsHandle = false;

	template <class Tag>
	inline constexpr bool kIsHandle<Handle<Tag>> = true;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Forward<Member>
	{
		static_assert(!kIsHandle<R>,
			"an entry returning a Handle must use Vending, which records it with the validation registry. "
			"Forward passes the handle straight through, so nothing has heard of it and the first use is "
			"refused as stale.");

		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	[[nodiscard]] inline Error * PickError(Error * found, Error * candidate) noexcept
	{
		return candidate != nullptr ? candidate : found;
	}

	template <class T>
	[[nodiscard]] Error * PickError(Error * found, const T &) noexcept
	{
		return found;
	}

	template <class T>
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator &, const T &) noexcept
	{
		return true;
	}

	template <class Tag>
	requires requires { detail::ResourceTypeOf<Handle<Tag>>::kValue; }
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator & validator, const Handle<Tag> handle) noexcept
	{
		if (!handle.IsValid())
		{
			return true;
		}

		return validator.Handles().Lookup(RegisteredHandle{
				   .type	   = detail::ResourceTypeOf<Handle<Tag>>::kValue,
				   .index	   = handle.index,
				   .generation = handle.generation,
			   }) != nullptr;
	}

	template <class... Handles>
	[[nodiscard]] bool AllUsable(DeviceValidator & validator, const Handles &... handles) noexcept
	{
		bool usable = true;
		((usable = usable && ArgumentIsUsable(validator, handles)), ...);
		return usable;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PlacedBufferDesc & desc) noexcept
	{
		return AllUsable(validator, desc.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PlacedTextureDesc & desc) noexcept
	{
		return AllUsable(validator, desc.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const PipelineLayoutDesc & desc) noexcept
	{
		for (const DescriptorSetLayoutHandle layout : desc.sets)
		{
			if (!AllUsable(validator, layout))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const GraphicsPipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const ComputePipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RayTracingPipelineDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout, desc.pipelineCache);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AccelerationStructureDesc & desc) noexcept
	{
		return AllUsable(validator, desc.storage);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorSetAllocDesc & desc) noexcept
	{
		return AllUsable(validator, desc.layout);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RetirePoint & point) noexcept
	{
		return AllUsable(validator, point.timeline);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const TimelinePoint & point) noexcept
	{
		return AllUsable(validator, point.timeline);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DestroyDesc & desc) noexcept
	{
		return AllUsable(validator, desc.safeAfter);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const RenderingAttachment & attachment) noexcept
	{
		return AllUsable(validator, attachment.view);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BufferBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const TextureBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AliasBarrier & barrier) noexcept
	{
		return AllUsable(validator, barrier.beforeBuffer, barrier.beforeTexture, barrier.afterBuffer, barrier.afterTexture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeTouchedBuffer & touched) noexcept
	{
		return AllUsable(validator, touched.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeTouchedTexture & touched) noexcept
	{
		return AllUsable(validator, touched.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const ResidencyPriorityDesc & desc) noexcept
	{
		return AllUsable(validator, desc.buffer, desc.texture);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseBufferBind & bind) noexcept
	{
		return AllUsable(validator, bind.buffer, bind.page.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseTextureBind & bind) noexcept
	{
		return AllUsable(validator, bind.texture, bind.page.heap);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const ShaderBindingTableDesc & sbt) noexcept
	{
		return AllUsable(validator, sbt.rayGeneration.buffer, sbt.miss.buffer, sbt.hit.buffer, sbt.callable.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const AccelerationStructureBuildDesc & build) noexcept
	{
		if (!AllUsable(validator, build.dst, build.src, build.instanceBuffer, build.scratchBuffer))
		{
			return false;
		}

		for (const AccelerationStructureGeometryDesc & geometry : build.geometries)
		{
			if (!AllUsable(validator, geometry.vertexBuffer, geometry.indexBuffer))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteBuffer & write) noexcept
	{
		return AllUsable(validator, write.set, write.buffer);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteTexture & write) noexcept
	{
		return AllUsable(validator, write.set, write.view, write.sampler);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteSampler & write) noexcept
	{
		return AllUsable(validator, write.set, write.sampler);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorWriteAccelerationStructure & write) noexcept
	{
		return AllUsable(validator, write.set, write.accelerationStructure);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const DescriptorSetLayoutDesc & desc) noexcept
	{
		for (const DescriptorBinding & binding : desc.bindings)
		{
			for (const SamplerHandle sampler : binding.immutableSamplers)
			{
				if (!AllUsable(validator, sampler))
				{
					return false;
				}
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SubmitDesc & desc) noexcept
	{
		for (const TimelinePoint & point : desc.waits)
		{
			if (!ArgumentIsUsable(validator, point))
			{
				return false;
			}
		}

		for (const TimelinePoint & point : desc.signals)
		{
			if (!ArgumentIsUsable(validator, point))
			{
				return false;
			}
		}

		return true;
	}

	template <class T>
	[[nodiscard]] bool ArgumentIsUsable(DeviceValidator & validator, const std::span<const T> items) noexcept
	{
		for (const T & item : items)
		{
			if (!ArgumentIsUsable(validator, item))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BeginRenderingDesc & desc) noexcept
	{
		if (!ArgumentIsUsable(validator, desc.colors))
		{
			return false;
		}

		if (desc.timestamps != nullptr && !AllUsable(validator, desc.timestamps->pool))
		{
			return false;
		}

		return desc.depthStencil == nullptr || ArgumentIsUsable(validator, *desc.depthStencil);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const BarrierBatch & batch) noexcept
	{
		return ArgumentIsUsable(validator, batch.buffers) && ArgumentIsUsable(validator, batch.textures);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const NativeMutationDesc & desc) noexcept
	{
		return ArgumentIsUsable(validator, desc.buffers) && ArgumentIsUsable(validator, desc.textures);
	}

	[[nodiscard]] inline bool ArgumentIsUsable(DeviceValidator & validator, const SparseBindDesc & desc) noexcept
	{
		return ArgumentIsUsable(validator, desc.buffers) && ArgumentIsUsable(validator, desc.textures) && ArgumentIsUsable(validator, desc.timelineWaits) &&
			   ArgumentIsUsable(validator, desc.timelineSignals);
	}

	template <auto Member>
	struct Checked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Checked<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			bool usable = true;
			((usable = usable && ArgumentIsUsable(*self->validator, args)), ...);
			if (!usable)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "an operation names a handle this device has already taken back");
			}

			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	[[nodiscard]] inline bool OnItsRecordingThread(WrappedCommandList * self) noexcept
	{
		return !self->checksThread || std::this_thread::get_id() == self->recordingThread;
	}

	[[nodiscard]] inline bool RecordedOnItsOwnThread(WrappedCommandList * self, Error * error) noexcept
	{
		return OnItsRecordingThread(self) ? true : self->validator->Fail(error, "a command recorded on a thread other than the one that began the list");
	}

	[[nodiscard]] inline bool RecordedIntoAnOpenList(WrappedCommandList * self, Error * error) noexcept
	{
		return self->recording ? true : self->validator->Fail(error, "a command recorded on a list that is not between Begin and End");
	}

	[[nodiscard]] inline bool RecordedLegally(WrappedCommandList * self, Error * error) noexcept
	{
		return RecordedOnItsOwnThread(self, error) && RecordedIntoAnOpenList(self, error);
	}

	template <auto Member>
	struct Recorded;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct Recorded<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!OnItsRecordingThread(self) || !self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				static_cast<void>(RecordedLegally(self, error));
				return R{};
			}

			return (InnerBlock<Block>(self)->*Member)(self->inner, args...);
		}
	};

	template <auto Member>
	struct RecordedChecked;

	template <class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct RecordedChecked<Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if (!OnItsRecordingThread(self) || !self->recording) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				static_cast<void>(RecordedLegally(self, error));
				return R{};
			}

			return Checked<Member>::Call(impl, args...);
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
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!RecordedLegally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->ChecksState() && self->rendering) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(
					error, "a transfer or dispatch recorded inside a rendering scope, which has to be recorded between passes");
			}

			return RecordedCheckedEntry<ChecksThread, Member>::Call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct InsideRenderingWithGraphics;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct InsideRenderingWithGraphics<ChecksThread, Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!RecordedLegally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->ChecksState())
			{
				if (!self->rendering) [[unlikely]]
				{
					Error * error = nullptr;
					((error = PickError(error, args)), ...);
					return self->validator->FailValue<R>(error, "a draw recorded outside a rendering scope");
				}

				if (!self->graphicsBound) [[unlikely]]
				{
					Error * error = nullptr;
					((error = PickError(error, args)), ...);
					return self->validator->FailValue<R>(error, "a draw recorded with no graphics pipeline bound");
				}
			}

			return RecordedCheckedEntry<ChecksThread, Member>::Call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct OutsideRenderingWithCompute;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct OutsideRenderingWithCompute<ChecksThread, Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!RecordedLegally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->ChecksState() && !self->computeBound) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a dispatch recorded with no compute pipeline bound");
			}

			return OutsideRendering<ChecksThread, Member>::Call(impl, args...);
		}
	};

	template <bool ChecksThread, auto Member>
	struct OutsideRenderingWithRayTracing;

	template <bool ChecksThread, class Block, class R, class... Args, R (*Block::*Member)(void *, Args...) noexcept>
	struct OutsideRenderingWithRayTracing<ChecksThread, Member>
	{
		static R Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			if constexpr (ChecksThread)
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				if (!RecordedLegally(self, error)) [[unlikely]]
				{
					return R{};
				}
			}

			if (self->validator->ChecksState() && !self->rayTracingBound) [[unlikely]]
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<R>(error, "a trace recorded with no ray tracing pipeline bound");
			}

			return OutsideRendering<ChecksThread, Member>::Call(impl, args...);
		}
	};

	inline constexpr std::uint64_t kUsageDeclared = 1ull << 63u;

	template <class T>
	[[nodiscard]] std::uint64_t PickUsage(const std::uint64_t found, const T &) noexcept
	{
		return found;
	}

	inline constexpr std::uint64_t kSizeDeclared = 1ull << 63u;
	inline constexpr unsigned kSizeShift		 = 32u;

	inline constexpr std::uint64_t kSizeMax = 0x3fffffffull;

	[[nodiscard]] inline std::uint64_t PickUsage([[maybe_unused]] std::uint64_t found, const BufferDesc & desc) noexcept
	{
		const std::uint64_t usage = kUsageDeclared | desc.usage.Bits();

		if (desc.size == 0 || desc.size > kSizeMax)
		{
			return usage;
		}

		return usage | kSizeDeclared | (desc.size << kSizeShift);
	}

	[[nodiscard]] inline std::uint64_t DeclaredSizeFrom(const std::uint64_t detail) noexcept
	{
		return (detail & kSizeDeclared) != 0 ? (detail >> kSizeShift) & kSizeMax : 0;
	}

	inline constexpr std::uint64_t kExtentsDeclared = 1ull << 62u;
	inline constexpr unsigned kMipCountShift		= 32u;
	inline constexpr unsigned kLayerCountShift		= 40u;
	inline constexpr unsigned kAspectShift			= 56u;
	inline constexpr std::uint64_t kMipCountMax		= 0xffull;
	inline constexpr std::uint64_t kLayerCountMax	= 0xffffull;
	inline constexpr std::uint64_t kAspectMax		= 0x3full;

	[[nodiscard]] inline std::uint64_t AspectsOfFormat(const Format format) noexcept
	{
		if (PlaneCountOf(format) > 1)
		{
			const std::uint64_t planes = static_cast<std::uint64_t>(TextureAspect::ePlane0) | static_cast<std::uint64_t>(TextureAspect::ePlane1);
			return PlaneCountOf(format) > 2 ? planes | static_cast<std::uint64_t>(TextureAspect::ePlane2) : planes;
		}

		if (IsDepthFormat(format))
		{
			const std::uint64_t depth = static_cast<std::uint64_t>(TextureAspect::eDepth);
			const bool stencil		  = format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
			return stencil ? depth | static_cast<std::uint64_t>(TextureAspect::eStencil) : depth;
		}

		return static_cast<std::uint64_t>(TextureAspect::eColor);
	}

	[[nodiscard]] inline std::uint64_t PickUsage([[maybe_unused]] std::uint64_t found, const TextureDesc & desc) noexcept
	{
		static_assert(std::numeric_limits<std::underlying_type_t<TextureUsage>>::digits <= kMipCountShift,
			"a texture usage bit reaches the mip count, so the two would overwrite each other");
		static_assert((kAspectMax << kAspectShift) < kExtentsDeclared, "the aspect field runs into the declared-extents and declared-usage flags above it");
		static_assert(kAllAspects.Bits() <= kAspectMax, "an aspect enumerator reaches past the field, so it would be read as one of the flags above it");

		const std::uint64_t usage = kUsageDeclared | desc.usage.Bits();

		if (desc.mipLevels == 0 || desc.mipLevels > kMipCountMax || desc.arrayLayers == 0 || desc.arrayLayers > kLayerCountMax)
		{
			return usage;
		}

		return usage | kExtentsDeclared | (static_cast<std::uint64_t>(desc.mipLevels) << kMipCountShift) |
			   (static_cast<std::uint64_t>(desc.arrayLayers) << kLayerCountShift) | (AspectsOfFormat(desc.format) << kAspectShift);
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
	[[nodiscard]] Format PickFormat(const Format found, const T &) noexcept
	{
		return found;
	}

	[[nodiscard]] inline Format PickFormat([[maybe_unused]] const Format found, const TextureDesc & desc) noexcept
	{
		return desc.format;
	}

	[[nodiscard]] inline Format PickFormat(const Format found, const PlacedTextureDesc & desc) noexcept
	{
		return PickFormat(found, desc.texture);
	}

	struct DeclaredExtents final
	{
		std::uint32_t mips	  = 0;
		std::uint32_t layers  = 0;
		std::uint32_t aspects = 0;
		std::uint64_t bytes	  = 0;
	};

	[[nodiscard]] inline DeclaredExtents ExtentsFrom(const std::uint64_t detail) noexcept
	{
		DeclaredExtents extents{ .bytes = DeclaredSizeFrom(detail) };

		if ((detail & kExtentsDeclared) == 0)
		{
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
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);

			if (!AllUsable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<Produced>(error, "a create names a handle this device has already taken back");
			}

			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (handle.IsValid())
			{
				const RegisteredHandle registered{
					.type		= Type,
					.index		= handle.index,
					.generation = handle.generation,
				};
				if (self->validator->Handles().Record(registered))
				{
					std::uint64_t usage = 0;
					((usage = PickUsage(usage, args)), ...);
					if (usage != 0)
					{
						self->validator->Handles().Lookup(registered)->detail.store(usage, std::memory_order_relaxed);
					}

					Format format = Format::eUndefined;
					((format = PickFormat(format, args)), ...);
					if (format != Format::eUndefined)
					{
						self->validator->Handles().Lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
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
	[[nodiscard]] AdoptedSeed PickAdoptedSeed(AdoptedSeed found, const T &) noexcept
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
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self = static_cast<WrappedObject *>(impl);
			if (!AllUsable(*self->validator, args...))
			{
				Error * error = nullptr;
				((error = PickError(error, args)), ...);
				return self->validator->FailValue<Produced>(error, "an adopt names a handle this device has already taken back");
			}

			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (!handle.IsValid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (!self->validator->Handles().Record(registered))
			{
				return handle;
			}

			AdoptedSeed seed{};
			((seed = PickAdoptedSeed(seed, args)), ...);
			if (!seed.present)
			{
				return handle;
			}

			if (ResourceRecord * record = self->validator->Handles().Lookup(registered))
			{
				record->use.store(seed.state.use.Bits(), std::memory_order_relaxed);
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
		static Produced Call(void * impl, Args... args) noexcept
		{
			auto * self			  = static_cast<WrappedObject *>(impl);
			const Produced handle = (InnerBlock<Block>(self)->*Member)(self->inner, args...);
			if (!handle.IsValid())
			{
				return handle;
			}

			const RegisteredHandle registered{
				.type		= Type,
				.index		= handle.index,
				.generation = handle.generation,
			};
			if (self->validator->Handles().Lookup(registered) == nullptr)
			{
				static_cast<void>(self->validator->Handles().Record(registered));
			}

			return handle;
		}
	};

	[[nodiscard]] void * WrapDevice(void * deviceImpl, ValidationMode mode) noexcept;

	[[nodiscard]] AZO_RHI_API DeviceValidator * ValidatorOf(void * deviceImpl) noexcept;

}

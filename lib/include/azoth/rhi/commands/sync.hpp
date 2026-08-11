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
 * \brief Public synchronization, barrier, submission, and queue facade API.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{

	class CommandList;
	struct SparseBindDesc;

	namespace detail
	{
		struct FacadeBuilder;
	} // namespace detail

	struct QueueBlocks;

	/**
	 * \brief What a resource is being used as.
	 *
	 * Read uses combine freely. A write use stands alone except where this vocabulary says otherwise, eDepthStencilTarget being read-write by definition.
	 * Buffers use the subset that means something for a buffer and validation ignores the rest.
	 */
	enum class ResourceUse : std::uint32_t
	{
		eNone = 0,

		/**
		 * \brief Prior contents are not preserved. Legal only as a before state.
		 */
		eDiscard = 1u << 0u,

		/**
		 * \brief Indirect argument fetch for draw, dispatch, or trace commands.
		 */
		eIndirectArgs = 1u << 1u,

		eVertexBuffer = 1u << 2u,
		eIndexBuffer  = 1u << 3u,

		/**
		 * \brief Read through a uniform or constant buffer binding.
		 */
		eUniformRead = 1u << 4u,

		/**
		 * \brief Read through a sampled texture binding.
		 */
		eSampledRead = 1u << 5u,

		eStorageRead  = 1u << 6u,
		eStorageWrite = 1u << 7u,

		eColorTarget = 1u << 8u,

		/**
		 * \brief Depth and stencil attachment with writes enabled, which is read and write by definition.
		 */
		eDepthStencilTarget = 1u << 9u,

		/**
		 * \brief Depth and stencil attachment tested or sampled without writes.
		 */
		eDepthStencilRead = 1u << 10u,

		eCopySrc = 1u << 11u,
		eCopyDst = 1u << 12u,

		eResolveSrc = 1u << 13u,
		eResolveDst = 1u << 14u,

		eHostRead  = 1u << 15u,
		eHostWrite = 1u << 16u,

		/**
		 * \brief Geometry, transform, or instance data read by an acceleration-structure build.
		 */
		eAccelBuildInput = 1u << 17u,

		eAccelRead	= 1u << 18u,
		eAccelWrite = 1u << 19u,

		/**
		 * \brief Presentable swapchain state. Legal only on a swapchain texture.
		 */
		ePresent = 1u << 20u,
	};

	/**
	 * \brief Where in the pipeline a use happens.
	 *
	 * A distinction survives here only when more than one backend expresses it in its own barrier model. eNone means the backend derives a conservative set
	 * from the use.
	 */
	enum class Stage : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		/**
		 * \brief Indirect argument fetch for draw, dispatch, or trace commands.
		 */
		eIndirectFetch = 1ull << 0u,

		/**
		 * \brief Vertex and index fetch through vertex, tessellation, and geometry shading.
		 */
		eVertexWork = 1ull << 1u,

		eFragmentShading = 1ull << 2u,

		/**
		 * \brief Depth and stencil testing, early and late alike.
		 */
		eDepthStencil = 1ull << 3u,

		/**
		 * \brief Color attachment blending, logic ops, and render-target writes.
		 */
		eColorOutput = 1ull << 4u,

		eCompute = 1ull << 5u,

		/**
		 * \brief Copy, clear, and blit commands.
		 */
		eCopy = 1ull << 6u,

		/**
		 * \brief Explicit resolve commands.
		 */
		eResolve = 1ull << 7u,

		/**
		 * \brief Host-side memory access.
		 */
		eHost = 1ull << 8u,

		eRayTracing = 1ull << 9u,

		/**
		 * \brief Acceleration-structure build, update, copy, or compaction commands.
		 */
		eAccelBuild = 1ull << 10u,

		/**
		 * \brief Alias for all graphics pipeline stages.
		 */
		eAllGraphics = 1ull << 30u,

		/**
		 * \brief Alias for all queue command stages known to the backend.
		 */
		eAllCommands = 1ull << 31u,
	};

	/**
	 * \brief Whether a stage mask names at most one stage, which is all a timestamp can be written at.
	 *
	 * Every Stage enumerator is its own bit, so this popcount is exact. The empty mask counts as one answer, not as none.
	 */
	[[nodiscard]] constexpr bool IsOneTimestampStage(const Flags<Stage> stage) noexcept
	{
		const std::uint64_t bits = stage.Bits();
		return (bits & (bits - 1u)) == 0u;
	}

	/**
	 * \brief Timeline synchronization object used for CPU to GPU progress, frame retirement, and cross-queue dependencies.
	 */
	struct TimelineDesc final
	{
		std::uint64_t initialValue = 0;
		const char * debugName	   = nullptr;

		/**
		 * \brief Handle types this timeline may be exported to, empty when it is not exportable.
		 *
		 * Declared at creation because both target APIs require it there: Vulkan chains an export structure into the create call and Direct3D 12 takes a shared
		 * heap flag at the same point. A timeline created without this cannot be exported afterwards. Creation refuses a type the adapter cannot export, which
		 * Instance::QueryExternalHandleSupport answers ahead of the attempt.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/**
	 * \brief Timeline wait or signal value.
	 */
	struct TimelinePoint final
	{
		TimelineHandle timeline{};
		std::uint64_t value = 0;

		// Consumed only when the point is used as a submit wait.
		Flags<Stage> waitStages = Stage::eAllCommands;
	};

	/**
	 * \brief Binary semaphore used for swapchain acquire and present edges.
	 *
	 * Engine progress should use timelines.
	 */
	struct BinarySemaphoreDesc final
	{
		const char * debugName = nullptr;

		/**
		 * \brief Handle types this semaphore may be exported to, empty when it is not exportable.
		 *
		 * Declared at creation because both target APIs require it there: Vulkan chains an export structure into the create call and Direct3D 12 takes a shared
		 * heap flag at the same point. A semaphore created without this cannot be exported afterwards. Creation refuses a type the adapter cannot export, which
		 * Instance::QueryExternalHandleSupport answers ahead of the attempt.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	/**
	 * \brief An external semaphore handle and the timeline to build over it.
	 *
	 * \attention The importing side's ordering rules can be stricter than this one's. Vulkan permits waiting on a timeline value before the signal reaching it
	 * is submitted. CUDA forbids wait before signal on an imported semaphore. A program legal here can deadlock under a stricter importer, and nothing in this
	 * RHI can see the other side to check.
	 */
	struct ExternalTimelineImportDesc final
	{
		ExternalHandle handle{};
		TimelineDesc desc{};
	};

	/**
	 * \brief An external semaphore handle and the binary semaphore to build over it.
	 *
	 * Carries the same importer ordering caveat as the timeline form, and one more: a stricter importer can also require exactly one wait per signal on
	 * an imported binary semaphore.
	 */
	struct ExternalBinarySemaphoreImportDesc final
	{
		ExternalHandle handle{};
		BinarySemaphoreDesc desc{};
	};

	/**
	 * \brief The pair of binary semaphores one presented surface contributes to a submission.
	 *
	 * Binary semaphores are the swapchain edge and nothing else, which lets a submission carry them here. acquired comes from AcquireResult and renderFinished
	 * is what Present waits on.
	 *
	 * waitStages defaults to the color output stage, since that is where the acquire has to land. Either half may be left invalid and is then skipped,
	 * which a frame split across several submissions needs.
	 */
	struct SwapchainSync final
	{
		BinarySemaphoreHandle acquired{};
		BinarySemaphoreHandle renderFinished{};
		Flags<Stage> waitStages = Stage::eColorOutput;
	};

	/**
	 * \brief Producer domain that owns timeline value allocation and signal ordering.
	 *
	 * Use one producer per timeline unless an external coordinator serializes both value allocation and signaling.
	 */
	enum class TimelineProducerDomain : std::uint8_t
	{
		/**
		 * \brief Timeline values are signaled by the CPU.
		 */
		eHost,

		eGraphicsQueue,
		eComputeQueue,
		eCopyQueue,

		/**
		 * \brief Timeline values are allocated and ordered by an external coordinator.
		 */
		eCoordinator,
	};

	/**
	 * \brief Timeline producer ownership used for validation and diagnostics.
	 *
	 * queueType is meaningful when producer names a queue domain.
	 */
	struct TimelineOwnershipDesc final
	{
		TimelineProducerDomain producer = TimelineProducerDomain::eHost;
		QueueType queueType				= QueueType::eGraphics;
	};

	/**
	 * \brief Texture aspect bitmask.
	 */
	enum class TextureAspect : std::uint8_t
	{
		eColor	 = 1u << 0u,
		eDepth	 = 1u << 1u,
		eStencil = 1u << 2u,

		/**
		 * \brief One plane of a multi-planar format, which is how its texels are reached.
		 *
		 * A view naming a plane exposes it as an ordinary single-plane texture, so plane 0 of a biplanar 4:2:0 format samples as eR8UNorm and plane 1 as
		 * eRG8UNorm at half the extent. No backend converts Y'CbCr to RGB while sampling this way, which the shader does instead.
		 */
		ePlane0 = 1u << 3u,
		ePlane1 = 1u << 4u,
		ePlane2 = 1u << 5u,
	};

	/**
	 * \brief Returned by PlaneIndexOf for aspects that name no plane.
	 */
	inline constexpr std::uint32_t kNoPlane = ~0u;

	/**
	 * \brief The plane an aspect mask names, or kNoPlane when it names none. The lowest wins, a plane view carrying exactly one.
	 */
	[[nodiscard]] constexpr std::uint32_t PlaneIndexOf(const Flags<TextureAspect> aspects) noexcept
	{
		if (aspects.Contains(TextureAspect::ePlane0))
		{
			return 0;
		}
		if (aspects.Contains(TextureAspect::ePlane1))
		{
			return 1;
		}
		if (aspects.Contains(TextureAspect::ePlane2))
		{
			return 2;
		}

		return kNoPlane;
	}

	/**
	 * \brief Contiguous texture subresource range.
	 */
	struct TextureSubresourceRange final
	{
		Flags<TextureAspect> aspects = TextureAspect::eColor;
		std::uint32_t baseMip		 = 0;
		std::uint32_t mipCount		 = 1;
		std::uint32_t baseLayer		 = 0;
		std::uint32_t layerCount	 = 1;
	};

	/**
	 * \brief One texture subresource.
	 */
	struct TextureSubresource final
	{
		Flags<TextureAspect> aspects = TextureAspect::eColor;
		std::uint32_t mip			 = 0;
		std::uint32_t layer			 = 0;
	};

	/**
	 * \brief Synchronization state for a resource.
	 *
	 * An empty stage set asks the backend for a conservative set derived from the use. An empty use is legal only as a before state, where it means the
	 * contents are not preserved, and it is an error as an after state.
	 *
	 * A barrier releasing queue ownership is the exception, since the state this queue leaves the resource in is not something the releasing side can answer.
	 * Both halves of the pair name the state the acquiring queue wants, and the release names none.
	 */
	struct ResourceState final
	{
		Flags<ResourceUse> use = ResourceUse::eNone;
		Flags<Stage> stages	   = Stage::eNone;
	};

	/**
	 * \brief Which half of a queue ownership transfer a barrier records.
	 *
	 * A transfer is a pair: the release records on the queue giving the resource up, the acquire on the queue taking it, ordered by a timeline edge the caller
	 * already needs. The external forms name the far side of a sharing boundary, which is not a queue this device can name.
	 */
	enum class OwnershipOp : std::uint8_t
	{
		eNone,

		eRelease,
		eAcquire,

		eReleaseToExternal,
		eAcquireFromExternal,
	};

	/**
	 * \brief One half of a queue ownership transfer, or nothing.
	 *
	 * counterpart names the queue at the other end and is read only by eRelease and eAcquire. A backend without queue ownership lowers this to nothing, since
	 * the timeline edge the pair is ordered by already carries the dependency.
	 */
	struct QueueOwnership final
	{
		OwnershipOp op		  = OwnershipOp::eNone;
		QueueType counterpart = QueueType::eGraphics;
	};

	/**
	 * \brief Barrier for a byte range of one buffer.
	 *
	 * size uses max uint64 as the whole-buffer sentinel.
	 */
	struct BufferBarrier final
	{
		BufferHandle buffer{};
		ResourceState before{};
		ResourceState after{};
		QueueOwnership ownership{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = std::numeric_limits<std::uint64_t>::max();
	};

	/**
	 * \brief Barrier for a texture subresource range.
	 */
	struct TextureBarrier final
	{
		TextureHandle texture{};
		ResourceState before{};
		ResourceState after{};
		QueueOwnership ownership{};
		TextureSubresourceRange range{};
	};

	/**
	 * \brief Global memory dependency not tied to a specific resource.
	 */
	struct MemoryBarrier final
	{
		ResourceState before{};
		ResourceState after{};
	};

	/**
	 * \brief Barrier batch recorded as one command.
	 *
	 * Backends may lower this into one native barrier operation. The spans are borrowed for the duration of the recording call.
	 */
	struct BarrierBatch final
	{
		std::span<const MemoryBarrier> memory;
		std::span<const BufferBarrier> buffers;
		std::span<const TextureBarrier> textures;
	};

	/**
	 * \brief Timeline value the GPU must reach before the native resource can be released.
	 */
	struct RetirePoint final
	{
		TimelineHandle timeline{};
		std::uint64_t value = 0;
	};

	/**
	 * \brief Queue submission payload.
	 *
	 * Command list storage is not owned. The allocating command pool must not reset until the submitted work completes.
	 */
	struct SubmitDesc final
	{
		std::span<const CommandList *> commandLists;

		// Engine progress, which is every wait and signal that is not a swapchain edge. TimelinePoint::waitStages is read on waits and ignored on signals.
		std::span<const TimelinePoint> waits;
		std::span<const TimelinePoint> signals;

		// One entry per surface this submission renders to. Empty for work that does not present.
		std::span<const SwapchainSync> swapchains;

		const char * debugName = nullptr;
	};

	/**
	 * \brief Submission queue of one type.
	 *
	 * Concurrent submits need external serialization. Timeline value allocation is caller-owned unless a coordinator owns the timeline.
	 */
	class AZO_RHI_API Queue final
	{
	public:
		Queue() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		[[nodiscard]] QueueType GetType() const noexcept;

		[[nodiscard]] bool Submit(const SubmitDesc & desc) noexcept;
		[[nodiscard]] bool Submit(const SubmitDesc & desc, Error & error) noexcept;
		[[nodiscard]] bool WaitIdle() noexcept;
		[[nodiscard]] bool WaitIdle(Error & error) noexcept;

		// Scalar outputs are reset to zero on failure.
		[[nodiscard]] bool GetCompletedValue(TimelineHandle timeline, std::uint64_t & out) const noexcept;
		[[nodiscard]] bool GetCompletedValue(TimelineHandle timeline, std::uint64_t & out, Error & error) const noexcept;
		[[nodiscard]] Result<std::uint64_t> GetCompletedValueWithResult(TimelineHandle timeline) const noexcept;

		/**
		 * \brief Waits until a timeline reaches value or the timeout expires.
		 *
		 * \param timeoutNanoseconds Nanosecond timeout. Max uint64 means an infinite wait.
		 */
		[[nodiscard]] bool Wait(
			TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()) noexcept;
		[[nodiscard]] bool Wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error & error) noexcept;

		[[nodiscard]] bool Signal(TimelineHandle timeline, std::uint64_t value) noexcept;
		[[nodiscard]] bool Signal(TimelineHandle timeline, std::uint64_t value, Error & error) noexcept;

		/**
		 * \brief Binds sparse memory pages as an ordered queue operation.
		 */
		[[nodiscard]] bool BindSparse(const SparseBindDesc & desc) noexcept;
		[[nodiscard]] bool BindSparse(const SparseBindDesc & desc, Error & error) noexcept;

		/**
		 * \brief Begins a queue-level debug-label region for GPU debuggers.
		 *
		 * \param color Packed 0xRRGGBBAA color. Zero requests the backend default.
		 * \note This is a no-op on backends without queue labels.
		 */
		bool BeginDebugLabel(const char * name, std::uint32_t color = 0) noexcept;
		bool BeginDebugLabel(const char * name, std::uint32_t color, Error & error) noexcept;
		bool EndDebugLabel() noexcept;
		bool EndDebugLabel(Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		Queue(void * impl, const QueueBlocks * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		void * m_impl				 = nullptr;
		const QueueBlocks * m_blocks = nullptr;
	};

} // namespace azo::rhi

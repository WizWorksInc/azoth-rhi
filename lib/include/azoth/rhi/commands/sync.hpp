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
	 * \brief Pipeline stage mask used by barriers and submit waits.
	 *
	 * eAllGraphics and eAllCommands are broad aliases expanded by the backend. Prefer narrower stages once the hazard is known.
	 */
	enum class PipelineStage : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		/**
		 * \brief Indirect argument fetch for draw, dispatch, or trace commands.
		 */
		eDrawIndirect = 1ull << 0u,

		/**
		 * \brief Vertex and index fetch before shader execution.
		 */
		eVertexInput = 1ull << 1u,

		eVertexShader				  = 1ull << 2u,
		eTessellationControlShader	  = 1ull << 3u,
		eTessellationEvaluationShader = 1ull << 4u,
		eGeometryShader				  = 1ull << 5u,
		eFragmentShader				  = 1ull << 6u,

		/**
		 * \brief Depth and stencil tests before fragment shader completion.
		 */
		eEarlyFragmentTests = 1ull << 7u,

		/**
		 * \brief Depth and stencil tests after fragment shader execution.
		 */
		eLateFragmentTests = 1ull << 8u,

		/**
		 * \brief Color attachment blending, logic ops, and render-target writes.
		 */
		eColorOutput = 1ull << 9u,

		eComputeShader = 1ull << 10u,

		/**
		 * \brief Copy and transfer commands.
		 */
		eCopy = 1ull << 11u,

		/**
		 * \brief Explicit resolve commands.
		 */
		eResolve = 1ull << 12u,

		/**
		 * \brief Explicit clear commands outside attachment load operations.
		 */
		eClear = 1ull << 13u,

		/**
		 * \brief Host-side memory access.
		 */
		eHost = 1ull << 14u,

		eRayTracingShader = 1ull << 15u,

		/**
		 * \brief Acceleration-structure build, update, copy, or compaction commands.
		 */
		eAccelerationStructureBuild = 1ull << 16u,

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
	 * \brief Access mask used by barriers.
	 *
	 * eMemoryRead and eMemoryWrite are the broad aliases that cover any access. Prefer narrower masks once producer and consumer are known.
	 */
	enum class Access : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		/**
		 * \brief Read access to indirect command arguments.
		 */
		eIndirectRead = 1ull << 0u,

		eVertexRead = 1ull << 1u,
		eIndexRead	= 1ull << 2u,

		/**
		 * \brief Read access through uniform or constant buffer bindings.
		 */
		eConstantRead = 1ull << 3u,

		/**
		 * \brief Read access from shader resource bindings.
		 */
		eShaderRead = 1ull << 4u,

		/**
		 * \brief Write access from shader storage bindings.
		 */
		eShaderWrite = 1ull << 5u,

		eColorRead = 1ull << 6u,

		eColorWrite = 1ull << 7u,

		eDepthStencilRead = 1ull << 8u,

		eDepthStencilWrite = 1ull << 9u,

		eCopyRead  = 1ull << 10u,
		eCopyWrite = 1ull << 11u,

		/**
		 * \brief CPU read access to host-visible memory.
		 */
		eHostRead = 1ull << 12u,

		/**
		 * \brief CPU write access to host-visible memory.
		 */
		eHostWrite = 1ull << 13u,

		/**
		 * \brief Broad read access alias for conservative barriers.
		 */
		eMemoryRead = 1ull << 14u,

		/**
		 * \brief Broad write access alias for conservative barriers.
		 */
		eMemoryWrite = 1ull << 15u,

		/**
		 * \brief Read access to acceleration-structure data or build inputs.
		 */
		eAccelerationStructureRead = 1ull << 16u,

		/**
		 * \brief Write access to acceleration-structure data.
		 */
		eAccelerationStructureWrite = 1ull << 17u,
	};

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
		Flags<PipelineStage> waitStages = PipelineStage::eAllCommands;
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
	 * waitStages defaults to the color-attachment stage, since that is where the acquire has to land. Either half may be left invalid and is then skipped,
	 * which a frame split across several submissions needs.
	 */
	struct SwapchainSync final
	{
		BinarySemaphoreHandle acquired{};
		BinarySemaphoreHandle renderFinished{};
		Flags<PipelineStage> waitStages = PipelineStage::eColorOutput;
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
	 * \brief Texture subresource layout used by synchronization.
	 *
	 * ePresent is a swapchain layout ordered by swapchain calls and binary WSI semaphores.
	 */
	enum class TextureLayout : std::uint8_t
	{
		/**
		 * \brief Contents are undefined or previous contents are not preserved.
		 */
		eUndefined,

		/**
		 * \brief General layout for unordered or mixed access.
		 */
		eGeneral,

		eColorAttachment,
		eDepthStencilAttachment,

		eDepthStencilReadOnly,

		/**
		 * \brief Shader-readable texture layout.
		 */
		eShaderReadOnly,

		eCopySrc,
		eCopyDst,

		/**
		 * \brief Source layout for an explicit resolve operation.
		 */
		eResolveSrc,

		/**
		 * \brief Destination layout for an explicit resolve operation.
		 */
		eResolveDst,

		/**
		 * \brief Presentable swapchain image layout.
		 */
		ePresent,
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
	 * layout is ignored for buffers.
	 */
	struct ResourceState final
	{
		Flags<PipelineStage> stages = PipelineStage::eNone;
		Flags<Access> access		= Access::eNone;
		TextureLayout layout		= TextureLayout::eUndefined;
	};

	/**
	 * \brief Queue family ownership transfer.
	 *
	 * kIgnoreQueueFamily at both ends means no ownership transfer.
	 */
	struct QueueFamilyTransfer final
	{
		std::uint32_t src = kIgnoreQueueFamily;
		std::uint32_t dst = kIgnoreQueueFamily;
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
		QueueFamilyTransfer ownership{};
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
		QueueFamilyTransfer ownership{};
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
		[[nodiscard]] std::uint32_t GetFamilyIndex() const noexcept;

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

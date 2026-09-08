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
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

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
	}

	struct QueueBlocks;

	enum class ResourceUse : std::uint32_t
	{
		eNone = 0,

		eDiscard = 1u << 0u,

		eIndirectArgs = 1u << 1u,

		eVertexBuffer = 1u << 2u,
		eIndexBuffer  = 1u << 3u,

		eUniformRead = 1u << 4u,

		eSampledRead = 1u << 5u,

		eStorageRead  = 1u << 6u,
		eStorageWrite = 1u << 7u,

		eColorTarget = 1u << 8u,

		eDepthStencilTarget = 1u << 9u,

		eDepthStencilRead = 1u << 10u,

		eCopySrc = 1u << 11u,
		eCopyDst = 1u << 12u,

		eResolveSrc = 1u << 13u,
		eResolveDst = 1u << 14u,

		eHostRead  = 1u << 15u,
		eHostWrite = 1u << 16u,

		eAccelBuildInput = 1u << 17u,

		eAccelRead = 1u << 18u,

		eAccelWrite = 1u << 19u,

		ePresent = 1u << 20u,

		eAccelBuildScratch = 1u << 21u,
	};

	enum class Stage : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		eIndirectFetch = 1ull << 0u,

		eVertexWork = 1ull << 1u,

		eFragmentShading = 1ull << 2u,

		eDepthStencil = 1ull << 3u,

		eColorOutput = 1ull << 4u,

		eCompute = 1ull << 5u,

		eCopy = 1ull << 6u,

		eResolve = 1ull << 7u,

		eHost = 1ull << 8u,

		eRayTracing = 1ull << 9u,

		eAccelBuild = 1ull << 10u,

		eAllGraphics = 1ull << 30u,

		eAllCommands = 1ull << 31u,
	};

	[[nodiscard]] constexpr bool IsOneTimestampStage(const Flags<Stage> stage) noexcept
	{
		const std::uint64_t bits = stage.Bits();
		return (bits & (bits - 1u)) == 0u;
	}

	[[nodiscard]] constexpr bool QueueCanNameStage(const QueueType queue, const Flags<Stage> stage) noexcept
	{
		constexpr Flags<Stage> everywhere = Flags<Stage>(Stage::eHost) | Stage::eAllCommands;
		constexpr Flags<Stage> transfer	  = everywhere | Stage::eCopy | Stage::eResolve;
		constexpr Flags<Stage> compute	  = transfer | Stage::eIndirectFetch | Stage::eCompute | Stage::eRayTracing | Stage::eAccelBuild;

		switch (queue)
		{
		case QueueType::eCompute:  return compute.Contains(stage);
		case QueueType::eCopy:	   return transfer.Contains(stage);
		case QueueType::eGraphics: break;
		}

		return true;
	}

	struct TimelineDesc final
	{
		std::uint64_t initialValue = 0;
		const char * debugName	   = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct TimelinePoint final
	{
		TimelineHandle timeline{};
		std::uint64_t value = 0;

		Flags<Stage> waitStages = Stage::eAllCommands;
	};

	struct BinarySemaphoreDesc final
	{
		const char * debugName = nullptr;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct ExternalTimelineImportDesc final
	{
		ExternalHandle handle{};
		TimelineDesc desc{};
	};

	struct ExternalBinarySemaphoreImportDesc final
	{
		ExternalHandle handle{};
		BinarySemaphoreDesc desc{};
	};

	struct SwapchainSync final
	{
		BinarySemaphoreHandle acquired{};
		BinarySemaphoreHandle renderFinished{};
		Flags<Stage> waitStages = Stage::eColorOutput;
	};

	enum class TimelineProducerDomain : std::uint8_t
	{
		eHost,

		eGraphicsQueue,
		eComputeQueue,
		eCopyQueue,

		eCoordinator,
	};

	struct TimelineOwnershipDesc final
	{
		TimelineProducerDomain producer = TimelineProducerDomain::eHost;
		QueueType queueType				= QueueType::eGraphics;
	};

	enum class TextureAspect : std::uint8_t
	{
		eColor	 = 1u << 0u,
		eDepth	 = 1u << 1u,
		eStencil = 1u << 2u,

		ePlane0 = 1u << 3u,
		ePlane1 = 1u << 4u,
		ePlane2 = 1u << 5u,
	};

	inline constexpr Flags<TextureAspect> kAllAspects = Flags<TextureAspect>(TextureAspect::eColor) | TextureAspect::eDepth | TextureAspect::eStencil |
														TextureAspect::ePlane0 | TextureAspect::ePlane1 | TextureAspect::ePlane2;

	inline constexpr std::uint32_t kNoPlane = ~0u;

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

	struct TextureSubresourceRange final
	{
		Flags<TextureAspect> aspects = TextureAspect::eColor;
		std::uint32_t baseMip		 = 0;
		std::uint32_t mipCount		 = 1;
		std::uint32_t baseLayer		 = 0;
		std::uint32_t layerCount	 = 1;
	};

	struct TextureSubresource final
	{
		Flags<TextureAspect> aspects = TextureAspect::eColor;
		std::uint32_t mip			 = 0;
		std::uint32_t layer			 = 0;
	};

	struct ResourceState final
	{
		Flags<ResourceUse> use = ResourceUse::eNone;
		Flags<Stage> stages	   = Stage::eNone;
	};

	enum class OwnershipOp : std::uint8_t
	{
		eNone,

		eRelease,
		eAcquire,

		eReleaseToExternal,
		eAcquireFromExternal,
	};

	struct QueueOwnership final
	{
		OwnershipOp op		  = OwnershipOp::eNone;
		QueueType counterpart = QueueType::eGraphics;
	};

	struct BufferBarrier final
	{
		BufferHandle buffer{};
		ResourceState before{};
		ResourceState after{};
		QueueOwnership ownership{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = std::numeric_limits<std::uint64_t>::max();
	};

	struct TextureBarrier final
	{
		TextureHandle texture{};
		ResourceState before{};
		ResourceState after{};
		QueueOwnership ownership{};
		TextureSubresourceRange range{};
	};

	struct MemoryBarrier final
	{
		ResourceState before{};
		ResourceState after{};
	};

	struct BarrierBatch final
	{
		std::span<const MemoryBarrier> memory;
		std::span<const BufferBarrier> buffers;
		std::span<const TextureBarrier> textures;
	};

	struct RetirePoint final
	{
		TimelineHandle timeline{};
		std::uint64_t value = 0;
	};

	struct SubmitDesc final
	{
		std::span<const CommandList *> commandLists;

		std::span<const TimelinePoint> waits;
		std::span<const TimelinePoint> signals;

		std::span<const SwapchainSync> swapchains;

		const char * debugName = nullptr;
	};

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

		[[nodiscard]] bool GetCompletedValue(TimelineHandle timeline, std::uint64_t & out) const noexcept;
		[[nodiscard]] bool GetCompletedValue(TimelineHandle timeline, std::uint64_t & out, Error & error) const noexcept;
		[[nodiscard]] Result<std::uint64_t> GetCompletedValueWithResult(TimelineHandle timeline) const noexcept;

		[[nodiscard]] bool Wait(
			TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds = std::numeric_limits<std::uint64_t>::max()) noexcept;
		[[nodiscard]] bool Wait(TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error & error) noexcept;

		[[nodiscard]] bool Signal(TimelineHandle timeline, std::uint64_t value) noexcept;
		[[nodiscard]] bool Signal(TimelineHandle timeline, std::uint64_t value, Error & error) noexcept;

		[[nodiscard]] bool BindSparse(const SparseBindDesc & desc) noexcept;
		[[nodiscard]] bool BindSparse(const SparseBindDesc & desc, Error & error) noexcept;

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

}

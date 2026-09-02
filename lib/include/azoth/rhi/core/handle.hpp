// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace azo::rhi
{
	inline constexpr std::uint32_t kInvalidHandleIndex = std::numeric_limits<std::uint32_t>::max();

	namespace detail
	{
		inline constexpr std::uint32_t kHandleSlotBits = 24u;
		inline constexpr std::uint32_t kHandleSlotMask = (1u << kHandleSlotBits) - 1u;

		inline constexpr std::uint32_t kMaxSlotCount = kHandleSlotMask;

		inline constexpr std::uint32_t kMaxDeviceTag = (1u << (32u - kHandleSlotBits)) - 1u;

		[[nodiscard]] constexpr std::uint32_t SlotOfIndex(std::uint32_t indexWord) noexcept
		{
			return indexWord & kHandleSlotMask;
		}

		[[nodiscard]] constexpr std::uint32_t TagOfIndex(std::uint32_t indexWord) noexcept
		{
			return indexWord >> kHandleSlotBits;
		}

		[[nodiscard]] constexpr std::uint32_t ComposeIndex(std::uint32_t deviceTag, std::uint32_t slot) noexcept
		{
			return (deviceTag << kHandleSlotBits) | slot;
		}
	}

	enum class ResourceType : std::uint8_t
	{
		eBuffer,
		eTexture,
		eTextureView,
		eSampler,
		eHeap,
		eDescriptorSetLayout,
		eDescriptorSet,
		ePipelineLayout,
		eGraphicsPipeline,
		eComputePipeline,
		eRayTracingPipeline,
		ePipelineCache,
		eAccelerationStructure,
		eQueryPool,
		eTimeline,
		eBinarySemaphore,
	};

	inline constexpr std::size_t kResourceTypeCount = static_cast<std::size_t>(ResourceType::eBinarySemaphore) + 1;

	struct RawHandle final
	{
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		[[nodiscard]] friend constexpr bool operator==(RawHandle lhs, RawHandle rhs) noexcept = default;
	};

	template <class Tag>
	struct Handle final
	{
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return index != kInvalidHandleIndex;
		}

		[[nodiscard]] friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept = default;
	};
}

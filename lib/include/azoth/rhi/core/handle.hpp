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
 * \brief Public resource handle types and handle index packing helpers.
 */

#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>

namespace azo::rhi
{
	/**
	 * \brief Invalid uint32 index sentinel used by empty handles.
	 */
	inline constexpr std::uint32_t kInvalidHandleIndex = std::numeric_limits<std::uint32_t>::max();

	namespace detail
	{
		// The index word stores the owning device tag in the high bits and the slot index in the low bits. Generation stays in a separate word so stale-handle
		// counters cannot increment into the device tag.
		inline constexpr std::uint32_t kHandleSlotBits = 24u;
		inline constexpr std::uint32_t kHandleSlotMask = (1u << kHandleSlotBits) - 1u;

		// Slot value kHandleSlotMask is reserved so a fully set index word remains kInvalidHandleIndex.
		inline constexpr std::uint32_t kMaxSlotCount = kHandleSlotMask;

		inline constexpr std::uint32_t kMaxDeviceTag = (1u << (32u - kHandleSlotBits)) - 1u;

		/**
		 * \brief Extracts the low slot-index field from a packed handle index word.
		 */
		[[nodiscard]] constexpr std::uint32_t SlotOfIndex(std::uint32_t indexWord) noexcept
		{
			return indexWord & kHandleSlotMask;
		}

		/**
		 * \brief Extracts the high device-tag field from a packed handle index word.
		 */
		[[nodiscard]] constexpr std::uint32_t TagOfIndex(std::uint32_t indexWord) noexcept
		{
			return indexWord >> kHandleSlotBits;
		}

		/**
		 * \brief Packs a device tag and slot index into a handle index word.
		 *
		 * \attention deviceTag and slot are not clamped. slot must not be kHandleSlotMask because that value is reserved.
		 */
		[[nodiscard]] constexpr std::uint32_t ComposeIndex(std::uint32_t deviceTag, std::uint32_t slot) noexcept
		{
			return (deviceTag << kHandleSlotBits) | slot;
		}
	} // namespace detail

	/**
	 * \brief Public resource kind named by a handle.
	 *
	 * Backend destroy entries and validation registries use this as an ABI-facing discriminator, so external backend code may switch on these values.
	 */
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

	/**
	 * \brief Number of public resource kinds.
	 *
	 * Use this for arrays keyed by ResourceType so adding a kind updates the required storage size in one place.
	 */
	inline constexpr std::size_t kResourceTypeCount = static_cast<std::size_t>(ResourceType::eBinarySemaphore) + 1;

	/**
	 * \brief Type-erased resource handle used when ResourceType is carried separately.
	 */
	struct RawHandle final
	{
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		[[nodiscard]] friend constexpr bool operator==(RawHandle lhs, RawHandle rhs) noexcept = default;
	};

	/**
	 * \brief Strongly typed handle into a slot-based resource store.
	 */
	template <class Tag>
	struct Handle final
	{
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		/**
		 * \brief Returns true when the index word is not the invalid sentinel.
		 *
		 * \note This does not prove the handle still names a live object. Generation validation happens in the owning resource table.
		 */
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return index != kInvalidHandleIndex;
		}

		[[nodiscard]] friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept = default;
	};
} // namespace azo::rhi

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
 * \brief Chunked slot map for backend resource handles.
 */

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/platform.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <memory>
#include <utility>

namespace azo::rhi
{
	/**
	 * \brief Who destroys the native object a slot names.
	 *
	 * Three states, each answering destroy and teardown differently, which is why the pair of bools this replaced could be set to a combination that meant
	 * nothing. A backend switching on this gets a total switch. Shared, not repeated per backend because all three make the same distinctions.
	 */
	enum class SlotLifetime : std::uint8_t
	{
		/**
		 * \brief The device made it and destroys it. Every slot the create paths produce, which is why it is the default.
		 */
		eOwned,

		/**
		 * \brief The swapchain owns it. Destroy is refused, because a caller destroying a back buffer has made a mistake, and teardown skips the native
		 * release.
		 */
		eSwapchainBorrowed,

		/**
		 * \brief The caller owns it. Destroy retires the slot and leaves the object alone, that being the only way to hand the slot back, and teardown
		 * skips the native release because the caller is about to free it through whatever made it.
		 */
		eAdopted,
	};

	/**
	 * \brief Stable storage table behind one resource-handle type.
	 *
	 * Slots live in fixed chunks that never move until Reset, so Resolve can read slot metadata without taking the writer lock. Retire marks a slot dead and
	 * increments its generation before recycling the slot index, making stale handles fail validated lookup.
	 *
	 * \attention Store, Retire, RetireIf, and Reset are writer-side operations. The caller must serialize them against each other for this table.
	 */
	template <class Tag, class Payload>
	class SlotMap final
	{
	public:
		using HandleType = Handle<Tag>;

		SlotMap() = default;

		explicit SlotMap(std::uint32_t deviceTag) noexcept : m_deviceTag(deviceTag) {}

		SlotMap(const SlotMap &)			 = delete;
		SlotMap & operator=(const SlotMap &) = delete;
		SlotMap(SlotMap &&)					 = delete;
		SlotMap & operator=(SlotMap &&)		 = delete;

		~SlotMap()
		{
			Reset();
		}

		/**
		 * \brief Clears every slot and encodes deviceTag into future handles.
		 */
		void Rebind(const std::uint32_t deviceTag) noexcept
		{
			Reset();
			m_deviceTag = deviceTag;
		}

		/**
		 * \brief Stores a payload and returns a handle for it.
		 *
		 * \note Reuses a retired slot when possible. A returned invalid handle means the slot space is full or a required chunk could not be allocated.
		 */
		[[nodiscard]] HandleType Store(Payload payload)
		{
			if (!m_free.empty())
			{
				const std::uint32_t slotIndex = m_free.back();
				m_free.pop_back();

				Slot & slot	 = At(slotIndex);
				slot.payload = std::move(payload);

				// Publish live only after the payload is stored.
				slot.live.store(true, std::memory_order_release);
				return HandleType{
					.index		= detail::ComposeIndex(m_deviceTag, slotIndex),
					.generation = slot.generation.load(std::memory_order_relaxed),
				};
			}

			const std::uint32_t slotIndex = m_count.load(std::memory_order_relaxed);
			if (slotIndex >= detail::kMaxSlotCount || !EnsureChunkFor(slotIndex))
			{
				return HandleType{};
			}

			Slot & slot = At(slotIndex);
			slot.generation.store(kInitialGeneration, std::memory_order_relaxed);
			slot.payload = std::move(payload);
			slot.live.store(true, std::memory_order_release);

			// Publish the slot only after the object above has been constructed.
			m_count.store(slotIndex + 1, std::memory_order_release);
			return HandleType{
				.index		= detail::ComposeIndex(m_deviceTag, slotIndex),
				.generation = kInitialGeneration,
			};
		}

		/**
		 * \brief Resolves a handle to a mutable payload pointer.
		 *
		 * \attention Passing validate false still checks the device tag and slot bounds, but skips live-state and generation checks.
		 */
		[[nodiscard]] AZO_RHI_FORCE_INLINE Payload * Resolve(HandleType handle, bool validate) noexcept
		{
			Slot * slot = Find(handle, validate);
			return slot != nullptr ? &slot->payload : nullptr;
		}

		/**
		 * \brief Resolves a handle to a const payload pointer.
		 *
		 * \attention Passing validate false still checks the device tag and slot bounds, but skips live-state and generation checks.
		 */
		[[nodiscard]] AZO_RHI_FORCE_INLINE const Payload * Resolve(HandleType handle, bool validate) const noexcept
		{
			const Slot * slot = const_cast<SlotMap *>(this)->Find(handle, validate);
			return slot != nullptr ? &slot->payload : nullptr;
		}

		/**
		 * \brief Retires a handle and makes it fail future validated lookup.
		 *
		 * \note The payload is not destroyed immediately. The slot is overwritten by Store when reused or destroyed by Reset.
		 * \attention If the free list cannot grow, the slot remains retired but is not recycled.
		 */
		[[nodiscard]] bool Retire(HandleType handle, bool validate) noexcept
		{
			Slot * slot = Find(handle, validate);
			if (slot == nullptr)
			{
				return false;
			}

			slot->live.store(false, std::memory_order_release);
			slot->generation.fetch_add(1, std::memory_order_release);

			static_cast<void>(detail::TryPushBack(m_free, detail::SlotOfIndex(handle.index)));
			return true;
		}

		/**
		 * \brief Retires every live slot accepted by predicate.
		 *
		 * \note As in Retire, a slot whose index the free list cannot take stays retired but is not recycled.
		 * \attention This is a writer-side operation. The caller must serialize it against Store, Retire, and Reset for this table.
		 */
		template <class Fn>
		// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): predicate runs once per live slot, so forwarding it would move from it on the first one.
		std::size_t RetireIf(Fn && predicate)
		{
			const std::uint32_t count = m_count.load(std::memory_order_relaxed);
			std::size_t retired		  = 0;

			for (std::uint32_t index = 0; index < count; ++index)
			{
				Slot & slot = At(index);
				if (!slot.live.load(std::memory_order_relaxed) || !predicate(std::as_const(slot.payload)))
				{
					continue;
				}

				slot.live.store(false, std::memory_order_release);
				slot.generation.fetch_add(1, std::memory_order_release);
				static_cast<void>(detail::TryPushBack(m_free, index));
				++retired;
			}

			return retired;
		}

		/**
		 * \brief Visits every slot currently marked live.
		 *
		 * \note The visitor observes payloads that were live when each slot was checked. It does not create a table-wide snapshot.
		 */
		template <class Fn>
		// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): fn runs once per live slot, so forwarding it would move from it on the first one.
		void ForEachLive(Fn && fn)
		{
			const std::uint32_t count = m_count.load(std::memory_order_acquire);
			for (std::uint32_t index = 0; index < count; ++index)
			{
				Slot & slot = At(index);
				if (slot.live.load(std::memory_order_acquire))
				{
					fn(slot.payload);
				}
			}
		}

		/**
		 * \brief Destroys all constructed slots and releases every chunk.
		 *
		 * \attention No concurrent Resolve or ForEachLive call may overlap Reset because chunks are released.
		 */
		void Reset() noexcept
		{
			const std::uint32_t count = m_count.load(std::memory_order_relaxed);
			for (std::uint32_t chunk = 0; chunk < kMaxChunks; ++chunk)
			{
				Slot * slots = m_chunks[chunk];
				if (slots == nullptr)
				{
					continue;
				}

				const std::uint32_t base = BaseOfChunk(chunk);
				const std::uint32_t size = SizeOfChunk(chunk);
				std::destroy_n(slots, count > base ? std::min(size, count - base) : 0);
				HostFree(slots, static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
				m_chunks[chunk] = nullptr;
			}

			m_count.store(0, std::memory_order_relaxed);
			m_free.clear();
		}

		/**
		 * \brief Counts slots currently marked live.
		 *
		 * \note The returned count is diagnostic only when writers may be mutating the table concurrently.
		 */
		[[nodiscard]] std::size_t LiveCount() const noexcept
		{
			const std::uint32_t count = m_count.load(std::memory_order_acquire);
			std::size_t live		  = 0;
			for (std::uint32_t index = 0; index < count; ++index)
			{
				if (const_cast<SlotMap *>(this)->At(index).live.load(std::memory_order_acquire))
				{
					++live;
				}
			}

			return live;
		}

	private:
		static constexpr std::uint32_t kInitialGeneration = 1;
		static constexpr std::uint32_t kFirstChunkSlots	  = 256;
		static constexpr std::uint32_t kMaxChunks		  = 17;

		/**
		 * \brief One stable handle slot.
		 *
		 * generation and live are atomic because Resolve may read them without the writer lock.
		 */
		struct Slot final
		{
			std::atomic<std::uint32_t> generation{ 0 };
			std::atomic<bool> live{ false };
			Payload payload{};
		};

		[[nodiscard]] static constexpr std::uint32_t ChunkOfSlot(const std::uint32_t slotIndex) noexcept
		{
			return static_cast<std::uint32_t>(std::bit_width((slotIndex / kFirstChunkSlots) + 1u)) - 1u;
		}

		[[nodiscard]] static constexpr std::uint32_t BaseOfChunk(const std::uint32_t chunk) noexcept
		{
			return kFirstChunkSlots * ((1u << chunk) - 1u);
		}

		[[nodiscard]] static constexpr std::uint32_t SizeOfChunk(const std::uint32_t chunk) noexcept
		{
			return kFirstChunkSlots << chunk;
		}

		[[nodiscard]] AZO_RHI_FORCE_INLINE Slot & At(const std::uint32_t slotIndex) noexcept
		{
			const std::uint32_t chunk = ChunkOfSlot(slotIndex);
			return m_chunks[chunk][slotIndex - BaseOfChunk(chunk)]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		}

		/**
		 * \brief Allocates the chunk that owns slotIndex when needed.
		 *
		 * \note Chunks stay allocated until Reset so existing slot addresses remain stable.
		 */
		[[nodiscard]] bool EnsureChunkFor(const std::uint32_t slotIndex) noexcept
		{
			const std::uint32_t chunk = ChunkOfSlot(slotIndex);
			if (chunk >= kMaxChunks)
			{
				return false;
			}

			if (m_chunks[chunk] != nullptr)
			{
				return true;
			}

			const std::uint32_t size = SizeOfChunk(chunk);
			void * storage			 = HostAllocate(static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
			if (storage == nullptr)
			{
				return false;
			}

			auto * slots = static_cast<Slot *>(storage);
			std::uninitialized_value_construct_n(slots, size);
			m_chunks[chunk] = slots;
			return true;
		}

		/**
		 * \brief Finds the slot addressed by a handle.
		 *
		 * The device tag is always checked. validate true also checks live state and generation.
		 */
		[[nodiscard]] AZO_RHI_FORCE_INLINE Slot * Find(const HandleType handle, const bool validate) noexcept
		{
			if (detail::TagOfIndex(handle.index) != m_deviceTag)
			{
				return nullptr;
			}

			const std::uint32_t slotIndex = detail::SlotOfIndex(handle.index);

			if (slotIndex >= m_count.load(std::memory_order_acquire))
			{
				return nullptr;
			}

			Slot & slot = At(slotIndex);
			if (validate && (!slot.live.load(std::memory_order_acquire) || slot.generation.load(std::memory_order_acquire) != handle.generation))
			{
				return nullptr;
			}

			return &slot;
		}

		std::array<Slot *, kMaxChunks> m_chunks{};
		std::atomic<std::uint32_t> m_count{ 0 };
		detail::HostVector<std::uint32_t> m_free;
		std::uint32_t m_deviceTag = 0;
	};
} // namespace azo::rhi

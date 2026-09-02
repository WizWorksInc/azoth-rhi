// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/platform.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace azo::rhi
{
	enum class SlotLifetime : std::uint8_t
	{
		eOwned,

		eSwapchainBorrowed,

		eAdopted,
	};

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

		void Rebind(const std::uint32_t deviceTag) noexcept
		{
			Reset();
			m_deviceTag = deviceTag;
		}

		[[nodiscard]] HandleType Store(Payload payload)
		{
			if (!m_free.empty())
			{
				const std::uint32_t slotIndex = m_free.back();
				m_free.pop_back();

				Slot & slot	 = At(slotIndex);
				slot.payload = std::move(payload);

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

			m_count.store(slotIndex + 1, std::memory_order_release);
			return HandleType{
				.index		= detail::ComposeIndex(m_deviceTag, slotIndex),
				.generation = kInitialGeneration,
			};
		}

		[[nodiscard]] AZO_RHI_FORCE_INLINE Payload * Resolve(HandleType handle, bool validate) noexcept
		{
			Slot * slot = Find(handle, validate);
			return slot != nullptr ? &slot->payload : nullptr;
		}

		[[nodiscard]] AZO_RHI_FORCE_INLINE const Payload * Resolve(HandleType handle, bool validate) const noexcept
		{
			const Slot * slot = const_cast<SlotMap *>(this)->Find(handle, validate);
			return slot != nullptr ? &slot->payload : nullptr;
		}

		[[nodiscard]] bool Retire(HandleType handle, bool validate) noexcept
		{
			Slot * slot = Find(handle, validate);
			if (slot == nullptr)
			{
				return false;
			}

			if (!slot->live.exchange(false, std::memory_order_acq_rel))
			{
				return false;
			}

			slot->generation.fetch_add(1, std::memory_order_release);

			static_cast<void>(detail::TryPushBack(m_free, detail::SlotOfIndex(handle.index)));
			return true;
		}

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
}

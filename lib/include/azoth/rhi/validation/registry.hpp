// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace azo::rhi::validation
{

	struct RegisteredHandle final
	{
		ResourceType type		 = ResourceType::eBuffer;
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		[[nodiscard]] friend constexpr bool operator==(RegisteredHandle lhs, RegisteredHandle rhs) noexcept = default;
	};

	struct ResourceRecord final
	{
		ResourceRecord() = default;

		ResourceRecord(const ResourceRecord &)			   = delete;
		ResourceRecord & operator=(const ResourceRecord &) = delete;
		ResourceRecord(ResourceRecord &&)				   = delete;
		ResourceRecord & operator=(ResourceRecord &&)	   = delete;
		~ResourceRecord()								   = default;

		std::atomic<std::uint32_t> use{ 0 };

		std::atomic<bool> useKnown{ false };

		std::atomic<std::uint8_t> owner{ 0 };

		std::atomic<bool> owned{ false };

		std::atomic<std::uint64_t> origin{ 0 };

		std::atomic<std::uint64_t> detail{ 0 };

		std::atomic<std::uint16_t> format{ 0 };
	};

	class HandleRegistry final
	{
	public:
		HandleRegistry() = default;

		HandleRegistry(const HandleRegistry &)			   = delete;
		HandleRegistry & operator=(const HandleRegistry &) = delete;
		HandleRegistry(HandleRegistry &&)				   = delete;
		HandleRegistry & operator=(HandleRegistry &&)	   = delete;

		~HandleRegistry()
		{
			for (Table & table : m_tables)
			{
				table.Release();
			}
		}

		[[nodiscard]] bool Record(const RegisteredHandle handle) noexcept
		{
			if (handle.index == kInvalidHandleIndex)
			{
				return false;
			}

			return TableFor(handle.type).Claim(handle.index, handle.generation) != nullptr;
		}

		[[nodiscard]] ResourceRecord * Lookup(const RegisteredHandle handle) noexcept
		{
			return TableFor(handle.type).Find(handle.index, handle.generation);
		}

		[[nodiscard]] const ResourceRecord * Lookup(const RegisteredHandle handle) const noexcept
		{
			return const_cast<HandleRegistry *>(this)->Lookup(handle);
		}

		[[nodiscard]] bool IsLive(const RegisteredHandle handle) const noexcept
		{
			return Lookup(handle) != nullptr;
		}

		[[nodiscard]] bool Retire(const RegisteredHandle handle) noexcept
		{
			return TableFor(handle.type).Retire(handle.index, handle.generation);
		}

		[[nodiscard]] bool Restore(const RegisteredHandle handle) noexcept
		{
			return TableFor(handle.type).Restore(handle.index, handle.generation);
		}

		std::size_t RetireFrom(const ResourceType type, const std::uint64_t origin) noexcept
		{
			return TableFor(type).RetireFrom(origin);
		}

		[[nodiscard]] std::size_t LiveCount(const ResourceType type) const noexcept
		{
			return const_cast<HandleRegistry *>(this)->TableFor(type).LiveCount();
		}

		[[nodiscard]] std::size_t LiveCount() const noexcept
		{
			std::size_t live = 0;
			for (std::size_t kind = 0; kind < kResourceTypeCount; ++kind)
			{
				live += LiveCount(static_cast<ResourceType>(kind));
			}

			return live;
		}

	private:
		class Table final
		{
		public:
			static constexpr std::uint32_t kFirstChunkSlots = 256;

			static constexpr std::uint32_t kMaxChunks = 17;

			[[nodiscard]] ResourceRecord * Claim(const std::uint32_t index, const std::uint32_t generation) noexcept
			{
				Slot * slot = EnsureSlot(detail::SlotOfIndex(index));
				if (slot == nullptr)
				{
					return nullptr;
				}

				slot->index.store(index, std::memory_order_relaxed);
				slot->generation.store(generation, std::memory_order_relaxed);

				slot->record.use.store(0, std::memory_order_relaxed);
				slot->record.useKnown.store(false, std::memory_order_relaxed);
				slot->record.owner.store(0, std::memory_order_relaxed);
				slot->record.owned.store(false, std::memory_order_relaxed);
				slot->record.detail.store(0, std::memory_order_relaxed);
				slot->record.format.store(0, std::memory_order_relaxed);
				slot->record.origin.store(0, std::memory_order_relaxed);

				if (!slot->live.exchange(true, std::memory_order_release))
				{
					m_live.fetch_add(1, std::memory_order_relaxed);
				}

				return &slot->record;
			}

			[[nodiscard]] ResourceRecord * Find(const std::uint32_t index, const std::uint32_t generation) noexcept
			{
				Slot * slot = At(detail::SlotOfIndex(index));
				if (slot == nullptr || !slot->live.load(std::memory_order_acquire))
				{
					return nullptr;
				}

				return Identifies(*slot, index, generation) ? &slot->record : nullptr;
			}

			[[nodiscard]] bool Retire(const std::uint32_t index, const std::uint32_t generation) noexcept
			{
				Slot * slot = At(detail::SlotOfIndex(index));
				if (slot == nullptr || !Identifies(*slot, index, generation))
				{
					return false;
				}

				if (!slot->live.exchange(false, std::memory_order_release))
				{
					return false;
				}

				m_live.fetch_sub(1, std::memory_order_relaxed);
				return true;
			}

			[[nodiscard]] bool Restore(const std::uint32_t index, const std::uint32_t generation) noexcept
			{
				Slot * slot = At(detail::SlotOfIndex(index));
				if (slot == nullptr || !Identifies(*slot, index, generation))
				{
					return false;
				}

				if (slot->live.exchange(true, std::memory_order_release))
				{
					return false;
				}

				m_live.fetch_add(1, std::memory_order_relaxed);
				return true;
			}

			[[nodiscard]] std::size_t RetireFrom(const std::uint64_t origin) noexcept
			{
				if (origin == 0)
				{
					return 0;
				}

				const std::uint32_t reach = m_count.load(std::memory_order_acquire);
				std::size_t retired		  = 0;

				for (std::uint32_t index = 0; index < reach; ++index)
				{
					Slot * slot = At(index);
					if (slot == nullptr || slot->record.origin.load(std::memory_order_relaxed) != origin)
					{
						continue;
					}

					if (slot->live.exchange(false, std::memory_order_release))
					{
						m_live.fetch_sub(1, std::memory_order_relaxed);
						++retired;
					}
				}

				return retired;
			}

			[[nodiscard]] std::size_t LiveCount() const noexcept
			{
				return m_live.load(std::memory_order_relaxed);
			}

			void Release() noexcept
			{
				for (std::uint32_t chunk = 0; chunk < kMaxChunks; ++chunk)
				{
					Slot * slots = m_chunks[chunk].load(std::memory_order_relaxed);
					if (slots == nullptr)
					{
						continue;
					}

					const std::uint32_t size = SizeOfChunk(chunk);
					std::destroy_n(slots, size);
					HostFree(slots, static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
					m_chunks[chunk].store(nullptr, std::memory_order_relaxed);
				}

				m_count.store(0, std::memory_order_relaxed);
				m_live.store(0, std::memory_order_relaxed);
			}

		private:
			struct Slot final
			{
				std::atomic<std::uint32_t> index{ kInvalidHandleIndex };
				std::atomic<std::uint32_t> generation{ 0 };
				std::atomic<bool> live{ false };
				ResourceRecord record;
			};

			[[nodiscard]] static bool Identifies(const Slot & slot, const std::uint32_t index, const std::uint32_t generation) noexcept
			{
				return slot.index.load(std::memory_order_acquire) == index && slot.generation.load(std::memory_order_acquire) == generation;
			}

			[[nodiscard]] static constexpr std::uint32_t ChunkOfSlot(const std::uint32_t index) noexcept
			{
				return static_cast<std::uint32_t>(std::bit_width((index / kFirstChunkSlots) + 1u)) - 1u;
			}

			[[nodiscard]] static constexpr std::uint32_t BaseOfChunk(const std::uint32_t chunk) noexcept
			{
				return kFirstChunkSlots * ((1u << chunk) - 1u);
			}

			[[nodiscard]] static constexpr std::uint32_t SizeOfChunk(const std::uint32_t chunk) noexcept
			{
				return kFirstChunkSlots << chunk;
			}

			[[nodiscard]] Slot * At(const std::uint32_t index) noexcept
			{
				if (index >= m_count.load(std::memory_order_acquire))
				{
					return nullptr;
				}

				const std::uint32_t chunk = ChunkOfSlot(index);
				Slot * slots			  = m_chunks[chunk].load(std::memory_order_acquire);
				return slots != nullptr ? slots + (index - BaseOfChunk(chunk)) : nullptr; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			}

			[[nodiscard]] Slot * EnsureSlot(const std::uint32_t index) noexcept
			{
				const std::uint32_t chunk = ChunkOfSlot(index);
				if (chunk >= kMaxChunks)
				{
					return nullptr;
				}

				Slot * slots = m_chunks[chunk].load(std::memory_order_acquire);
				if (slots == nullptr)
				{
					const std::uint32_t size = SizeOfChunk(chunk);
					void * storage			 = HostAllocate(static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
					if (storage == nullptr)
					{
						return nullptr;
					}

					auto * built = static_cast<Slot *>(storage);
					std::uninitialized_value_construct_n(built, size);

					slots = nullptr;
					if (!m_chunks[chunk].compare_exchange_strong(slots, built, std::memory_order_release, std::memory_order_acquire))
					{
						std::destroy_n(built, size);
						HostFree(built, static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
					}
					else
					{
						slots = built;
					}
				}

				const std::uint32_t reach = BaseOfChunk(chunk) + SizeOfChunk(chunk);
				std::uint32_t seen		  = m_count.load(std::memory_order_relaxed);
				while (seen < reach && !m_count.compare_exchange_weak(seen, reach, std::memory_order_release, std::memory_order_relaxed))
				{
				}

				return slots + (index - BaseOfChunk(chunk)); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			}

			std::array<std::atomic<Slot *>, kMaxChunks> m_chunks{};

			std::atomic<std::uint32_t> m_count{ 0 };
			std::atomic<std::size_t> m_live{ 0 };
		};

		[[nodiscard]] Table & TableFor(const ResourceType type) noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		std::array<Table, kResourceTypeCount> m_tables;
	};

}

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
 * \brief Validation registry for live resource handles and diagnostic resource state.
 */

#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/host/allocator.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <memory>

namespace azo::rhi::validation
{

	/**
	 * \brief Resource kind, index word, and generation for one registered handle.
	 *
	 * The kind is stored beside the raw handle identity because the registry keeps one table per resource type.
	 */
	struct RegisteredHandle final
	{
		ResourceType type		 = ResourceType::eBuffer;
		std::uint32_t index		 = kInvalidHandleIndex;
		std::uint32_t generation = 0;

		[[nodiscard]] friend constexpr bool operator==(RegisteredHandle lhs, RegisteredHandle rhs) noexcept = default;
	};

	/**
	 * \brief Diagnostic state known for one live resource.
	 *
	 * Fields are atomic because validated command recording can read state while another recording thread writes barrier state. Relaxed reads are enough for
	 * diagnostics. A program racing the barrier that would make a check pass is already racing at the API level.
	 */
	struct ResourceRecord final
	{
		ResourceRecord() = default;

		ResourceRecord(const ResourceRecord &)			   = delete;
		ResourceRecord & operator=(const ResourceRecord &) = delete;
		ResourceRecord(ResourceRecord &&)				   = delete;
		ResourceRecord & operator=(ResourceRecord &&)	   = delete;
		~ResourceRecord()								   = default;

		/**
		 * \brief The state the resource arrived in, for a resource that arrived in one.
		 *
		 * Only an adopted resource has this: it was made elsewhere and is in whatever state its producer left it in. A created one genuinely starts in no state,
		 * which is why the flag below exists, not zero standing for both an unset field and the empty access mask.
		 */
		std::atomic<std::uint32_t> access{ 0 };

		/**
		 * \brief True while access still describes the resource, which is until the first barrier moves it.
		 *
		 * Consumed, not kept, by the first barrier in any command list that names this resource. After that the recording's own tracking owns the state, and a
		 * device-wide field would be claiming to track something across command lists that nothing here tracks.
		 */
		std::atomic<bool> accessKnown{ false };

		/**
		 * \brief Queue family that last owned the resource.
		 */
		std::atomic<std::uint8_t> owner{ 0 };

		/**
		 * \brief True once queue ownership has been established.
		 */
		std::atomic<bool> owned{ false };

		/**
		 * \brief Pool or arena that produced this handle, or zero for non-pooled resource kinds.
		 *
		 * Descriptor arena reset uses this stamp to retire every descriptor set allocated from that arena.
		 */
		std::atomic<std::uint64_t> origin{ 0 };

		/**
		 * \brief Kind-specific packed validation detail.
		 *
		 * Descriptor set layouts use this to store binding types, four bits per binding, without allocating side storage.
		 */
		std::atomic<std::uint64_t> detail{ 0 };
	};

	/**
	 * \brief Per-device registry of handles the backend handed out and has not retired.
	 *
	 * Lookup is wait free and allocation free so validated command recording can call it for every named handle. Create and retire paths are serialized externally
	 * by the backend block guards.
	 *
	 * Slots live in chunks that are never moved or freed until registry destruction. A reader can hold a slot pointer while another thread grows storage.
	 */
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

		/**
		 * \brief Records a handle a backend just created.
		 *
		 * Returns false when the registry cannot grow to the handle's slot. The generation is copied from the backend handle.
		 */
		[[nodiscard]] bool Record(const RegisteredHandle handle) noexcept
		{
			if (handle.index == kInvalidHandleIndex)
			{
				return false;
			}

			ResourceRecord * record = TableFor(handle.type).Claim(handle.index, handle.generation);
			if (record == nullptr)
			{
				return false;
			}

			record->access.store(0, std::memory_order_relaxed);
			record->accessKnown.store(false, std::memory_order_relaxed);
			record->owned.store(false, std::memory_order_relaxed);
			record->detail.store(0, std::memory_order_relaxed);
			record->origin.store(0, std::memory_order_relaxed);
			return true;
		}

		/**
		 * \brief Returns the live record for handle, or null when it is stale, foreign, or unknown.
		 *
		 * This is the hot read path used by validated command recording. It takes no lock and allocates nothing.
		 */
		[[nodiscard]] ResourceRecord * Lookup(const RegisteredHandle handle) noexcept
		{
			return TableFor(handle.type).Find(handle.index, handle.generation);
		}

		[[nodiscard]] const ResourceRecord * Lookup(const RegisteredHandle handle) const noexcept
		{
			return const_cast<HandleRegistry *>(this)->Lookup(handle);
		}

		/**
		 * \brief Returns true when handle is known to this registry and has not been retired.
		 */
		[[nodiscard]] bool IsLive(const RegisteredHandle handle) const noexcept
		{
			return Lookup(handle) != nullptr;
		}

		/**
		 * \brief Retires one live handle.
		 *
		 * Returns false when the handle was not live, which is a double destroy or foreign handle diagnostic.
		 */
		[[nodiscard]] bool Retire(const RegisteredHandle handle) noexcept
		{
			return TableFor(handle.type).Retire(handle.index, handle.generation);
		}

		/**
		 * \brief Retires every live handle of type that came from origin.
		 *
		 * Pool and arena reset use this for objects reclaimed wholesale. The scan is linear in slots ever used for that resource type.
		 */
		std::size_t RetireFrom(const ResourceType type, const std::uint64_t origin) noexcept
		{
			return TableFor(type).RetireFrom(origin);
		}

		/**
		 * \brief Returns the number of live handles for one resource kind.
		 */
		[[nodiscard]] std::size_t LiveCount(const ResourceType type) const noexcept
		{
			return const_cast<HandleRegistry *>(this)->TableFor(type).LiveCount();
		}

		/**
		 * \brief Returns the number of live handles across all resource kinds.
		 */
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
		/**
		 * \brief Slot table for one ResourceType.
		 */
		class Table final
		{
		public:
			/**
			 * \brief Number of slots in the first chunk.
			 */
			static constexpr std::uint32_t kFirstChunkSlots = 256;

			/**
			 * \brief Maximum chunk count covering the 24-bit slot field.
			 */
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

				// Publish live after index and generation so a reader that sees live also sees the identity fields.
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

			[[nodiscard]] std::size_t RetireFrom(const std::uint64_t origin) noexcept
			{
				// Zero is the non-pooled sentinel, so matching it would retire unrelated resources.
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

			/**
			 * \brief Releases all allocated chunks.
			 *
			 * Called only during registry destruction, when no lookup can still be reading.
			 */
			void Release() noexcept
			{
				for (std::uint32_t chunk = 0; chunk < kMaxChunks; ++chunk)
				{
					Slot * slots = m_chunks[chunk];
					if (slots == nullptr)
					{
						continue;
					}

					const std::uint32_t size = SizeOfChunk(chunk);
					std::destroy_n(slots, size);
					HostFree(slots, static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
					m_chunks[chunk] = nullptr;
				}

				m_count.store(0, std::memory_order_relaxed);
				m_live.store(0, std::memory_order_relaxed);
			}

		private:
			struct Slot final
			{
				// Full index word, not only the slot bits used to choose this row.
				std::atomic<std::uint32_t> index{ kInvalidHandleIndex };
				std::atomic<std::uint32_t> generation{ 0 };
				std::atomic<bool> live{ false };
				ResourceRecord record;
			};

			/**
			 * \brief Returns true when slot holds the exact index word and generation.
			 *
			 * The table row is selected from the slot field, but identity is the whole index word plus generation so foreign device tags and backend-specific index bits
			 * are preserved.
			 */
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

			/**
			 * \brief Finds an existing slot without allocating.
			 */
			[[nodiscard]] Slot * At(const std::uint32_t index) noexcept
			{
				// Acquire pairs with the writer's count publish so visible slots are constructed before use.
				if (index >= m_count.load(std::memory_order_acquire))
				{
					return nullptr;
				}

				const std::uint32_t chunk = ChunkOfSlot(index);
				Slot * slots			  = m_chunks[chunk];
				return slots != nullptr ? slots + (index - BaseOfChunk(chunk)) : nullptr; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			}

			/**
			 * \brief Returns the slot, creating it when this index has none yet.
			 *
			 * Writer side only. The caller serializes creation for this resource kind.
			 */
			[[nodiscard]] Slot * EnsureSlot(const std::uint32_t index) noexcept
			{
				const std::uint32_t chunk = ChunkOfSlot(index);
				if (chunk >= kMaxChunks)
				{
					return nullptr;
				}

				if (m_chunks[chunk] == nullptr)
				{
					const std::uint32_t size = SizeOfChunk(chunk);
					void * storage			 = HostAllocate(static_cast<std::size_t>(size) * sizeof(Slot), alignof(Slot));
					if (storage == nullptr)
					{
						return nullptr;
					}

					auto * slots = static_cast<Slot *>(storage);
					std::uninitialized_value_construct_n(slots, size);
					m_chunks[chunk] = slots;
				}

				const std::uint32_t reach = BaseOfChunk(chunk) + SizeOfChunk(chunk);
				if (reach > m_count.load(std::memory_order_relaxed))
				{
					m_count.store(reach, std::memory_order_release);
				}

				return m_chunks[chunk] + (index - BaseOfChunk(chunk)); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			}

			std::array<Slot *, kMaxChunks> m_chunks{};

			// Published slot reach. It only rises while the table is live.
			std::atomic<std::uint32_t> m_count{ 0 };
			std::atomic<std::size_t> m_live{ 0 };
		};

		[[nodiscard]] Table & TableFor(const ResourceType type) noexcept
		{
			return m_tables[static_cast<std::size_t>(type)];
		}

		std::array<Table, kResourceTypeCount> m_tables;
	};

} // namespace azo::rhi::validation

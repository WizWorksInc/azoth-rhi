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

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace azo::rhi::detail
{
	template <class T>
	class TypedObjectPool final
	{
	public:
		explicit TypedObjectPool(std::size_t blocksPerPage, std::size_t maxPages = 0, const char * debugName = nullptr) noexcept
			: m_blocksPerPage(blocksPerPage != 0 ? blocksPerPage : 1),
			  m_maxPages(maxPages),
			  m_debugName(debugName)
		{
		}

		~TypedObjectPool()
		{
			Reset();
		}

		TypedObjectPool(const TypedObjectPool &)			 = delete;
		TypedObjectPool & operator=(const TypedObjectPool &) = delete;

		TypedObjectPool(TypedObjectPool && other) noexcept
			: m_pages(std::move(other.m_pages)),
			  m_count(other.m_count),
			  m_blocksPerPage(other.m_blocksPerPage),
			  m_maxPages(other.m_maxPages),
			  m_debugName(other.m_debugName)
		{
			other.m_count = 0;
		}

		TypedObjectPool & operator=(TypedObjectPool && other) noexcept
		{
			if (this != &other)
			{
				Reset();

				m_pages			= std::move(other.m_pages);
				m_count			= other.m_count;
				m_blocksPerPage = other.m_blocksPerPage;
				m_maxPages		= other.m_maxPages;
				m_debugName		= other.m_debugName;
				other.m_count	= 0;
			}

			return *this;
		}

		template <class... Args>
		[[nodiscard]] T * New(Args &&... args)
		{
			const std::size_t pageIndex = m_count / m_blocksPerPage;
			if (pageIndex == m_pages.size())
			{
				if (m_maxPages != 0 && m_pages.size() >= m_maxPages)
				{
					return nullptr;
				}

				const std::size_t bytes = m_blocksPerPage * sizeof(T);
				void * memory			= HostAllocate(bytes, static_cast<std::size_t>(kAlign));
				if (memory == nullptr)
				{
					return nullptr;
				}

				if (!TryPushBack(m_pages, Page{ static_cast<std::byte *>(memory), bytes }))
				{
					return nullptr;
				}
			}

			std::byte * slot = m_pages[pageIndex].memory + ((m_count % m_blocksPerPage) * sizeof(T));
			T * object = std::construct_at(reinterpret_cast<T *>(slot), std::forward<Args>(args)...); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
			++m_count;
			return object;
		}

		void Reset() noexcept
		{
			for (std::size_t i = m_count; i > 0; --i)
			{
				const std::size_t index = i - 1;
				std::byte * slot		= m_pages[index / m_blocksPerPage].memory + ((index % m_blocksPerPage) * sizeof(T));
				std::destroy_at(reinterpret_cast<T *>(slot)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
			}

			m_pages.clear();
			m_count = 0;
		}

		[[nodiscard]] std::size_t LiveCount() const noexcept
		{
			return m_count;
		}

		[[nodiscard]] const char * DebugName() const noexcept
		{
			return m_debugName;
		}

	private:
		static constexpr std::align_val_t kAlign{ alignof(T) > alignof(void *) ? alignof(T) : alignof(void *) };

		struct Page final
		{
			std::byte * memory = nullptr;
			std::size_t bytes  = 0;

			Page(std::byte * m, std::size_t n) noexcept : memory(m), bytes(n) {}

			~Page()
			{
				Release();
			}

			Page(const Page &)			   = delete;
			Page & operator=(const Page &) = delete;

			Page(Page && other) noexcept : memory(other.memory), bytes(other.bytes)
			{
				other.memory = nullptr;
				other.bytes	 = 0;
			}

			Page & operator=(Page && other) noexcept
			{
				if (this != &other)
				{
					Release();

					memory		 = other.memory;
					bytes		 = other.bytes;
					other.memory = nullptr;
					other.bytes	 = 0;
				}

				return *this;
			}

		private:
			void Release() noexcept
			{
				if (memory != nullptr)
				{
					HostFree(memory, bytes, static_cast<std::size_t>(kAlign));
				}
			}
		};

		HostVector<Page> m_pages;
		std::size_t m_count			= 0;
		std::size_t m_blocksPerPage = 1;
		std::size_t m_maxPages		= 0;
		const char * m_debugName	= nullptr;
	};
}

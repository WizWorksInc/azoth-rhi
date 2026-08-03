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
 * \brief Host-allocator container aliases and noexcept growth helpers.
 */

#include "azoth/rhi/host/allocator.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace azo::rhi::detail
{
	/**
	 * \brief Vector whose dynamic storage is routed through the RHI host allocator.
	 */
	template <class T>
	using HostVector = std::vector<T, HostAllocatorAdapter<T>>;

	/**
	 * \brief Deque whose dynamic storage is routed through the RHI host allocator.
	 */
	template <class T>
	using HostDeque = std::deque<T, HostAllocatorAdapter<T>>;

	// ReSharper reports this allocator-specialized string alias as deprecated on some toolchains. ReSharper disable once CppDeprecatedEntity
	using HostString = std::basic_string<char, std::char_traits<char>, HostAllocatorAdapter<char>>;

	/**
	 * \brief Unordered map whose node and bucket storage is routed through the RHI host allocator.
	 */
	template <class Key, class Value, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
	using HostMap = std::unordered_map<Key, Value, Hash, Eq, HostAllocatorAdapter<std::pair<const Key, Value>>>;

	/**
	 * \brief Unordered set whose node and bucket storage is routed through the RHI host allocator.
	 */
	template <class Key, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
	using HostSet = std::unordered_set<Key, Hash, Eq, HostAllocatorAdapter<Key>>;

	/**
	 * \brief Appends to a host vector without letting allocation or move-construction exceptions cross a noexcept backend entry.
	 *
	 * \attention With AZOTH_RHI_NO_EXCEPTIONS enabled, failure follows the allocator and standard-library no-exception behavior and cannot be reported as
	 * false.
	 */
	template <class T, class U>
	[[nodiscard]] bool TryPushBack(HostVector<T> & into, U && value) noexcept
	{
#ifdef AZOTH_RHI_NO_EXCEPTIONS
		into.push_back(std::forward<U>(value));
		return true;
#else
		try
		{
			into.push_back(std::forward<U>(value));
			return true;
		}
		catch (...)
		{
			return false;
		}
#endif
	}

	/**
	 * \brief Reserves host-vector capacity without letting allocation exceptions cross a noexcept backend entry.
	 *
	 * \attention With AZOTH_RHI_NO_EXCEPTIONS enabled, failure follows the allocator and standard-library no-exception behavior and cannot be reported as
	 * false.
	 */
	template <class T>
	[[nodiscard]] bool TryReserve(HostVector<T> & vec, const std::size_t count) noexcept
	{
#ifdef AZOTH_RHI_NO_EXCEPTIONS
		vec.reserve(count);
		return true;
#else
		try
		{
			vec.reserve(count);
			return true;
		}
		catch (...)
		{
			return false;
		}
#endif
	}

	/**
	 * \brief Inserts or assigns a map value without letting allocation or value-construction exceptions cross a noexcept backend entry.
	 *
	 * \attention With AZOTH_RHI_NO_EXCEPTIONS enabled, failure follows the allocator and standard-library no-exception behavior and cannot be reported as
	 * false.
	 */
	template <class Map, class Key, class Value>
	[[nodiscard]] bool TryInsertOrAssign(Map & into, const Key & key, Value && value) noexcept
	{
#ifdef AZOTH_RHI_NO_EXCEPTIONS
		into.insert_or_assign(key, std::forward<Value>(value));
		return true;
#else
		try
		{
			into.insert_or_assign(key, std::forward<Value>(value));
			return true;
		}
		catch (...)
		{
			return false;
		}
#endif
	}

} // namespace azo::rhi::detail

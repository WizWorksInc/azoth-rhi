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

#include "azoth/rhi/host/allocator.hpp"

#include <cstddef>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace azo::rhi::detail
{
	template <class Container>
	[[nodiscard]] constexpr decltype(auto) at(Container & values, const std::size_t index)
	{
		if (index >= std::size(values)) [[unlikely]]
		{
			std::abort();
		}

		return values[index]; // NOLINT(*-pro-bounds-avoid-unchecked-container-access,*-pro-bounds-constant-array-index)
	}

	template <class T, std::size_t Extent>
	[[nodiscard]] constexpr T & at(std::span<T, Extent> values, const std::size_t index)
	{
		return at<std::span<T, Extent>>(values, index);
	}

	template <class T>
	using HostVector = std::vector<T, HostAllocatorAdapter<T>>;

	template <class T>
	using HostDeque = std::deque<T, HostAllocatorAdapter<T>>;

	using HostString = std::basic_string<char, std::char_traits<char>, HostAllocatorAdapter<char>>;

	template <class Key, class Value, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
	using HostMap = std::unordered_map<Key, Value, Hash, Eq, HostAllocatorAdapter<std::pair<const Key, Value>>>;

	template <class Key, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
	using HostSet = std::unordered_set<Key, Hash, Eq, HostAllocatorAdapter<Key>>;

	template <class T, class U>
	[[nodiscard]] bool try_push_back(HostVector<T> & into, U && value) noexcept
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

	template <class T>
	[[nodiscard]] bool try_reserve(HostVector<T> & vec, const std::size_t count) noexcept
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

	template <class Map, class Key, class Value>
	[[nodiscard]] bool try_insert_or_assign(Map & into, const Key & key, Value && value) noexcept
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

}

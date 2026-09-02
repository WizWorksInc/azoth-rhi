// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/hash.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace azo::rhi
{

	struct GraphicsApiId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(GraphicsApiId lhs, GraphicsApiId rhs) noexcept = default;
	};

	[[nodiscard]] consteval GraphicsApiId MakeGraphicsApiId(const std::string_view name) noexcept
	{
		return GraphicsApiId{ hash::Fnv1a64Hash(name) };
	}

	[[nodiscard]] constexpr std::string_view ShortApiName(const std::string_view canonicalName) noexcept
	{
		const std::size_t dot = canonicalName.rfind('.');
		if (dot == std::string_view::npos)
		{
			return canonicalName;
		}

		std::string_view tail = canonicalName;
		tail.remove_prefix(dot + 1);
		return tail;
	}

	struct GraphicsApiTagRoot
	{
	};

	template <class T>
	concept GraphicsApiTag = std::derived_from<T, GraphicsApiTagRoot> && std::is_empty_v<T> && std::is_final_v<T> && requires {
		{ T::id } -> std::convertible_to<GraphicsApiId>;
		{ T::canonicalName } -> std::convertible_to<std::string_view>;
		{ T::displayName } -> std::convertible_to<std::string_view>;
	};

	struct VulkanApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.vulkan";
		static constexpr std::string_view displayName	= "Vulkan";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<VulkanApi>);

	struct D3D12Api final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.d3d12";
		static constexpr std::string_view displayName	= "Direct3D 12";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<D3D12Api>);

	struct MetalApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.metal";
		static constexpr std::string_view displayName	= "Metal 3";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<MetalApi>);

	struct Metal4Api final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.metal4";
		static constexpr std::string_view displayName	= "Metal 4";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<Metal4Api>);

	[[nodiscard]] constexpr bool IsMetalFamily(const GraphicsApiId api) noexcept
	{
		return api == MetalApi::id || api == Metal4Api::id;
	}

	struct NullApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.null";
		static constexpr std::string_view displayName	= "Null RHI";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<NullApi>);

}

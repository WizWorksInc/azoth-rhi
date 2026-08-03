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
 * \brief Graphics API identity values and compile-time backend tags.
 */

#include "azoth/rhi/core/hash.hpp"

#include <concepts>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace azo::rhi
{

	/**
	 * \brief Stable runtime identity for a graphics API backend.
	 *
	 * The value is derived from the backend's globally namespaced canonical name.
	 */
	struct GraphicsApiId final
	{
		std::uint64_t value = 0;

		[[nodiscard]] friend constexpr bool operator==(GraphicsApiId lhs, GraphicsApiId rhs) noexcept = default;
	};

	/**
	 * \brief Builds a stable 64-bit backend id from a globally namespaced canonical backend name.
	 *
	 * \param name Canonical backend name, such as azoth.rhi.vulkan or studio.rhi.gnm.
	 */
	[[nodiscard]] consteval GraphicsApiId MakeGraphicsApiId(const std::string_view name) noexcept
	{
		return GraphicsApiId{ hash::Fnv1a64Hash(name) };
	}

	/**
	 * \brief Returns the final component of a canonical backend name.
	 *
	 * \param canonicalName Globally namespaced backend name. Names without a dot are returned unchanged.
	 *
	 * Short names are for user-facing selection through command lines, config, and environment variables. The canonical name still owns identity.
	 */
	[[nodiscard]] constexpr std::string_view ShortApiName(const std::string_view canonicalName) noexcept
	{
		const std::size_t dot = canonicalName.rfind('.');
		if (dot == std::string_view::npos)
		{
			return canonicalName;
		}

		// remove_prefix cannot throw, unlike substr, and rfind already proved dot + 1 is at most the string size.
		std::string_view tail = canonicalName;
		tail.remove_prefix(dot + 1);
		return tail;
	}

	/**
	 * \brief Marker base inherited by all compile-time graphics API tags.
	 */
	struct GraphicsApiTagRoot
	{
	};

	/**
	 * \brief Compile-time backend tag accepted by templated RHI entry points.
	 *
	 * A tag is an empty final type with a stable id, a globally namespaced canonical name, and a human-readable display name.
	 */
	template <class T>
	concept GraphicsApiTag = std::derived_from<T, GraphicsApiTagRoot> && std::is_empty_v<T> && std::is_final_v<T> && requires {
		{ T::id } -> std::convertible_to<GraphicsApiId>;
		{ T::canonicalName } -> std::convertible_to<std::string_view>;
		{ T::displayName } -> std::convertible_to<std::string_view>;
	};

	/**
	 * \brief Built-in backend tag for Vulkan.
	 */
	struct VulkanApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.vulkan";
		static constexpr std::string_view displayName	= "Vulkan";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<VulkanApi>);

	/**
	 * \brief Built-in backend tag for Direct3D 12.
	 */
	struct D3D12Api final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.d3d12";
		static constexpr std::string_view displayName	= "Direct3D 12";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<D3D12Api>);

	/**
	 * \brief Built-in backend tag for Metal 3.
	 *
	 * The generation every Apple machine this runs on can take, which is why it keeps the plain name.
	 */
	struct MetalApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.metal";
		static constexpr std::string_view displayName	= "Metal 3";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<MetalApi>);

	/**
	 * \brief Built-in backend tag for Metal 4.
	 *
	 * A backend of its own and not a mode of the one above. Metal 4 replaces submission, recording and binding outright and its command objects share no base
	 * with the Metal 3 ones, so the two are separate APIs that happen to agree about what a texture is.
	 */
	struct Metal4Api final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.metal4";
		static constexpr std::string_view displayName	= "Metal 4";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<Metal4Api>);

	/**
	 * \brief Whether an id names one of the Metal backends, either generation.
	 *
	 * For the questions that are about Metal the platform and not about the generation: which surface payload a window hands over, which target a shader is
	 * compiled for, how the binding ABI lays a set out. All three answer the same for both, and a comparison against one tag would silently answer no for the
	 * other.
	 */
	[[nodiscard]] constexpr bool IsMetalFamily(const GraphicsApiId api) noexcept
	{
		return api == MetalApi::id || api == Metal4Api::id;
	}

	/**
	 * \brief Built-in backend tag for tests and headless validation.
	 */
	struct NullApi final : GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "azoth.rhi.null";
		static constexpr std::string_view displayName	= "Null RHI";
		static constexpr GraphicsApiId id				= MakeGraphicsApiId(canonicalName);
	};

	static_assert(GraphicsApiTag<NullApi>);

} // namespace azo::rhi

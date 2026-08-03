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
 * \brief ABI stamp used to reject incompatible loadable backend modules.
 */

#include "azoth/rhi/core/version.hpp"

#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{
	namespace detail
	{

		// Encodes the standard library family and ABI mode as one opaque value. This is not a string because callers should compare the whole stamp, not parse
		// parts of it.
		inline constexpr std::uint64_t kStandardLibraryTag =
#ifdef _LIBCPP_VERSION
			(1ULL << 56)
	#ifdef _LIBCPP_ABI_VERSION
			| (static_cast<std::uint64_t>(_LIBCPP_ABI_VERSION) << 32)
	#endif
			| static_cast<std::uint64_t>(_LIBCPP_VERSION / 1000);
#elifdef __GLIBCXX__
			(2ULL << 56)
	#if defined(_GLIBCXX_USE_CXX11_ABI)
			| (static_cast<std::uint64_t>(_GLIBCXX_USE_CXX11_ABI) << 32)
	#endif
			;
#elif defined(_MSVC_STL_VERSION)
			(3ULL << 56) | (static_cast<std::uint64_t>(_MSVC_STL_VERSION) << 32);
#else
			0ULL;
#endif

		// Exception mode is part of the language stamp because it changes allocator failure behavior across the boundary.
		inline constexpr std::uint64_t kNoExceptionsBit =
#ifdef AZOTH_RHI_NO_EXCEPTIONS
			1ULL << 63;
#else
			0ULL;
#endif

		// MSVC needs _MSVC_LANG unless /Zc:__cplusplus is enabled. Reading __cplusplus there would compare a useless value.
#ifdef _MSVC_LANG
		inline constexpr std::uint64_t kLanguageValue = _MSVC_LANG | kNoExceptionsBit;
#else
		inline constexpr std::uint64_t kLanguageValue = __cplusplus | kNoExceptionsBit;
#endif

	} // namespace detail

	/**
	 * \brief Host and module ABI facts that must match before a loadable backend is called.
	 *
	 * Backend blocks pass standard-library types such as std::span and std::string_view. A module built with a different standard-library ABI or language mode
	 * may lay those types out differently while still compiling against the same headers.
	 *
	 * This stamp tracks layout-affecting facts, not compiler patch versions. Block-level size and version checks still guard individual blocks.
	 */
	struct AbiStamp final
	{
		/**
		 * \brief Manual version for ABI shapes outside per-block byteSize and version checks.
		 *
		 * Covers module entry points and surrounding structs such as BackendEntry and BackendInfo.
		 */
		std::uint32_t moduleAbiVersion = 1;

		std::uint32_t pointerBits	  = sizeof(void *) * 8;
		std::uint64_t languageValue	  = detail::kLanguageValue;
		std::uint64_t standardLibrary = detail::kStandardLibraryTag;

		/**
		 * \brief RHI release used to reject modules built against a different public surface.
		 */
		std::uint32_t rhiVersionMajor = kVersionMajor;
		std::uint32_t rhiVersionMinor = kVersionMinor;

		[[nodiscard]] friend constexpr bool operator==(const AbiStamp & lhs, const AbiStamp & rhs) noexcept = default;
	};

	/**
	 * \brief Returns the ABI stamp this translation unit was built with.
	 */
	[[nodiscard]] constexpr AbiStamp CurrentAbiStamp() noexcept
	{
		return AbiStamp{};
	}

} // namespace azo::rhi

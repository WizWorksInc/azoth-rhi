// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/version.hpp"

#include <cstddef>
#include <cstdint>

namespace azo::rhi
{
	namespace detail
	{

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

		inline constexpr std::uint64_t kNoExceptionsBit =
#ifdef AZOTH_RHI_NO_EXCEPTIONS
			1ULL << 63;
#else
			0ULL;
#endif

#ifdef _MSVC_LANG
		inline constexpr std::uint64_t kLanguageValue = _MSVC_LANG | kNoExceptionsBit;
#else
		inline constexpr std::uint64_t kLanguageValue = __cplusplus | kNoExceptionsBit;
#endif

	}

	struct AbiStamp final
	{
		std::uint32_t moduleAbiVersion = 1;

		std::uint32_t pointerBits	  = sizeof(void *) * 8;
		std::uint64_t languageValue	  = detail::kLanguageValue;
		std::uint64_t standardLibrary = detail::kStandardLibraryTag;

		std::uint32_t rhiVersionMajor = kVersionMajor;
		std::uint32_t rhiVersionMinor = kVersionMinor;

		[[nodiscard]] friend constexpr bool operator==(const AbiStamp & lhs, const AbiStamp & rhs) noexcept = default;
	};

	[[nodiscard]] constexpr AbiStamp CurrentAbiStamp() noexcept
	{
		return AbiStamp{};
	}

}

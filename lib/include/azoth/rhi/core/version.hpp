// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/c_string.hpp"

#include "azoth/rhi/core/version_numbers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace azo::rhi
{

	inline constexpr std::uint32_t kVersionMajor = detail::kGeneratedVersionMajor;
	inline constexpr std::uint32_t kVersionMinor = detail::kGeneratedVersionMinor;
	inline constexpr std::uint32_t kVersionPatch = detail::kGeneratedVersionPatch;

	namespace detail
	{
		inline constexpr std::size_t kVersionStringCapacity = 33;

		[[nodiscard]] constexpr std::size_t DigitCount(std::uint32_t value) noexcept
		{
			std::size_t digits = 1;
			while (value >= 10)
			{
				value /= 10;
				++digits;
			}

			return digits;
		}

		[[nodiscard]] consteval auto BuildVersionString(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) noexcept
		{
			std::array<char, kVersionStringCapacity> text{};
			std::size_t at = 0;

			const auto append = [&text, &at](std::uint32_t value)
			{
				const std::size_t digits = DigitCount(value);
				for (std::size_t i = digits; i > 0; --i)
				{
					text[at + i - 1] = static_cast<char>('0' + (value % 10));
					value /= 10;
				}
				at += digits;
			};

			append(major);
			text[at++] = '.';
			append(minor);
			text[at++] = '.';
			append(patch);
			text[at] = '\0';

			return text;
		}

		inline constexpr auto kVersionStringStorage = BuildVersionString(kVersionMajor, kVersionMinor, kVersionPatch);
	}

	inline constexpr CString kVersionString = detail::kVersionStringStorage.data();

	struct BuildInfo final
	{
		std::uint32_t versionMajor = kVersionMajor;
		std::uint32_t versionMinor = kVersionMinor;
		std::uint32_t versionPatch = kVersionPatch;

		bool profilingEnabled = false;

		bool tracyEnabled = false;

		bool pixEnabled = false;
	};

	[[nodiscard]] constexpr BuildInfo GetBuildInfo() noexcept
	{
		BuildInfo info{};

#ifdef AZOTH_RHI_ENABLE_PROFILING
		info.profilingEnabled = true;
#endif

#ifdef TRACY_ENABLE
		info.tracyEnabled = true;
#endif

#ifdef AZOTH_RHI_ENABLE_PIX
		info.pixEnabled = true;
#endif

		return info;
	}

}

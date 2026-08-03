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
 * \brief Build version constants and compile-time build feature reporting.
 */

#include "azoth/rhi/core/c_string.hpp"

// Generated into the build tree from AZOTH_RHI_BUILD_VERSION.
#include "azoth/rhi/core/version_numbers.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	inline constexpr std::uint32_t kVersionMajor = detail::kGeneratedVersionMajor;
	inline constexpr std::uint32_t kVersionMinor = detail::kGeneratedVersionMinor;
	inline constexpr std::uint32_t kVersionPatch = detail::kGeneratedVersionPatch;

	namespace detail
	{
		// Three uint32 values at ten digits each, plus two dots and the null terminator.
		inline constexpr std::size_t kVersionStringCapacity = 33;

		/**
		 * \brief Counts decimal digits in a uint32 value.
		 *
		 * \note This stays constexpr because MSVC does not reliably carry an enclosing consteval context into the lambda that calls it.
		 */
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

		/**
		 * \brief Builds the dotted semantic version string from the generated numeric version fields.
		 *
		 * Keeping the string derived from the numbers makes a version bump one generated-data edit and keeps GetBuildInfo in sync.
		 */
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

		// Static storage gives kVersionString a process-lifetime pointer.
		inline constexpr auto kVersionStringStorage = BuildVersionString(kVersionMajor, kVersionMinor, kVersionPatch);
	} // namespace detail

	inline constexpr CString kVersionString = detail::kVersionStringStorage.data();

	/**
	 * \brief Build-time facts exposed to diagnostics, tools, and version checks.
	 *
	 * Backend availability is intentionally not listed here. Query registered backends through selection APIs so external backends appear the same way
	 * built-in backends do.
	 */
	struct BuildInfo final
	{
		std::uint32_t versionMajor = kVersionMajor;
		std::uint32_t versionMinor = kVersionMinor;
		std::uint32_t versionPatch = kVersionPatch;

		// False means every AZO_RHI_PROFILE macro compiled out, so an installed profiler sink is never called.
		bool profilingEnabled = false;

		// True means this build saw the host's TRACY_ENABLE and compiled the Tracy sink against that client.
		bool tracyEnabled = false;

		// True means the PIX sink is compiled in. The WinPixEventRuntime dependency remains the host application's responsibility.
		bool pixEnabled = false;
	};

	/**
	 * \brief Returns build-time feature flags and version numbers.
	 *
	 * The result constant-folds, so checking build features does not require runtime probing.
	 */
	[[nodiscard]] constexpr BuildInfo GetBuildInfo() noexcept
	{
		BuildInfo info{};

#ifdef AZOTH_RHI_ENABLE_PROFILING
		info.profilingEnabled = true;
#endif // AZOTH_RHI_ENABLE_PROFILING

#ifdef TRACY_ENABLE
		info.tracyEnabled = true;
#endif // TRACY_ENABLE

#ifdef AZOTH_RHI_ENABLE_PIX
		info.pixEnabled = true;
#endif // AZOTH_RHI_ENABLE_PIX

		return info;
	}

} // namespace azo::rhi

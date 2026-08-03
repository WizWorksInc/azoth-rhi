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

#include "harness/environment.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace azo::rhi::test
{

	namespace
	{

		[[nodiscard]] std::string_view ReadEnv(const char * name) noexcept
		{
			// NOLINTNEXTLINE(concurrency-mt-unsafe): read once during suite setup, before any test thread starts.
			const char * value = std::getenv(name);
			return value != nullptr ? std::string_view{ value } : std::string_view{};
		}

	} // namespace

	std::string_view RequestedBackends()
	{
		static const std::string_view value = ReadEnv("AZOTH_RHI_TEST_BACKENDS");
		return value;
	}

	std::string_view RequiredBackends()
	{
		static const std::string_view value = ReadEnv("AZOTH_RHI_TEST_REQUIRE_BACKENDS");
		return value;
	}

	std::uint32_t StressScale()
	{
		static const std::uint32_t value = []
		{
			const std::string_view text = ReadEnv("AZOTH_RHI_TEST_STRESS_SCALE");
			std::uint32_t parsed		= 1;
			if (text.empty() || std::from_chars(text.data(), text.data() + text.size(), parsed).ec != std::errc{})
			{
				return 1u;
			}
			return parsed;
		}();
		return value;
	}

	std::uint32_t ScaledIterations(const std::uint32_t baseline)
	{
		return std::max(1u, baseline * StressScale());
	}

} // namespace azo::rhi::test

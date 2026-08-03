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

#include "support/driver_version.hpp"

#include <format>
#include <iterator>
#include <string>

namespace azo::rhi
{
	namespace
	{
		// The standard Vulkan VK_MAKE_API_VERSION field extraction, reimplemented here so this translation unit stays free of the Vulkan headers (and thus
		// unit-testable without a GPU SDK). These match VK_API_VERSION_MAJOR and VK_API_VERSION_MINOR.
		constexpr std::uint32_t StandardMajor(const std::uint32_t v) noexcept
		{
			return (v >> 22) & 0x7FU;
		}

		constexpr std::uint32_t StandardMinor(const std::uint32_t v) noexcept
		{
			return (v >> 12) & 0x3FFU;
		}
	} // namespace

	detail::HostString FormatVulkanDriverVersion(const DriverId id, const std::uint32_t rawVersion)
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
		switch (id)
		{
		case DriverId::eNvidiaProprietary:
			// NVIDIA packs 10.8.8.6 bit fields. The displayed version is the top two.
			major = rawVersion >> 22;
			minor = (rawVersion >> 14) & 0xFFU;
			break;
		case DriverId::eIntelProprietaryWindows:
			// Intel's Windows driver keeps the whole minor in the low 14 bits.
			major = rawVersion >> 14;
			minor = rawVersion & 0x3FFFU;
			break;
		case DriverId::eQualcommProprietary:
			// Adreno drivers are shown with the major offset into the 512.x range.
			major = StandardMajor(rawVersion) | 0x200U;
			minor = StandardMinor(rawVersion);
			break;
		case DriverId::eMoltenvk:
			// MoltenVK packs major*10000 + minor*100 + patch, not the Vulkan layout.
			major = rawVersion / 10000U;
			minor = (rawVersion / 100U) % 100U;
			break;
		default:
			major = StandardMajor(rawVersion);
			minor = StandardMinor(rawVersion);
			break;
		}
		detail::HostString out;
		std::format_to(std::back_inserter(out), "{}.{}", major, minor);
		return out;
	}

	detail::HostString FormatD3D12DriverVersion(const std::uint64_t umdVersion)
	{
		const std::uint32_t product	   = static_cast<std::uint32_t>((umdVersion >> 48) & 0xFFFFU);
		const std::uint32_t version	   = static_cast<std::uint32_t>((umdVersion >> 32) & 0xFFFFU);
		const std::uint32_t subVersion = static_cast<std::uint32_t>((umdVersion >> 16) & 0xFFFFU);
		const std::uint32_t build	   = static_cast<std::uint32_t>(umdVersion & 0xFFFFU);
		detail::HostString out;
		std::format_to(std::back_inserter(out), "{}.{}.{}.{}", product, version, subVersion, build);
		return out;
	}
} // namespace azo::rhi

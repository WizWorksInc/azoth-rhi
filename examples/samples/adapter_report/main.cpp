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

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// The backend name this sample takes as its first argument or null when it was not given one.

	const char * Yes(const bool value)
	{
		return value ? "yes" : "no";
	}

} // namespace

namespace
{

	const char * Describe(rhi::AdapterType type)
	{
		switch (type)
		{
		case rhi::AdapterType::eDiscrete:	return "discrete";
		case rhi::AdapterType::eIntegrated: return "integrated";
		case rhi::AdapterType::eVirtual:	return "virtual";
		case rhi::AdapterType::eCpu:		return "cpu";
		default:							return "unknown";
		}
	}

	const char * Describe(rhi::DriverId driver)
	{
		switch (driver)
		{
		case rhi::DriverId::eNvidiaProprietary:		  return "NVIDIA proprietary";
		case rhi::DriverId::eAmdProprietary:		  return "AMD proprietary";
		case rhi::DriverId::eAmdOpenSource:			  return "AMD open source";
		case rhi::DriverId::eMesaRadv:				  return "Mesa RADV";
		case rhi::DriverId::eMesaNvk:				  return "Mesa NVK";
		case rhi::DriverId::eIntelProprietaryWindows: return "Intel proprietary";
		case rhi::DriverId::eIntelOpenSourceMesa:	  return "Intel Mesa";
		case rhi::DriverId::eMoltenvk:				  return "MoltenVK";
		case rhi::DriverId::eMesaLlvmpipe:			  return "Mesa llvmpipe";
		case rhi::DriverId::eGoogleSwiftshader:		  return "SwiftShader";
		default:									  return nullptr;
		}
	}

	const char * OrNone(const char * text)
	{
		return text != nullptr ? text : "not reported";
	}

	std::uint64_t Mebibytes(const std::uint64_t bytes)
	{
		return bytes / (static_cast<std::uint64_t>(1024 * 1024));
	}

	void PrintAdapter(const rhi::AdapterInfo & adapter)
	{
		LOG_INFO(fw::Log(), "  [{}] {} ({})", adapter.adapterIndex, (adapter.name != nullptr ? adapter.name : "unnamed"), Describe(adapter.type));
		LOG_INFO(fw::Log(), "      ids:     vendor 0x{:x}, device 0x{:x}", adapter.vendorId, adapter.deviceId);

		// Composed, not printed a piece at a time: a log line arrives whole, and a half line would interleave with whatever the backend thread writes next.
		const char * driver = Describe(adapter.driverId);
		LOG_INFO(fw::Log(), "      driver:  {}{}", driver != nullptr ? std::format("{} ", driver) : std::string{}, OrNone(adapter.driverVersion));

		if (adapter.driverInfo != nullptr)
		{
			LOG_INFO(fw::Log(), "      info:    {}", adapter.driverInfo);
		}

		std::string memory = std::format(
			"      memory:  {} MiB dedicated, {} MiB shared", Mebibytes(adapter.dedicatedVideoMemoryBytes), Mebibytes(adapter.sharedSystemMemoryBytes));
		if (adapter.unifiedMemoryArchitecture)
		{
			memory += ", unified";
		}
		if (adapter.linkedAdapter)
		{
			memory += ", linked";
		}
		LOG_INFO(fw::Log(), "{}", memory);
	}

	// The two-call idiom: an empty span asks how many there are, the second call fills a span that size. Returns an empty vector on failure having already
	// reported why.
	std::vector<rhi::AdapterInfo> CollectAdapters(const rhi::Instance & instance)
	{
		rhi::Error error{};
		std::uint32_t count = 0;
		if (!instance.EnumerateAdapters({}, count, error))
		{
			fw::ReportError("      adapter count failed", error);
			return {};
		}

		std::vector<rhi::AdapterInfo> adapters(count);
		if (count != 0 && !instance.EnumerateAdapters(adapters, count, error))
		{
			fw::ReportError("      adapter enumeration failed", error);
			return {};
		}

		// A backend may report fewer than it counted if an adapter went away in between.
		adapters.resize(count);
		return adapters;
	}

} // namespace

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (backends.IsEmpty())
	{
		LOG_INFO(fw::Log(), "this build registered no backends");
		return 1;
	}

	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	rhi::InstanceDesc desc{};
	desc.applicationName = "adapter_report";
	desc.validation		 = rhi::ValidationMode::eOff; // nothing is created here so there is nothing to validate

	std::uint32_t found = 0;
	// What each backend reports about itself such as whether it shipped with the RHI or was registered here.
	for (const rhi::BackendInfo & backend : backends.Preferred())
	{
		LOG_INFO(fw::Log(), "{}", backend.displayName);
		LOG_INFO(fw::Log(),
			"  surfaces: {}, debug markers: {}, native access: {}",
			Yes(backend.supportsSurfaces),
			Yes(backend.supportsDebugMarkers),
			Yes(backend.supportsExternalNativeAccess));

		// One backend at a time so each gets its own instance and not the runtime picking one of them for us.
		const rhi::Result<rhi::UniqueInstance> instance = backends.CreateInstance(backend.id, desc);
		if (!instance)
		{
			fw::ReportError("  no instance", instance.GetError());
			continue;
		}

		const std::vector<rhi::AdapterInfo> adapters = CollectAdapters(instance.Value().Get());
		if (adapters.empty())
		{
			LOG_INFO(fw::Log(), "  no adapters");
			continue;
		}

		for (const rhi::AdapterInfo & adapter : adapters)
		{
			PrintAdapter(adapter);
		}

		found += static_cast<std::uint32_t>(adapters.size());
	}

	LOG_INFO(fw::Log(), "\n{} adapter(s) across {} backend(s)", found, backends.Preferred().size());
	return found != 0 ? 0 : 1;
}

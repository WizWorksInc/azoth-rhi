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
#include "azoth/rhi/core/version.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// The backend name this sample takes as its first argument or null when it was not given one.

	// The failure line this sample prints, since a diagnostic string is optional.

	const char * Yes(const bool value)
	{
		return value ? "yes" : "no";
	}

	const char * Name(const rhi::SparseTier tier)
	{
		switch (tier)
		{
		case rhi::SparseTier::eNone:			 return "none";
		case rhi::SparseTier::eBuffers:			 return "buffers";
		case rhi::SparseTier::eResidentTextures: return "resident textures";
		case rhi::SparseTier::eResidentVolumes:	 return "resident volumes";
		}

		return "unknown";
	}

	const char * Name(const rhi::BindingTier tier)
	{
		switch (tier)
		{
		case rhi::BindingTier::eBasic:			 return "basic";
		case rhi::BindingTier::eDynamicIndexing: return "dynamic indexing";
		case rhi::BindingTier::eUnbounded:		 return "unbounded";
		}

		return "unknown";
	}

	const char * Name(const rhi::ConservativeRasterTier tier)
	{
		switch (tier)
		{
		case rhi::ConservativeRasterTier::eNone:			  return "none";
		case rhi::ConservativeRasterTier::eBasic:			  return "basic";
		case rhi::ConservativeRasterTier::eDegenerateCulling: return "degenerate culling";
		case rhi::ConservativeRasterTier::eInnerCoverage:	  return "inner coverage";
		}

		return "unknown";
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

	const char * OrUnknown(const char * text)
	{
		return text != nullptr ? text : "unknown";
	}

} // namespace

int main(int argc, char ** argv)
{
	constexpr rhi::BuildInfo build = rhi::GetBuildInfo();
	LOG_INFO(fw::Log(), "Azoth RHI {}", rhi::kVersionString);

	// Read, not listed. A backend added to the RHI or one of your own registered beside them, shows up here without this line changing.
	std::string available = "  backends:";
	for (const rhi::BackendEntry & backend : rhi::AvailableBackends())
	{
		available += std::format(" {}", rhi::ShortApiName(backend.canonicalName));
	}
	LOG_INFO(fw::Log(), "{}", available);
	LOG_INFO(fw::Log(), "  profiling: {}, tracy: {}\n", Yes(build.profilingEnabled), Yes(build.tracyEnabled));

	// Register everything this build contains, then let the runtime pick.
	const char * requested = fw::RequestedBackend(argc, argv);

	// Every backend this build has, registered, with the requested one first. What that set is was settled when the library was compiled so this sample never has
	// to ask.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	rhi::DeviceDesc desc{};
	desc.requireSwapchain = false; // headless so no surface is needed

	/*
	 * Every optional feature, preferred, not required. A device grants nothing the caller did not ask for, so a sample whose job is reporting what a device can do
	 * has to ask for everything or it reports a row of no.
	 *
	 * Preferred makes that a report and not a demand: each one is enabled where the adapter has it and skipped where it does not. That is also how a caller
	 * discovers what is available without a second query.
	 */
	static constexpr std::array reportable{
		rhi::DeviceFeature::eTimestampQueries,
		rhi::DeviceFeature::eSamplerAnisotropy,
		rhi::DeviceFeature::eIndependentBlend,
		rhi::DeviceFeature::eDepthBounds,
		rhi::DeviceFeature::ePipelineStatisticsQueries,
		rhi::DeviceFeature::eMultiDrawIndirect,
		rhi::DeviceFeature::eDrawIndirectFirstInstance,
		rhi::DeviceFeature::eShaderDrawParameters,
		rhi::DeviceFeature::eSparseResources,
		rhi::DeviceFeature::eSparseBuffers,
		rhi::DeviceFeature::eSparseTextures,
		rhi::DeviceFeature::eSparseVolumes,
		rhi::DeviceFeature::eTextureViewSwizzle,
		rhi::DeviceFeature::eMultiPlanarFormats,
		rhi::DeviceFeature::eSamplerYcbcrConversion,
	};
	desc.preferredFeatures = reportable;

	const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(desc);
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	const rhi::Device handle		 = device.Value().Get();
	const rhi::AdapterInfo & adapter = handle.GetAdapterInfo();
	const rhi::DeviceCaps & caps	 = handle.GetCaps();

	// The version reported, not the one asked for, which on a backend with more than one generation is the only way to tell which came up. Metal answers
	// 3 or 4 here.
	LOG_INFO(fw::Log(), "backend: {} {}.{}", handle.GetGraphicsApiName(), caps.apiVersion.major, caps.apiVersion.minor);
	LOG_INFO(fw::Log(), "adapter: {} ({})", OrUnknown(adapter.name), Describe(adapter.type));
	LOG_INFO(fw::Log(), "  vendor id:      0x{:x}", adapter.vendorId);
	LOG_INFO(fw::Log(), "  device id:      0x{:x}", adapter.deviceId);
	LOG_INFO(fw::Log(), "  driver:         {}", OrUnknown(adapter.driverVersion));
	LOG_INFO(fw::Log(), "  dedicated vram: {} MiB", adapter.dedicatedVideoMemoryBytes / (std::uint64_t{ 1024 } * 1024));
	LOG_INFO(fw::Log(), "  unified memory: {}", Yes(adapter.unifiedMemoryArchitecture));

	// What an importer matches on and not the PCI pair above, which two identical adapters in one machine share.
	LOG_INFO(fw::Log(), "  device uuid:    {}", rhi::FormatAdapterUuid(adapter.deviceUUID).data());
	LOG_INFO(fw::Log(), "  driver uuid:    {}", rhi::FormatAdapterUuid(adapter.driverUUID).data());
	LOG_INFO(fw::Log(), "  device luid:    {}", adapter.deviceLUIDValid ? rhi::FormatAdapterLuid(adapter.deviceLUID).data() : "none");

	LOG_INFO(fw::Log(), "\nqueues");
	LOG_INFO(fw::Log(), "  graphics: {}", caps.graphicsQueueCount);
	LOG_INFO(fw::Log(), "  compute:  {} (dedicated engine: {})", caps.computeQueueCount, Yes(caps.hasDedicatedComputeQueue));
	LOG_INFO(fw::Log(), "  copy:     {} (dedicated engine: {})", caps.copyQueueCount, Yes(caps.hasDedicatedTransferQueue));

	LOG_INFO(fw::Log(), "\ncapabilities");
	LOG_INFO(fw::Log(), "  timeline sync:        {}", Yes(caps.supportsTimelineSync));
	LOG_INFO(fw::Log(), "  dynamic rendering:    {}", Yes(caps.supportsDynamicRendering));
	LOG_INFO(fw::Log(), "  binding tier:         {}", Name(caps.bindingTier));
	LOG_INFO(fw::Log(), "  update after bind:    {}", Yes(caps.supportsUpdateAfterBind));
	LOG_INFO(fw::Log(), "  enhanced barriers:    {}", Yes(caps.supportsEnhancedBarriers));
	LOG_INFO(fw::Log(), "  placed resources:     {}", Yes(caps.supportsPlacedResources));
	LOG_INFO(fw::Log(), "  sparse tier:          {}", Name(caps.sparseTier));
	LOG_INFO(fw::Log(), "  conservative raster:  {}", Name(caps.conservativeRasterTier));
	LOG_INFO(fw::Log(), "  ray tracing:          {}", Yes(caps.supportsRayTracing));
	LOG_INFO(fw::Log(), "  multi draw indirect:  {}", Yes(caps.supportsMultiDrawIndirect));
	LOG_INFO(fw::Log(), "  timestamp queries:    {}", Yes(caps.supportsTimestampQueries));

	return 0;
}

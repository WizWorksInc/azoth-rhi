// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "shared/shapes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bench::native
{

#ifdef AZOTH_RHI_BENCH_VULKAN

	[[nodiscard]] bool PrepareVulkan(azo::rhi::Device device, const Workload & work);
	void ReleaseVulkan();
	[[nodiscard]] std::string_view VulkanGap(Kind kind);
	[[nodiscard]] bool RecordVulkan(Kind kind, azo::rhi::CommandList & list, const azo::rhi::NativeMutationDesc & mutation, const Workload & work,
		std::size_t commands, std::uint64_t & elapsed);

#endif

#ifdef AZOTH_RHI_BENCH_METAL

	[[nodiscard]] bool PrepareMetal(azo::rhi::Device device, const Workload & work);
	void ReleaseMetal();
	[[nodiscard]] std::string_view MetalGap(Kind kind);
	[[nodiscard]] bool MetalRecordsNothing(Kind kind);
	[[nodiscard]] bool RecordMetal(Kind kind, azo::rhi::CommandList & list, const azo::rhi::NativeMutationDesc & mutation, const Workload & work,
		std::size_t commands, std::uint64_t & elapsed);

#endif

#ifdef AZOTH_RHI_BENCH_D3D12

	[[nodiscard]] bool PrepareD3D12(azo::rhi::Device device, const Workload & work);
	[[nodiscard]] std::string_view D3D12Gap(Kind kind);
	[[nodiscard]] bool RecordD3D12(Kind kind, azo::rhi::CommandList & list, const azo::rhi::NativeMutationDesc & mutation, const Workload & work,
		std::size_t commands, std::uint64_t & elapsed);

#endif

}

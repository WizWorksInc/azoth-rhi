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

#include "native_delta/native/arms.hpp"

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "native_delta/native/backend_arms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rhi = azo::rhi;

namespace bench::native
{

	namespace
	{

		// Which of the arms below is answering, settled the once in Prepare. One device a run so this is what a caller would otherwise be passing back in.
		rhi::GraphicsApiId g_api{};

		bool g_ready = false;

	} // namespace

	bool Prepare([[maybe_unused]] rhi::Device device, [[maybe_unused]] const Workload & work)
	{
		g_api = device.GetGraphicsApiId();

#ifdef AZOTH_RHI_BENCH_VULKAN
		if (g_api == rhi::VulkanApi::id)
		{
			g_ready = PrepareVulkan(device, work);
			return g_ready;
		}
#endif

#ifdef AZOTH_RHI_BENCH_METAL
		if (rhi::IsMetalFamily(g_api))
		{
			g_ready = PrepareMetal(device, work);
			return g_ready;
		}
#endif

#ifdef AZOTH_RHI_BENCH_D3D12
		if (g_api == rhi::D3D12Api::id)
		{
			g_ready = PrepareD3D12(device, work);
			return g_ready;
		}
#endif

		return false;
	}

	void Release()
	{
		if (!g_ready)
		{
			return;
		}

#ifdef AZOTH_RHI_BENCH_VULKAN
		if (g_api == rhi::VulkanApi::id)
		{
			ReleaseVulkan();
		}
#endif

#ifdef AZOTH_RHI_BENCH_METAL
		if (rhi::IsMetalFamily(g_api))
		{
			ReleaseMetal();
		}
#endif

		g_ready = false;
	}

	std::string_view Gap([[maybe_unused]] const Kind kind)
	{
#ifdef AZOTH_RHI_BENCH_VULKAN
		if (g_api == rhi::VulkanApi::id)
		{
			return VulkanGap(kind);
		}
#endif

#ifdef AZOTH_RHI_BENCH_METAL
		if (rhi::IsMetalFamily(g_api))
		{
			return MetalGap(kind);
		}
#endif

#ifdef AZOTH_RHI_BENCH_D3D12
		if (g_api == rhi::D3D12Api::id)
		{
			return D3D12Gap(kind);
		}
#endif

		return "no native arm for this backend";
	}

	bool RecordsNothing([[maybe_unused]] const Kind kind)
	{
#ifdef AZOTH_RHI_BENCH_METAL
		if (rhi::IsMetalFamily(g_api))
		{
			return MetalRecordsNothing(kind);
		}
#endif

		return false;
	}

	bool Record([[maybe_unused]] const Kind kind, [[maybe_unused]] rhi::CommandList & list, [[maybe_unused]] const Workload & work,
		[[maybe_unused]] const std::size_t commands, std::uint64_t & elapsed)
	{
		elapsed = 0;

		// What the native recording touched. The target is left in the state the rendering scope has it in, which is where these commands leave it.
		const std::array touched{ rhi::NativeTouchedTexture{
			.texture	= work.target,
			.access		= rhi::NativeMutationAccess::eReadWrite,
			.range		= {},
			.finalState = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		} };
		const rhi::NativeMutationDesc mutation{ .textures = touched, .debugName = "bench.native" };

#ifdef AZOTH_RHI_BENCH_VULKAN
		if (g_api == rhi::VulkanApi::id)
		{
			return RecordVulkan(kind, list, mutation, work, commands, elapsed);
		}
#endif

#ifdef AZOTH_RHI_BENCH_METAL
		if (rhi::IsMetalFamily(g_api))
		{
			return RecordMetal(kind, list, mutation, work, commands, elapsed);
		}
#endif

#ifdef AZOTH_RHI_BENCH_D3D12
		if (g_api == rhi::D3D12Api::id)
		{
			return RecordD3D12(kind, list, mutation, work, commands, elapsed);
		}
#endif

		return false;
	}

} // namespace bench::native

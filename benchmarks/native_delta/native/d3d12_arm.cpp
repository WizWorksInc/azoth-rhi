// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/native/d3d12_native.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "native_delta/native/backend_arms.hpp"
#include "shared/options.hpp"

#include <d3d12.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rhi = azo::rhi;

namespace bench::native
{

	namespace
	{

		struct Arm final
		{
			ID3D12Resource * resource = nullptr;
		};

		Arm g_arm;

		[[nodiscard]] bool PrepareArm(rhi::Device device, const Workload & work)
		{
			rhi::NativeTexture<rhi::D3D12Api> texture{};
			rhi::Error error{};
			if (!device.GetNativeTexture<rhi::D3D12Api>(work.target, texture, error))
			{
				ReportError("failed to reach the ID3D12Resource behind the target", error);
				return false;
			}

			g_arm.resource = texture.resource;
			return g_arm.resource != nullptr;
		}

		[[nodiscard]] std::uint64_t RecordShape(const Kind kind, ID3D12GraphicsCommandList * commandList, const Workload & work, const std::size_t commands)
		{
			const D3D12_VIEWPORT viewport{
				.TopLeftX = work.viewport.x,
				.TopLeftY = work.viewport.y,
				.Width	  = work.viewport.width,
				.Height	  = work.viewport.height,
				.MinDepth = work.viewport.minDepth,
				.MaxDepth = work.viewport.maxDepth,
			};
			const D3D12_RECT scissor{
				.left	= work.scissor.x,
				.top	= work.scissor.y,
				.right	= work.scissor.x + static_cast<LONG>(work.scissor.width),
				.bottom = work.scissor.y + static_cast<LONG>(work.scissor.height),
			};

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type		  = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.Flags		  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.UAV.pResource = g_arm.resource;

			const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
			switch (kind)
			{
			case Kind::eSetViewport:
				for (std::size_t index = 0; index < commands; ++index)
				{
					commandList->RSSetViewports(1, &viewport);
				}
				break;

			case Kind::eSetScissor:
				for (std::size_t index = 0; index < commands; ++index)
				{
					commandList->RSSetScissorRects(1, &scissor);
				}
				break;

			case Kind::eDraw:
				for (std::size_t index = 0; index < commands; ++index)
				{
					commandList->DrawInstanced(3, 1, 0, 0);
				}
				break;

			case Kind::eDrawIndexed:
				for (std::size_t index = 0; index < commands; ++index)
				{
					commandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
				}
				break;

			case Kind::eBarrier:
				for (std::size_t index = 0; index < commands; ++index)
				{
					commandList->ResourceBarrier(1, &barrier);
				}
				break;

			case Kind::ePushConstants:
			case Kind::eBindDescriptorSet:
			case Kind::eSetGraphicsPipeline: break;
			}
			const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		}

	}

	bool PrepareD3D12(rhi::Device device, const Workload & work)
	{
		return PrepareArm(device, work);
	}

	std::string_view D3D12Gap(const Kind kind)
	{
		if (NeedsPipeline(kind))
		{
			return "no pipeline: this device takes compiled binaries and the only shader here is Metal source";
		}

		if (kind == Kind::ePushConstants || kind == Kind::eBindDescriptorSet)
		{
			return "the root parameter index the RHI's layout chose is not on the native surface";
		}

		return {};
	}

	bool RecordD3D12(const Kind kind, rhi::CommandList & list, const rhi::NativeMutationDesc & mutation, const Workload & work, const std::size_t commands,
		std::uint64_t & elapsed)
	{
		rhi::Error error{};
		const bool recorded = list.ModifyNative<rhi::D3D12Api>(
			mutation,
			[&](const rhi::native::D3D12CommandListView & view)
			{
				elapsed = RecordShape(kind, view.commandList, work, commands);
			},
			error);
		if (!recorded)
		{
			ReportError("the native mutation scope was refused", error);
		}

		return recorded;
	}

}

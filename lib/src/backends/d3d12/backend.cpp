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

#ifdef _WIN32

	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif

	#include "azoth/rhi/backend/dispatch.hpp"
	#include "azoth/rhi/backend/support/slot_map.hpp"
	#include "azoth/rhi/backend/table_validation.hpp"
	#include "azoth/rhi/native/d3d12_native.hpp"

	#include "backends/d3d12/internal.hpp"
	#include "backends/registration.hpp"
	#include "support/driver_version.hpp"

	#include <d3d12.h>
	#include <D3D12MemAlloc.h>
	#include <dxgi1_6.h>
	#include <wrl/client.h>

	#if defined(AZOTH_RHI_ENABLE_PIX)
		// d3d12.h above defines __d3d12_h__, which unlocks pix3.h's command-list and queue event overloads.
		#include <pix3.h>
	#endif

	#include <algorithm>
	#include <array>
	#include <cstdint>
	#include <limits>
	#include <memory>
	#include <span>
	#include <string>
	#include <string_view>
	#include <thread>
	#include <tuple>
	#include <type_traits>
	#include <utility>
	#include <vector>

namespace azo::rhi
{

	Result<D3D12NativeDevice> GetD3D12NativeDevice(Device device)
	{
		if (device.GetGraphicsApiId() != D3D12Api::id)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetD3D12NativeDevice called on a non-D3D12 device",
			};
		}

		auto * impl = static_cast<d3d12::D3D12Device *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(device), d3d12::CoreDeviceBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetD3D12NativeDevice reached something other than a D3D12 device behind the facade",
			};
		}

		const auto firstQueue = [](const detail::HostVector<d3d12::D3D12Queue> & pool) noexcept -> ID3D12CommandQueue *
		{
			return pool.empty() ? nullptr : pool.front().queue.Get();
		};
		return D3D12NativeDevice{ .device = impl->device.Get(),
			.adapter					  = impl->adapter.Get(),
			.factory					  = impl->factory.Get(),
			.graphicsQueue				  = firstQueue(impl->graphicsQueues),
			.computeQueue				  = firstQueue(impl->computeQueues),
			.copyQueue					  = firstQueue(impl->copyQueues),
			.allocator					  = impl->allocator.Get() };
	}

	Result<D3D12NativeSwapchain> GetD3D12NativeSwapchain(Swapchain swapchain)
	{
		auto * impl = static_cast<d3d12::D3D12Swapchain *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(swapchain), d3d12::SwapchainBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetD3D12NativeSwapchain called on a swapchain that is not a D3D12 one",
			};
		}

		return D3D12NativeSwapchain{
			.swapchain = impl->swapchain.Get(), .format = impl->format, .width = impl->width, .height = impl->height, .imageCount = impl->imageCount
		};
	}

	ID3D12GraphicsCommandList * GetD3D12CommandList(CommandList commandList)
	{
		const auto * impl =
			static_cast<d3d12::D3D12CommandList *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandList), d3d12::RenderCommandBlock()));
		return impl != nullptr ? impl->list.Get() : nullptr;
	}

	ID3D12CommandAllocator * GetD3D12CommandAllocator(CommandPool commandPool)
	{
		const auto * impl = static_cast<d3d12::D3D12CommandPool *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandPool), d3d12::CommandPoolBlock()));
		return impl != nullptr ? impl->allocator.Get() : nullptr;
	}

	Result<void> RegisterD3D12Backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName				   = D3D12Api::canonicalName;
		info.info.displayName				   = D3D12Api::displayName;
		info.info.apiVersionMajor			   = 12;
		info.info.supportsSurfaces			   = true;
		info.info.supportsDebugMarkers		   = true;
		info.info.supportsExternalNativeAccess = true;
		info.createInstance					   = &d3d12::D3D12CreateInstance;
		return registry.Register<D3D12Api>(info);
	}

	template <>
	Result<UniqueDevice> CreateDevice<D3D12Api>(const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		Error error{};
		auto instance = d3d12::BuildInstance(InstanceDescForDevice(desc), &error);
		if (!instance)
		{
			return error;
		}

		d3d12::D3D12Instance * instanceRaw = instance.get();
		d3d12::D3D12Device * device		   = d3d12::MakeOwnedDevice(instanceRaw, std::move(instance), desc, &error);
		if (device == nullptr)
		{
			return error;
		}

		void * deviceImpl		 = device;
		BackendBlockSet * blocks = detail::ResolveDeviceBlocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueDevice(deviceImpl, blocks);
	}

	namespace native
	{
		D3D12CommandListView NativeAccess<D3D12Api>::MakeCommandListView(void * commandListImpl) noexcept
		{
			return D3D12CommandListView{ .commandList = static_cast<d3d12::D3D12CommandList *>(commandListImpl)->list.Get() };
		}
	} // namespace native

} // namespace azo::rhi

#endif // _WIN32

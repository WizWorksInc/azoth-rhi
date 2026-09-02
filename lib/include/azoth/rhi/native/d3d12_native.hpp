// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;
struct ID3D12Resource;
struct ID3D12Fence;
struct IDXGIAdapter4;
struct IDXGIFactory6;
struct IDXGISwapChain3;

namespace D3D12MA
{
	class Allocator;
}

namespace azo::rhi
{

	template <>
	struct NativeBuffer<D3D12Api> final
	{
		ID3D12Resource * resource = nullptr;
	};

	template <>
	struct NativeTexture<D3D12Api> final
	{
		ID3D12Resource * resource = nullptr;
	};

	struct D3D12NativeDevice final
	{
		ID3D12Device * device			   = nullptr;
		IDXGIAdapter4 * adapter			   = nullptr;
		IDXGIFactory6 * factory			   = nullptr;
		ID3D12CommandQueue * graphicsQueue = nullptr;
		ID3D12CommandQueue * computeQueue  = nullptr;
		ID3D12CommandQueue * copyQueue	   = nullptr;
		D3D12MA::Allocator * allocator	   = nullptr;
	};

	[[nodiscard]] AZO_RHI_API Result<D3D12NativeDevice> GetD3D12NativeDevice(Device device);

	struct D3D12NativeSwapchain final
	{
		IDXGISwapChain3 * swapchain = nullptr;
		Format format				= Format::eUndefined;
		std::uint32_t width			= 0;
		std::uint32_t height		= 0;
		std::uint32_t imageCount	= 0;
	};

	[[nodiscard]] AZO_RHI_API Result<D3D12NativeSwapchain> GetD3D12NativeSwapchain(Swapchain swapchain);

	[[nodiscard]] AZO_RHI_API ID3D12GraphicsCommandList * GetD3D12CommandList(CommandList commandList);

	[[nodiscard]] AZO_RHI_API ID3D12CommandAllocator * GetD3D12CommandAllocator(CommandPool commandPool);

	template <>
	struct NativeTimeline<D3D12Api> final
	{
		ID3D12Fence * fence = nullptr;
	};

	template <>
	struct NativeBinarySemaphore<D3D12Api> final
	{
		ID3D12Fence * fence = nullptr;
	};

}

namespace azo::rhi::native
{

	struct D3D12QueueView final
	{
		ID3D12CommandQueue * queue = nullptr;
	};

	struct D3D12CommandListView final
	{
		ID3D12GraphicsCommandList * commandList = nullptr;
	};

	template <>
	struct NativeAccess<D3D12Api> final
	{
		using QueueView		  = D3D12QueueView;
		using CommandListView = D3D12CommandListView;

		[[nodiscard]] static AZO_RHI_API D3D12CommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

}

namespace azo::rhi
{

	[[nodiscard]] AZO_RHI_API Result<native::D3D12QueueView> GetD3D12QueueView(Queue queue);

}

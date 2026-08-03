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
 * \brief Direct3D 12 native interop payloads and accessors.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

// Forward declarations of native Direct3D 12 and DXGI interfaces.
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
} // namespace D3D12MA

namespace azo::rhi
{

	/**
	 * \brief Native D3D12 buffer import or export payload.
	 *
	 * On import, resource is AddRef'd by the RHI slot for the imported handle lifetime.
	 */
	template <>
	struct NativeBuffer<D3D12Api> final
	{
		ID3D12Resource * resource = nullptr;
	};

	/**
	 * \brief Native D3D12 texture import or export payload.
	 *
	 * On import, resource is AddRef'd by the RHI slot for the imported handle lifetime.
	 */
	template <>
	struct NativeTexture<D3D12Api> final
	{
		ID3D12Resource * resource = nullptr;
	};

	/**
	 * \brief Borrowed native objects owned by a D3D12-backed RHI device.
	 *
	 * The pointers are valid while the owning UniqueDevice is alive. Do not Release them. The swapchain is exposed separately.
	 *
	 * \attention These pointers are unchecked native access. Keeping one past the device that returned it is undefined.
	 */
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

	/**
	 * \brief Returns native handles from a D3D12-backed device.
	 *
	 * Returns eUnsupportedApi when device is not backed by D3D12.
	 */
	[[nodiscard]] AZO_RHI_API Result<D3D12NativeDevice> GetD3D12NativeDevice(Device device);

	/**
	 * \brief Borrowed native objects owned by a D3D12-backed RHI swapchain.
	 *
	 * The pointers are valid until swapchain resize, swapchain destruction, or device teardown. Do not Release them.
	 */
	struct D3D12NativeSwapchain final
	{
		IDXGISwapChain3 * swapchain = nullptr;
		Format format				= Format::eUndefined;
		std::uint32_t width			= 0;
		std::uint32_t height		= 0;
		std::uint32_t imageCount	= 0;
	};

	/**
	 * \brief Returns native handles from a D3D12-backed swapchain.
	 *
	 * Returns eUnsupportedApi when swapchain was not created by the D3D12 backend.
	 */
	[[nodiscard]] AZO_RHI_API Result<D3D12NativeSwapchain> GetD3D12NativeSwapchain(Swapchain swapchain);

	/**
	 * \brief Returns the native D3D12 command list for an RHI command list, or null when unavailable.
	 */
	[[nodiscard]] ID3D12GraphicsCommandList * GetD3D12CommandList(CommandList commandList);

	/**
	 * \brief Returns the native D3D12 command allocator for an RHI command pool, or null when unavailable.
	 */
	[[nodiscard]] ID3D12CommandAllocator * GetD3D12CommandAllocator(CommandPool commandPool);

	/**
	 * \brief The ID3D12Fence an adopted timeline stands for.
	 */
	template <>
	struct NativeTimeline<D3D12Api> final
	{
		ID3D12Fence * fence = nullptr;
	};

	/**
	 * \brief The ID3D12Fence behind a binary semaphore, readable but not adoptable.
	 *
	 * This backend models a binary semaphore as a fence plus two counters it advances itself, and those cannot be recovered from a fence another component has
	 * been signalling, so the adopt direction refuses and only the read direction works.
	 */
	template <>
	struct NativeBinarySemaphore<D3D12Api> final
	{
		ID3D12Fence * fence = nullptr;
	};

} // namespace azo::rhi

namespace azo::rhi::native
{

	/**
	 * \brief Borrowed Direct3D 12 objects backing an RHI device, for the native access surface.
	 */
	struct D3D12DeviceView final
	{
		ID3D12Device * device = nullptr;
	};

	/**
	 * \brief Borrowed Direct3D 12 queue backing an RHI queue.
	 */
	struct D3D12QueueView final
	{
		ID3D12CommandQueue * queue = nullptr;
	};

	/**
	 * \brief Borrowed Direct3D 12 command list backing an RHI command list, which is what a ModifyNative scope records into.
	 */
	struct D3D12CommandListView final
	{
		ID3D12GraphicsCommandList * commandList = nullptr;
	};

	/**
	 * \brief Borrowed Direct3D 12 resource backing an RHI buffer.
	 */
	struct D3D12BufferView final
	{
		ID3D12Resource * resource = nullptr;
	};

	/**
	 * \brief Borrowed Direct3D 12 resource backing an RHI texture.
	 */
	struct D3D12TextureView final
	{
		ID3D12Resource * resource = nullptr;
	};

	/**
	 * \brief Native access surface for the Direct3D 12 backend, the other half of the gap the Vulkan specialization closed.
	 */
	template <>
	struct NativeAccess<D3D12Api> final
	{
		using DeviceView	  = D3D12DeviceView;
		using QueueView		  = D3D12QueueView;
		using CommandListView = D3D12CommandListView;
		using BufferView	  = D3D12BufferView;
		using TextureView	  = D3D12TextureView;

		/**
		 * \brief Builds a command-list native view from the backend's concrete command-list object.
		 */
		[[nodiscard]] static D3D12CommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

} // namespace azo::rhi::native

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
 * \brief Metal native interop payloads and accessors.
 */

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"

// Forward declarations of metal-cpp types.
namespace MTL
{
	class Device;
	class CommandQueue;
	class Buffer;
	class Texture;
	class CommandBuffer;
	class RenderCommandEncoder;
	class SamplerState;
	class SharedEvent;
} // namespace MTL

// The Metal 4 command objects, which are unrelated types to the ones above, not versions of them.
namespace MTL4
{
	class CommandQueue;
	class CommandBuffer;
	class RenderCommandEncoder;
	class ComputeCommandEncoder;
	class ArgumentTable;
} // namespace MTL4

namespace azo::rhi::native
{

	/**
	 * \brief Borrowed Metal queue backing an RHI queue.
	 */
	struct MetalQueueView final
	{
		MTL::CommandQueue * queue = nullptr;
	};

	/**
	 * \brief Borrowed Metal command buffer backing an RHI command list.
	 */
	struct MetalCommandListView final
	{
		MTL::CommandBuffer * commandBuffer = nullptr;
	};

	/**
	 * \brief Native access surface for the Metal 3 backend.
	 */
	template <>
	struct NativeAccess<MetalApi> final
	{
		using QueueView		  = MetalQueueView;
		using CommandListView = MetalCommandListView;

		/**
		 * \brief Builds a command-list native view from the backend's concrete command-list object.
		 */
		[[nodiscard]] static AZO_RHI_API MetalCommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

	/**
	 * \brief Borrowed Metal 4 queue backing an RHI queue.
	 *
	 * MTL4CommandQueue shares no base with MTLCommandQueue, which is why this is its own type and not the other generation's.
	 */
	struct Metal4QueueView final
	{
		MTL4::CommandQueue * queue = nullptr;
	};

	/**
	 * \brief Borrowed Metal 4 command buffer backing an RHI command list.
	 */
	struct Metal4CommandListView final
	{
		MTL4::CommandBuffer * commandBuffer = nullptr;
	};

	/**
	 * \brief Native access surface for the Metal 4 backend, whose command side is a Metal 4 type of its own throughout.
	 */
	template <>
	struct NativeAccess<Metal4Api> final
	{
		using QueueView		  = Metal4QueueView;
		using CommandListView = Metal4CommandListView;

		/**
		 * \brief Builds a command-list native view from the backend's concrete command-list object.
		 */
		[[nodiscard]] static AZO_RHI_API Metal4CommandListView MakeCommandListView(void * commandListImpl) noexcept;
	};

} // namespace azo::rhi::native

namespace azo::rhi
{

	class CommandList;

	/**
	 * \brief Native Metal buffer import or export payload.
	 */
	template <>
	struct NativeBuffer<MetalApi> final
	{
		MTL::Buffer * buffer = nullptr;
	};

	/**
	 * \brief Native Metal texture import or export payload.
	 */
	template <>
	struct NativeTexture<MetalApi> final
	{
		MTL::Texture * texture = nullptr;
	};

	/**
	 * \brief Borrowed native objects owned by a Metal-backed RHI device.
	 *
	 * The pointers are valid while the owning UniqueDevice is alive. Do not release them.
	 *
	 * \attention These pointers are unchecked native access. Keeping one past the device that returned it is undefined.
	 */
	struct MetalNativeDevice final
	{
		MTL::Device * device	  = nullptr;
		MTL::CommandQueue * queue = nullptr;
	};

	/**
	 * \brief Returns native handles from a Metal-backed device.
	 *
	 * Returns eUnsupportedApi when device is not backed by Metal 3. A Metal 4 device answers GetMetal4NativeDevice instead, its queue being an unrelated type
	 * and not a newer one.
	 */
	[[nodiscard]] AZO_RHI_API Result<MetalNativeDevice> GetMetalNativeDevice(Device device);

	/**
	 * \brief Returns the MTLCommandQueue behind an RHI queue.
	 *
	 * The device accessor above reports only the graphics queue, so this is the one way to reach the compute and copy ones. Returns eUnsupportedApi when the
	 * queue is not backed by Metal 3.
	 */
	[[nodiscard]] AZO_RHI_API Result<native::MetalQueueView> GetMetalQueueView(Queue queue);

	/**
	 * \brief Borrowed native objects owned by a Metal 4 backed RHI device.
	 *
	 * \attention These pointers are unchecked native access. Keeping one past the device that returned it is undefined.
	 */
	struct Metal4NativeDevice final
	{
		MTL::Device * device	   = nullptr;
		MTL4::CommandQueue * queue = nullptr;
	};

	/**
	 * \brief Returns native handles from a Metal 4 backed device.
	 *
	 * Returns eUnsupportedApi when device is not backed by Metal 4.
	 */
	[[nodiscard]] AZO_RHI_API Result<Metal4NativeDevice> GetMetal4NativeDevice(Device device);

	/**
	 * \brief Returns the MTL4CommandQueue behind an RHI queue, on the same terms as the Metal 3 accessor.
	 *
	 * Returns eUnsupportedApi when the queue is not backed by Metal 4. A Metal 3 queue answers GetMetalQueueView, the two queue objects sharing no base.
	 */
	[[nodiscard]] AZO_RHI_API Result<native::Metal4QueueView> GetMetal4QueueView(Queue queue);

	/**
	 * \brief Returns the native Metal command buffer backing an RHI command list, or null when unavailable.
	 */
	[[nodiscard]] AZO_RHI_API MTL::CommandBuffer * GetMetalCommandBuffer(CommandList commandList);

	/**
	 * \brief Returns the active Metal render encoder for a command list inside a render pass.
	 *
	 * Returns null when the command list is not inside an active Metal render encoding scope.
	 */
	[[nodiscard]] AZO_RHI_API MTL::RenderCommandEncoder * GetMetalRenderCommandEncoder(CommandList commandList);

	/**
	 * \brief The Metal 4 command buffer behind a command list, on a list from a Metal 4 device.
	 *
	 * Null on a Metal 3 list, where GetMetalCommandBuffer is the one that answers. The two never both do: a device records through one backend's objects and an
	 * MTL4CommandBuffer is not an MTLCommandBuffer. Device::GetGraphicsApiId says which to ask for, and asking the wrong one answers null instead of lying.
	 */
	[[nodiscard]] AZO_RHI_API MTL4::CommandBuffer * GetMetal4CommandBuffer(CommandList commandList);

	/**
	 * \brief The active Metal 4 render encoder for a command list inside a rendering scope.
	 *
	 * Null outside a scope, and null on a Metal 3 device.
	 */
	[[nodiscard]] AZO_RHI_API MTL4::RenderCommandEncoder * GetMetal4RenderCommandEncoder(CommandList commandList);

	/**
	 * \brief The active Metal 4 compute encoder, which on this generation also carries copies and fills.
	 *
	 * Null when no compute scope is open, and null on a Metal 3 device.
	 */
	[[nodiscard]] AZO_RHI_API MTL4::ComputeCommandEncoder * GetMetal4ComputeCommandEncoder(CommandList commandList);

	/**
	 * \brief The argument table a command list binds through on Metal 4.
	 *
	 * Where Metal 3 binds by calling setBuffer and setTexture on an encoder, this generation writes a table the encoder holds. Anything recording native
	 * commands against a Metal 4 list needs it to bind at all, which is why it is reachable and not private.
	 *
	 * Null on a Metal 3 device.
	 */
	[[nodiscard]] AZO_RHI_API MTL4::ArgumentTable * GetMetal4ArgumentTable(CommandList commandList);

	/**
	 * \brief The MTLTexture an adopted texture view stands for, a Metal view over another texture's storage being a texture itself.
	 */
	template <>
	struct NativeTextureView<MetalApi> final
	{
		MTL::Texture * texture = nullptr;
	};

	/**
	 * \brief The MTLSamplerState an adopted sampler stands for.
	 */
	template <>
	struct NativeSampler<MetalApi> final
	{
		MTL::SamplerState * sampler = nullptr;
	};

	/**
	 * \brief The MTLSharedEvent an adopted timeline stands for.
	 */
	template <>
	struct NativeTimeline<MetalApi> final
	{
		MTL::SharedEvent * event = nullptr;
	};

	/**
	 * \brief The MTLSharedEvent an adopted binary semaphore stands for, this backend having one object behind both kinds.
	 */
	template <>
	struct NativeBinarySemaphore<MetalApi> final
	{
		MTL::SharedEvent * event = nullptr;
	};

	/*
	 * The Metal 4 adoption payloads, which are the same objects under a second tag.
	 *
	 * A buffer, a texture, a view, a sampler and an event did not change with the generation, and saying so with a specialization each is what lets an object
	 * cross between the two backends: export from a Metal 3 device, adopt into a Metal 4 one, and neither has to know the other exists.
	 */

	template <>
	struct NativeBuffer<Metal4Api> final
	{
		MTL::Buffer * buffer = nullptr;
	};

	template <>
	struct NativeTexture<Metal4Api> final
	{
		MTL::Texture * texture = nullptr;
	};

	template <>
	struct NativeTextureView<Metal4Api> final
	{
		MTL::Texture * texture = nullptr;
	};

	template <>
	struct NativeSampler<Metal4Api> final
	{
		MTL::SamplerState * sampler = nullptr;
	};

	template <>
	struct NativeTimeline<Metal4Api> final
	{
		MTL::SharedEvent * event = nullptr;
	};

	template <>
	struct NativeBinarySemaphore<Metal4Api> final
	{
		MTL::SharedEvent * event = nullptr;
	};

} // namespace azo::rhi

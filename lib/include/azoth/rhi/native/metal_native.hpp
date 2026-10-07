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

#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/native_access.hpp"

namespace MTL // NOLINT(readability-identifier-naming)
{
	class Device;
	class CommandQueue;
	class Buffer;
	class Texture;
	class CommandBuffer;
	class RenderCommandEncoder;
	class SamplerState;
	class SharedEvent;
}

namespace MTL4 // NOLINT(readability-identifier-naming)
{
	class CommandQueue;
	class CommandBuffer;
	class RenderCommandEncoder;
	class ComputeCommandEncoder;
	class ArgumentTable;
}

namespace azo::rhi::native
{

	struct MetalQueueView final
	{
		MTL::CommandQueue * queue = nullptr;
	};

	struct MetalCommandListView final
	{
		MTL::CommandBuffer * commandBuffer = nullptr;
	};

	template <>
	struct NativeAccess<MetalApi> final
	{
		using QueueView		  = MetalQueueView;
		using CommandListView = MetalCommandListView;

		[[nodiscard]] static AZO_RHI_API MetalCommandListView make_command_list_view(void * commandListImpl) noexcept;
	};

	struct Metal4QueueView final
	{
		MTL4::CommandQueue * queue = nullptr;
	};

	struct Metal4CommandListView final
	{
		MTL4::CommandBuffer * commandBuffer = nullptr;
	};

	template <>
	struct NativeAccess<Metal4Api> final
	{
		using QueueView		  = Metal4QueueView;
		using CommandListView = Metal4CommandListView;

		[[nodiscard]] static AZO_RHI_API Metal4CommandListView make_command_list_view(void * commandListImpl) noexcept;
	};

}

namespace azo::rhi
{

	class CommandList;

	template <>
	struct NativeBuffer<MetalApi> final
	{
		MTL::Buffer * buffer = nullptr;
	};

	template <>
	struct NativeTexture<MetalApi> final
	{
		MTL::Texture * texture = nullptr;
	};

	struct MetalNativeDevice final
	{
		MTL::Device * device	  = nullptr;
		MTL::CommandQueue * queue = nullptr;
	};

	[[nodiscard]] AZO_RHI_API Result<MetalNativeDevice> get_metal_native_device(Device device);

	[[nodiscard]] AZO_RHI_API Result<native::MetalQueueView> get_metal_queue_view(Queue queue);

	struct Metal4NativeDevice final
	{
		MTL::Device * device	   = nullptr;
		MTL4::CommandQueue * queue = nullptr;
	};

	[[nodiscard]] AZO_RHI_API Result<Metal4NativeDevice> get_metal4_native_device(Device device);

	[[nodiscard]] AZO_RHI_API Result<native::Metal4QueueView> get_metal4_queue_view(Queue queue);

	[[nodiscard]] AZO_RHI_API MTL::CommandBuffer * get_metal_command_buffer(CommandList commandList);

	[[nodiscard]] AZO_RHI_API MTL::RenderCommandEncoder * get_metal_render_command_encoder(CommandList commandList);

	[[nodiscard]] AZO_RHI_API MTL4::CommandBuffer * get_metal4_command_buffer(CommandList commandList);

	[[nodiscard]] AZO_RHI_API MTL4::RenderCommandEncoder * get_metal4_render_command_encoder(CommandList commandList);

	[[nodiscard]] AZO_RHI_API MTL4::ComputeCommandEncoder * get_metal4_compute_command_encoder(CommandList commandList);

	[[nodiscard]] AZO_RHI_API MTL4::ArgumentTable * get_metal4_argument_table(CommandList commandList);

	template <>
	struct NativeTextureView<MetalApi> final
	{
		MTL::Texture * texture = nullptr;
	};

	template <>
	struct NativeSampler<MetalApi> final
	{
		MTL::SamplerState * sampler = nullptr;
	};

	template <>
	struct NativeTimeline<MetalApi> final
	{
		MTL::SharedEvent * event = nullptr;
	};

	template <>
	struct NativeBinarySemaphore<MetalApi> final
	{
		MTL::SharedEvent * event = nullptr;
	};

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

}

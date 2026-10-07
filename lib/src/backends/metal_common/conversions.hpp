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

#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <tuple>
#include <utility>

namespace azo::rhi::metal_common
{

	inline constexpr std::uint32_t kMetalMaxBufferArguments	 = 31;
	inline constexpr std::uint32_t kMetalMaxTextureArguments = 128;
	inline constexpr std::uint32_t kMetalMaxSamplerArguments = 16;

	inline constexpr std::uint32_t kMetalPushConstantIndex = 0;
	inline constexpr std::uint32_t kMetalVertexBufferBase  = 16;

	bool succeed(Error * error) noexcept;
	bool fail(Error * error, ErrorCode code, const char * message) noexcept;
	bool metal_wait_for_event(MTL::SharedEvent * event, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;

	template <typename... Args>
	[[nodiscard]] Error * last_error(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	template <typename T, typename... Args>
	[[nodiscard]] T * output_before_error(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 1);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 2>(tuple);
	}

	template <typename T>
	[[nodiscard]] T return_value(T value, Error * error) noexcept
	{
		succeed(error);
		return value;
	}

	template <typename T>
	[[nodiscard]] T fail_value(Error * error, ErrorCode code, const char * message) noexcept
	{
		fail(error, code, message);
		return {};
	}

	template <typename T>
	[[nodiscard]] bool store(T * out, T value, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
		}

		*out = std::move(value);
		return succeed(error);
	}

	template <typename... Args>
	bool noop_void([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return succeed(last_error(args...));
	}

	template <typename T, typename... Args>
	bool default_value([[maybe_unused]] void * impl, Args... args) noexcept
	{
		T * out = output_before_error<T>(args...);
		if (out == nullptr)
		{
			return fail(last_error(args...), ErrorCode::eInvalidArgument, "operation called with a null output");
		}

		return store(out, T{}, last_error(args...));
	}

	template <typename HandleT>
	[[nodiscard]] constexpr HandleT typed(const RawHandle handle) noexcept
	{
		return HandleT{ .index = handle.index, .generation = handle.generation };
	}

	[[nodiscard]] MTL::PixelFormat metal_pixel_format(Format format) noexcept;

	[[nodiscard]] MTL::VertexFormat metal_vertex_format(Format format) noexcept;

	[[nodiscard]] constexpr bool is_stencil_format(const Format format) noexcept
	{
		return format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
	}

	[[nodiscard]] constexpr bool is_compressed_format(const Format format) noexcept
	{
		return detail::is_compressed_format(format);
	}

	[[nodiscard]] constexpr bool is_integer_format(const Format format) noexcept
	{
		return detail::is_integer_format(format);
	}

	[[nodiscard]] constexpr bool is_blendable_format(const Format format) noexcept
	{
		return !is_depth_format(format) && !is_compressed_format(format) && !is_integer_format(format);
	}

	[[nodiscard]] constexpr bool is_color_renderable_format(const Format format) noexcept
	{
		return !is_depth_format(format) && !is_compressed_format(format);
	}

	[[nodiscard]] bool metal_refuse_unblendable_attachment(Format format, Error * error);

	[[nodiscard]] bool metal_refuse_unrenderable_attachment(Format format, Error * error);

	[[nodiscard]] MTL::ResourceOptions metal_buffer_storage(MemoryUsage usage) noexcept;
	[[nodiscard]] MTL::StorageMode metal_heap_storage(HeapType type) noexcept;
	[[nodiscard]] MTL::ResourceOptions metal_resource_options(MTL::StorageMode mode) noexcept;

	[[nodiscard]] MTL::TextureType metal_view_type(TextureViewType type) noexcept;
	[[nodiscard]] MTL::TextureSwizzleChannels metal_swizzle_channels(ComponentMapping mapping) noexcept;
	[[nodiscard]] MTL::SamplerMinMagFilter metal_min_mag_filter(Filter filter) noexcept;
	[[nodiscard]] MTL::SamplerMipFilter metal_mip_filter(MipmapMode mode) noexcept;
	[[nodiscard]] MTL::SamplerAddressMode metal_address_mode(AddressMode mode) noexcept;
	[[nodiscard]] MTL::CompareFunction metal_compare_function(CompareOp op) noexcept;
	[[nodiscard]] MTL::StencilOperation metal_stencil_op(StencilOp op) noexcept;
	[[nodiscard]] MTL::SamplerBorderColor metal_border_color(BorderColor color) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::TextureDescriptor> build_texture_descriptor(const TextureDesc & desc, Error * error) noexcept;

	[[nodiscard]] bool view_range_fits_texture(const MTL::Texture * texture, const TextureSubresourceRange & range, Error * error) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::SamplerDescriptor> build_sampler_descriptor(const SamplerDesc & desc) noexcept;

	[[nodiscard]] MTL::PrimitiveType metal_primitive_type(PrimitiveTopology topology) noexcept;
	[[nodiscard]] MTL::CullMode metal_cull_mode(CullMode mode) noexcept;
	[[nodiscard]] MTL::Winding metal_winding(FrontFace face) noexcept;
	[[nodiscard]] MTL::TriangleFillMode metal_fill_mode(FillMode mode) noexcept;
	[[nodiscard]] MTL::BlendFactor metal_blend_factor(BlendFactor factor) noexcept;
	[[nodiscard]] MTL::BlendOperation metal_blend_op(BlendOp op) noexcept;
	[[nodiscard]] MTL::ColorWriteMask metal_color_write_mask(Flags<ColorWrite> mask) noexcept;
	[[nodiscard]] MTL::LoadAction metal_load_action(LoadOp op) noexcept;
	[[nodiscard]] MTL::StoreAction metal_store_action(StoreOp op) noexcept;
	[[nodiscard]] MTL::FunctionType metal_function_type(ShaderStage stage) noexcept;
	[[nodiscard]] MTL::IndexType metal_index_type(bool index32) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::Library> metal_compile_library(MTL::Device * device, const ShaderBinary & shader, Error * error);

	[[nodiscard]] NS::SharedPtr<MTL::Function> compile_function(MTL::Device * device, const ShaderBinary & shader, Error * error);

	[[nodiscard]] bool metal_refuse_unbuildable_graphics_stage(ShaderStage stage, Error * error);

	[[nodiscard]] NS::SharedPtr<MTL::DepthStencilState> build_depth_stencil_state(MTL::Device * device, const DepthStencilStateDesc & desc);

	void set_metal_label(MTL::Resource * resource, CString debugName) noexcept;

}

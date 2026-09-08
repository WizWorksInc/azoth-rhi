// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	bool Succeed(Error * error) noexcept;
	bool Fail(Error * error, ErrorCode code, const char * message) noexcept;

	template <typename... Args>
	[[nodiscard]] Error * LastError(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	template <typename T, typename... Args>
	[[nodiscard]] T * OutputBeforeError(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 1);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 2>(tuple);
	}

	template <typename T>
	[[nodiscard]] T ReturnValue(T value, Error * error) noexcept
	{
		Succeed(error);
		return value;
	}

	template <typename T>
	[[nodiscard]] T FailValue(Error * error, ErrorCode code, const char * message) noexcept
	{
		Fail(error, code, message);
		return {};
	}

	template <typename T>
	[[nodiscard]] bool Store(T * out, T value, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
		}

		*out = std::move(value);
		return Succeed(error);
	}

	template <typename... Args>
	bool NoopVoid([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return Succeed(LastError(args...));
	}

	template <typename T, typename... Args>
	bool DefaultValue([[maybe_unused]] void * impl, Args... args) noexcept
	{
		T * out = OutputBeforeError<T>(args...);
		if (out == nullptr)
		{
			return Fail(LastError(args...), ErrorCode::eInvalidArgument, "operation called with a null output");
		}

		return Store(out, T{}, LastError(args...));
	}

	template <typename HandleT>
	[[nodiscard]] constexpr HandleT Typed(const RawHandle handle) noexcept
	{
		return HandleT{ .index = handle.index, .generation = handle.generation };
	}

	[[nodiscard]] MTL::PixelFormat MetalPixelFormat(Format format) noexcept;

	[[nodiscard]] MTL::VertexFormat MetalVertexFormat(Format format) noexcept;

	[[nodiscard]] constexpr bool IsStencilFormat(const Format format) noexcept
	{
		return format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
	}

	[[nodiscard]] constexpr bool IsCompressedFormat(const Format format) noexcept
	{
		return detail::IsCompressedFormat(format);
	}

	[[nodiscard]] constexpr bool IsIntegerFormat(const Format format) noexcept
	{
		return detail::IsIntegerFormat(format);
	}

	[[nodiscard]] constexpr bool IsBlendableFormat(const Format format) noexcept
	{
		return !IsDepthFormat(format) && !IsCompressedFormat(format) && !IsIntegerFormat(format);
	}

	[[nodiscard]] constexpr bool IsColorRenderableFormat(const Format format) noexcept
	{
		return !IsDepthFormat(format) && !IsCompressedFormat(format);
	}

	[[nodiscard]] bool MetalRefuseUnblendableAttachment(Format format, Error * error);

	[[nodiscard]] bool MetalRefuseUnrenderableAttachment(Format format, Error * error);

	[[nodiscard]] MTL::ResourceOptions MetalBufferStorage(MemoryUsage usage) noexcept;
	[[nodiscard]] MTL::StorageMode MetalHeapStorage(HeapType type) noexcept;
	[[nodiscard]] MTL::ResourceOptions MetalResourceOptions(MTL::StorageMode mode) noexcept;

	[[nodiscard]] MTL::TextureType MetalViewType(TextureViewType type) noexcept;
	[[nodiscard]] MTL::TextureSwizzleChannels MetalSwizzleChannels(ComponentMapping mapping) noexcept;
	[[nodiscard]] MTL::SamplerMinMagFilter MetalMinMagFilter(Filter filter) noexcept;
	[[nodiscard]] MTL::SamplerMipFilter MetalMipFilter(MipmapMode mode) noexcept;
	[[nodiscard]] MTL::SamplerAddressMode MetalAddressMode(AddressMode mode) noexcept;
	[[nodiscard]] MTL::CompareFunction MetalCompareFunction(CompareOp op) noexcept;
	[[nodiscard]] MTL::StencilOperation MetalStencilOp(StencilOp op) noexcept;
	[[nodiscard]] MTL::SamplerBorderColor MetalBorderColor(BorderColor color) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::TextureDescriptor> BuildTextureDescriptor(const TextureDesc & desc, Error * error) noexcept;

	[[nodiscard]] bool ViewRangeFitsTexture(const MTL::Texture * texture, const TextureSubresourceRange & range, Error * error) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::SamplerDescriptor> BuildSamplerDescriptor(const SamplerDesc & desc) noexcept;

	[[nodiscard]] MTL::PrimitiveType MetalPrimitiveType(PrimitiveTopology topology) noexcept;
	[[nodiscard]] MTL::CullMode MetalCullMode(CullMode mode) noexcept;
	[[nodiscard]] MTL::Winding MetalWinding(FrontFace face) noexcept;
	[[nodiscard]] MTL::TriangleFillMode MetalFillMode(FillMode mode) noexcept;
	[[nodiscard]] MTL::BlendFactor MetalBlendFactor(BlendFactor factor) noexcept;
	[[nodiscard]] MTL::BlendOperation MetalBlendOp(BlendOp op) noexcept;
	[[nodiscard]] MTL::ColorWriteMask MetalColorWriteMask(Flags<ColorWrite> mask) noexcept;
	[[nodiscard]] MTL::LoadAction MetalLoadAction(LoadOp op) noexcept;
	[[nodiscard]] MTL::StoreAction MetalStoreAction(StoreOp op) noexcept;
	[[nodiscard]] MTL::FunctionType MetalFunctionType(ShaderStage stage) noexcept;
	[[nodiscard]] MTL::IndexType MetalIndexType(bool index32) noexcept;

	[[nodiscard]] NS::SharedPtr<MTL::Library> MetalCompileLibrary(MTL::Device * device, const ShaderBinary & shader, Error * error);

	[[nodiscard]] NS::SharedPtr<MTL::Function> CompileFunction(MTL::Device * device, const ShaderBinary & shader, Error * error);

	[[nodiscard]] bool MetalRefuseUnbuildableGraphicsStage(ShaderStage stage, Error * error);

	[[nodiscard]] NS::SharedPtr<MTL::DepthStencilState> BuildDepthStencilState(MTL::Device * device, const DepthStencilStateDesc & desc);

	void SetMetalLabel(MTL::Resource * resource, CString debugName) noexcept;

}

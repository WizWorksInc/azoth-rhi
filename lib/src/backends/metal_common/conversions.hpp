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
 * \brief How an RHI value becomes a Metal value, for both Metal backends.
 *
 * The leaf of the Metal half of the tree. Nothing here holds state, names a device type or knows which generation is calling, so metal/ and metal4/ both build
 * on it and neither builds on the other.
 *
 * What lives here is what Metal answers once and not once per generation, so a format added to the RHI is one switch to extend, not two that drift.
 */

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

	/*
	 * The per-stage argument table sizes on the macOS families, which bound a set below the tier that has argument buffers, where descriptors go in one at a time.
	 * Above it a set is an argument buffer at a buffer index. The buffer table is what bounds how many sets there can be. Metal states these in its feature set
	 * tables and exposes no query for them, unlike the sampler count inside an argument buffer, which is queried.
	 */
	inline constexpr std::uint32_t kMetalMaxBufferArguments	 = 31;
	inline constexpr std::uint32_t kMetalMaxTextureArguments = 128;
	inline constexpr std::uint32_t kMetalMaxSamplerArguments = 16;

	/*
	 * Where the published binding ABI puts a push constant, and where vertex buffers start.
	 *
	 * slangc emits push constants at Metal buffer 0 and feeds vertex attributes through [[stage_in]]. Vertex buffers are bound above the resource buffers so a
	 * vertex slot never collides with a push constant or a descriptor-set buffer binding. Shared because the ABI a metallib was built against does not change
	 * with the generation that binds it, which is what lets one binary run on both.
	 */
	inline constexpr std::uint32_t kMetalPushConstantIndex = 0;
	inline constexpr std::uint32_t kMetalVertexBufferBase  = 16;

	/// Error reporting, which every backend entry does the same way.

	bool Succeed(Error * error) noexcept;
	bool Fail(Error * error, ErrorCode code, const char * message) noexcept;

	/*
	 * The trailing Error * of a backend entry, picked out of a pack.
	 *
	 * Every entry in a block takes it last, so the helpers that succeed or fail on a caller's behalf can take the whole pack and find it without each one naming
	 * its own signature.
	 */
	template <typename... Args>
	[[nodiscard]] Error * LastError(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	// The output an entry writes before its Error *, which is the second from last of the pack.
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

	/*
	 * Succeeds without recording anything, for the entries where doing nothing is correct on Metal. Reserved for operations another API has to spell out and Metal
	 * does not: coherent-memory flushes and hints with no observable contract. An operation Metal simply cannot do belongs on the backend's own Unimplemented
	 * instead so a caller finds out. Otherwise they watch the call succeed and the work never happen.
	 */
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

	// Rebuilds a typed handle from the erased one a destroy arrives with so it can name the table that holds it.
	template <typename HandleT>
	[[nodiscard]] constexpr HandleT Typed(const RawHandle handle) noexcept
	{
		return HandleT{ .index = handle.index, .generation = handle.generation };
	}

	/// Formats.

	// Maps a portability format to a Metal pixel format. Returns PixelFormatInvalid for formats Metal has no texture format for.
	[[nodiscard]] MTL::PixelFormat MetalPixelFormat(Format format) noexcept;

	// Maps an RHI vertex-attribute format onto its Metal vertex format for the pipeline vertex descriptor.
	[[nodiscard]] MTL::VertexFormat MetalVertexFormat(Format format) noexcept;

	[[nodiscard]] constexpr bool IsStencilFormat(const Format format) noexcept
	{
		return format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
	}

	// Defers to the shared block table so this and the copy stride math cannot disagree about what is compressed.
	[[nodiscard]] constexpr bool IsCompressedFormat(const Format format) noexcept
	{
		return detail::IsCompressedFormat(format);
	}

	[[nodiscard]] constexpr bool IsIntegerFormat(const Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UInt:
		case Format::eR8SInt:
		case Format::eR16UInt:
		case Format::eR16SInt:
		case Format::eR32UInt:
		case Format::eR32SInt: return true;
		default:			   return false;
		}
	}

	/// Memory and storage.

	[[nodiscard]] MTL::ResourceOptions MetalBufferStorage(MemoryUsage usage) noexcept;
	[[nodiscard]] MTL::StorageMode MetalHeapStorage(HeapType type) noexcept;
	[[nodiscard]] MTL::ResourceOptions MetalResourceOptions(MTL::StorageMode mode) noexcept;

	/// Textures, views and samplers.

	[[nodiscard]] MTL::TextureType MetalViewType(TextureViewType type) noexcept;
	[[nodiscard]] MTL::TextureSwizzleChannels MetalSwizzleChannels(ComponentMapping mapping) noexcept;
	[[nodiscard]] MTL::SamplerMinMagFilter MetalMinMagFilter(Filter filter) noexcept;
	[[nodiscard]] MTL::SamplerMipFilter MetalMipFilter(MipmapMode mode) noexcept;
	[[nodiscard]] MTL::SamplerAddressMode MetalAddressMode(AddressMode mode) noexcept;
	[[nodiscard]] MTL::CompareFunction MetalCompareFunction(CompareOp op) noexcept;
	[[nodiscard]] MTL::SamplerBorderColor MetalBorderColor(BorderColor color) noexcept;

	/**
	 * \brief Builds a Metal texture descriptor from a portability texture desc.
	 *
	 * \return Null with the error set when the format is not representable. Shared by texture creation, texture import and heap footprint queries.
	 */
	[[nodiscard]] NS::SharedPtr<MTL::TextureDescriptor> BuildTextureDescriptor(const TextureDesc & desc, Error * error) noexcept;

	/**
	 * \brief Builds the sampler descriptor a SamplerDesc asks for, without creating the sampler.
	 *
	 * Split from creation because the two backends make the object off their own MTLDevice, and every decision before that point is the same one.
	 */
	[[nodiscard]] NS::SharedPtr<MTL::SamplerDescriptor> BuildSamplerDescriptor(const SamplerDesc & desc) noexcept;

	/// Pipeline state, none of which a pipeline object carries on either generation.

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

	/**
	 * \brief Turns a ShaderBinary into a Metal library.
	 *
	 * Shared because a metallib is a metallib. What differs is what each backend builds out of it: an MTLFunction on Metal 3, a function descriptor naming this
	 * library on Metal 4.
	 */
	[[nodiscard]] NS::SharedPtr<MTL::Library> MetalCompileLibrary(MTL::Device * device, const ShaderBinary & shader, Error * error);

	/**
	 * \brief Resolves one shader binary to the Metal function a Metal 3 pipeline binds.
	 *
	 * The function is looked up by the name the caller gave and not by taking the first of a matching type. A library holding two functions of one stage is
	 * ordinary, and picking whichever enumerated first bound the wrong one with nothing reporting it. Slang turns main into main_0, so that is what its callers
	 * pass.
	 */
	[[nodiscard]] NS::SharedPtr<MTL::Function> CompileFunction(MTL::Device * device, const ShaderBinary & shader, Error * error);

	/**
	 * \brief The depth-stencil state a pipeline sets on the encoder, Metal keeping it off the pipeline.
	 *
	 * \return Null when the desc tests neither depth nor stencil, which is the state a caller binds nothing for.
	 */
	[[nodiscard]] NS::SharedPtr<MTL::DepthStencilState> BuildDepthStencilState(MTL::Device * device, const DepthStencilStateDesc & desc);

	// Names a native object after the desc that asked for it so Xcode's frame debugger and the Metal validation messages say what the resource is, not reporting
	// an address. Callers pass the debugName straight through and a null one leaves the object unnamed.
	void SetMetalLabel(MTL::Resource * resource, CString debugName) noexcept;

} // namespace azo::rhi::metal_common

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace azo::rhi
{

	struct PipelineCacheDesc final
	{
		const void * initialData = nullptr;
		std::size_t initialSize	 = 0;
		const char * debugName	 = nullptr;
	};

	struct PipelineCacheData final
	{
		const void * data = nullptr;
		std::size_t size  = 0;
	};

	enum class ShaderBinaryFormat : std::uint8_t
	{
		eSpirV,
		eDxil,

		eBackendNative,
	};

	struct ShaderBindingEntry final
	{
		std::uint32_t set	  = 0;
		std::uint32_t binding = 0;

		NativeSlot resource{};

		NativeSlot sampler{};
		bool hasSampler = false;
	};

	struct ShaderBindingMap final
	{
		ShaderAbiVersion abi{};

		std::span<const ShaderBindingEntry> bindings;

		const char * vertexSemanticName = nullptr;
	};

	struct ThreadgroupSize final
	{
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		std::uint32_t z = 0;

		[[nodiscard]] constexpr bool IsStated() const noexcept
		{
			return x != 0 && y != 0 && z != 0;
		}
	};

	struct ShaderBinary final
	{
		ShaderStage stage		  = ShaderStage::eNone;
		ShaderBinaryFormat format = ShaderBinaryFormat::eBackendNative;
		const void * data		  = nullptr;
		std::size_t size		  = 0;
		const char * entryPoint	  = "main";

		bool isSource = false;

		const ShaderBindingMap * bindingMap = nullptr;

		ThreadgroupSize threadgroupSize{};

		const char * debugName = nullptr;
	};

	struct VertexBindingDesc final
	{
		std::uint32_t binding = 0;
		std::uint32_t stride  = 0;

		bool perInstance = false;
	};

	struct VertexAttributeDesc final
	{
		std::uint32_t location = 0;
		std::uint32_t binding  = 0;
		Format format		   = Format::eUndefined;

		std::uint32_t offset = 0;
	};

	struct RasterStateDesc final
	{
		FillMode fillMode	= FillMode::eSolid;
		CullMode cullMode	= CullMode::eBack;
		FrontFace frontFace = FrontFace::eCounterClockwise;

		bool depthClampEnable = false;

		bool conservativeRasterEnable = false;

		bool rasterizerDiscardEnable = false;

		bool depthBiasEnable		  = false;
		float depthBiasConstantFactor = 0.0f;

		float depthBiasClamp = 0.0f;

		float depthBiasSlopeFactor = 0.0f;
	};

	struct StencilFaceDesc final
	{
		StencilOp failOp	  = StencilOp::eKeep;
		StencilOp passOp	  = StencilOp::eKeep;
		StencilOp depthFailOp = StencilOp::eKeep;
		CompareOp compareOp	  = CompareOp::eAlways;

		std::uint32_t compareMask = 0xffffffffu;

		std::uint32_t writeMask = 0xffffffffu;

		std::uint32_t reference = 0;
	};

	struct DepthStencilStateDesc final
	{
		bool depthTestEnable = false;

		bool depthWriteEnable = false;

		CompareOp depthCompareOp = CompareOp::eLess;

		bool depthBoundsTestEnable = false;

		bool stencilTestEnable = false;
		StencilFaceDesc front{};
		StencilFaceDesc back{};
		float minDepthBounds = 0.0f;
		float maxDepthBounds = 1.0f;
	};

	enum class ColorWrite : std::uint8_t
	{
		eR = 1u << 0u,
		eG = 1u << 1u,
		eB = 1u << 2u,
		eA = 1u << 3u,
	};

	struct ColorBlendAttachmentDesc final
	{
		bool blendEnable				= false;
		BlendFactor srcColorBlendFactor = BlendFactor::eOne;
		BlendFactor dstColorBlendFactor = BlendFactor::eZero;
		BlendOp colorBlendOp			= BlendOp::eAdd;

		BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;

		BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
		BlendOp alphaBlendOp			= BlendOp::eAdd;

		Flags<ColorWrite> colorWriteMask = Flags<ColorWrite>(ColorWrite::eR) | ColorWrite::eG | ColorWrite::eB | ColorWrite::eA;
	};

	struct BlendStateDesc final
	{
		bool logicOpEnable = false;

		std::array<float, 4> blendConstants{ 0.0f, 0.0f, 0.0f, 0.0f };

		std::array<ColorBlendAttachmentDesc, 8> attachments{};

		std::uint32_t attachmentCount = 0;
	};

	enum class DynamicState : std::uint32_t // NOLINT(performance-enum-size)
	{
		eViewport = 1u << 0u,
		eScissor  = 1u << 1u,

		eBlendConstants = 1u << 2u,

		eStencilReference = 1u << 3u,

		eDepthBias = 1u << 4u,
	};

	struct VertexInputDesc final
	{
		std::span<const VertexBindingDesc> bindings;
		std::span<const VertexAttributeDesc> attributes;
		PrimitiveTopology topology	= PrimitiveTopology::eTriangleList;
		bool primitiveRestartEnable = false;

		std::uint32_t patchControlPoints = 0;
	};

	struct RenderTargetDesc final
	{
		std::array<Format, 8> colorFormats{};
		std::uint32_t colorFormatCount = 0;
		Format depthStencilFormat	   = Format::eUndefined;
		SampleCount samples			   = SampleCount::e1;
		std::uint32_t sampleMask	   = 0xffffffffu;
		bool alphaToCoverageEnable	   = false;
	};

	struct GraphicsPipelineDesc final
	{
		PipelineLayoutHandle layout{};
		std::span<const ShaderBinary> shaders;

		const VertexInputDesc * vertexInput = nullptr;

		RasterStateDesc raster{};
		DepthStencilStateDesc depthStencil{};
		BlendStateDesc blend{};
		RenderTargetDesc renderTarget{};

		PipelineCacheHandle pipelineCache{};
		Flags<DynamicState> dynamicStates;
		const char * debugName = nullptr;
	};

	struct ComputePipelineDesc final
	{
		PipelineLayoutHandle layout{};
		ShaderBinary shader{};
		PipelineCacheHandle pipelineCache{};
		const char * debugName = nullptr;
	};

	enum class AccelerationStructureType : std::uint8_t
	{
		eBottomLevel,

		eTopLevel,
	};

	enum class AccelerationStructureBuildFlag : std::uint32_t // NOLINT(performance-enum-size)
	{
		eAllowUpdate = 1u << 0u,

		eAllowCompaction = 1u << 1u,

		ePreferFastTrace = 1u << 2u,

		ePreferFastBuild = 1u << 3u,

		eMinimizeMemory = 1u << 4u,
	};

	enum class AccelerationStructureBuildMode : std::uint8_t
	{
		eBuild,

		eUpdate,
	};

	struct AccelerationStructureDesc final
	{
		AccelerationStructureType type = AccelerationStructureType::eBottomLevel;
		BufferHandle storage{};
		std::uint64_t storageOffset = 0;
		std::uint64_t size			= 0;
		const char * debugName		= nullptr;
	};

	struct AccelerationStructureGeometryDesc final
	{
		BufferHandle vertexBuffer{};
		std::uint64_t vertexOffset = 0;
		std::uint32_t vertexStride = 0;
		Format vertexFormat		   = Format::eRGB32Float;
		BufferHandle indexBuffer{};
		std::uint64_t indexOffset	 = 0;
		bool index32				 = true;
		std::uint32_t primitiveCount = 0;
		bool opaque					 = true;
	};

	struct AccelerationStructureBuildDesc final
	{
		AccelerationStructureHandle dst{};
		AccelerationStructureHandle src{};
		AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::eBuild;
		Flags<AccelerationStructureBuildFlag> flags;
		std::span<const AccelerationStructureGeometryDesc> geometries;
		BufferHandle instanceBuffer{};
		std::uint64_t instanceOffset = 0;
		std::uint32_t instanceCount	 = 0;
		BufferHandle scratchBuffer{};
		std::uint64_t scratchOffset = 0;
	};

	enum class RayTracingShaderGroupType : std::uint8_t
	{
		eRayGeneration,

		eMiss,

		eTrianglesHitGroup,

		eProceduralHitGroup,

		eCallable,
	};

	struct RayTracingShaderGroupDesc final
	{
		RayTracingShaderGroupType type	 = RayTracingShaderGroupType::eRayGeneration;
		std::uint32_t generalShader		 = kInvalidIndex;
		std::uint32_t closestHitShader	 = kInvalidIndex;
		std::uint32_t anyHitShader		 = kInvalidIndex;
		std::uint32_t intersectionShader = kInvalidIndex;
	};

	struct RayTracingPipelineDesc final
	{
		PipelineLayoutHandle layout{};
		std::span<const ShaderBinary> shaders;
		std::span<const RayTracingShaderGroupDesc> groups;
		std::uint32_t maxRayRecursionDepth = 1;
		std::uint32_t maxPayloadBytes	   = 0;
		std::uint32_t maxAttributeBytes	   = 8;
		PipelineCacheHandle pipelineCache{};
		const char * debugName = nullptr;
	};

	struct ShaderBindingTableRegion final
	{
		BufferHandle buffer{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = 0;
		std::uint64_t stride = 0;
	};

	struct ShaderBindingTableDesc final
	{
		ShaderBindingTableRegion rayGeneration{};
		ShaderBindingTableRegion miss{};
		ShaderBindingTableRegion hit{};
		ShaderBindingTableRegion callable{};
	};

}

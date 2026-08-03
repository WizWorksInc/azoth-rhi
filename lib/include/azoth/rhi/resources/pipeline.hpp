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
 * \brief Pipeline state descriptions, shader binaries, acceleration structures, and shader binding tables.
 */

#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include <array>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Pipeline cache creation request.
	 *
	 * initialData and debugName are borrowed for the duration of creation. The blob is backend-specific and may be ignored when incompatible.
	 */
	struct PipelineCacheDesc final
	{
		const void * initialData = nullptr;
		std::size_t initialSize	 = 0;
		const char * debugName	 = nullptr;
	};

	/**
	 * \brief Serialized pipeline cache data returned by the backend.
	 *
	 * Pipelines must still be creatable from shader binaries and descriptors alone.
	 */
	struct PipelineCacheData final
	{
		const void * data = nullptr;
		std::size_t size  = 0;
	};

	/**
	 * \brief Binary encoding passed to pipeline creation.
	 */
	enum class ShaderBinaryFormat : std::uint8_t
	{
		eSpirV,
		eDxil,

		/**
		 * \brief Backend-private encoding, interpreted only by backends that declare support.
		 *
		 * A compiled Metal library on that backend, or its shading language source where ShaderBinary::isSource says so.
		 */
		eBackendNative,
	};

	/**
	 * \brief Where one RHI binding actually landed in a shader binary.
	 *
	 * Carried only by a binary that does not conform to the published ABI. The slots are what its compiler chose, which is what lets the RHI bind the layout to
	 * match and not to what it would have chosen.
	 */
	struct ShaderBindingEntry final
	{
		std::uint32_t set	  = 0;
		std::uint32_t binding = 0;

		NativeSlot resource{};

		/**
		 * \brief The second slot a combined image sampler occupies where the backend splits it.
		 */
		NativeSlot sampler{};
		bool hasSampler = false;
	};

	/**
	 * \brief What a shader binary says about itself when it does not follow the binding ABI.
	 *
	 * The escape hatch for a toolchain that cannot be made to conform. Everything here is what the RHI would otherwise have had to reflect out of the binary,
	 * which it does not do.
	 *
	 * \attention Both spans are borrowed for the duration of pipeline creation.
	 */
	struct ShaderBindingMap final
	{
		/**
		 * \brief Which revision of the ABI the binary was built against, when it was built against one at all.
		 *
		 * A binary that conforms to a revision the RHI still implements needs no entries. One that conforms to nothing leaves this zero and lists every binding it
		 * uses.
		 */
		ShaderAbiVersion abi{};

		/**
		 * \brief Where each binding landed, for a binary that conforms to no revision.
		 *
		 * A binding the layout declares and this does not list is refused and not guessed at from the ABI: a map that is partly trusted is worse than one that is not
		 * trusted at all, because the half that was guessed binds silently.
		 */
		std::span<const ShaderBindingEntry> bindings;

		/**
		 * \brief The semantic name this binary's vertex attributes carry, when it is not kVertexSemanticName.
		 *
		 * Only Direct3D 12 reads it, since only Direct3D 12 addresses vertex inputs by name. Null means the ABI's name, which is also what every stage other than the
		 * vertex stage leaves it as.
		 *
		 * \attention Borrowed for the duration of pipeline creation.
		 */
		const char * vertexSemanticName = nullptr;
	};

	/**
	 * \brief Threads per threadgroup a compute entry point was compiled for.
	 *
	 * Zero in any component means unstated, which is refused, not treated as one.
	 */
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

	/**
	 * \brief Compiled bytecode for one pipeline shader stage.
	 *
	 * data, entryPoint, bindingMap, and debugName are borrowed for the duration of pipeline creation. Backends copy anything they retain.
	 */
	struct ShaderBinary final
	{
		ShaderStage stage		  = ShaderStage::eNone;
		ShaderBinaryFormat format = ShaderBinaryFormat::eBackendNative;
		const void * data		  = nullptr;
		std::size_t size		  = 0;
		const char * entryPoint	  = "main";

		/**
		 * \brief Whether data is shading language source for the backend to compile and not something already compiled.
		 *
		 * Separate from the format because the two are separate questions: the format says what the bytes are and this says whether they still need a compiler.
		 * A backend that takes source says so through DeviceCaps::supportsShaderSource.
		 *
		 * \attention The backend invokes the platform shader compiler once per stage per pipeline with no cache, so a pipeline built twice compiles twice. Ship
		 * compiled.
		 */
		bool isSource = false;

		/**
		 * \brief Where this binary put its bindings, when it did not follow the published binding ABI.
		 *
		 * Null asserts conformance, which is the ordinary case and costs nothing. Non-null is for a binary whose compiler cannot be made to conform, and states what
		 * it did instead.
		 *
		 * \attention Borrowed for the duration of pipeline creation.
		 */
		const ShaderBindingMap * bindingMap = nullptr;

		/**
		 * \brief Threads per threadgroup, required for a compute stage and ignored for the rest.
		 *
		 * SPIR-V and DXIL carry this inside the binary. Metal takes it at dispatch and MSL has no equivalent of the HLSL numthreads attribute, leaving it to be
		 * stated here. Every backend refuses a compute stage that leaves it zero.
		 *
		 * \attention Never checked against the binary. A value disagreeing with what the shader was compiled for dispatches the wrong number of threads on
		 * Metal.
		 */
		ThreadgroupSize threadgroupSize{};

		const char * debugName = nullptr;
	};

	/**
	 * \brief Vertex buffer binding slot and layout stride.
	 */
	struct VertexBindingDesc final
	{
		std::uint32_t binding = 0;
		std::uint32_t stride  = 0;

		/**
		 * \brief Advances once per instance instead of once per vertex.
		 */
		bool perInstance = false;
	};

	/**
	 * \brief Maps one shader input location to a byte range inside one vertex buffer binding.
	 */
	struct VertexAttributeDesc final
	{
		std::uint32_t location = 0;
		std::uint32_t binding  = 0;
		Format format		   = Format::eUndefined;

		/**
		 * \brief Byte offset relative to the selected vertex binding.
		 */
		std::uint32_t offset = 0;
	};

	/**
	 * \brief Fixed-function rasterization state.
	 */
	struct RasterStateDesc final
	{
		FillMode fillMode	= FillMode::eSolid;
		CullMode cullMode	= CullMode::eBack;
		FrontFace frontFace = FrontFace::eCounterClockwise;

		/**
		 * \brief Clamps primitives outside the depth range instead of clipping them. May require a feature bit.
		 */
		bool depthClampEnable = false;

		/**
		 * \brief Rasterizes a primitive if it touches a pixel at all and not only when it covers the sample point.
		 *
		 * Needs DeviceCaps::conservativeRasterTier above eNone. A device reporting eNone refuses the pipeline without rasterizing normally, since silently dropping
		 * this changes which pixels are covered and nothing downstream can tell.
		 */
		bool conservativeRasterEnable = false;

		/**
		 * \brief Stops after vertex processing and produces no fragments.
		 */
		bool rasterizerDiscardEnable = false;

		bool depthBiasEnable		  = false;
		float depthBiasConstantFactor = 0.0f;

		/**
		 * \brief Zero requests the native unclamped depth-bias behavior.
		 */
		float depthBiasClamp = 0.0f;

		/**
		 * \brief Scales depth bias by depth slope so grazing surfaces receive a larger offset.
		 */
		float depthBiasSlopeFactor = 0.0f;
	};

	/**
	 * \brief Stencil operations and masks for one face.
	 */
	struct StencilFaceDesc final
	{
		StencilOp failOp	  = StencilOp::eKeep;
		StencilOp passOp	  = StencilOp::eKeep;
		StencilOp depthFailOp = StencilOp::eKeep;
		CompareOp compareOp	  = CompareOp::eAlways;

		/**
		 * \brief Applied to both the reference value and stored value before comparison.
		 */
		std::uint32_t compareMask = 0xffffffffu;

		/**
		 * \brief Selects which stencil bits writes may modify.
		 */
		std::uint32_t writeMask = 0xffffffffu;

		std::uint32_t reference = 0;
	};

	/**
	 * \brief Fixed-function depth and stencil state.
	 */
	struct DepthStencilStateDesc final
	{
		bool depthTestEnable = false;

		/**
		 * \brief Off gives read-only depth testing for passes such as prepass consumers or transparent draws.
		 */
		bool depthWriteEnable = false;

		CompareOp depthCompareOp = CompareOp::eLess;

		/**
		 * \brief Fragments pass only inside the configured depth-bounds range. May require a feature bit.
		 */
		bool depthBoundsTestEnable = false;

		bool stencilTestEnable = false;
		StencilFaceDesc front{};
		StencilFaceDesc back{};
		float minDepthBounds = 0.0f;
		float maxDepthBounds = 1.0f;
	};

	/**
	 * \brief Color attachment write mask bits.
	 */
	enum class ColorWrite : std::uint8_t
	{
		eR = 1u << 0u,
		eG = 1u << 1u,
		eB = 1u << 2u,
		eA = 1u << 3u,
	};

	/**
	 * \brief Blend and write-mask state for one color attachment.
	 */
	struct ColorBlendAttachmentDesc final
	{
		bool blendEnable				= false;
		BlendFactor srcColorBlendFactor = BlendFactor::eOne;
		BlendFactor dstColorBlendFactor = BlendFactor::eZero;
		BlendOp colorBlendOp			= BlendOp::eAdd;

		/**
		 * \brief Alpha may use different factors from RGB.
		 */
		BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;

		BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
		BlendOp alphaBlendOp			= BlendOp::eAdd;

		/**
		 * \brief Channels not in the mask keep their previous attachment values.
		 */
		Flags<ColorWrite> colorWriteMask = Flags<ColorWrite>(ColorWrite::eR) | ColorWrite::eG | ColorWrite::eB | ColorWrite::eA;
	};

	/**
	 * \brief Blend state across all color attachments.
	 */
	struct BlendStateDesc final
	{
		/**
		 * \brief Uses fixed-function logic ops instead of blending. Mutually exclusive with blending in many native APIs.
		 */
		bool logicOpEnable = false;

		/**
		 * \brief Consumed by constant-color and constant-alpha blend factors.
		 */
		std::array<float, 4> blendConstants{ 0.0f, 0.0f, 0.0f, 0.0f };

		/**
		 * \brief Only the first attachmentCount entries are active.
		 */
		std::array<ColorBlendAttachmentDesc, 8> attachments{};

		std::uint32_t attachmentCount = 0;
	};

	/**
	 * \brief Pipeline state that may be set dynamically on the command list.
	 *
	 * State not declared dynamic at creation is baked into the pipeline object. The width is fixed at 32 bits for headroom.
	 */
	enum class DynamicState : std::uint32_t // NOLINT(performance-enum-size)
	{
		/**
		 * \brief Always dynamic, whether or not it is named here.
		 *
		 * This description carries no viewport and no scissor rectangle, so there is nothing static to bake in and both are command-list state on every backend.
		 * Naming them is allowed and changes nothing, which is why a pipeline that leaves them out still has to be given both through the command list before it
		 * draws.
		 */
		eViewport = 1u << 0u,
		eScissor  = 1u << 1u,

		/**
		 * \brief Required by constant-color and constant-alpha blend factors unless the constants are baked in.
		 */
		eBlendConstants = 1u << 2u,

		/**
		 * \brief The compare and write masks remain part of depth-stencil state.
		 */
		eStencilReference = 1u << 3u,

		eDepthBias = 1u << 4u,
	};

	/**
	 * \brief Where a graphics pipeline's primitives come from.
	 *
	 * Separated from the pipeline description because a pipeline that does not source vertices has nothing to say here, and leaving four members empty says that
	 * far less clearly than leaving the whole group out.
	 *
	 * Span members are borrowed for the duration of creation.
	 */
	struct VertexInputDesc final
	{
		std::span<const VertexBindingDesc> bindings;
		std::span<const VertexAttributeDesc> attributes;
		PrimitiveTopology topology	= PrimitiveTopology::eTriangleList;
		bool primitiveRestartEnable = false;

		/**
		 * \brief Vertices per patch, read only when topology is ePatchList.
		 *
		 * Zero on every other topology. A patch list with zero control points is refused, not guessed at.
		 */
		std::uint32_t patchControlPoints = 0;
	};

	/**
	 * \brief What a graphics pipeline writes into.
	 */
	struct RenderTargetDesc final
	{
		std::array<Format, 8> colorFormats{};
		std::uint32_t colorFormatCount = 0;
		Format depthStencilFormat	   = Format::eUndefined;
		SampleCount samples			   = SampleCount::e1;
		std::uint32_t sampleMask	   = 0xffffffffu;
		bool alphaToCoverageEnable	   = false;
	};

	/**
	 * \brief Graphics pipeline creation request.
	 *
	 * Grouped by concern, not flat, so a new capability arrives as one optional group instead of widening an aggregate every caller reads. Span members,
	 * vertexInput and debugName are borrowed for the duration of creation.
	 */
	struct GraphicsPipelineDesc final
	{
		PipelineLayoutHandle layout{};
		std::span<const ShaderBinary> shaders;

		/**
		 * \brief Null when primitives do not come from vertex buffers.
		 *
		 * Today that is refused by name, since every backend here sources vertices. It is also the shape a mesh pipeline needs, which is why the group is a pointer
		 * and not a member with an empty state.
		 */
		const VertexInputDesc * vertexInput = nullptr;

		RasterStateDesc raster{};
		DepthStencilStateDesc depthStencil{};
		BlendStateDesc blend{};
		RenderTargetDesc renderTarget{};

		PipelineCacheHandle pipelineCache{};
		Flags<DynamicState> dynamicStates;
		const char * debugName = nullptr;
	};

	/**
	 * \brief Compute pipeline creation request.
	 */
	struct ComputePipelineDesc final
	{
		PipelineLayoutHandle layout{};
		ShaderBinary shader{};
		PipelineCacheHandle pipelineCache{};
		const char * debugName = nullptr;
	};

	/**
	 * \brief Acceleration-structure level in the ray tracing hierarchy.
	 */
	enum class AccelerationStructureType : std::uint8_t
	{
		/**
		 * \brief Triangles or procedural AABBs for one mesh or geometry set.
		 */
		eBottomLevel,

		/**
		 * \brief Instances of bottom-level structures with transforms, ids, masks, and shader-table contributions.
		 */
		eTopLevel,
	};

	/**
	 * \brief Build behavior hints for an acceleration structure.
	 *
	 * Bitflags trade build time, trace performance, memory use, and update support against each other.
	 */
	enum class AccelerationStructureBuildFlag : std::uint32_t // NOLINT(performance-enum-size)
	{
		/**
		 * \brief Enables later in-place refit at some cost in memory or optimality.
		 */
		eAllowUpdate = 1u << 0u,

		/**
		 * \brief Lets the backend report a compacted size for copying into a smaller allocation.
		 */
		eAllowCompaction = 1u << 1u,

		/**
		 * \brief Prefer traversal performance for mostly static geometry.
		 */
		ePreferFastTrace = 1u << 2u,

		/**
		 * \brief Prefer build performance for dynamic geometry rebuilt often.
		 */
		ePreferFastBuild = 1u << 3u,

		/**
		 * \brief Minimize memory, possibly at a build or trace performance cost.
		 */
		eMinimizeMemory = 1u << 4u,
	};

	/**
	 * \brief Acceleration-structure build command mode.
	 */
	enum class AccelerationStructureBuildMode : std::uint8_t
	{
		eBuild,

		/**
		 * \brief Refits a structure built with update support.
		 *
		 * Usually faster than rebuilding, but lower quality if geometry moved far.
		 */
		eUpdate,
	};

	/**
	 * \brief Creation parameters for an acceleration structure and its backing storage.
	 */
	struct AccelerationStructureDesc final
	{
		AccelerationStructureType type = AccelerationStructureType::eBottomLevel;
		BufferHandle storage{};
		std::uint64_t storageOffset = 0;
		std::uint64_t size			= 0;
		const char * debugName		= nullptr;
	};

	/**
	 * \brief One bottom-level acceleration-structure geometry input.
	 */
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

	/**
	 * \brief Parameters for one acceleration-structure build or update command.
	 *
	 * Scratch buffers must be created with BufferUsage::eStorage and obey DeviceCaps alignment.
	 */
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

	/**
	 * \brief Ray tracing shader group kind.
	 */
	enum class RayTracingShaderGroupType : std::uint8_t
	{
		/**
		 * \brief Starting point for a dispatch, usually a single-record region.
		 */
		eRayGeneration,

		/**
		 * \brief Runs when a ray hits nothing.
		 */
		eMiss,

		/**
		 * \brief Closest-hit and optional any-hit for built-in triangle intersection.
		 */
		eTrianglesHitGroup,

		/**
		 * \brief Intersection plus optional any-hit and closest-hit for procedural primitives.
		 */
		eProceduralHitGroup,

		/**
		 * \brief Invoked explicitly from other ray tracing shaders.
		 */
		eCallable,
	};

	/**
	 * \brief One ray tracing shader group.
	 *
	 * Shader fields index into RayTracingPipelineDesc::shaders. Unused slots stay at kInvalidIndex.
	 */
	struct RayTracingShaderGroupDesc final
	{
		RayTracingShaderGroupType type	 = RayTracingShaderGroupType::eRayGeneration;
		std::uint32_t generalShader		 = kInvalidIndex;
		std::uint32_t closestHitShader	 = kInvalidIndex;
		std::uint32_t anyHitShader		 = kInvalidIndex;
		std::uint32_t intersectionShader = kInvalidIndex;
	};

	/**
	 * \brief Ray tracing pipeline creation request.
	 *
	 * shaders, groups, and debugName are borrowed for the duration of creation.
	 */
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

	/**
	 * \brief One shader binding table region.
	 *
	 * offset, size, and stride are byte counts inside buffer.
	 */
	struct ShaderBindingTableRegion final
	{
		BufferHandle buffer{};
		std::uint64_t offset = 0;
		std::uint64_t size	 = 0;
		std::uint64_t stride = 0;
	};

	/**
	 * \brief The four shader binding table regions used by a ray dispatch.
	 */
	struct ShaderBindingTableDesc final
	{
		ShaderBindingTableRegion rayGeneration{};
		ShaderBindingTableRegion miss{};
		ShaderBindingTableRegion hit{};
		ShaderBindingTableRegion callable{};
	};

} // namespace azo::rhi

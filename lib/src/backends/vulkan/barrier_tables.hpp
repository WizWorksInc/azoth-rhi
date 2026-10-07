// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/flags.hpp"

#include <vulkan/vulkan.hpp>

namespace azo::rhi::vulkan
{

	[[nodiscard]] constexpr vk::PipelineStageFlags2 map_stages(const Flags<Stage> stages) noexcept
	{
		vk::PipelineStageFlags2 out{};

		if (stages.contains(Stage::eIndirectFetch))
		{
			out |= vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (stages.contains(Stage::eVertexWork))
		{
			out |= vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::ePreRasterizationShaders;
		}

		if (stages.contains(Stage::eFragmentShading))
		{
			out |= vk::PipelineStageFlagBits2::eFragmentShader;
		}

		if (stages.contains(Stage::eDepthStencil))
		{
			out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (stages.contains(Stage::eColorOutput))
		{
			out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (stages.contains(Stage::eCompute))
		{
			out |= vk::PipelineStageFlagBits2::eComputeShader;
		}

		if (stages.contains(Stage::eCopy))
		{
			out |= vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear;
		}

		if (stages.contains(Stage::eResolve))
		{
			out |= vk::PipelineStageFlagBits2::eResolve;
		}

		if (stages.contains(Stage::eHost))
		{
			out |= vk::PipelineStageFlagBits2::eHost;
		}

		if (stages.contains(Stage::eRayTracing))
		{
			out |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		if (stages.contains(Stage::eAccelBuild))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (stages.contains(Stage::eAllGraphics))
		{
			out |= vk::PipelineStageFlagBits2::eAllGraphics;
		}

		if (stages.contains(Stage::eAllCommands))
		{
			out |= vk::PipelineStageFlagBits2::eAllCommands;
		}

		return out;
	}

	[[nodiscard]] constexpr vk::AccessFlags2 map_barrier_access(const Flags<ResourceUse> use) noexcept
	{
		vk::AccessFlags2 out{};

		if (use.contains(ResourceUse::eIndirectArgs))
		{
			out |= vk::AccessFlagBits2::eIndirectCommandRead;
		}

		if (use.contains(ResourceUse::eVertexBuffer))
		{
			out |= vk::AccessFlagBits2::eVertexAttributeRead;
		}

		if (use.contains(ResourceUse::eIndexBuffer))
		{
			out |= vk::AccessFlagBits2::eIndexRead;
		}

		if (use.contains(ResourceUse::eUniformRead))
		{
			out |= vk::AccessFlagBits2::eUniformRead;
		}

		if (use.contains(ResourceUse::eSampledRead) || use.contains(ResourceUse::eStorageRead))
		{
			out |= vk::AccessFlagBits2::eShaderRead;
		}

		if (use.contains(ResourceUse::eStorageWrite))
		{
			out |= vk::AccessFlagBits2::eShaderWrite;
		}

		if (use.contains(ResourceUse::eColorTarget))
		{
			out |= vk::AccessFlagBits2::eColorAttachmentWrite;
		}

		if (use.contains(ResourceUse::eDepthStencilTarget))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
		}

		if (use.contains(ResourceUse::eDepthStencilRead))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		}

		if (use.contains(ResourceUse::eCopySrc) || use.contains(ResourceUse::eResolveSrc))
		{
			out |= vk::AccessFlagBits2::eTransferRead;
		}

		if (use.contains(ResourceUse::eCopyDst) || use.contains(ResourceUse::eResolveDst))
		{
			out |= vk::AccessFlagBits2::eTransferWrite;
		}

		if (use.contains(ResourceUse::eHostRead))
		{
			out |= vk::AccessFlagBits2::eHostRead;
		}

		if (use.contains(ResourceUse::eHostWrite))
		{
			out |= vk::AccessFlagBits2::eHostWrite;
		}

		if (use.contains(ResourceUse::eAccelBuildInput))
		{
			out |= vk::AccessFlagBits2::eShaderRead;
		}

		if (use.contains(ResourceUse::eAccelRead))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR;
		}

		if (use.contains(ResourceUse::eAccelWrite))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
		}

		if (use.contains(ResourceUse::eAccelBuildScratch))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
		}

		return out;
	}

	[[nodiscard]] constexpr vk::PipelineStageFlags2 derive_barrier_stages(const Flags<ResourceUse> use) noexcept
	{
		vk::PipelineStageFlags2 out{};

		if (use.contains(ResourceUse::eIndirectArgs))
		{
			out |= vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (use.contains(ResourceUse::eVertexBuffer) || use.contains(ResourceUse::eIndexBuffer))
		{
			out |= vk::PipelineStageFlagBits2::eVertexInput;
		}

		if (use.contains(ResourceUse::eUniformRead) || use.contains(ResourceUse::eSampledRead) || use.contains(ResourceUse::eStorageRead) ||
			use.contains(ResourceUse::eStorageWrite))
		{
			out |= vk::PipelineStageFlagBits2::eAllCommands;
		}

		if (use.contains(ResourceUse::eColorTarget))
		{
			out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (use.contains(ResourceUse::eDepthStencilTarget) || use.contains(ResourceUse::eDepthStencilRead))
		{
			out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (use.contains(ResourceUse::eCopySrc) || use.contains(ResourceUse::eCopyDst))
		{
			out |= vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear;
		}

		if (use.contains(ResourceUse::eResolveSrc) || use.contains(ResourceUse::eResolveDst))
		{
			out |= vk::PipelineStageFlagBits2::eResolve;
		}

		if (use.contains(ResourceUse::eHostRead) || use.contains(ResourceUse::eHostWrite))
		{
			out |= vk::PipelineStageFlagBits2::eHost;
		}

		if (use.contains(ResourceUse::eAccelBuildInput) || use.contains(ResourceUse::eAccelWrite) || use.contains(ResourceUse::eAccelBuildScratch))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (use.contains(ResourceUse::eAccelRead))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		return out ? out : vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eAllCommands);
	}

	[[nodiscard]] constexpr vk::PipelineStageFlags2 map_barrier_stages(const Flags<Stage> stages, const Flags<ResourceUse> use) noexcept
	{
		if (!stages.empty())
		{
			return map_stages(stages);
		}

		return map_barrier_access(use) ? derive_barrier_stages(use) : vk::PipelineStageFlags2{};
	}

	[[nodiscard]] constexpr vk::PipelineStageFlagBits2 timestamp_stage(const Flags<Stage> stage) noexcept
	{
		if (stage.contains(Stage::eIndirectFetch))
		{
			return vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (stage.contains(Stage::eVertexWork))
		{
			return vk::PipelineStageFlagBits2::eVertexShader;
		}

		if (stage.contains(Stage::eFragmentShading))
		{
			return vk::PipelineStageFlagBits2::eFragmentShader;
		}

		if (stage.contains(Stage::eDepthStencil))
		{
			return vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (stage.contains(Stage::eColorOutput))
		{
			return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (stage.contains(Stage::eCompute))
		{
			return vk::PipelineStageFlagBits2::eComputeShader;
		}

		if (stage.contains(Stage::eCopy))
		{
			return vk::PipelineStageFlagBits2::eAllTransfer;
		}

		if (stage.contains(Stage::eResolve))
		{
			return vk::PipelineStageFlagBits2::eResolve;
		}

		if (stage.contains(Stage::eHost))
		{
			return vk::PipelineStageFlagBits2::eHost;
		}

		if (stage.contains(Stage::eRayTracing))
		{
			return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		if (stage.contains(Stage::eAccelBuild))
		{
			return vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (stage.contains(Stage::eAllGraphics))
		{
			return vk::PipelineStageFlagBits2::eAllGraphics;
		}

		return vk::PipelineStageFlagBits2::eAllCommands;
	}

	static_assert(
		map_stages(Stage::eVertexWork) == (vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::ePreRasterizationShaders),
		"vertex work covers vertex input and every supported pre-rasterization shader stage"
	);

	static_assert(
		!(map_stages(Stage::eVertexWork) & (vk::PipelineStageFlagBits2::eTessellationControlShader | vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
											   vk::PipelineStageFlagBits2::eGeometryShader)),
		"vertex work must remain valid when optional tessellation and geometry features are disabled"
	);

	static_assert(
		map_stages(Stage::eCopy) == (vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear),
		"none of the three transfer stage bits implies the others, so a copy barrier naming only COPY never reaches a blit or a clear"
	);

	static_assert(
		map_stages(Stage::eDepthStencil) == (vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests),
		"depth testing straddles both fragment test stages and the vocabulary deliberately stopped distinguishing them"
	);

	static_assert(!map_stages(Flags<Stage>()), "an empty stage set has to stay empty, since it is what tells MapBarrierStages to derive from the use instead");

	static_assert(
		map_barrier_access(ResourceUse::eDepthStencilTarget) ==
			(vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite),
		"a depth target is read-write by definition, so naming only the write half loses the ordering against the test that reads it"
	);

	static_assert(
		map_barrier_access(ResourceUse::eAccelBuildInput) == vk::AccessFlagBits2::eShaderRead,
		"build inputs are geometry read by the build and the spec names SHADER_READ for them, the structure bits covering the structures themselves"
	);

	static_assert(
		map_barrier_access(ResourceUse::eAccelRead) == vk::AccessFlagBits2::eAccelerationStructureReadKHR &&
			map_barrier_access(ResourceUse::eAccelWrite) == vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
		"reading and building a structure are the two accesses the structure bits exist for"
	);

	static_assert(
		map_barrier_access(ResourceUse::eAccelBuildScratch) ==
			(vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR),
		"the spec names the pair for a scratch, not the write bit alone, so naming half of it leaves the build's own reads of that memory unordered"
	);

	static_assert(
		map_barrier_stages(Flags<Stage>(), ResourceUse::eAccelBuildScratch) == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
		"the same sentence names the build stage for a scratch, which is the only stage that touches it"
	);

	static_assert(
		map_barrier_access(Flags<ResourceUse>(ResourceUse::eCopySrc) | ResourceUse::eResolveSrc) == vk::AccessFlagBits2::eTransferRead,
		"Vulkan has no resolve access of its own, so a resolve reads and writes through the transfer accesses a copy uses"
	);

	static_assert(
		!map_barrier_access(ResourceUse::eDiscard) && !map_barrier_access(ResourceUse::ePresent),
		"discard and present reach no access, which is what keeps either from deriving a stage and landing a full-pipeline barrier per frame"
	);

	static_assert(
		map_barrier_stages(Flags<Stage>(), ResourceUse::eColorTarget) == vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		"a use with no stage must derive one, since an access mask with an empty stage mask is invalid under synchronization2"
	);

	static_assert(
		map_barrier_stages(Stage::eCompute, ResourceUse::eColorTarget) == vk::PipelineStageFlagBits2::eComputeShader,
		"an explicit stage set is honoured as written and never widened by what the use would have derived"
	);

	static_assert(
		!map_barrier_stages(Flags<Stage>(), Flags<ResourceUse>()) && !map_barrier_stages(Flags<Stage>(), ResourceUse::ePresent),
		"an empty state and a present state both stay empty, which is what every swept before-state relies on"
	);

	static_assert(
		map_barrier_stages(Flags<Stage>(), ResourceUse::eSampledRead) == vk::PipelineStageFlagBits2::eAllCommands,
		"a shader read names no stage the barrier can narrow to, so the derivation stays conservative rather than guessing one"
	);

	static_assert(
		is_one_timestamp_stage(Flags<Stage>()) && is_one_timestamp_stage(Stage::eCompute) &&
			!is_one_timestamp_stage(Flags<Stage>(Stage::eCompute) | Stage::eCopy),
		"a timestamp names one point in the pipeline, so a mask carrying two is refused rather than silently reduced to one of them"
	);

	static_assert(
		timestamp_stage(Stage::eCopy) == vk::PipelineStageFlagBits2::eAllTransfer,
		"the barrier table answers eCopy with three bits and a timestamp may carry one, which ALL_TRANSFER covers without dropping blit or clear"
	);

	static_assert(
		timestamp_stage(Stage::eDepthStencil) == vk::PipelineStageFlagBits2::eLateFragmentTests,
		"the later test is the one that has all the depth work behind it, so sampling there cannot land between the two halves"
	);

	static_assert(
		timestamp_stage(Flags<Stage>()) == vk::PipelineStageFlagBits2::eAllCommands,
		"an unset stage means after everything, since an empty mask is not a pipeline stage and cannot be written as one"
	);

}

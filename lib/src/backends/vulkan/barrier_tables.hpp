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

	[[nodiscard]] constexpr vk::PipelineStageFlags2 MapStages(const Flags<Stage> stages) noexcept
	{
		vk::PipelineStageFlags2 out{};

		if (stages.Contains(Stage::eIndirectFetch))
		{
			out |= vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (stages.Contains(Stage::eVertexWork))
		{
			out |= vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::eVertexShader |
				   vk::PipelineStageFlagBits2::eTessellationControlShader | vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
				   vk::PipelineStageFlagBits2::eGeometryShader;
		}

		if (stages.Contains(Stage::eFragmentShading))
		{
			out |= vk::PipelineStageFlagBits2::eFragmentShader;
		}

		if (stages.Contains(Stage::eDepthStencil))
		{
			out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (stages.Contains(Stage::eColorOutput))
		{
			out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (stages.Contains(Stage::eCompute))
		{
			out |= vk::PipelineStageFlagBits2::eComputeShader;
		}

		if (stages.Contains(Stage::eCopy))
		{
			out |= vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear;
		}

		if (stages.Contains(Stage::eResolve))
		{
			out |= vk::PipelineStageFlagBits2::eResolve;
		}

		if (stages.Contains(Stage::eHost))
		{
			out |= vk::PipelineStageFlagBits2::eHost;
		}

		if (stages.Contains(Stage::eRayTracing))
		{
			out |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		if (stages.Contains(Stage::eAccelBuild))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (stages.Contains(Stage::eAllGraphics))
		{
			out |= vk::PipelineStageFlagBits2::eAllGraphics;
		}

		if (stages.Contains(Stage::eAllCommands))
		{
			out |= vk::PipelineStageFlagBits2::eAllCommands;
		}

		return out;
	}

	[[nodiscard]] constexpr vk::AccessFlags2 MapBarrierAccess(const Flags<ResourceUse> use) noexcept
	{
		vk::AccessFlags2 out{};

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			out |= vk::AccessFlagBits2::eIndirectCommandRead;
		}

		if (use.Contains(ResourceUse::eVertexBuffer))
		{
			out |= vk::AccessFlagBits2::eVertexAttributeRead;
		}

		if (use.Contains(ResourceUse::eIndexBuffer))
		{
			out |= vk::AccessFlagBits2::eIndexRead;
		}

		if (use.Contains(ResourceUse::eUniformRead))
		{
			out |= vk::AccessFlagBits2::eUniformRead;
		}

		if (use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead))
		{
			out |= vk::AccessFlagBits2::eShaderRead;
		}

		if (use.Contains(ResourceUse::eStorageWrite))
		{
			out |= vk::AccessFlagBits2::eShaderWrite;
		}

		if (use.Contains(ResourceUse::eColorTarget))
		{
			out |= vk::AccessFlagBits2::eColorAttachmentWrite;
		}

		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
		}

		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		}

		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eResolveSrc))
		{
			out |= vk::AccessFlagBits2::eTransferRead;
		}

		if (use.Contains(ResourceUse::eCopyDst) || use.Contains(ResourceUse::eResolveDst))
		{
			out |= vk::AccessFlagBits2::eTransferWrite;
		}

		if (use.Contains(ResourceUse::eHostRead))
		{
			out |= vk::AccessFlagBits2::eHostRead;
		}

		if (use.Contains(ResourceUse::eHostWrite))
		{
			out |= vk::AccessFlagBits2::eHostWrite;
		}

		if (use.Contains(ResourceUse::eAccelBuildInput))
		{
			out |= vk::AccessFlagBits2::eShaderRead;
		}

		if (use.Contains(ResourceUse::eAccelRead))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR;
		}

		if (use.Contains(ResourceUse::eAccelWrite))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
		}

		if (use.Contains(ResourceUse::eAccelBuildScratch))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
		}

		return out;
	}

	[[nodiscard]] constexpr vk::PipelineStageFlags2 DeriveBarrierStages(const Flags<ResourceUse> use) noexcept
	{
		vk::PipelineStageFlags2 out{};

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			out |= vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (use.Contains(ResourceUse::eVertexBuffer) || use.Contains(ResourceUse::eIndexBuffer))
		{
			out |= vk::PipelineStageFlagBits2::eVertexInput;
		}

		if (use.Contains(ResourceUse::eUniformRead) || use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead) ||
			use.Contains(ResourceUse::eStorageWrite))
		{
			out |= vk::PipelineStageFlagBits2::eAllCommands;
		}

		if (use.Contains(ResourceUse::eColorTarget))
		{
			out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (use.Contains(ResourceUse::eDepthStencilTarget) || use.Contains(ResourceUse::eDepthStencilRead))
		{
			out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eCopyDst))
		{
			out |= vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear;
		}

		if (use.Contains(ResourceUse::eResolveSrc) || use.Contains(ResourceUse::eResolveDst))
		{
			out |= vk::PipelineStageFlagBits2::eResolve;
		}

		if (use.Contains(ResourceUse::eHostRead) || use.Contains(ResourceUse::eHostWrite))
		{
			out |= vk::PipelineStageFlagBits2::eHost;
		}

		if (use.Contains(ResourceUse::eAccelBuildInput) || use.Contains(ResourceUse::eAccelWrite) || use.Contains(ResourceUse::eAccelBuildScratch))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (use.Contains(ResourceUse::eAccelRead))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		return out ? out : vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eAllCommands);
	}

	[[nodiscard]] constexpr vk::PipelineStageFlags2 MapBarrierStages(const Flags<Stage> stages, const Flags<ResourceUse> use) noexcept
	{
		if (!stages.Empty())
		{
			return MapStages(stages);
		}

		return MapBarrierAccess(use) ? DeriveBarrierStages(use) : vk::PipelineStageFlags2{};
	}

	[[nodiscard]] constexpr vk::PipelineStageFlagBits2 TimestampStage(const Flags<Stage> stage) noexcept
	{
		if (stage.Contains(Stage::eIndirectFetch))
		{
			return vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (stage.Contains(Stage::eVertexWork))
		{
			return vk::PipelineStageFlagBits2::eVertexShader;
		}

		if (stage.Contains(Stage::eFragmentShading))
		{
			return vk::PipelineStageFlagBits2::eFragmentShader;
		}

		if (stage.Contains(Stage::eDepthStencil))
		{
			return vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (stage.Contains(Stage::eColorOutput))
		{
			return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (stage.Contains(Stage::eCompute))
		{
			return vk::PipelineStageFlagBits2::eComputeShader;
		}

		if (stage.Contains(Stage::eCopy))
		{
			return vk::PipelineStageFlagBits2::eAllTransfer;
		}

		if (stage.Contains(Stage::eResolve))
		{
			return vk::PipelineStageFlagBits2::eResolve;
		}

		if (stage.Contains(Stage::eHost))
		{
			return vk::PipelineStageFlagBits2::eHost;
		}

		if (stage.Contains(Stage::eRayTracing))
		{
			return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		if (stage.Contains(Stage::eAccelBuild))
		{
			return vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (stage.Contains(Stage::eAllGraphics))
		{
			return vk::PipelineStageFlagBits2::eAllGraphics;
		}

		return vk::PipelineStageFlagBits2::eAllCommands;
	}

	static_assert(MapStages(Stage::eVertexWork) == (vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::eVertexShader |
													   vk::PipelineStageFlagBits2::eTessellationControlShader |
													   vk::PipelineStageFlagBits2::eTessellationEvaluationShader | vk::PipelineStageFlagBits2::eGeometryShader),
		"one semantic vertex bit stands for the whole Vulkan front end, and dropping any of the five leaves that stage's work unordered");

	static_assert(MapStages(Stage::eCopy) == (vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear),
		"none of the three transfer stage bits implies the others, so a copy barrier naming only COPY never reaches a blit or a clear");

	static_assert(MapStages(Stage::eDepthStencil) == (vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests),
		"depth testing straddles both fragment test stages and the vocabulary deliberately stopped distinguishing them");

	static_assert(!MapStages(Flags<Stage>()), "an empty stage set has to stay empty, since it is what tells MapBarrierStages to derive from the use instead");

	static_assert(MapBarrierAccess(ResourceUse::eDepthStencilTarget) ==
					  (vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite),
		"a depth target is read-write by definition, so naming only the write half loses the ordering against the test that reads it");

	static_assert(MapBarrierAccess(ResourceUse::eAccelBuildInput) == vk::AccessFlagBits2::eShaderRead,
		"build inputs are geometry read by the build and the spec names SHADER_READ for them, the structure bits covering the structures themselves");

	static_assert(MapBarrierAccess(ResourceUse::eAccelRead) == vk::AccessFlagBits2::eAccelerationStructureReadKHR &&
					  MapBarrierAccess(ResourceUse::eAccelWrite) == vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
		"reading and building a structure are the two accesses the structure bits exist for");

	static_assert(MapBarrierAccess(ResourceUse::eAccelBuildScratch) ==
					  (vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR),
		"the spec names the pair for a scratch, not the write bit alone, so naming half of it leaves the build's own reads of that memory unordered");

	static_assert(MapBarrierStages(Flags<Stage>(), ResourceUse::eAccelBuildScratch) == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
		"the same sentence names the build stage for a scratch, which is the only stage that touches it");

	static_assert(MapBarrierAccess(Flags<ResourceUse>(ResourceUse::eCopySrc) | ResourceUse::eResolveSrc) == vk::AccessFlagBits2::eTransferRead,
		"Vulkan has no resolve access of its own, so a resolve reads and writes through the transfer accesses a copy uses");

	static_assert(!MapBarrierAccess(ResourceUse::eDiscard) && !MapBarrierAccess(ResourceUse::ePresent),
		"discard and present reach no access, which is what keeps either from deriving a stage and landing a full-pipeline barrier per frame");

	static_assert(MapBarrierStages(Flags<Stage>(), ResourceUse::eColorTarget) == vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		"a use with no stage must derive one, since an access mask with an empty stage mask is invalid under synchronization2");

	static_assert(MapBarrierStages(Stage::eCompute, ResourceUse::eColorTarget) == vk::PipelineStageFlagBits2::eComputeShader,
		"an explicit stage set is honoured as written and never widened by what the use would have derived");

	static_assert(!MapBarrierStages(Flags<Stage>(), Flags<ResourceUse>()) && !MapBarrierStages(Flags<Stage>(), ResourceUse::ePresent),
		"an empty state and a present state both stay empty, which is what every swept before-state relies on");

	static_assert(MapBarrierStages(Flags<Stage>(), ResourceUse::eSampledRead) == vk::PipelineStageFlagBits2::eAllCommands,
		"a shader read names no stage the barrier can narrow to, so the derivation stays conservative rather than guessing one");

	static_assert(
		IsOneTimestampStage(Flags<Stage>()) && IsOneTimestampStage(Stage::eCompute) && !IsOneTimestampStage(Flags<Stage>(Stage::eCompute) | Stage::eCopy),
		"a timestamp names one point in the pipeline, so a mask carrying two is refused rather than silently reduced to one of them");

	static_assert(TimestampStage(Stage::eCopy) == vk::PipelineStageFlagBits2::eAllTransfer,
		"the barrier table answers eCopy with three bits and a timestamp may carry one, which ALL_TRANSFER covers without dropping blit or clear");

	static_assert(TimestampStage(Stage::eDepthStencil) == vk::PipelineStageFlagBits2::eLateFragmentTests,
		"the later test is the one that has all the depth work behind it, so sampling there cannot land between the two halves");

	static_assert(TimestampStage(Flags<Stage>()) == vk::PipelineStageFlagBits2::eAllCommands,
		"an unset stage means after everything, since an empty mask is not a pipeline stage and cannot be written as one");

}

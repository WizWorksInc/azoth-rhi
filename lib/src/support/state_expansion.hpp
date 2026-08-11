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
 * \brief Expands a semantic ResourceState into the stage, access, and layout triple the backends lowered from before the semantic API landed.
 *
 * Transitional. Each backend drops this the moment it lowers ResourceUse and Stage natively, and the header goes with the last one.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/flags.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>

namespace azo::rhi
{

	enum class PipelineStage : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		eDrawIndirect = 1ull << 0u,
		eVertexInput  = 1ull << 1u,

		eVertexShader				  = 1ull << 2u,
		eTessellationControlShader	  = 1ull << 3u,
		eTessellationEvaluationShader = 1ull << 4u,
		eGeometryShader				  = 1ull << 5u,
		eFragmentShader				  = 1ull << 6u,

		eEarlyFragmentTests = 1ull << 7u,
		eLateFragmentTests	= 1ull << 8u,

		eColorOutput   = 1ull << 9u,
		eComputeShader = 1ull << 10u,

		eCopy	 = 1ull << 11u,
		eResolve = 1ull << 12u,
		eClear	 = 1ull << 13u,
		eHost	 = 1ull << 14u,

		eRayTracingShader			= 1ull << 15u,
		eAccelerationStructureBuild = 1ull << 16u,

		eAllGraphics = 1ull << 30u,
		eAllCommands = 1ull << 31u,
	};

	enum class Access : std::uint64_t // NOLINT(performance-enum-size)
	{
		eNone = 0,

		eIndirectRead = 1ull << 0u,
		eVertexRead	  = 1ull << 1u,
		eIndexRead	  = 1ull << 2u,
		eConstantRead = 1ull << 3u,

		eShaderRead	 = 1ull << 4u,
		eShaderWrite = 1ull << 5u,

		eColorRead	= 1ull << 6u,
		eColorWrite = 1ull << 7u,

		eDepthStencilRead  = 1ull << 8u,
		eDepthStencilWrite = 1ull << 9u,

		eCopyRead  = 1ull << 10u,
		eCopyWrite = 1ull << 11u,

		eHostRead  = 1ull << 12u,
		eHostWrite = 1ull << 13u,

		eMemoryRead	 = 1ull << 14u,
		eMemoryWrite = 1ull << 15u,

		eAccelerationStructureRead	= 1ull << 16u,
		eAccelerationStructureWrite = 1ull << 17u,
	};

	enum class TextureLayout : std::uint8_t
	{
		eUndefined,
		eGeneral,

		eColorAttachment,
		eDepthStencilAttachment,
		eDepthStencilReadOnly,
		eShaderReadOnly,

		eCopySrc,
		eCopyDst,

		eResolveSrc,
		eResolveDst,

		ePresent,
	};

	struct ExpandedState final
	{
		Flags<PipelineStage> stages = PipelineStage::eNone;
		Flags<Access> access		= Access::eNone;
		TextureLayout layout		= TextureLayout::eUndefined;
	};

	[[nodiscard]] constexpr Flags<Access> ExpandAccess(const Flags<ResourceUse> use) noexcept
	{
		Flags<Access> out{};

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			out |= Access::eIndirectRead;
		}
		if (use.Contains(ResourceUse::eVertexBuffer))
		{
			out |= Access::eVertexRead;
		}
		if (use.Contains(ResourceUse::eIndexBuffer))
		{
			out |= Access::eIndexRead;
		}
		if (use.Contains(ResourceUse::eUniformRead))
		{
			out |= Access::eConstantRead;
		}
		if (use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead))
		{
			out |= Access::eShaderRead;
		}
		if (use.Contains(ResourceUse::eStorageWrite))
		{
			out |= Access::eShaderWrite;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			out |= Access::eColorWrite;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			out |= Flags<Access>(Access::eDepthStencilRead) | Access::eDepthStencilWrite;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			out |= Access::eDepthStencilRead;
		}
		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eResolveSrc))
		{
			out |= Access::eCopyRead;
		}
		if (use.Contains(ResourceUse::eCopyDst) || use.Contains(ResourceUse::eResolveDst))
		{
			out |= Access::eCopyWrite;
		}
		if (use.Contains(ResourceUse::eHostRead))
		{
			out |= Access::eHostRead;
		}
		if (use.Contains(ResourceUse::eHostWrite))
		{
			out |= Access::eHostWrite;
		}
		if (use.Contains(ResourceUse::eAccelBuildInput) || use.Contains(ResourceUse::eAccelRead))
		{
			out |= Access::eAccelerationStructureRead;
		}
		if (use.Contains(ResourceUse::eAccelWrite))
		{
			out |= Access::eAccelerationStructureWrite;
		}

		return out;
	}

	[[nodiscard]] constexpr TextureLayout ExpandLayout(const Flags<ResourceUse> use) noexcept
	{
		if (use.Contains(ResourceUse::eDiscard))
		{
			return TextureLayout::eUndefined;
		}
		if (use.Contains(ResourceUse::ePresent))
		{
			return TextureLayout::ePresent;
		}
		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			return TextureLayout::eGeneral;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead) && use.Contains(ResourceUse::eSampledRead))
		{
			return TextureLayout::eDepthStencilReadOnly;
		}

		TextureLayout chosen = TextureLayout::eUndefined;
		int distinct		 = 0;

		if (use.Contains(ResourceUse::eColorTarget))
		{
			chosen = TextureLayout::eColorAttachment;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			chosen = TextureLayout::eDepthStencilAttachment;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			chosen = TextureLayout::eDepthStencilReadOnly;
			++distinct;
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			chosen = TextureLayout::eShaderReadOnly;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			chosen = TextureLayout::eCopySrc;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			chosen = TextureLayout::eCopyDst;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			chosen = TextureLayout::eResolveSrc;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			chosen = TextureLayout::eResolveDst;
			++distinct;
		}

		return distinct > 1 ? TextureLayout::eGeneral : chosen;
	}

	[[nodiscard]] constexpr Flags<PipelineStage> DeriveStages(const Flags<ResourceUse> use) noexcept
	{
		Flags<PipelineStage> out{};

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			out |= PipelineStage::eDrawIndirect;
		}
		if (use.Contains(ResourceUse::eVertexBuffer) || use.Contains(ResourceUse::eIndexBuffer))
		{
			out |= PipelineStage::eVertexInput;
		}
		if (use.Contains(ResourceUse::eUniformRead) || use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead) ||
			use.Contains(ResourceUse::eStorageWrite))
		{
			out |= PipelineStage::eAllCommands;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			out |= PipelineStage::eColorOutput;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget) || use.Contains(ResourceUse::eDepthStencilRead))
		{
			out |= Flags<PipelineStage>(PipelineStage::eEarlyFragmentTests) | PipelineStage::eLateFragmentTests;
		}
		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eCopyDst))
		{
			out |= Flags<PipelineStage>(PipelineStage::eCopy) | PipelineStage::eClear;
		}
		if (use.Contains(ResourceUse::eResolveSrc) || use.Contains(ResourceUse::eResolveDst))
		{
			out |= PipelineStage::eResolve;
		}
		if (use.Contains(ResourceUse::eHostRead) || use.Contains(ResourceUse::eHostWrite))
		{
			out |= PipelineStage::eHost;
		}
		if (use.Contains(ResourceUse::eAccelBuildInput) || use.Contains(ResourceUse::eAccelWrite))
		{
			out |= PipelineStage::eAccelerationStructureBuild;
		}
		if (use.Contains(ResourceUse::eAccelRead))
		{
			out |= Flags<PipelineStage>(PipelineStage::eAccelerationStructureBuild) | PipelineStage::eRayTracingShader;
		}

		return out.Empty() ? Flags<PipelineStage>(PipelineStage::eAllCommands) : out;
	}

	[[nodiscard]] constexpr Flags<PipelineStage> ExpandStages(const Flags<Stage> stages) noexcept
	{
		Flags<PipelineStage> out{};

		if (stages.Contains(Stage::eIndirectFetch))
		{
			out |= PipelineStage::eDrawIndirect;
		}
		if (stages.Contains(Stage::eVertexWork))
		{
			out |= Flags<PipelineStage>(PipelineStage::eVertexInput) | PipelineStage::eVertexShader | PipelineStage::eTessellationControlShader |
				   PipelineStage::eTessellationEvaluationShader | PipelineStage::eGeometryShader;
		}
		if (stages.Contains(Stage::eFragmentShading))
		{
			out |= PipelineStage::eFragmentShader;
		}
		if (stages.Contains(Stage::eDepthStencil))
		{
			out |= Flags<PipelineStage>(PipelineStage::eEarlyFragmentTests) | PipelineStage::eLateFragmentTests;
		}
		if (stages.Contains(Stage::eColorOutput))
		{
			out |= PipelineStage::eColorOutput;
		}
		if (stages.Contains(Stage::eCompute))
		{
			out |= PipelineStage::eComputeShader;
		}
		if (stages.Contains(Stage::eCopy))
		{
			out |= Flags<PipelineStage>(PipelineStage::eCopy) | PipelineStage::eClear;
		}
		if (stages.Contains(Stage::eResolve))
		{
			out |= PipelineStage::eResolve;
		}
		if (stages.Contains(Stage::eHost))
		{
			out |= PipelineStage::eHost;
		}
		if (stages.Contains(Stage::eRayTracing))
		{
			out |= PipelineStage::eRayTracingShader;
		}
		if (stages.Contains(Stage::eAccelBuild))
		{
			out |= PipelineStage::eAccelerationStructureBuild;
		}
		if (stages.Contains(Stage::eAllGraphics))
		{
			out |= PipelineStage::eAllGraphics;
		}
		if (stages.Contains(Stage::eAllCommands))
		{
			out |= PipelineStage::eAllCommands;
		}

		return out;
	}

	[[nodiscard]] constexpr ExpandedState Expand(const ResourceState & state) noexcept
	{
		const Flags<Access> access = ExpandAccess(state.use);

		// Derive only where there is an access to protect. A use that reaches no access has no hazard, and deriving there costs a full-pipeline barrier.
		const bool derive = state.stages.Empty() && !access.Empty();

		return ExpandedState{ .stages = derive ? DeriveStages(state.use) : ExpandStages(state.stages), .access = access, .layout = ExpandLayout(state.use) };
	}

	static_assert(Expand(ResourceState{}).stages.Empty(), "an empty state must stay empty, which is what every swept before-state relies on");
	static_assert(Expand(ResourceState{ .use = ResourceUse::ePresent }).stages.Empty(),
		"present and discard reach no access, so neither may derive a stage and land a full-pipeline barrier on the per-frame path");
	static_assert(Expand(ResourceState{ .use = ResourceUse::eColorTarget }).stages.Contains(PipelineStage::eColorOutput),
		"a use with no stage must derive one, since an access mask with an empty stage mask is invalid under synchronization2");

} // namespace azo::rhi

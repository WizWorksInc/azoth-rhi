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

#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bench
{

	constexpr std::uint32_t kTargetExtent = 64;

	// One full push-constant block, since Metal takes the block whole and refuses a partial update.
	constexpr std::uint32_t kPushConstantBytes = 64;

	constexpr std::uint64_t kScratchBufferBytes = 1024;

	// Three vertices at one point so a draw assembles a triangle of no area and the pass leaves no fragment work behind to wait on.
	constexpr std::string_view kMetalSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut
{
	float4 position [[position]];
};

vertex VertexOut vertexMain(uint id [[vertex_id]])
{
	return VertexOut{ float4(0.0, 0.0, 0.0, 1.0) };
}

fragment float4 fragmentMain(VertexOut fragment_in [[stage_in]])
{
	return float4(1.0, 1.0, 1.0, 1.0);
}
)";

	/**
	 * \brief The command shapes the two arms are compared on.
	 *
	 * One shape a pass, because the dispatch cost differs per shape and a mixed sequence would report an average of whatever the mix happened to be.
	 */
	enum class Kind : std::uint8_t
	{
		eSetViewport,
		eSetScissor,
		ePushConstants,
		eBindDescriptorSet,
		eSetGraphicsPipeline,
		eDraw,
		eDrawIndexed,
		eBarrier,
	};

	constexpr std::array kKinds{
		Kind::eSetViewport,
		Kind::eSetScissor,
		Kind::ePushConstants,
		Kind::eBindDescriptorSet,
		Kind::eSetGraphicsPipeline,
		Kind::eDraw,
		Kind::eDrawIndexed,
		Kind::eBarrier,
	};

	[[nodiscard]] constexpr std::string_view KindName(const Kind kind)
	{
		switch (kind)
		{
		case Kind::eSetViewport:		 return "SetViewport";
		case Kind::eSetScissor:			 return "SetScissor";
		case Kind::ePushConstants:		 return "PushConstants";
		case Kind::eBindDescriptorSet:	 return "BindDescriptorSet";
		case Kind::eSetGraphicsPipeline: return "SetGraphicsPipeline";
		case Kind::eDraw:				 return "Draw";
		case Kind::eDrawIndexed:		 return "DrawIndexed";
		case Kind::eBarrier:			 return "Barriers";
		}

		return "unknown";
	}

	// The three shapes that only mean anything with a pipeline: two that need one bound to be a legal draw, one that is the bind itself.
	[[nodiscard]] constexpr bool NeedsPipeline(const Kind kind)
	{
		return kind == Kind::eSetGraphicsPipeline || kind == Kind::eDraw || kind == Kind::eDrawIndexed;
	}

	// A barrier belongs outside a render pass and Vulkan says so. The barrier pass therefore records with no rendering scope open and every other pass records
	// inside one.
	[[nodiscard]] constexpr bool NeedsRenderingScope(const Kind kind)
	{
		return kind != Kind::eBarrier;
	}

	/**
	 * \brief The arguments both arms record, built once so neither arm pays for building them inside its loop.
	 */
	struct Workload final
	{
		azo::rhi::Viewport viewport{
			.width	= static_cast<float>(kTargetExtent),
			.height = static_cast<float>(kTargetExtent),
		};
		azo::rhi::Rect2D scissor{ .width = kTargetExtent, .height = kTargetExtent };
		std::array<std::uint32_t, kPushConstantBytes / sizeof(std::uint32_t)> pushConstants{};

		azo::rhi::TextureHandle target{};
		azo::rhi::BufferHandle scratch{};
		azo::rhi::PipelineLayoutHandle layout{};
		azo::rhi::DescriptorSetHandle set{};
		azo::rhi::GraphicsPipelineHandle pipeline{};

		// A barrier that leaves the target where it already is. Same-state so a pass can record it two million times and still describe the truth.
		std::array<azo::rhi::TextureBarrier, 1> holdBarrier{};
	};

} // namespace bench

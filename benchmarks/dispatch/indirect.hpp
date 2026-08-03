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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include <cstdint>
#include <span>

namespace bench
{

	/**
	 * \brief The floor: one indirect call through a table, with nothing behind it.
	 *
	 * The entries carry the signatures RenderCommandApi declares, so the arm calls what the RHI calls with the same arguments in the same registers, and behind
	 * each is a function returning true. What is left is the call itself.
	 *
	 * One hop where the RHI is two: a CommandList reads the render block out of a block set first, and that hop is part of the delta.
	 */
	struct IndirectApi final
	{
		bool (*setGraphicsPipeline)(void * impl, azo::rhi::GraphicsPipelineHandle pipeline, azo::rhi::Error * error) noexcept = nullptr;

		bool (*bindDescriptorSet)(void * impl, azo::rhi::PipelineLayoutHandle layout, std::uint32_t setIndex, azo::rhi::DescriptorSetHandle set,
			std::span<const azo::rhi::DynamicDescriptorOffset> dynamicOffsets, azo::rhi::Error * error) noexcept = nullptr;
		bool (*pushConstants)(void * impl, azo::rhi::PipelineLayoutHandle layout, azo::rhi::Flags<azo::rhi::ShaderStage> stages, std::uint32_t offset,
			std::uint32_t size, const void * data, azo::rhi::Error * error) noexcept							 = nullptr;

		bool (*setViewport)(void * impl, const azo::rhi::Viewport & viewport, azo::rhi::Error * error) noexcept = nullptr;
		bool (*setScissor)(void * impl, const azo::rhi::Rect2D & scissor, azo::rhi::Error * error) noexcept		= nullptr;

		bool (*draw)(void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance,
			azo::rhi::Error * error) noexcept							   = nullptr;
		bool (*drawIndexed)(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, azo::rhi::Error * error) noexcept = nullptr;

		bool (*barriers)(void * impl, const azo::rhi::BarrierBatch & barriers, azo::rhi::Error * error) noexcept = nullptr;
	};

	/**
	 * \brief The table, in a translation unit of its own so a compiler knows no more about this arm than about the RHI's block.
	 *
	 * A table defined beside the loop that calls it is one a compiler can read the entries out of and inline, timing the arm at nothing and leaving the delta
	 * reporting the whole of the RHI. Answering it from here keeps the two arms under the same optimiser.
	 */
	[[nodiscard]] const IndirectApi & IndirectBlock() noexcept;

	/**
	 * \brief Somewhere for the impl argument to point so the call passes what the RHI passes.
	 *
	 * The RHI hands its entries the command list implementation. Nothing here reads it, but a null constant in that register is something a compiler can fold
	 * against and a real address is not.
	 */
	[[nodiscard]] void * IndirectImpl() noexcept;

} // namespace bench

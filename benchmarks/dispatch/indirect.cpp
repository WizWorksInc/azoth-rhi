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

#include "dispatch/indirect.hpp"

#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include <cstdint>
#include <span>

namespace rhi = azo::rhi;

namespace bench
{

	namespace
	{

		/*
		 * Every entry takes its arguments and answers true, which is the whole of this arm.
		 *
		 * Written out one at a time and not folded into a variadic template so each one is a distinct symbol with the argument list it is standing in for. A
		 * template collapses to one body a compiler can share between shapes. The shapes would then stop being separately measurable.
		 */
		bool IndirectSetGraphicsPipeline(
			[[maybe_unused]] void * impl, [[maybe_unused]] rhi::GraphicsPipelineHandle pipeline, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectBindDescriptorSet([[maybe_unused]] void * impl, [[maybe_unused]] rhi::PipelineLayoutHandle layout, [[maybe_unused]] std::uint32_t setIndex,
			[[maybe_unused]] rhi::DescriptorSetHandle set, [[maybe_unused]] std::span<const rhi::DynamicDescriptorOffset> dynamicOffsets,
			[[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectPushConstants([[maybe_unused]] void * impl, [[maybe_unused]] rhi::PipelineLayoutHandle layout,
			[[maybe_unused]] rhi::Flags<rhi::ShaderStage> stages, [[maybe_unused]] std::uint32_t offset, [[maybe_unused]] std::uint32_t size,
			[[maybe_unused]] const void * data, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectSetViewport([[maybe_unused]] void * impl, [[maybe_unused]] const rhi::Viewport & viewport, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectSetScissor([[maybe_unused]] void * impl, [[maybe_unused]] const rhi::Rect2D & scissor, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectDraw([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount,
			[[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectDrawIndexed([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t indexCount, [[maybe_unused]] std::uint32_t instanceCount,
			[[maybe_unused]] std::uint32_t firstIndex, [[maybe_unused]] std::int32_t vertexOffset, [[maybe_unused]] std::uint32_t firstInstance,
			[[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		bool IndirectBarriers([[maybe_unused]] void * impl, [[maybe_unused]] const rhi::BarrierBatch & barriers, [[maybe_unused]] rhi::Error * error) noexcept
		{
			return true;
		}

		// What impl points at. One object for the run, never read. Its address is what keeps the argument from folding to a constant.
		std::uint64_t g_impl = 0;

	} // namespace

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.setGraphicsPipeline = &IndirectSetGraphicsPipeline,
			.bindDescriptorSet	 = &IndirectBindDescriptorSet,
			.pushConstants		 = &IndirectPushConstants,
			.setViewport		 = &IndirectSetViewport,
			.setScissor			 = &IndirectSetScissor,
			.draw				 = &IndirectDraw,
			.drawIndexed		 = &IndirectDrawIndexed,
			.barriers			 = &IndirectBarriers,
		};

		return block;
	}

	void * IndirectImpl() noexcept
	{
		return &g_impl;
	}

} // namespace bench

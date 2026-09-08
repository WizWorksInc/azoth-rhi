// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	[[nodiscard]] const IndirectApi & IndirectBlock() noexcept;

	[[nodiscard]] void * IndirectImpl() noexcept;

}

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
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <cstdint>
#include <span>

namespace azo::rhi
{

	struct ClearColor final
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;
	};

	struct ClearDepthStencil final
	{
		float depth			  = 1.0f;
		std::uint32_t stencil = 0;
	};

	struct RenderingAttachment final
	{
		TextureViewHandle view{};
		ResourceState state{};
		LoadOp load	  = LoadOp::eLoad;
		StoreOp store = StoreOp::eStore;
		ClearColor clearColor{};
		ClearDepthStencil clearDepthStencil{};
	};

	struct RenderingTimestampWrites final
	{
		QueryPoolHandle pool{};
		std::uint32_t beginQuery = kInvalidIndex;
		std::uint32_t endQuery	 = kInvalidIndex;
	};

	struct BeginRenderingDesc final
	{
		std::span<const RenderingAttachment> colors;
		const RenderingAttachment * depthStencil = nullptr;

		std::uint32_t x		 = 0;
		std::uint32_t y		 = 0;
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;

		std::uint32_t layers = 1;

		const RenderingTimestampWrites * timestamps = nullptr;
	};

}

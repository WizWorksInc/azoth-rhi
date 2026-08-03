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
 * \brief Dynamic rendering clear values and attachment descriptions.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Color clear value interpreted in the attachment format domain.
	 */
	struct ClearColor final
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;
	};

	/**
	 * \brief Depth-stencil clear value.
	 */
	struct ClearDepthStencil final
	{
		float depth			  = 1.0f;
		std::uint32_t stencil = 0;
	};

	/**
	 * \brief One attachment used by a dynamic rendering scope.
	 *
	 * state describes the resource state expected when rendering begins. clearColor or clearDepthStencil is consumed only when load is LoadOp::eClear.
	 */
	struct RenderingAttachment final
	{
		TextureViewHandle view{};
		ResourceState state{};
		LoadOp load	  = LoadOp::eLoad;
		StoreOp store = StoreOp::eStore;
		ClearColor clearColor{};
		ClearDepthStencil clearDepthStencil{};
	};

	/**
	 * \brief Timestamps written where a rendering scope opens and closes.
	 *
	 * Both slots index pool, which must have been created with QueryType::eTimestamp. Either may be left at kInvalidIndex to skip that write.
	 *
	 * Some devices decline a WriteTimestamp recorded between the begin and end of a scope, which DeviceCaps::supportsTimestampWritesInScope reports, because
	 * the hardware fixes the sample points when the scope opens. These two writes sit exactly on those points.
	 */
	struct RenderingTimestampWrites final
	{
		QueryPoolHandle pool{};
		std::uint32_t beginQuery = kInvalidIndex;
		std::uint32_t endQuery	 = kInvalidIndex;
	};

	/**
	 * \brief Attachments, render area, and layer count for a dynamic rendering scope.
	 *
	 * colors, depthStencil and timestamps are borrowed by the begin-rendering call and are not retained by the command list.
	 */
	struct BeginRenderingDesc final
	{
		std::span<const RenderingAttachment> colors;
		const RenderingAttachment * depthStencil = nullptr;

		// Framebuffer-space render area in pixels.
		std::uint32_t x		 = 0;
		std::uint32_t y		 = 0;
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;

		std::uint32_t layers = 1;

		/**
		 * \brief Optional timestamp writes bracketing this scope, ignored by a device without timestamp query support.
		 */
		const RenderingTimestampWrites * timestamps = nullptr;
	};

} // namespace azo::rhi

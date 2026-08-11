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
 * \brief Lowering from the semantic barrier vocabulary onto D3D12 enhanced barriers, pinned by the assert table at the end.
 *
 * Constexpr so the rows are checked by a compile. No GPU can answer whether a row is right, but a compiler can answer whether it still says what it was
 * written to say, and this backend has no machine that can run it. Sources for every row:
 *
 * Enhanced barriers: https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html
 * Access enum: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_barrier_access
 * Raytracing build inputs: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html
 *
 * Compound assignment on these enums is not constexpr in the Windows SDK, which is why every accumulation below is written as a binary or.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"

#ifndef NOMINMAX
	#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>

namespace azo::rhi::d3d12
{

	// Restricts a sync scope to what the recording queue can name. Never a remap to another scope, since this RHI tracks no state to remap against.
	[[nodiscard]] constexpr D3D12_BARRIER_SYNC ClampSyncToQueue(const D3D12_BARRIER_SYNC sync, const QueueType queue) noexcept
	{
		D3D12_BARRIER_SYNC clamped = sync;
		switch (queue)
		{
		case QueueType::eGraphics: return sync;
		case QueueType::eCompute:
			clamped = clamped &
					  (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_COPY |
						  D3D12_BARRIER_SYNC_EXECUTE_INDIRECT | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
						  D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
						  D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO);
			break;
		case QueueType::eCopy: clamped = clamped & (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY); break;
		}
		return clamped == D3D12_BARRIER_SYNC_NONE ? D3D12_BARRIER_SYNC_ALL : clamped;
	}

	// The access counterpart. An emptied mask needs no fallback, since COMMON is the zero value and is legal against any layout on any queue.
	[[nodiscard]] constexpr D3D12_BARRIER_ACCESS ClampAccessToQueue(const D3D12_BARRIER_ACCESS access, const QueueType queue) noexcept
	{
		switch (queue)
		{
		case QueueType::eGraphics: return access;
		case QueueType::eCompute:
			return access & (D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
								D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT | D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE |
								D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE);
		case QueueType::eCopy: return access & (D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE);
		}
		return access;
	}

	// The heap counterpart. Resolve is outside both sets because the spec gates it on sampler feedback, and a dropped bit answers COMMON, which restricts nothing.
	[[nodiscard]] constexpr D3D12_BARRIER_ACCESS ClampAccessToHeap(const D3D12_BARRIER_ACCESS access, const D3D12_HEAP_TYPE heap) noexcept
	{
		// A deactivated resource names no access at all, which is not one of the accesses a heap restricts. Masking it would read as an ordinary access instead.
		if (access == D3D12_BARRIER_ACCESS_NO_ACCESS)
		{
			return access;
		}
		switch (heap)
		{
		case D3D12_HEAP_TYPE_UPLOAD:
			return access & (D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_INDEX_BUFFER |
								D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT | D3D12_BARRIER_ACCESS_COPY_SOURCE);
		case D3D12_HEAP_TYPE_READBACK: return access & D3D12_BARRIER_ACCESS_COPY_DEST;
		default:					   return access;
		}
	}

	// An acceleration structure buffer never leaves the state it was created in, so a barrier naming anything else removes the command list rather than misorders.
	[[nodiscard]] constexpr bool AccessLegalOnAccelerationStructure(const D3D12_BARRIER_ACCESS access) noexcept
	{
		constexpr D3D12_BARRIER_ACCESS allowed =
			D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
		return access == D3D12_BARRIER_ACCESS_NO_ACCESS || (access & allowed) == access;
	}

	// The layout counterpart, which bites only on compute. A copy queue is answered ahead of every layout row, and a graphics queue may name all of them.
	[[nodiscard]] constexpr D3D12_BARRIER_LAYOUT ClampLayoutToQueue(const D3D12_BARRIER_LAYOUT layout, const QueueType queue) noexcept
	{
		if (queue != QueueType::eCompute)
		{
			return layout;
		}
		switch (layout)
		{
		case D3D12_BARRIER_LAYOUT_UNDEFINED:
		case D3D12_BARRIER_LAYOUT_COMMON:
		case D3D12_BARRIER_LAYOUT_GENERIC_READ:
		case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
		case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
		case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
		case D3D12_BARRIER_LAYOUT_COPY_DEST:		return layout;
		default:									return D3D12_BARRIER_LAYOUT_COMMON;
		}
	}

	[[nodiscard]] constexpr D3D12_BARRIER_SYNC DeriveBarrierSync(const Flags<ResourceUse> use) noexcept
	{
		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			sync = sync | D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (use.Contains(ResourceUse::eVertexBuffer) || use.Contains(ResourceUse::eIndexBuffer))
		{
			sync = sync | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (use.Contains(ResourceUse::eUniformRead) || use.Contains(ResourceUse::eSampledRead) || use.Contains(ResourceUse::eStorageRead) ||
			use.Contains(ResourceUse::eStorageWrite))
		{
			sync = sync | D3D12_BARRIER_SYNC_ALL_SHADING;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			sync = sync | D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget) || use.Contains(ResourceUse::eDepthStencilRead))
		{
			sync = sync | D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		// Both rows carry the same bits as their Stage counterparts below, so the two axes cannot disagree about what a copy or an acceleration build covers.
		if (use.Contains(ResourceUse::eCopySrc) || use.Contains(ResourceUse::eCopyDst))
		{
			sync = sync | D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
		}
		if (use.Contains(ResourceUse::eResolveSrc) || use.Contains(ResourceUse::eResolveDst))
		{
			sync = sync | D3D12_BARRIER_SYNC_RESOLVE;
		}
		if (use.Contains(ResourceUse::eAccelBuildInput) || use.Contains(ResourceUse::eAccelWrite))
		{
			sync = sync | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
				   D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
		}
		if (use.Contains(ResourceUse::eAccelRead))
		{
			sync = sync | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
		}

		// A discard reaches no bit and lands here. NONE would be the tighter answer but it claims nothing touched the resource earlier in the submission.
		return sync == D3D12_BARRIER_SYNC_NONE ? D3D12_BARRIER_SYNC_ALL : sync;
	}

	[[nodiscard]] constexpr D3D12_BARRIER_SYNC MapBarrierSync(const Flags<Stage> stages, const Flags<ResourceUse> use, const QueueType queue) noexcept
	{
		if (stages.Empty())
		{
			return ClampSyncToQueue(DeriveBarrierSync(use), queue);
		}

		D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;

		if (stages.Contains(Stage::eIndirectFetch))
		{
			sync = sync | D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (stages.Contains(Stage::eVertexWork))
		{
			sync = sync | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (stages.Contains(Stage::eFragmentShading))
		{
			sync = sync | D3D12_BARRIER_SYNC_PIXEL_SHADING;
		}
		if (stages.Contains(Stage::eDepthStencil))
		{
			sync = sync | D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		}
		if (stages.Contains(Stage::eColorOutput))
		{
			sync = sync | D3D12_BARRIER_SYNC_RENDER_TARGET;
		}
		if (stages.Contains(Stage::eCompute))
		{
			sync = sync | D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		}
		if (stages.Contains(Stage::eCopy))
		{
			sync = sync | D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
		}
		if (stages.Contains(Stage::eResolve))
		{
			sync = sync | D3D12_BARRIER_SYNC_RESOLVE;
		}
		if (stages.Contains(Stage::eRayTracing))
		{
			sync = sync | D3D12_BARRIER_SYNC_RAYTRACING;
		}
		if (stages.Contains(Stage::eAccelBuild))
		{
			sync = sync | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
				   D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
		}
		if (stages.Contains(Stage::eAllGraphics))
		{
			sync = sync | D3D12_BARRIER_SYNC_DRAW;
		}
		if (stages.Contains(Stage::eAllCommands))
		{
			sync = sync | D3D12_BARRIER_SYNC_ALL;
		}

		return ClampSyncToQueue(sync == D3D12_BARRIER_SYNC_NONE ? DeriveBarrierSync(use) : sync, queue);
	}

	[[nodiscard]] constexpr D3D12_BARRIER_ACCESS MapBarrierAccess(const Flags<ResourceUse> use, const QueueType queue) noexcept
	{
		// Ahead of the clamp, which would otherwise mask NO_ACCESS away. A discard names the one access every queue can express, which is none of them.
		if (use.Contains(ResourceUse::eDiscard))
		{
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}

		D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;

		if (use.Contains(ResourceUse::eIndirectArgs))
		{
			access = access | D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
		}
		if (use.Contains(ResourceUse::eVertexBuffer))
		{
			access = access | D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		}
		if (use.Contains(ResourceUse::eIndexBuffer))
		{
			access = access | D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		}
		if (use.Contains(ResourceUse::eUniformRead))
		{
			access = access | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			access = access | D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			access = access | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		}
		if (use.Contains(ResourceUse::eColorTarget))
		{
			access = access | D3D12_BARRIER_ACCESS_RENDER_TARGET;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			access = access | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			access = access | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			access = access | D3D12_BARRIER_ACCESS_COPY_SOURCE;
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			access = access | D3D12_BARRIER_ACCESS_COPY_DEST;
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			access = access | D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			access = access | D3D12_BARRIER_ACCESS_RESOLVE_DEST;
		}
		// Build inputs are ordinary geometry buffers read as shader resources. The acceleration structure access below is for reading a built structure.
		if (use.Contains(ResourceUse::eAccelBuildInput))
		{
			access = access | D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		}
		if (use.Contains(ResourceUse::eAccelRead))
		{
			access = access | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
		}
		if (use.Contains(ResourceUse::eAccelWrite))
		{
			access = access | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
		}

		return ClampAccessToQueue(access, queue);
	}

	[[nodiscard]] constexpr D3D12_BARRIER_LAYOUT MapBarrierLayout(const Flags<ResourceUse> use, const QueueType queue) noexcept
	{
		// Ahead of the discard row, since a copy queue has no layout transitions at all: both ends of one of its barriers answer the common layout.
		if (queue == QueueType::eCopy)
		{
			return D3D12_BARRIER_LAYOUT_COMMON;
		}
		if (use.Contains(ResourceUse::eDiscard))
		{
			return D3D12_BARRIER_LAYOUT_UNDEFINED;
		}
		if (use.Contains(ResourceUse::ePresent))
		{
			return D3D12_BARRIER_LAYOUT_PRESENT;
		}
		if (use.Contains(ResourceUse::eStorageRead) || use.Contains(ResourceUse::eStorageWrite))
		{
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead) && use.Contains(ResourceUse::eSampledRead))
		{
			return ClampLayoutToQueue(D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ, queue);
		}

		D3D12_BARRIER_LAYOUT chosen = D3D12_BARRIER_LAYOUT_COMMON;
		int distinct				= 0;
		bool write					= false;

		if (use.Contains(ResourceUse::eColorTarget))
		{
			chosen = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilTarget))
		{
			chosen = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eDepthStencilRead))
		{
			chosen = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
			++distinct;
		}
		if (use.Contains(ResourceUse::eSampledRead))
		{
			chosen = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopySrc))
		{
			chosen = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eCopyDst))
		{
			chosen = D3D12_BARRIER_LAYOUT_COPY_DEST;
			write  = true;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveSrc))
		{
			chosen = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
			++distinct;
		}
		if (use.Contains(ResourceUse::eResolveDst))
		{
			chosen = D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
			write  = true;
			++distinct;
		}

		if (distinct <= 1)
		{
			return ClampLayoutToQueue(chosen, queue);
		}

		// GENERIC_READ is read only, so a combination naming a write answers COMMON, which is legal against any access. Validation rejects the pairing upstream.
		return ClampLayoutToQueue(write ? D3D12_BARRIER_LAYOUT_COMMON : D3D12_BARRIER_LAYOUT_GENERIC_READ, queue);
	}

	// The discard flag rides an undefined before-layout alone. The spec attaches no range condition, so a barrier over some mips carries it as a whole one does.
	[[nodiscard]] constexpr D3D12_TEXTURE_BARRIER_FLAGS TextureBarrierFlags(const D3D12_BARRIER_LAYOUT before) noexcept
	{
		return before == D3D12_BARRIER_LAYOUT_UNDEFINED ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;
	}

	// A clamp restricts, so its answer is a subset of what went in, or the universal scope once the mask has emptied. Substituting a third scope is the failure.
	[[nodiscard]] constexpr bool SyncClampRestricts(const D3D12_BARRIER_SYNC sync, const QueueType queue) noexcept
	{
		const D3D12_BARRIER_SYNC clamped = ClampSyncToQueue(sync, queue);
		return (clamped & sync) == clamped || clamped == D3D12_BARRIER_SYNC_ALL;
	}

	// The access clamp has no fallback, so the subset property holds with nothing attached to it.
	[[nodiscard]] constexpr bool AccessClampRestricts(const D3D12_BARRIER_ACCESS access, const QueueType queue) noexcept
	{
		return (ClampAccessToQueue(access, queue) & access) == ClampAccessToQueue(access, queue);
	}

	// The heap clamp is the same property against a different axis, with the deactivation access passing through as itself rather than as a subset of anything.
	[[nodiscard]] constexpr bool HeapClampRestricts(const D3D12_BARRIER_ACCESS access, const D3D12_HEAP_TYPE heap) noexcept
	{
		return (ClampAccessToHeap(access, heap) & access) == ClampAccessToHeap(access, heap);
	}

	// Layouts are not a bitmask, so restricting means answering the input or falling to the one layout legal against anything.
	[[nodiscard]] constexpr bool LayoutClampRestricts(const D3D12_BARRIER_LAYOUT layout, const QueueType queue) noexcept
	{
		const D3D12_BARRIER_LAYOUT clamped = ClampLayoutToQueue(layout, queue);
		return clamped == layout || clamped == D3D12_BARRIER_LAYOUT_COMMON;
	}

	static_assert(
		MapBarrierSync(Stage::eCopy, ResourceUse::eNone, QueueType::eGraphics) == (D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW) &&
			DeriveBarrierSync(ResourceUse::eCopyDst) == (D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW) &&
			DeriveBarrierSync(ResourceUse::eCopySrc) == (D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW),
		"a copy covers clears as well, since CLEAR_UNORDERED_ACCESS_VIEW is its own sync scope and Stage::eCopy is documented as copy, clear and blit");

	static_assert(
		MapBarrierSync(Stage::eVertexWork, ResourceUse::eNone, QueueType::eGraphics) == (D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_INDEX_INPUT),
		"index fetch is a sync scope separate from vertex shading, and Stage::eVertexWork is documented as covering both");

	static_assert(MapBarrierSync(Stage::eAccelBuild, ResourceUse::eNone, QueueType::eGraphics) ==
						  (D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
							  D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO) &&
					  DeriveBarrierSync(ResourceUse::eAccelWrite) ==
						  (D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
							  D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO),
		"build, copy and emit-postbuild are three sync scopes, and Stage::eAccelBuild is documented as covering build, update, copy and compaction");

	static_assert(MapBarrierAccess(ResourceUse::eAccelBuildInput, QueueType::eGraphics) == D3D12_BARRIER_ACCESS_SHADER_RESOURCE &&
					  (MapBarrierAccess(ResourceUse::eAccelBuildInput, QueueType::eGraphics) & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ) ==
						  D3D12_BARRIER_ACCESS_COMMON,
		"a build reads its geometry as a shader resource, and the acceleration-structure access is for reading a built structure instead");

	static_assert(MapBarrierSync(Flags<Stage>(Stage::eHost) | Stage::eCompute, ResourceUse::eNone, QueueType::eGraphics) ==
						  MapBarrierSync(Stage::eCompute, ResourceUse::eNone, QueueType::eGraphics) &&
					  MapBarrierAccess(Flags<ResourceUse>(ResourceUse::eHostRead) | ResourceUse::eSampledRead, QueueType::eGraphics) ==
						  MapBarrierAccess(ResourceUse::eSampledRead, QueueType::eGraphics) &&
					  MapBarrierAccess(ResourceUse::eHostWrite, QueueType::eGraphics) == D3D12_BARRIER_ACCESS_COMMON,
		"D3D12 has no host scope on either axis, so naming one adds nothing rather than mapping to something near it");

	static_assert(ClampSyncToQueue(D3D12_BARRIER_SYNC_COPY, QueueType::eCopy) == D3D12_BARRIER_SYNC_COPY &&
					  ClampSyncToQueue(D3D12_BARRIER_SYNC_ALL, QueueType::eCopy) == D3D12_BARRIER_SYNC_ALL &&
					  ClampSyncToQueue(D3D12_BARRIER_SYNC_COPY | D3D12_BARRIER_SYNC_PIXEL_SHADING, QueueType::eCopy) == D3D12_BARRIER_SYNC_COPY &&
					  ClampSyncToQueue(D3D12_BARRIER_SYNC_RENDER_TARGET, QueueType::eCopy) == D3D12_BARRIER_SYNC_ALL &&
					  ClampAccessToQueue(D3D12_BARRIER_ACCESS_COPY_DEST, QueueType::eCopy) == D3D12_BARRIER_ACCESS_COPY_DEST &&
					  ClampAccessToQueue(D3D12_BARRIER_ACCESS_COPY_SOURCE, QueueType::eCopy) == D3D12_BARRIER_ACCESS_COPY_SOURCE &&
					  ClampAccessToQueue(D3D12_BARRIER_ACCESS_RENDER_TARGET, QueueType::eCopy) == D3D12_BARRIER_ACCESS_COMMON,
		"a copy queue admits only the copy and all sync scopes and the two copy accesses, and everything else falls back rather than crossing over");

	static_assert(ClampSyncToQueue(D3D12_BARRIER_SYNC_PIXEL_SHADING, QueueType::eCompute) == D3D12_BARRIER_SYNC_ALL &&
					  ClampSyncToQueue(D3D12_BARRIER_SYNC_RENDER_TARGET | D3D12_BARRIER_SYNC_COMPUTE_SHADING, QueueType::eCompute) ==
						  D3D12_BARRIER_SYNC_COMPUTE_SHADING &&
					  ClampAccessToQueue(D3D12_BARRIER_ACCESS_RENDER_TARGET | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, QueueType::eCompute) ==
						  D3D12_BARRIER_ACCESS_UNORDERED_ACCESS &&
					  ClampAccessToQueue(D3D12_BARRIER_ACCESS_VERTEX_BUFFER, QueueType::eCompute) == D3D12_BARRIER_ACCESS_COMMON &&
					  ClampLayoutToQueue(D3D12_BARRIER_LAYOUT_RENDER_TARGET, QueueType::eCompute) == D3D12_BARRIER_LAYOUT_COMMON &&
					  ClampLayoutToQueue(D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, QueueType::eCompute) == D3D12_BARRIER_LAYOUT_COMMON &&
					  ClampLayoutToQueue(D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, QueueType::eCompute) == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
		"a compute queue has no raster scopes, no vertex or index access and none of the render-target or depth-stencil layouts");

	static_assert(MapBarrierLayout(ResourceUse::eDiscard, QueueType::eCopy) == D3D12_BARRIER_LAYOUT_COMMON &&
					  MapBarrierLayout(ResourceUse::eDiscard, QueueType::eGraphics) == D3D12_BARRIER_LAYOUT_UNDEFINED &&
					  MapBarrierLayout(ResourceUse::eCopyDst, QueueType::eCopy) == D3D12_BARRIER_LAYOUT_COMMON,
		"a copy queue transitions no layouts at all, so a discard there answers common and never undefined, which would be a transition");

	static_assert(ClampAccessToHeap(D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_HEAP_TYPE_UPLOAD) == D3D12_BARRIER_ACCESS_COPY_SOURCE &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_CONSTANT_BUFFER, D3D12_HEAP_TYPE_UPLOAD) == D3D12_BARRIER_ACCESS_CONSTANT_BUFFER &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_HEAP_TYPE_UPLOAD) == D3D12_BARRIER_ACCESS_COMMON &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_HEAP_TYPE_UPLOAD) == D3D12_BARRIER_ACCESS_COMMON &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_HEAP_TYPE_READBACK) == D3D12_BARRIER_ACCESS_COPY_DEST &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_HEAP_TYPE_READBACK) == D3D12_BARRIER_ACCESS_COMMON &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_HEAP_TYPE_READBACK) == D3D12_BARRIER_ACCESS_COMMON &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_HEAP_TYPE_DEFAULT) == D3D12_BARRIER_ACCESS_RENDER_TARGET &&
					  ClampAccessToHeap(D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_HEAP_TYPE_UPLOAD) == D3D12_BARRIER_ACCESS_NO_ACCESS,
		"an upload heap buffer is read and copied from, a readback heap buffer is only copied into, and a device-local one is restricted by its queue alone");

	static_assert(TextureBarrierFlags(MapBarrierLayout(ResourceUse::eDiscard, QueueType::eGraphics)) == D3D12_TEXTURE_BARRIER_FLAG_DISCARD &&
					  TextureBarrierFlags(MapBarrierLayout(ResourceUse::eDiscard, QueueType::eCopy)) == D3D12_TEXTURE_BARRIER_FLAG_NONE &&
					  TextureBarrierFlags(MapBarrierLayout(ResourceUse::eCopyDst, QueueType::eGraphics)) == D3D12_TEXTURE_BARRIER_FLAG_NONE,
		"only an undefined before-layout discards, so a copy queue, which transitions no layouts and never names undefined, initialises no metadata either");

	static_assert(AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eAccelRead, QueueType::eCompute)) &&
					  AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eAccelWrite, QueueType::eCompute)) &&
					  AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eDiscard, QueueType::eCompute)) &&
					  AccessLegalOnAccelerationStructure(D3D12_BARRIER_ACCESS_COMMON) &&
					  !AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eStorageWrite, QueueType::eCompute)) &&
					  !AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eAccelBuildInput, QueueType::eCompute)) &&
					  !AccessLegalOnAccelerationStructure(MapBarrierAccess(ResourceUse::eCopySrc, QueueType::eCopy)),
		"only the two acceleration-structure accesses reach such a buffer, so a build scratch, which is unordered access on this API, is not one of these "
		"buffers");

	static_assert(SyncClampRestricts(D3D12_BARRIER_SYNC_RENDER_TARGET, QueueType::eCopy) &&
					  SyncClampRestricts(D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_COPY, QueueType::eCompute) &&
					  SyncClampRestricts(D3D12_BARRIER_SYNC_DRAW, QueueType::eCompute) &&
					  AccessClampRestricts(D3D12_BARRIER_ACCESS_RENDER_TARGET | D3D12_BARRIER_ACCESS_SHADER_RESOURCE, QueueType::eCompute) &&
					  AccessClampRestricts(D3D12_BARRIER_ACCESS_INDEX_BUFFER, QueueType::eCopy) &&
					  HeapClampRestricts(D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_HEAP_TYPE_UPLOAD) &&
					  HeapClampRestricts(D3D12_BARRIER_ACCESS_RESOLVE_DEST, D3D12_HEAP_TYPE_READBACK) &&
					  LayoutClampRestricts(D3D12_BARRIER_LAYOUT_RESOLVE_DEST, QueueType::eCompute) &&
					  LayoutClampRestricts(D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, QueueType::eCompute),
		"a clamp restricts and never substitutes, which is what lets a derived state be trusted on a queue that tracks nothing");

} // namespace azo::rhi::d3d12

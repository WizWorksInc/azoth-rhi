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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	bool D3D12CmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resetQueryPool with an invalid handle");
		}
		if (firstQuery + queryCount > slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resetQueryPool runs past the end of the pool");
		}
		return Succeed(error);
	}

	bool D3D12CmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, [[maybe_unused]] Flags<PipelineStage> stage, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "writeTimestamp with an invalid query pool");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "writeTimestamp names a query past the end of the pool");
		}
		list->list->EndQuery(slot->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
		return Succeed(error);
	}

	bool D3D12CmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "beginQuery with an invalid query pool");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "beginQuery names a query past the end of the pool");
		}
		list->list->BeginQuery(slot->heap.Get(), MapQueryType(slot->type), query);
		return Succeed(error);
	}

	bool D3D12CmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "endQuery with an invalid query pool");
		}
		if (query >= slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "endQuery names a query past the end of the pool");
		}
		list->list->EndQuery(slot->heap.Get(), MapQueryType(slot->type), query);
		return Succeed(error);
	}

	bool D3D12CmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		QueryPoolSlot * slot = ResolveQueryPool(list->owner, pool);
		BufferSlot * dstSlot = ResolveBuffer(list->owner, dst);
		if (slot == nullptr || dstSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "resolveQueryData with an invalid handle");
		}
		if (firstQuery + queryCount > slot->queryCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "resolveQueryData runs past the end of the pool");
		}
		list->list->ResolveQueryData(slot->heap.Get(), MapQueryType(slot->type), firstQuery, queryCount, dstSlot->resource.Get(), dstOffset);
		return Succeed(error);
	}

	#if defined(AZOTH_RHI_ENABLE_PIX)
	// Maps an RHI 0xRRGGBBAA label color to the PIX event tint. PIX ignores the alpha byte.
	[[nodiscard]] UINT PixColor(std::uint32_t rgba) noexcept
	{
		return PIX_COLOR(static_cast<BYTE>((rgba >> 24) & 0xFFu), static_cast<BYTE>((rgba >> 16) & 0xFFu), static_cast<BYTE>((rgba >> 8) & 0xFFu));
	}
	#endif

	// Opens a PIX event so the region shows by name and color. The raw BeginEvent takes a PIX-specific blob so it has to go through PIXBeginEvent to be
	// decoded. A no-op without PIX, and a no-op when DeviceDesc turned labels off.
	bool D3D12CmdBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
	#if defined(AZOTH_RHI_ENABLE_PIX)
		auto * list = static_cast<D3D12CommandList *>(impl);
		if (list->owner->debugLabels)
		{
			PIXBeginEvent(list->list.Get(), PixColor(color), "%s", name != nullptr ? name : "");
		}
	#endif
		return Succeed(error);
	}

	bool D3D12CmdEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
	#if defined(AZOTH_RHI_ENABLE_PIX)
		auto * list = static_cast<D3D12CommandList *>(impl);
		if (list->owner->debugLabels)
		{
			PIXEndEvent(list->list.Get());
		}
	#endif
		return Succeed(error);
	}

	// The queue-level analog, which PIX shows around the submit boundary.
	bool D3D12QueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept
	{
	#if defined(AZOTH_RHI_ENABLE_PIX)
		auto * queue = static_cast<D3D12Queue *>(impl);
		if (queue->owner->debugLabels)
		{
			PIXBeginEvent(queue->queue.Get(), PixColor(color), "%s", name != nullptr ? name : "");
		}
	#endif
		return Succeed(error);
	}

	bool D3D12QueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept
	{
	#if defined(AZOTH_RHI_ENABLE_PIX)
		auto * queue = static_cast<D3D12Queue *>(impl);
		if (queue->owner->debugLabels)
		{
			PIXEndEvent(queue->queue.Get());
		}
	#endif
		return Succeed(error);
	}

	// The native mutation scope lets an app record raw D3D12 onto this list. Recording already targets it so the scope is a marker only.
	bool D3D12CmdBeginNativeMutation(void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
	{
		if (api != D3D12Api::id)
		{
			return Fail(error, ErrorCode::eUnsupportedApi, "native mutation requested for a different graphics API");
		}
		(void)impl;
		return Succeed(error);
	}

	bool D3D12CmdEndNativeMutation(void * impl, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
	{
		(void)impl;
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

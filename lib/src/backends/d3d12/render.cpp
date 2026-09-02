// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef _WIN32

	#include "azoth/rhi/core/build_config.hpp"

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	bool D3D12CmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.beginRendering");

		auto * list			 = static_cast<D3D12CommandList *>(impl);
		D3D12Device * device = list->owner;

		QueryPoolSlot * timestamps	  = nullptr;
		list->pendingEndTimestampHeap = nullptr;
		if (desc.timestamps != nullptr)
		{
			timestamps = ResolveQueryPool(device, desc.timestamps->pool);
			if (timestamps == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "rendering timestamp writes name a query pool this device never created");
			}
			if (timestamps->type != QueryType::eTimestamp)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a pool that holds another query type");
			}
			if ((desc.timestamps->beginQuery != kInvalidIndex && desc.timestamps->beginQuery >= timestamps->queryCount) ||
				(desc.timestamps->endQuery != kInvalidIndex && desc.timestamps->endQuery >= timestamps->queryCount))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "rendering timestamp writes name a query past the end of the pool");
			}
		}

		list->pendingDiscards.clear();
		detail::HostVector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
		rtvHandles.reserve(desc.colors.size());
		for (const RenderingAttachment & color : desc.colors)
		{
			TextureViewSlot * view = ResolveTextureView(device, color.view);
			if (view == nullptr || view->rtvIndex == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "beginRendering color attachment is not a render-target view");
			}
			rtvHandles.push_back(device->rtvHeap.Handle(view->rtvIndex));
			if (color.store == StoreOp::eDontCare)
			{
				if (TextureSlot * tex = ResolveTexture(device, view->texture); tex != nullptr)
				{
					list->pendingDiscards.push_back(tex->resource.Get());
				}
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
		bool haveDepth	   = false;
		bool dsvHasStencil = false;
		if (desc.depthStencil != nullptr)
		{
			TextureViewSlot * view = ResolveTextureView(device, desc.depthStencil->view);
			if (view == nullptr || view->dsvIndex == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "beginRendering depth attachment is not a depth-stencil view");
			}
			dsvHandle	  = device->dsvHeap.Handle(view->dsvIndex);
			haveDepth	  = true;
			dsvHasStencil = view->format == DXGI_FORMAT_D24_UNORM_S8_UINT || view->format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			if (desc.depthStencil->store == StoreOp::eDontCare)
			{
				if (TextureSlot * tex = ResolveTexture(device, view->texture); tex != nullptr)
				{
					list->pendingDiscards.push_back(tex->resource.Get());
				}
			}
		}

		if (timestamps != nullptr)
		{
			if (desc.timestamps->beginQuery != kInvalidIndex)
			{
				list->list->EndQuery(timestamps->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, desc.timestamps->beginQuery);
			}
			if (desc.timestamps->endQuery != kInvalidIndex)
			{
				list->pendingEndTimestampHeap  = timestamps->heap.Get();
				list->pendingEndTimestampQuery = desc.timestamps->endQuery;
			}
		}

		list->list->OMSetRenderTargets(
			static_cast<UINT>(rtvHandles.size()), rtvHandles.empty() ? nullptr : rtvHandles.data(), FALSE, haveDepth ? &dsvHandle : nullptr);

		for (std::size_t i = 0; i < desc.colors.size(); ++i)
		{
			const RenderingAttachment & color = desc.colors[i];
			if (color.load == LoadOp::eClear)
			{
				const std::array<float, 4> rgba{ color.clearColor.r, color.clearColor.g, color.clearColor.b, color.clearColor.a };
				list->list->ClearRenderTargetView(rtvHandles[i], rgba.data(), 0, nullptr);
			}
		}
		if (haveDepth && desc.depthStencil->load == LoadOp::eClear)
		{
			const D3D12_CLEAR_FLAGS clearFlags = dsvHasStencil ? (D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL) : D3D12_CLEAR_FLAG_DEPTH;
			list->list->ClearDepthStencilView(dsvHandle,
				clearFlags,
				desc.depthStencil->clearDepthStencil.depth,
				static_cast<UINT8>(desc.depthStencil->clearDepthStencil.stencil),
				0,
				nullptr);
		}
		return Succeed(error);
	}

	bool D3D12CmdEndRendering(void * impl, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);

		if (list->pendingEndTimestampHeap != nullptr)
		{
			list->list->EndQuery(list->pendingEndTimestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, list->pendingEndTimestampQuery);
			list->pendingEndTimestampHeap = nullptr;
		}

		for (ID3D12Resource * resource : list->pendingDiscards)
		{
			list->list->DiscardResource(resource, nullptr);
		}
		list->pendingDiscards.clear();
		return Succeed(error);
	}

	bool D3D12CmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept
	{
		auto * list					= static_cast<D3D12CommandList *>(impl);
		GraphicsPipelineSlot * slot = ResolveGraphicsPipeline(list->owner, pipeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setGraphicsPipeline with an invalid pipeline handle");
		}
		list->list->SetGraphicsRootSignature(slot->rootSignature.Get());
		list->list->SetPipelineState(slot->pipeline.Get());
		list->list->IASetPrimitiveTopology(slot->topology);

		if (slot->bakeBlendConstants)
		{
			list->list->OMSetBlendFactor(slot->blendConstants.data());
		}
		if (slot->bakeStencilReference)
		{
			list->list->OMSetStencilRef(slot->stencilReference);
		}
		if (list->owner->caps.supportsDepthBounds)
		{
			ComPtr<ID3D12GraphicsCommandList1> list1;
			if (SUCCEEDED(list->list.As(&list1)))
			{
				list1->OMSetDepthBounds(slot->depthBoundsTestEnable ? slot->minDepthBounds : 0.0f, slot->depthBoundsTestEnable ? slot->maxDepthBounds : 1.0f);
			}
		}

		list->vertexStrides		   = slot->vertexStrides;
		list->computePipelineBound = false;
		return Succeed(error);
	}

	bool D3D12CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept
	{
		auto * list				   = static_cast<D3D12CommandList *>(impl);
		ComputePipelineSlot * slot = ResolveComputePipeline(list->owner, pipeline);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setComputePipeline with an invalid pipeline handle");
		}
		list->list->SetComputeRootSignature(slot->rootSignature.Get());
		list->list->SetPipelineState(slot->pipeline.Get());
		list->computePipelineBound = true;
		return Succeed(error);
	}

	bool D3D12CmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data,
		Error * error) noexcept
	{
		auto * list						= static_cast<D3D12CommandList *>(impl);
		PipelineLayoutSlot * layoutSlot = ResolvePipelineLayout(list->owner, layout);
		if (layoutSlot == nullptr || layoutSlot->pushConstantParams.empty())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "pushConstants with a layout that has no root constants");
		}

		const PipelineLayoutSlot::PushConstantParam * target = nullptr;
		for (const PipelineLayoutSlot::PushConstantParam & pc : layoutSlot->pushConstantParams)
		{
			if (offset >= pc.offset && offset < pc.offset + pc.size)
			{
				target = &pc;
				break;
			}
		}
		if (target == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "pushConstants offset is outside every push-constant range of the layout");
		}

		const UINT num32Bit	 = size / sizeof(std::uint32_t);
		const UINT dstOffset = (offset - target->offset) / sizeof(std::uint32_t);
		if (stages.Contains(ShaderStage::eCompute))
		{
			list->list->SetComputeRoot32BitConstants(target->rootParam, num32Bit, data, dstOffset);
		}
		else
		{
			list->list->SetGraphicsRoot32BitConstants(target->rootParam, num32Bit, data, dstOffset);
		}
		return Succeed(error);
	}

	bool D3D12CmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept
	{
		D3D12_VIEWPORT vp{};
		const bool flip = GetClipSpace() == ClipSpaceConvention::eYDown;

		vp.TopLeftX = viewport.x;
		vp.TopLeftY = flip ? viewport.y + viewport.height : viewport.y;
		vp.Width	= viewport.width;
		vp.Height	= flip ? -viewport.height : viewport.height;
		vp.MinDepth = viewport.minDepth;
		vp.MaxDepth = viewport.maxDepth;
		static_cast<D3D12CommandList *>(impl)->list->RSSetViewports(1, &vp);
		return Succeed(error);
	}

	bool D3D12CmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept
	{
		D3D12_RECT rect{};
		rect.left	= scissor.x;
		rect.top	= scissor.y;
		rect.right	= scissor.x + static_cast<LONG>(scissor.width);
		rect.bottom = scissor.y + static_cast<LONG>(scissor.height);
		static_cast<D3D12CommandList *>(impl)->list->RSSetScissorRects(1, &rect);
		return Succeed(error);
	}

	bool D3D12CmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept
	{
		const std::array<float, 4> factor{ r, g, b, a };
		static_cast<D3D12CommandList *>(impl)->list->OMSetBlendFactor(factor.data());
		return Succeed(error);
	}

	bool D3D12CmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept
	{
		static_cast<D3D12CommandList *>(impl)->list->OMSetStencilRef(reference);
		return Succeed(error);
	}

	bool D3D12CmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept
	{
		ComPtr<ID3D12GraphicsCommandList9> list9;
		if (FAILED(static_cast<D3D12CommandList *>(impl)->list.As(&list9)))
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "dynamic depth bias requires ID3D12GraphicsCommandList9");
		}
		list9->RSSetDepthBias(constantFactor, clamp, slopeFactor);
		return Succeed(error);
	}

	bool D3D12CmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		BufferSlot * bufSlot = ResolveBuffer(list->owner, buffer);
		if (bufSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setVertexBuffer with an invalid buffer handle");
		}

		D3D12_VERTEX_BUFFER_VIEW view{};
		view.BufferLocation = bufSlot->resource->GetGPUVirtualAddress() + offset;
		view.SizeInBytes	= static_cast<UINT>(bufSlot->size - offset);
		// The slot is compared against the size on the same line. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		view.StrideInBytes = slot < list->vertexStrides.size() ? list->vertexStrides[slot] : 0;
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		list->list->IASetVertexBuffers(slot, 1, &view);
		return Succeed(error);
	}

	bool D3D12CmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept
	{
		auto * list			 = static_cast<D3D12CommandList *>(impl);
		BufferSlot * bufSlot = ResolveBuffer(list->owner, buffer);
		if (bufSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "setIndexBuffer with an invalid buffer handle");
		}

		D3D12_INDEX_BUFFER_VIEW view{};
		view.BufferLocation = bufSlot->resource->GetGPUVirtualAddress() + offset;
		view.SizeInBytes	= static_cast<UINT>(bufSlot->size - offset);
		view.Format			= index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
		list->list->IASetIndexBuffer(&view);
		return Succeed(error);
	}

	void FlushPendingDescriptorSets(D3D12CommandList * list) noexcept
	{
		for (D3D12CommandList::PendingDescriptorSet & pending : list->pendingSets)
		{
			if (!pending.valid)
			{
				continue;
			}
			if (pending.hasResource)
			{
				if (list->computePipelineBound)
				{
					list->list->SetComputeRootDescriptorTable(pending.resourceParam, pending.resourceHandle);
				}
				else
				{
					list->list->SetGraphicsRootDescriptorTable(pending.resourceParam, pending.resourceHandle);
				}
			}
			if (pending.hasSampler)
			{
				if (list->computePipelineBound)
				{
					list->list->SetComputeRootDescriptorTable(pending.samplerParam, pending.samplerHandle);
				}
				else
				{
					list->list->SetGraphicsRootDescriptorTable(pending.samplerParam, pending.samplerHandle);
				}
			}
			pending.valid = false;
		}
	}

	bool D3D12CmdDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);
		FlushPendingDescriptorSets(list);
		list->list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
		return Succeed(error);
	}

	bool D3D12CmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);
		FlushPendingDescriptorSets(list);
		list->list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
		return Succeed(error);
	}

	bool D3D12ExecuteIndirect(D3D12CommandList * list, D3D12_INDIRECT_ARGUMENT_TYPE type, std::uint32_t stride, BufferHandle args, std::uint64_t argsOffset,
		std::uint32_t maxCount, BufferHandle count, std::uint64_t countOffset, bool hasCount, Error * error) noexcept
	{
		D3D12Device * device  = list->owner;
		BufferSlot * argsSlot = ResolveBuffer(device, args);
		if (argsSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "indirect draw with an invalid argument buffer");
		}
		ID3D12Resource * countResource = nullptr;
		if (hasCount)
		{
			BufferSlot * countSlot = ResolveBuffer(device, count);
			if (countSlot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "indirect draw with an invalid count buffer");
			}
			countResource = countSlot->resource.Get();
		}

		ID3D12CommandSignature * signature = GetCommandSignature(list, type, stride);
		if (signature == nullptr)
		{
			return Fail(error, ErrorCode::eNativeApiError, "failed to create the indirect command signature");
		}
		FlushPendingDescriptorSets(list);
		list->list->ExecuteIndirect(signature, maxCount, argsSlot->resource.Get(), argsOffset, countResource, countOffset);
		return Succeed(error);
	}

	bool D3D12CmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		return D3D12ExecuteIndirect(
			static_cast<D3D12CommandList *>(impl), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, stride, args, offset, drawCount, {}, 0, false, error);
	}

	bool D3D12CmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept
	{
		return D3D12ExecuteIndirect(
			static_cast<D3D12CommandList *>(impl), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, stride, args, offset, drawCount, {}, 0, false, error);
	}

	bool D3D12CmdDrawIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept
	{
		return D3D12ExecuteIndirect(
			static_cast<D3D12CommandList *>(impl), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, stride, args, argsOffset, maxDrawCount, count, countOffset, true, error);
	}

	bool D3D12CmdDrawIndexedIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept
	{
		return D3D12ExecuteIndirect(static_cast<D3D12CommandList *>(impl),
			D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
			stride,
			args,
			argsOffset,
			maxDrawCount,
			count,
			countOffset,
			true,
			error);
	}

	bool D3D12CmdDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept
	{
		auto * list = static_cast<D3D12CommandList *>(impl);
		FlushPendingDescriptorSets(list);
		list->list->Dispatch(groupCountX, groupCountY, groupCountZ);
		return Succeed(error);
	}

	bool D3D12CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept
	{
		return D3D12ExecuteIndirect(static_cast<D3D12CommandList *>(impl),
			D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
			static_cast<std::uint32_t>(sizeof(D3D12_DISPATCH_ARGUMENTS)),
			args,
			offset,
			1,
			{},
			0,
			false,
			error);
	}

}

#endif

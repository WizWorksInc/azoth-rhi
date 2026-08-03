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
	/*
	 * Ray tracing is what is missing from this backend, on both sides: the device entries that would build a ray tracing pipeline or an acceleration structure
	 * and the recording entries that would bind and trace one. The device ones go to D3D12UnimplementedValue, the recording ones to D3D12Unimplemented.
	 *
	 * NegotiateCaps never sets supportsRayTracing so a caller that checks first never reaches these. DXR would be the way in, its tier queryable through
	 * D3D12_FEATURE_D3D12_OPTIONS5.
	 */
	const CoreDeviceApi & CoreDeviceBlock() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &D3D12DeviceApiId,
			.getGraphicsApiName			= &D3D12DeviceApiName,
			.createBuffer				= &D3D12CreateBuffer,
			.createTexture				= &D3D12CreateTexture,
			.createTextureView			= &D3D12CreateTextureView,
			.createSampler				= &D3D12CreateSampler,
			.createDescriptorSetLayout	= &D3D12CreateDescriptorSetLayout,
			.createPipelineLayout		= &D3D12CreatePipelineLayout,
			.createGraphicsPipeline		= &D3D12CreateGraphicsPipeline,
			.createComputePipeline		= &D3D12CreateComputePipeline,
			.createTimeline				= &D3D12CreateTimeline,
			.createBinarySemaphore		= &D3D12CreateBinarySemaphore,
			.createDescriptorArena		= &D3D12CreateDescriptorArena,
			.createCommandPool			= &D3D12CreateCommandPool,
			.getQueue					= &D3D12GetQueue,
			.map						= &D3D12Map,
			.unmap						= &D3D12Unmap,
			.flushMappedRange			= &D3D12FlushMappedRange,
			.invalidateMappedRange		= &D3D12InvalidateMappedRange,
			.updateDescriptorsBuffer	= &D3D12UpdateDescriptorsBuffer,
			.updateDescriptorsTexture	= &D3D12UpdateDescriptorsTexture,
			.updateDescriptorsSampler	= &D3D12UpdateDescriptorsSampler,
			.getCaps					= &D3D12DeviceCaps,
			.getFormatSupport			= &D3D12DeviceFormatSupport,
			.getAdapterInfo				= &D3D12DeviceAdapterInfo,
			.getValidationMessageCounts = &D3D12DeviceValidationMessageCounts,
			.destroy					= &D3D12Destroy,
			.collectGarbage				= &D3D12CollectGarbage,
			.collectGarbageTimeline		= &D3D12CollectGarbageTimeline,
			.destroyDevice				= &D3D12DestroyDevice,
		};

		return block;
	}

	const PresentApi & PresentBlock() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &D3D12CreateSwapchain,
		};

		return block;
	}

	const PlacedMemoryApi & PlacedMemoryBlock() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &D3D12CreateHeap,
			.createPlacedBuffer	  = &D3D12CreatePlacedBuffer,
			.createPlacedTexture  = &D3D12CreatePlacedTexture,
			.getTextureMemoryInfo = &D3D12GetTextureMemoryInfo,
			.getBufferMemoryInfo  = &D3D12GetBufferMemoryInfo,
		};

		return block;
	}

	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &D3D12GetTextureInfo,
			.getBufferInfo	= &D3D12GetBufferInfo,
		};

		return block;
	}

	const QueryApi & QueryBlock() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &D3D12CreateQueryPool,
			.calibrateTimestamp = &D3D12CalibrateTimestamp,
		};

		return block;
	}

	const PipelineCacheApi & PipelineCacheBlock() noexcept
	{
		static const PipelineCacheApi block{
			.createPipelineCache  = &D3D12CreatePipelineCache,
			.getPipelineCacheData = &D3D12GetPipelineCacheData,
		};

		return block;
	}

	const ResidencyApi & ResidencyBlock() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &D3D12QueryMemoryBudget,
			.setResidencyPriority = &D3D12SetResidencyPriority,
		};

		return block;
	}

	const AdoptionApi & AdoptionBlock() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &D3D12AdoptBuffer,
			.adoptTexture			  = &D3D12AdoptTexture,
			.getNativeBuffer		  = &D3D12GetNativeBuffer,
			.getNativeTexture		  = &D3D12GetNativeTexture,
			.adoptTextureView		  = &D3D12AdoptTextureView,
			.adoptSampler			  = &D3D12AdoptSampler,
			.getNativeTextureView	  = &D3D12GetNativeTextureView,
			.getNativeSampler		  = &D3D12GetNativeSampler,
			.adoptTimeline			  = &D3D12AdoptTimeline,
			.adoptBinarySemaphore	  = &D3D12AdoptBinarySemaphore,
			.getNativeTimeline		  = &D3D12GetNativeTimeline,
			.getNativeBinarySemaphore = &D3D12GetNativeBinarySemaphore,
		};

		return block;
	}

	const InstanceApi & InstanceBlock() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &D3D12InstanceApiId,
			.enumerateAdapters = &D3D12EnumerateAdapters,
			.createDevice	   = &D3D12InstanceCreateDevice,
			.destroyInstance   = &D3D12DestroyInstance,
		};

		return block;
	}

	// Declined by a backend that shares nothing, which is how every other optional capability says no. Published here because this one has answers.
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &D3D12QueryExternalHandleSupport,
		};

		return block;
	}

	const QueueApi & QueueBlock() noexcept
	{
		static const QueueApi block{
			.getType		   = &D3D12QueueType,
			.getFamilyIndex	   = &D3D12QueueFamilyIndex,
			.submit			   = &D3D12QueueSubmit,
			.waitIdle		   = &D3D12QueueWaitIdle,
			.getCompletedValue = &D3D12QueueGetCompletedValue,
			.wait			   = &D3D12QueueWait,
			.signal			   = &D3D12QueueSignal,
			.beginDebugLabel   = &D3D12QueueBeginDebugLabel,
			.endDebugLabel	   = &D3D12QueueEndDebugLabel,
		};

		return block;
	}

	const SparseApi & SparseBlock() noexcept
	{
		static const SparseApi block{
			.bindSparse = &D3D12QueueBindSparse,
		};

		return block;
	}

	const CommandPoolApi & CommandPoolBlock() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &D3D12CommandPoolAllocate,
			.reset	  = &D3D12CommandPoolReset,
		};

		return block;
	}

	const RenderCommandApi & RenderCommandBlock() noexcept
	{
		static const RenderCommandApi block{
			.begin				 = &D3D12CommandListBegin,
			.end				 = &D3D12CommandListEnd,
			.barriers			 = &D3D12CmdBarriers,
			.beginRendering		 = &D3D12CmdBeginRendering,
			.endRendering		 = &D3D12CmdEndRendering,
			.setGraphicsPipeline = &D3D12CmdSetGraphicsPipeline,
			.setComputePipeline	 = &D3D12CmdSetComputePipeline,
			.bindDescriptorSet	 = &D3D12CmdBindDescriptorSet,
			.pushConstants		 = &D3D12CmdPushConstants,
			.setViewport		 = &D3D12CmdSetViewport,
			.setScissor			 = &D3D12CmdSetScissor,
			.setBlendConstants	 = &D3D12CmdSetBlendConstants,
			.setStencilReference = &D3D12CmdSetStencilReference,
			.setDepthBias		 = &D3D12CmdSetDepthBias,
			.setVertexBuffer	 = &D3D12CmdSetVertexBuffer,
			.setIndexBuffer		 = &D3D12CmdSetIndexBuffer,
			.draw				 = &D3D12CmdDraw,
			.drawIndexed		 = &D3D12CmdDrawIndexed,
			.dispatch			 = &D3D12CmdDispatch,
			.copyBuffer			 = &D3D12CmdCopyBuffer,
			.copyBufferToTexture = &D3D12CmdCopyBufferToTexture,
			.copyTextureToBuffer = &D3D12CmdCopyTextureToBuffer,
			.copyTexture		 = &D3D12CmdCopyTexture,
			.clearBuffer		 = &D3D12CmdClearBuffer,
			.clearTexture		 = &D3D12CmdClearTexture,
			.resolveTexture		 = &D3D12CmdResolveTexture,
			.blit				 = &D3D12CmdBlit,
			.generateMips		 = &D3D12CmdGenerateMips,
			.beginDebugLabel	 = &D3D12CmdBeginDebugLabel,
			.endDebugLabel		 = &D3D12CmdEndDebugLabel,
		};

		return block;
	}

	const AliasingCommandApi & AliasingCommandBlock() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &D3D12CmdAliasBarriers,
		};

		return block;
	}

	const QueryCommandApi & QueryCommandBlock() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &D3D12CmdResetQueryPool,
			.writeTimestamp	  = &D3D12CmdWriteTimestamp,
			.beginQuery		  = &D3D12CmdBeginQuery,
			.endQuery		  = &D3D12CmdEndQuery,
			.resolveQueryData = &D3D12CmdResolveQueryData,
		};

		return block;
	}

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &D3D12CmdDrawIndirect,
			.drawIndexedIndirect = &D3D12CmdDrawIndexedIndirect,
			.dispatchIndirect	 = &D3D12CmdDispatchIndirect,
		};

		return block;
	}

	const IndirectCountApi & IndirectCountBlock() noexcept
	{
		static const IndirectCountApi block{
			.drawIndirectCount		  = &D3D12CmdDrawIndirectCount,
			.drawIndexedIndirectCount = &D3D12CmdDrawIndexedIndirectCount,
		};

		return block;
	}

	const NativeEscapeApi & NativeEscapeBlock() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &D3D12CmdBeginNativeMutation,
			.endNativeMutation	 = &D3D12CmdEndNativeMutation,
		};

		return block;
	}

	const SwapchainApi & SwapchainBlock() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &D3D12SwapchainAcquire,
			.present					 = &D3D12SwapchainPresent,
			.getBackBuffer				 = &D3D12SwapchainGetBackBuffer,
			.getBackBufferView			 = &D3D12SwapchainGetBackBufferView,
			.getPerImagePresentSemaphore = &D3D12SwapchainGetPresentSemaphore,
			.getFormat					 = &D3D12SwapchainGetFormat,
			.getPresentMode				 = &D3D12SwapchainGetPresentMode,
			.getImageCount				 = &D3D12SwapchainGetImageCount,
			.getWidth					 = &D3D12SwapchainGetWidth,
			.getHeight					 = &D3D12SwapchainGetHeight,
			.resize						 = &D3D12SwapchainResize,
			.setPresentMode				 = &D3D12SwapchainSetPresentMode,
			.supportsReadback			 = &D3D12SwapchainSupportsReadback,
		};

		return block;
	}

	const DescriptorArenaApi & DescriptorArenaBlock() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &D3D12DescriptorArenaAllocate,
			.reset	  = &D3D12DescriptorArenaReset,
		};

		return block;
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

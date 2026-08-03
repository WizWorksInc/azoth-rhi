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

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	const CoreDeviceApi & CoreDeviceBlock() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &VulkanDeviceApiId,
			.getGraphicsApiName			= &VulkanDeviceApiName,
			.createBuffer				= &VulkanCreateBuffer,
			.createTexture				= &VulkanCreateTexture,
			.createTextureView			= &VulkanCreateTextureView,
			.createSampler				= &VulkanCreateSampler,
			.createDescriptorSetLayout	= &VulkanCreateDescriptorSetLayout,
			.createPipelineLayout		= &VulkanCreatePipelineLayout,
			.createGraphicsPipeline		= &VulkanCreateGraphicsPipeline,
			.createComputePipeline		= &VulkanCreateComputePipeline,
			.createTimeline				= &VulkanCreateTimeline,
			.createBinarySemaphore		= &VulkanCreateBinarySemaphore,
			.createDescriptorArena		= &VulkanCreateDescriptorArena,
			.createCommandPool			= &VulkanCreateCommandPool,
			.getQueue					= &VulkanGetQueue,
			.map						= &VulkanMap,
			.unmap						= &VulkanUnmap,
			.flushMappedRange			= &VulkanFlushMappedRange,
			.invalidateMappedRange		= &VulkanInvalidateMappedRange,
			.updateDescriptorsBuffer	= &VulkanUpdateDescriptorsBuffer,
			.updateDescriptorsTexture	= &VulkanUpdateDescriptorsTexture,
			.updateDescriptorsSampler	= &VulkanUpdateDescriptorsSampler,
			.getCaps					= &VulkanDeviceCaps,
			.getFormatSupport			= &VulkanDeviceFormatSupport,
			.getAdapterInfo				= &VulkanDeviceAdapterInfo,
			.getValidationMessageCounts = &VulkanDeviceValidationMessageCounts,
			.destroy					= &VulkanDestroy,
			.collectGarbage				= &VulkanCollectGarbage,
			.collectGarbageTimeline		= &VulkanCollectGarbageTimeline,
			.destroyDevice				= &VulkanDestroyDevice,
		};

		return block;
	}

	const PresentApi & PresentBlock() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &VulkanCreateSwapchain,
		};

		return block;
	}

	const PlacedMemoryApi & PlacedMemoryBlock() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &VulkanCreateHeap,
			.createPlacedBuffer	  = &VulkanCreatePlacedBuffer,
			.createPlacedTexture  = &VulkanCreatePlacedTexture,
			.getTextureMemoryInfo = &VulkanGetTextureMemoryInfo,
			.getBufferMemoryInfo  = &VulkanGetBufferMemoryInfo,
		};

		return block;
	}

	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &VulkanGetTextureInfo,
			.getBufferInfo	= &VulkanGetBufferInfo,
		};

		return block;
	}

	const QueryApi & QueryBlock() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &VulkanCreateQueryPool,
			.calibrateTimestamp = &VulkanCalibrateTimestamp,
		};

		return block;
	}

	const PipelineCacheApi & PipelineCacheBlock() noexcept
	{
		static const PipelineCacheApi block{
			.createPipelineCache  = &VulkanCreatePipelineCache,
			.getPipelineCacheData = &VulkanGetPipelineCacheData,
		};

		return block;
	}

	const ResidencyApi & ResidencyBlock() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &VulkanQueryMemoryBudget,
			.setResidencyPriority = &VulkanSetResidencyPriority,
		};

		return block;
	}

	const InstanceApi & InstanceBlock() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &VulkanInstanceApiId,
			.enumerateAdapters = &VulkanEnumerateAdapters,
			.createDevice	   = &VulkanInstanceCreateDevice,
			.destroyInstance   = &VulkanDestroyInstance,
		};

		return block;
	}

	// Declined by a backend that shares nothing, which is how every other optional capability says no. Published here because this one has answers.
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &VulkanQueryExternalHandleSupport,
		};

		return block;
	}

	const SwapchainApi & SwapchainBlock() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &VulkanAcquire,
			.present					 = &VulkanPresent,
			.getBackBuffer				 = &VulkanSwapchainBackBuffer,
			.getBackBufferView			 = &VulkanSwapchainBackBufferView,
			.getPerImagePresentSemaphore = &VulkanSwapchainPresentSemaphore,
			.getFormat					 = &VulkanSwapchainFormat,
			.getPresentMode				 = &VulkanSwapchainGetPresentMode,
			.getImageCount				 = &VulkanSwapchainImageCount,
			.getWidth					 = &VulkanSwapchainWidth,
			.getHeight					 = &VulkanSwapchainHeight,
			.resize						 = &VulkanSwapchainResize,
			.setPresentMode				 = &VulkanSwapchainSetPresentMode,
			.supportsReadback			 = &VulkanSwapchainSupportsReadback,
		};

		return block;
	}

	const QueueApi & QueueBlock() noexcept
	{
		static const QueueApi block{
			.getType		   = &VulkanQueueType,
			.getFamilyIndex	   = &VulkanQueueFamilyIndex,
			.submit			   = &VulkanQueueSubmit,
			.waitIdle		   = &VulkanQueueWaitIdle,
			.getCompletedValue = &VulkanQueueGetCompletedValue,
			.wait			   = &VulkanQueueWait,
			.signal			   = &VulkanQueueSignal,
			.beginDebugLabel   = &VulkanQueueBeginDebugLabel,
			.endDebugLabel	   = &VulkanQueueEndDebugLabel,
		};

		return block;
	}

	const SparseApi & SparseBlock() noexcept
	{
		static const SparseApi block{
			.bindSparse = &VulkanQueueBindSparse,
		};

		return block;
	}

	const CommandPoolApi & CommandPoolBlock() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &VulkanCommandPoolAllocate,
			.reset	  = &VulkanCommandPoolReset,
		};

		return block;
	}

	// Ray tracing is what is missing here. Every other recording command lowers to vkCmd and the ray tracing entries report eUnsupportedFeature.
	const RenderCommandApi & RenderCommandBlock() noexcept
	{
		static const RenderCommandApi block{
			.begin				 = &VulkanCommandListBegin,
			.end				 = &VulkanCommandListEnd,
			.barriers			 = &VulkanCmdBarriers,
			.beginRendering		 = &VulkanCmdBeginRendering,
			.endRendering		 = &VulkanCmdEndRendering,
			.setGraphicsPipeline = &VulkanCmdSetGraphicsPipeline,
			.setComputePipeline	 = &VulkanCmdSetComputePipeline,
			.bindDescriptorSet	 = &VulkanCmdBindDescriptorSet,
			.pushConstants		 = &VulkanCmdPushConstants,
			.setViewport		 = &VulkanCmdSetViewport,
			.setScissor			 = &VulkanCmdSetScissor,
			.setBlendConstants	 = &VulkanCmdSetBlendConstants,
			.setStencilReference = &VulkanCmdSetStencilReference,
			.setDepthBias		 = &VulkanCmdSetDepthBias,
			.setVertexBuffer	 = &VulkanCmdSetVertexBuffer,
			.setIndexBuffer		 = &VulkanCmdSetIndexBuffer,
			.draw				 = &VulkanCmdDraw,
			.drawIndexed		 = &VulkanCmdDrawIndexed,
			.dispatch			 = &VulkanCmdDispatch,
			.copyBuffer			 = &VulkanCmdCopyBuffer,
			.copyBufferToTexture = &VulkanCmdCopyBufferToTexture,
			.copyTextureToBuffer = &VulkanCmdCopyTextureToBuffer,
			.copyTexture		 = &VulkanCmdCopyTexture,
			.clearBuffer		 = &VulkanCmdClearBuffer,
			.clearTexture		 = &VulkanCmdClearTexture,
			.resolveTexture		 = &VulkanCmdResolveTexture,
			.blit				 = &VulkanCmdBlit,
			.generateMips		 = &VulkanCmdGenerateMips,
			.beginDebugLabel	 = &VulkanCmdBeginDebugLabel,
			.endDebugLabel		 = &VulkanCmdEndDebugLabel,
		};

		return block;
	}

	const AliasingCommandApi & AliasingCommandBlock() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &VulkanCmdAliasBarriers,
		};

		return block;
	}

	const QueryCommandApi & QueryCommandBlock() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &VulkanCmdResetQueryPool,
			.writeTimestamp	  = &VulkanCmdWriteTimestamp,
			.beginQuery		  = &VulkanCmdBeginQuery,
			.endQuery		  = &VulkanCmdEndQuery,
			.resolveQueryData = &VulkanCmdResolveQueryData,
		};

		return block;
	}

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &VulkanCmdDrawIndirect,
			.drawIndexedIndirect = &VulkanCmdDrawIndexedIndirect,
			.dispatchIndirect	 = &VulkanCmdDispatchIndirect,
		};

		return block;
	}

	const IndirectCountApi & IndirectCountBlock() noexcept
	{
		static const IndirectCountApi block{
			.drawIndirectCount		  = &VulkanCmdDrawIndirectCount,
			.drawIndexedIndirectCount = &VulkanCmdDrawIndexedIndirectCount,
		};

		return block;
	}

	const NativeEscapeApi & NativeEscapeBlock() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &VulkanCmdBeginNativeMutation,
			.endNativeMutation	 = &VulkanCmdEndNativeMutation,
		};

		return block;
	}

} // namespace azo::rhi::vulkan

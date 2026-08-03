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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	const CoreDeviceApi & CoreDeviceBlock() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &MetalDeviceApiId,
			.getGraphicsApiName			= &MetalDeviceApiName,
			.createBuffer				= &MetalCreateBuffer,
			.createTexture				= &MetalCreateTexture,
			.createTextureView			= &MetalCreateTextureView,
			.createSampler				= &MetalCreateSampler,
			.createDescriptorSetLayout	= &MetalCreateDescriptorSetLayout,
			.createPipelineLayout		= &MetalCreatePipelineLayout,
			.createGraphicsPipeline		= &MetalCreateGraphicsPipeline,
			.createComputePipeline		= &MetalCreateComputePipeline,
			.createTimeline				= &MetalCreateTimeline,
			.createBinarySemaphore		= &MetalCreateBinarySemaphore,
			.createDescriptorArena		= &MetalCreateDescriptorArena,
			.createCommandPool			= &MetalCreateCommandPool,
			.getQueue					= &MetalGetQueue,
			.map						= &MetalMap,
			.unmap						= &NoopVoid,
			.flushMappedRange			= &NoopVoid,
			.invalidateMappedRange		= &NoopVoid,
			.updateDescriptorsBuffer	= &MetalUpdateDescriptorsBuffer,
			.updateDescriptorsTexture	= &MetalUpdateDescriptorsTexture,
			.updateDescriptorsSampler	= &MetalUpdateDescriptorsSampler,
			.getCaps					= &MetalDeviceCaps,
			.getFormatSupport			= &MetalDeviceFormatSupport,
			.getAdapterInfo				= &MetalDeviceAdapterInfo,
			.getValidationMessageCounts = &MetalDeviceValidationMessageCounts,
			.destroy					= &MetalDestroy,
			.collectGarbage				= &MetalCollectGarbage,
			.collectGarbageTimeline		= &MetalCollectGarbageTimeline,
			.destroyDevice				= &MetalDestroyDevice,
		};

		return block;
	}

	const PresentApi & PresentBlock() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &MetalCreateSwapchain,
		};

		return block;
	}

	const PlacedMemoryApi & PlacedMemoryBlock() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &MetalCreateHeap,
			.createPlacedBuffer	  = &MetalCreatePlacedBuffer,
			.createPlacedTexture  = &MetalCreatePlacedTexture,
			.getTextureMemoryInfo = &MetalGetTextureMemoryInfo,
			.getBufferMemoryInfo  = &MetalGetBufferMemoryInfo,
		};

		return block;
	}

	const RayTracingApi & RayTracingBlock() noexcept
	{
		static const RayTracingApi block{
			.createRayTracingPipeline				= &MetalCreateRayTracingPipeline,
			.createAccelerationStructure			= &MetalCreateAccelerationStructure,
			.updateDescriptorsAccelerationStructure = &MetalUnimplemented,
		};

		return block;
	}

	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &MetalGetTextureInfo,
			.getBufferInfo	= &MetalGetBufferInfo,
		};

		return block;
	}

	const QueryApi & QueryBlock() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &MetalCreateQueryPool,
			.calibrateTimestamp = &MetalCalibrateTimestamp,
		};

		return block;
	}

	const ResidencyApi & ResidencyBlock() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &MetalQueryMemoryBudget,
			.setResidencyPriority = &NoopVoid,
		};

		return block;
	}

	const AdoptionApi & AdoptionBlock() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &MetalAdoptBuffer,
			.adoptTexture			  = &MetalAdoptTexture,
			.getNativeBuffer		  = &MetalGetNativeBuffer,
			.getNativeTexture		  = &MetalGetNativeTexture,
			.adoptTextureView		  = &MetalAdoptTextureView,
			.adoptSampler			  = &MetalAdoptSampler,
			.getNativeTextureView	  = &MetalGetNativeTextureView,
			.getNativeSampler		  = &MetalGetNativeSampler,
			.adoptTimeline			  = &MetalAdoptTimeline,
			.adoptBinarySemaphore	  = &MetalAdoptBinarySemaphore,
			.getNativeTimeline		  = &MetalGetNativeTimeline,
			.getNativeBinarySemaphore = &MetalGetNativeBinarySemaphore,
		};

		return block;
	}

	const InstanceApi & InstanceBlock() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &MetalInstanceApiId,
			.enumerateAdapters = &MetalEnumerateAdapters,
			.createDevice	   = &MetalInstanceCreateDevice,
			.destroyInstance   = &MetalDestroyInstance,
		};

		return block;
	}

	// Declined by a backend that shares nothing, which is how every other optional capability says no. Published here because this one has answers.
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &MetalQueryExternalHandleSupport,
		};

		return block;
	}

	const QueueApi & QueueBlock() noexcept
	{
		static const QueueApi block{
			.getType		   = &MetalQueueTypeOf,
			.getFamilyIndex	   = &MetalQueueFamilyIndex,
			.submit			   = &MetalQueueSubmit,
			.waitIdle		   = &MetalQueueWaitIdle,
			.getCompletedValue = &MetalQueueGetCompletedValue,
			.wait			   = &MetalQueueWait,
			.signal			   = &MetalQueueSignal,
			.beginDebugLabel   = &MetalQueueBeginDebugLabel,
			.endDebugLabel	   = &MetalQueueEndDebugLabel,
		};

		return block;
	}

	const CommandPoolApi & CommandPoolBlock() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &MetalCommandPoolAllocate,
			.reset	  = &MetalCommandPoolReset,
		};

		return block;
	}

	/*
	 * Ray tracing and the count-buffer draws are the recording surfaces this backend does not reach. A command list declines each by not publishing its block,
	 * which turns the matching DeviceCaps flag false.
	 *
	 * The count-buffer draws are not simply absent from Metal. They read their draw count on the GPU, which Metal only reaches through an indirect command
	 * buffer encoded by a shader. That is not built here. Plain drawIndirect and dispatchIndirect are recorded.
	 */
	const RenderCommandApi & RenderCommandBlock() noexcept
	{
		static const RenderCommandApi block{
			.begin				 = &MetalCmdBegin,
			.end				 = &MetalCmdEnd,
			.barriers			 = &MetalCmdBarriers,
			.beginRendering		 = &MetalBeginRendering,
			.endRendering		 = &MetalEndRendering,
			.setGraphicsPipeline = &MetalSetGraphicsPipeline,
			.setComputePipeline	 = &MetalSetComputePipeline,
			.bindDescriptorSet	 = &MetalBindDescriptorSet,
			.pushConstants		 = &MetalPushConstants,
			.setViewport		 = &MetalSetViewport,
			.setScissor			 = &MetalSetScissor,
			.setBlendConstants	 = &MetalSetBlendConstants,
			.setStencilReference = &MetalSetStencilReference,
			.setDepthBias		 = &MetalSetDepthBias,
			.setVertexBuffer	 = &MetalSetVertexBuffer,
			.setIndexBuffer		 = &MetalSetIndexBuffer,
			.draw				 = &MetalDraw,
			.drawIndexed		 = &MetalDrawIndexed,
			.dispatch			 = &MetalDispatch,
			.copyBuffer			 = &MetalCopyBuffer,
			.copyBufferToTexture = &MetalCopyBufferToTexture,
			.copyTextureToBuffer = &MetalCopyTextureToBuffer,
			.copyTexture		 = &MetalCopyTexture,
			.clearBuffer		 = &MetalClearBuffer,
			.clearTexture		 = &MetalClearTexture,
			.resolveTexture		 = &MetalResolveTexture,
			.blit				 = &MetalBlit,
			.generateMips		 = &MetalGenerateMips,
			.beginDebugLabel	 = &MetalCmdBeginDebugLabel,
			.endDebugLabel		 = &MetalCmdEndDebugLabel,
		};

		return block;
	}

	/*
	 * Published whole, with the counting halves refusing by name. beginQuery and endQuery serve occlusion and pipeline statistics, which Metal reaches
	 * through a visibility result buffer and a different counter set, not through anything a pool created here holds.
	 *
	 * Publishing the block is still right, because it is what makes supportsTimestampQueries true and the timestamp entries are real. A block withheld
	 * until every entry is implemented would deny the caller the half that works.
	 */
	const QueryCommandApi & QueryCommandBlock() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &MetalCmdResetQueryPool,
			.writeTimestamp	  = &MetalCmdWriteTimestamp,
			.beginQuery		  = &MetalCmdBeginQuery,
			.endQuery		  = &MetalCmdEndQuery,
			.resolveQueryData = &MetalCmdResolveQueryData,
		};

		return block;
	}

	const AliasingCommandApi & AliasingCommandBlock() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &MetalCmdAliasBarriers,
		};

		return block;
	}

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &MetalDrawIndirect,
			.drawIndexedIndirect = &MetalDrawIndexedIndirect,
			.dispatchIndirect	 = &MetalDispatchIndirect,
		};

		return block;
	}

	const NativeEscapeApi & NativeEscapeBlock() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &MetalBeginNativeMutation,
			.endNativeMutation	 = &NoopVoid,
		};

		return block;
	}

	const DescriptorArenaApi & DescriptorArenaBlock() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &MetalArenaAllocate,
			.reset	  = &MetalArenaReset,
		};

		return block;
	}

} // namespace azo::rhi::metal

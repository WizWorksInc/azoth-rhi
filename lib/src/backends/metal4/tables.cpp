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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	const CoreDeviceApi & CoreDeviceBlock() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &Metal4DeviceApiId,
			.getGraphicsApiName			= &Metal4DeviceApiName,
			.createBuffer				= &Metal4CreateBuffer,
			.createTexture				= &Metal4CreateTexture,
			.createTextureView			= &Metal4CreateTextureView,
			.createSampler				= &Metal4CreateSampler,
			.createDescriptorSetLayout	= &Metal4CreateDescriptorSetLayout,
			.createPipelineLayout		= &Metal4CreatePipelineLayout,
			.createGraphicsPipeline		= &Metal4CreateGraphicsPipeline,
			.createComputePipeline		= &Metal4CreateComputePipeline,
			.createTimeline				= &Metal4CreateTimeline,
			.createBinarySemaphore		= &Metal4CreateBinarySemaphore,
			.createDescriptorArena		= &Metal4CreateDescriptorArena,
			.createCommandPool			= &Metal4CreateCommandPool,
			.getQueue					= &Metal4GetQueue,
			.map						= &Metal4Map,
			.unmap						= &NoopVoid,
			.flushMappedRange			= &NoopVoid,
			.invalidateMappedRange		= &NoopVoid,
			.updateDescriptorsBuffer	= &Metal4UpdateDescriptorsBuffer,
			.updateDescriptorsTexture	= &Metal4UpdateDescriptorsTexture,
			.updateDescriptorsSampler	= &Metal4UpdateDescriptorsSampler,
			.getCaps					= &Metal4DeviceCaps,
			.getFormatSupport			= &Metal4DeviceFormatSupport,
			.getAdapterInfo				= &Metal4DeviceAdapterInfo,
			.getValidationMessageCounts = &Metal4DeviceValidationMessageCounts,
			.destroy					= &Metal4Destroy,
			.collectGarbage				= &Metal4CollectGarbage,
			.collectGarbageTimeline		= &Metal4CollectGarbageTimeline,
			.destroyDevice				= &Metal4DestroyDevice,
		};

		return block;
	}

	const PresentApi & PresentBlock() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &Metal4CreateSwapchain,
		};

		return block;
	}

	const PlacedMemoryApi & PlacedMemoryBlock() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &Metal4CreateHeap,
			.createPlacedBuffer	  = &Metal4CreatePlacedBuffer,
			.createPlacedTexture  = &Metal4CreatePlacedTexture,
			.getTextureMemoryInfo = &Metal4GetTextureMemoryInfo,
			.getBufferMemoryInfo  = &Metal4GetBufferMemoryInfo,
		};

		return block;
	}

	const RayTracingApi & RayTracingBlock() noexcept
	{
		static const RayTracingApi block{
			.createRayTracingPipeline				= &Metal4CreateRayTracingPipeline,
			.createAccelerationStructure			= &Metal4CreateAccelerationStructure,
			.updateDescriptorsAccelerationStructure = &Metal4Unimplemented,
		};

		return block;
	}

	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &Metal4GetTextureInfo,
			.getBufferInfo	= &Metal4GetBufferInfo,
		};

		return block;
	}

	const QueryApi & QueryBlock() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &Metal4CreateQueryPool,
			.calibrateTimestamp = &Metal4CalibrateTimestamp,
		};

		return block;
	}

	const ResidencyApi & ResidencyBlock() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &Metal4QueryMemoryBudget,
			.setResidencyPriority = &NoopVoid,
		};

		return block;
	}

	const AdoptionApi & AdoptionBlock() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &Metal4AdoptBuffer,
			.adoptTexture			  = &Metal4AdoptTexture,
			.getNativeBuffer		  = &Metal4GetNativeBuffer,
			.getNativeTexture		  = &Metal4GetNativeTexture,
			.adoptTextureView		  = &Metal4AdoptTextureView,
			.adoptSampler			  = &Metal4AdoptSampler,
			.getNativeTextureView	  = &Metal4GetNativeTextureView,
			.getNativeSampler		  = &Metal4GetNativeSampler,
			.adoptTimeline			  = &Metal4AdoptTimeline,
			.adoptBinarySemaphore	  = &Metal4AdoptBinarySemaphore,
			.getNativeTimeline		  = &Metal4GetNativeTimeline,
			.getNativeBinarySemaphore = &Metal4GetNativeBinarySemaphore,
		};

		return block;
	}

	const InstanceApi & InstanceBlock() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &Metal4InstanceApiId,
			.enumerateAdapters = &Metal4EnumerateAdapters,
			.createDevice	   = &Metal4InstanceCreateDevice,
			.destroyInstance   = &Metal4DestroyInstance,
		};

		return block;
	}

	// Declined by a backend that shares nothing, which is how every other optional capability says no. Published here because this one has answers.
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &Metal4QueryExternalHandleSupport,
		};

		return block;
	}

	const QueueApi & QueueBlock() noexcept
	{
		static const QueueApi block{
			.getType		   = &Metal4QueueTypeOf,
			.getFamilyIndex	   = &Metal4QueueFamilyIndex,
			.submit			   = &Metal4QueueSubmit,
			.waitIdle		   = &Metal4QueueWaitIdle,
			.getCompletedValue = &Metal4QueueGetCompletedValue,
			.wait			   = &Metal4QueueWait,
			.signal			   = &Metal4QueueSignal,
			.beginDebugLabel   = &Metal4QueueBeginDebugLabel,
			.endDebugLabel	   = &Metal4QueueEndDebugLabel,
		};

		return block;
	}

	const CommandPoolApi & CommandPoolBlock() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &Metal4CommandPoolAllocate,
			.reset	  = &Metal4CommandPoolReset,
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
			.begin = &Metal4CmdBegin,
			.end   = &Metal4CmdEnd,

			// Real here. Metal 3 lowers this to nothing, tracking its own hazards.
			.barriers = &Metal4CmdBarriers,

			.beginRendering		 = &Metal4CmdBeginRendering,
			.endRendering		 = &Metal4CmdEndRendering,
			.setGraphicsPipeline = &Metal4CmdSetGraphicsPipeline,
			.setComputePipeline	 = &Metal4CmdSetComputePipeline,
			.bindDescriptorSet	 = &Metal4CmdBindDescriptorSet,
			.pushConstants		 = &Metal4CmdPushConstants,
			.setViewport		 = &Metal4CmdSetViewport,
			.setScissor			 = &Metal4CmdSetScissor,
			.setBlendConstants	 = &Metal4CmdSetBlendConstants,
			.setStencilReference = &Metal4CmdSetStencilReference,
			.setDepthBias		 = &Metal4CmdSetDepthBias,
			.setVertexBuffer	 = &Metal4CmdSetVertexBuffer,
			.setIndexBuffer		 = &Metal4CmdSetIndexBuffer,
			.draw				 = &Metal4CmdDraw,
			.drawIndexed		 = &Metal4CmdDrawIndexed,
			.dispatch			 = &Metal4CmdDispatch,

			// Every one of these is on the compute encoder here, this generation having no blit encoder.
			.copyBuffer			 = &Metal4CmdCopyBuffer,
			.copyBufferToTexture = &Metal4CmdCopyBufferToTexture,
			.copyTextureToBuffer = &Metal4CmdCopyTextureToBuffer,
			.copyTexture		 = &Metal4CmdCopyTexture,
			.clearBuffer		 = &Metal4CmdClearBuffer,
			.clearTexture		 = &Metal4CmdClearTexture,
			.resolveTexture		 = &Metal4CmdResolveTexture,
			.blit				 = &Metal4CmdBlit,
			.generateMips		 = &Metal4CmdGenerateMips,

			.beginDebugLabel = &Metal4CmdBeginDebugLabel,
			.endDebugLabel	 = &Metal4CmdEndDebugLabel,
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
			.resetQueryPool	  = &Metal4CmdResetQueryPool,
			.writeTimestamp	  = &Metal4CmdWriteTimestamp,
			.beginQuery		  = &Metal4CmdBeginQuery,
			.endQuery		  = &Metal4CmdEndQuery,
			.resolveQueryData = &Metal4CmdResolveQueryData,
		};

		return block;
	}

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &Metal4CmdDrawIndirect,
			.drawIndexedIndirect = &Metal4CmdDrawIndexedIndirect,
			.dispatchIndirect	 = &Metal4CmdDispatchIndirect,
		};

		return block;
	}

	const NativeEscapeApi & NativeEscapeBlock() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &Metal4BeginNativeMutation,
			.endNativeMutation	 = &NoopVoid,
		};

		return block;
	}

	const DescriptorArenaApi & DescriptorArenaBlock() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &Metal4ArenaAllocate,
			.reset	  = &Metal4ArenaReset,
		};

		return block;
	}

} // namespace azo::rhi::metal4

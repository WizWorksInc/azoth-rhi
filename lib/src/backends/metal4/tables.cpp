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
			.getGraphicsApiId			= &DeviceApiId,
			.getGraphicsApiName			= &DeviceApiName,
			.createBuffer				= &CreateBuffer,
			.createTexture				= &CreateTexture,
			.createTextureView			= &CreateTextureView,
			.createSampler				= &CreateSampler,
			.createDescriptorSetLayout	= &CreateDescriptorSetLayout,
			.createPipelineLayout		= &CreatePipelineLayout,
			.createGraphicsPipeline		= &CreateGraphicsPipeline,
			.createComputePipeline		= &CreateComputePipeline,
			.createTimeline				= &CreateTimeline,
			.createBinarySemaphore		= &CreateBinarySemaphore,
			.createDescriptorArena		= &CreateDescriptorArena,
			.createCommandPool			= &CreateCommandPool,
			.getQueue					= &GetQueue,
			.map						= &Map,
			.unmap						= &NoopVoid,
			.flushMappedRange			= &NoopVoid,
			.invalidateMappedRange		= &NoopVoid,
			.updateDescriptorsBuffer	= &UpdateDescriptorsBuffer,
			.updateDescriptorsTexture	= &UpdateDescriptorsTexture,
			.updateDescriptorsSampler	= &UpdateDescriptorsSampler,
			.getCaps					= &DeviceCapsOf,
			.getFormatSupport			= &DeviceFormatSupport,
			.getAdapterInfo				= &DeviceAdapterInfo,
			.getValidationMessageCounts = &DeviceValidationMessageCounts,
			.destroy					= &Destroy,
			.collectGarbage				= &CollectGarbage,
			.collectGarbageTimeline		= &CollectGarbageTimeline,
			.destroyDevice				= &DestroyDevice,
		};

		return block;
	}

	const PresentApi & PresentBlock() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &CreateSwapchain,
		};

		return block;
	}

	const PlacedMemoryApi & PlacedMemoryBlock() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &CreateHeap,
			.createPlacedBuffer	  = &CreatePlacedBuffer,
			.createPlacedTexture  = &CreatePlacedTexture,
			.getTextureMemoryInfo = &GetTextureMemoryInfo,
			.getBufferMemoryInfo  = &GetBufferMemoryInfo,
		};

		return block;
	}

	const RayTracingApi & RayTracingBlock() noexcept
	{
		static const RayTracingApi block{
			.createRayTracingPipeline				= &CreateRayTracingPipeline,
			.createAccelerationStructure			= &CreateAccelerationStructure,
			.updateDescriptorsAccelerationStructure = &Unimplemented,
		};

		return block;
	}

	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &GetTextureInfo,
			.getBufferInfo	= &GetBufferInfo,
		};

		return block;
	}

	const QueryApi & QueryBlock() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &CreateQueryPool,
			.calibrateTimestamp = &CalibrateTimestamp,
		};

		return block;
	}

	const ResidencyApi & ResidencyBlock() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &QueryMemoryBudget,
			.setResidencyPriority = &NoopVoid,
		};

		return block;
	}

	const AdoptionApi & AdoptionBlock() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &AdoptBuffer,
			.adoptTexture			  = &AdoptTexture,
			.getNativeBuffer		  = &GetNativeBuffer,
			.getNativeTexture		  = &GetNativeTexture,
			.adoptTextureView		  = &AdoptTextureView,
			.adoptSampler			  = &AdoptSampler,
			.getNativeTextureView	  = &GetNativeTextureView,
			.getNativeSampler		  = &GetNativeSampler,
			.adoptTimeline			  = &AdoptTimeline,
			.adoptBinarySemaphore	  = &AdoptBinarySemaphore,
			.getNativeTimeline		  = &GetNativeTimeline,
			.getNativeBinarySemaphore = &GetNativeBinarySemaphore,
		};

		return block;
	}

	const InstanceApi & InstanceBlock() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &InstanceApiId,
			.enumerateAdapters = &EnumerateAdapters,
			.createDevice	   = &InstanceCreateDevice,
			.destroyInstance   = &DestroyInstance,
		};

		return block;
	}

	// Declined by a backend that shares nothing, which is how every other optional capability says no. Published here because this one has answers.
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &QueryExternalHandleSupport,
		};

		return block;
	}

	const QueueApi & QueueBlock() noexcept
	{
		static const QueueApi block{
			.getType		   = &QueueTypeOf,
			.getFamilyIndex	   = &QueueFamilyIndex,
			.submit			   = &QueueSubmit,
			.waitIdle		   = &QueueWaitIdle,
			.getCompletedValue = &QueueGetCompletedValue,
			.wait			   = &QueueWait,
			.signal			   = &QueueSignal,
			.beginDebugLabel   = &QueueBeginDebugLabel,
			.endDebugLabel	   = &QueueEndDebugLabel,
		};

		return block;
	}

	const CommandPoolApi & CommandPoolBlock() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &CommandPoolAllocate,
			.reset	  = &CommandPoolReset,
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
			.begin = &CmdBegin,
			.end   = &CmdEnd,

			// Real here. Metal 3 lowers this to nothing, tracking its own hazards.
			.barriers = &CmdBarriers,

			.beginRendering		 = &CmdBeginRendering,
			.endRendering		 = &CmdEndRendering,
			.setGraphicsPipeline = &CmdSetGraphicsPipeline,
			.setComputePipeline	 = &CmdSetComputePipeline,
			.bindDescriptorSet	 = &CmdBindDescriptorSet,
			.pushConstants		 = &CmdPushConstants,
			.setViewport		 = &CmdSetViewport,
			.setScissor			 = &CmdSetScissor,
			.setBlendConstants	 = &CmdSetBlendConstants,
			.setStencilReference = &CmdSetStencilReference,
			.setDepthBias		 = &CmdSetDepthBias,
			.setVertexBuffer	 = &CmdSetVertexBuffer,
			.setIndexBuffer		 = &CmdSetIndexBuffer,
			.draw				 = &CmdDraw,
			.drawIndexed		 = &CmdDrawIndexed,
			.dispatch			 = &CmdDispatch,

			// Every one of these is on the compute encoder here, this generation having no blit encoder.
			.copyBuffer			 = &CmdCopyBuffer,
			.copyBufferToTexture = &CmdCopyBufferToTexture,
			.copyTextureToBuffer = &CmdCopyTextureToBuffer,
			.copyTexture		 = &CmdCopyTexture,
			.clearBuffer		 = &CmdClearBuffer,
			.clearTexture		 = &CmdClearTexture,
			.resolveTexture		 = &CmdResolveTexture,
			.blit				 = &CmdBlit,
			.generateMips		 = &CmdGenerateMips,

			.beginDebugLabel = &CmdBeginDebugLabel,
			.endDebugLabel	 = &CmdEndDebugLabel,
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
			.resetQueryPool	  = &CmdResetQueryPool,
			.writeTimestamp	  = &CmdWriteTimestamp,
			.beginQuery		  = &CmdBeginQuery,
			.endQuery		  = &CmdEndQuery,
			.resolveQueryData = &CmdResolveQueryData,
		};

		return block;
	}

	const IndirectApi & IndirectBlock() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &CmdDrawIndirect,
			.drawIndexedIndirect = &CmdDrawIndexedIndirect,
			.dispatchIndirect	 = &CmdDispatchIndirect,
		};

		return block;
	}

	const NativeEscapeApi & NativeEscapeBlock() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &BeginNativeMutation,
			.endNativeMutation	 = &NoopVoid,
		};

		return block;
	}

	const DescriptorArenaApi & DescriptorArenaBlock() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &ArenaAllocate,
			.reset	  = &ArenaReset,
		};

		return block;
	}

} // namespace azo::rhi::metal4

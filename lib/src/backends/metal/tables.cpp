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

#include "azoth/rhi/backend/blocks/command_list.hpp"
#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/blocks/descriptor_arena.hpp"
#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/blocks/instance.hpp"
#include "azoth/rhi/backend/blocks/queue.hpp"

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	const CoreDeviceApi & core_device_block() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &metal_device_api_id,
			.getGraphicsApiName			= &metal_device_api_name,
			.createBuffer				= &metal_create_buffer,
			.createTexture				= &metal_create_texture,
			.createTextureView			= &metal_create_texture_view,
			.createSampler				= &metal_create_sampler,
			.createDescriptorSetLayout	= &metal_create_descriptor_set_layout,
			.createPipelineLayout		= &metal_create_pipeline_layout,
			.createGraphicsPipeline		= &metal_create_graphics_pipeline,
			.createComputePipeline		= &metal_create_compute_pipeline,
			.createTimeline				= &metal_create_timeline,
			.createBinarySemaphore		= &metal_create_binary_semaphore,
			.createDescriptorArena		= &metal_create_descriptor_arena,
			.createCommandPool			= &metal_create_command_pool,
			.getQueue					= &metal_get_queue,
			.map						= &metal_map,
			.unmap						= &metal_unmap,
			.flushMappedRange			= &noop_void,
			.invalidateMappedRange		= &noop_void,
			.updateDescriptorsBuffer	= &metal_update_descriptors_buffer,
			.updateDescriptorsTexture	= &metal_update_descriptors_texture,
			.updateDescriptorsSampler	= &metal_update_descriptors_sampler,
			.getCaps					= &metal_device_caps,
			.getFormatSupport			= &metal_device_format_support,
			.getAdapterInfo				= &metal_device_adapter_info,
			.getValidationMessageCounts = &metal_device_validation_message_counts,
			.destroy					= &metal_destroy,
			.collectGarbage				= &metal_collect_garbage,
			.collectGarbageTimeline		= &metal_collect_garbage_timeline,
			.destroyDevice				= &metal_destroy_device,
		};

		return block;
	}

	const PresentApi & present_block() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &metal_create_swapchain,
		};

		return block;
	}

	const PlacedMemoryApi & placed_memory_block() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &metal_create_heap,
			.createPlacedBuffer	  = &metal_create_placed_buffer,
			.createPlacedTexture  = &metal_create_placed_texture,
			.getTextureMemoryInfo = &metal_get_texture_memory_info,
			.getBufferMemoryInfo  = &metal_get_buffer_memory_info,
		};

		return block;
	}

	const RayTracingApi & ray_tracing_block() noexcept
	{
		static const RayTracingApi block{
			.createRayTracingPipeline				= &metal_create_ray_tracing_pipeline,
			.createAccelerationStructure			= &metal_create_acceleration_structure,
			.updateDescriptorsAccelerationStructure = &metal_unimplemented,
		};

		return block;
	}

	const ResourceIntrospectionApi & resource_introspection_block() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &metal_get_texture_info,
			.getBufferInfo	= &metal_get_buffer_info,
		};

		return block;
	}

	const QueryApi & query_block() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &metal_create_query_pool,
			.calibrateTimestamp = &metal_calibrate_timestamp,
		};

		return block;
	}

	const ResidencyApi & residency_block() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &metal_query_memory_budget,
			.setResidencyPriority = &noop_void,
		};

		return block;
	}

	const AdoptionApi & adoption_block() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &metal_adopt_buffer,
			.adoptTexture			  = &metal_adopt_texture,
			.getNativeBuffer		  = &metal_get_native_buffer,
			.getNativeTexture		  = &metal_get_native_texture,
			.adoptTextureView		  = &metal_adopt_texture_view,
			.adoptSampler			  = &metal_adopt_sampler,
			.getNativeTextureView	  = &metal_get_native_texture_view,
			.getNativeSampler		  = &metal_get_native_sampler,
			.adoptTimeline			  = &metal_adopt_timeline,
			.adoptBinarySemaphore	  = &metal_adopt_binary_semaphore,
			.getNativeTimeline		  = &metal_get_native_timeline,
			.getNativeBinarySemaphore = &metal_get_native_binary_semaphore,
		};

		return block;
	}

	const InstanceApi & instance_block() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &metal_instance_api_id,
			.enumerateAdapters = &metal_enumerate_adapters,
			.createDevice	   = &metal_instance_create_device,
			.destroyInstance   = &metal_destroy_instance,
		};

		return block;
	}

	const ExternalCapabilityApi & external_capability_block() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &metal_query_external_handle_support,
		};

		return block;
	}

	const QueueApi & queue_block() noexcept
	{
		static const QueueApi block{
			.getType		   = &metal_queue_type_of,
			.submit			   = &metal_queue_submit,
			.waitIdle		   = &metal_queue_wait_idle,
			.getCompletedValue = &metal_queue_get_completed_value,
			.wait			   = &metal_queue_wait,
			.signal			   = &metal_queue_signal,
			.beginDebugLabel   = &metal_queue_begin_debug_label,
			.endDebugLabel	   = &metal_queue_end_debug_label,
		};

		return block;
	}

	const CommandPoolApi & command_pool_block() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &metal_command_pool_allocate,
			.reset	  = &metal_command_pool_reset,
		};

		return block;
	}

	const RenderCommandApi & render_command_block() noexcept
	{
		static const RenderCommandApi block{
			.begin				 = &metal_cmd_begin,
			.end				 = &metal_cmd_end,
			.barriers			 = &metal_cmd_barriers,
			.beginRendering		 = &metal_begin_rendering,
			.endRendering		 = &metal_end_rendering,
			.setGraphicsPipeline = &metal_set_graphics_pipeline,
			.setComputePipeline	 = &metal_set_compute_pipeline,
			.bindDescriptorSet	 = &metal_bind_descriptor_set,
			.pushConstants		 = &metal_push_constants,
			.setViewport		 = &metal_set_viewport,
			.setScissor			 = &metal_set_scissor,
			.setBlendConstants	 = &metal_set_blend_constants,
			.setStencilReference = &metal_set_stencil_reference,
			.setDepthBias		 = &metal_set_depth_bias,
			.setVertexBuffer	 = &metal_set_vertex_buffer,
			.setIndexBuffer		 = &metal_set_index_buffer,
			.draw				 = &metal_draw,
			.drawIndexed		 = &metal_draw_indexed,
			.dispatch			 = &metal_dispatch,
			.copyBuffer			 = &metal_copy_buffer,
			.copyBufferToTexture = &metal_copy_buffer_to_texture,
			.copyTextureToBuffer = &metal_copy_texture_to_buffer,
			.copyTexture		 = &metal_copy_texture,
			.clearBuffer		 = &metal_clear_buffer,
			.clearTexture		 = &metal_clear_texture,
			.resolveTexture		 = &metal_resolve_texture,
			.blit				 = &metal_blit,
			.generateMips		 = &metal_generate_mips,
			.beginDebugLabel	 = &metal_cmd_begin_debug_label,
			.endDebugLabel		 = &metal_cmd_end_debug_label,
		};

		return block;
	}

	const QueryCommandApi & query_command_block() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &metal_cmd_reset_query_pool,
			.writeTimestamp	  = &metal_cmd_write_timestamp,
			.beginQuery		  = &metal_cmd_begin_query,
			.endQuery		  = &metal_cmd_end_query,
			.resolveQueryData = &metal_cmd_resolve_query_data,
		};

		return block;
	}

	const AliasingCommandApi & aliasing_command_block() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &metal_cmd_alias_barriers,
		};

		return block;
	}

	const IndirectApi & indirect_block() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &metal_draw_indirect,
			.drawIndexedIndirect = &metal_draw_indexed_indirect,
			.dispatchIndirect	 = &metal_dispatch_indirect,
		};

		return block;
	}

	const NativeEscapeApi & native_escape_block() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &metal_begin_native_mutation,
			.endNativeMutation	 = &noop_void,
		};

		return block;
	}

	const DescriptorArenaApi & descriptor_arena_block() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &metal_arena_allocate,
			.reset	  = &metal_arena_reset,
		};

		return block;
	}

}

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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	const CoreDeviceApi & core_device_block() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &metal4_device_api_id,
			.getGraphicsApiName			= &metal4_device_api_name,
			.createBuffer				= &metal4_create_buffer,
			.createTexture				= &metal4_create_texture,
			.createTextureView			= &metal4_create_texture_view,
			.createSampler				= &metal4_create_sampler,
			.createDescriptorSetLayout	= &metal4_create_descriptor_set_layout,
			.createPipelineLayout		= &metal4_create_pipeline_layout,
			.createGraphicsPipeline		= &metal4_create_graphics_pipeline,
			.createComputePipeline		= &metal4_create_compute_pipeline,
			.createTimeline				= &metal4_create_timeline,
			.createBinarySemaphore		= &metal4_create_binary_semaphore,
			.createDescriptorArena		= &metal4_create_descriptor_arena,
			.createCommandPool			= &metal4_create_command_pool,
			.getQueue					= &metal4_get_queue,
			.map						= &metal4_map,
			.unmap						= &metal4_unmap,
			.flushMappedRange			= &noop_void,
			.invalidateMappedRange		= &noop_void,
			.updateDescriptorsBuffer	= &metal4_update_descriptors_buffer,
			.updateDescriptorsTexture	= &metal4_update_descriptors_texture,
			.updateDescriptorsSampler	= &metal4_update_descriptors_sampler,
			.getCaps					= &metal4_device_caps,
			.getFormatSupport			= &metal4_device_format_support,
			.getAdapterInfo				= &metal4_device_adapter_info,
			.getValidationMessageCounts = &metal4_device_validation_message_counts,
			.destroy					= &metal4_destroy,
			.collectGarbage				= &metal4_collect_garbage,
			.collectGarbageTimeline		= &metal4_collect_garbage_timeline,
			.destroyDevice				= &metal4_destroy_device,
		};

		return block;
	}

	const PresentApi & present_block() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &metal4_create_swapchain,
		};

		return block;
	}

	const PlacedMemoryApi & placed_memory_block() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &metal4_create_heap,
			.createPlacedBuffer	  = &metal4_create_placed_buffer,
			.createPlacedTexture  = &metal4_create_placed_texture,
			.getTextureMemoryInfo = &metal4_get_texture_memory_info,
			.getBufferMemoryInfo  = &metal4_get_buffer_memory_info,
		};

		return block;
	}

	const RayTracingApi & ray_tracing_block() noexcept
	{
		static const RayTracingApi block{
			.createRayTracingPipeline				= &metal4_create_ray_tracing_pipeline,
			.createAccelerationStructure			= &metal4_create_acceleration_structure,
			.updateDescriptorsAccelerationStructure = &metal4_unimplemented,
		};

		return block;
	}

	const ResourceIntrospectionApi & resource_introspection_block() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &metal4_get_texture_info,
			.getBufferInfo	= &metal4_get_buffer_info,
		};

		return block;
	}

	const QueryApi & query_block() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &metal4_create_query_pool,
			.calibrateTimestamp = &metal4_calibrate_timestamp,
		};

		return block;
	}

	const ResidencyApi & residency_block() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &metal4_query_memory_budget,
			.setResidencyPriority = &noop_void,
		};

		return block;
	}

	const AdoptionApi & adoption_block() noexcept
	{
		static const AdoptionApi block{
			.adoptBuffer			  = &metal4_adopt_buffer,
			.adoptTexture			  = &metal4_adopt_texture,
			.getNativeBuffer		  = &metal4_get_native_buffer,
			.getNativeTexture		  = &metal4_get_native_texture,
			.adoptTextureView		  = &metal4_adopt_texture_view,
			.adoptSampler			  = &metal4_adopt_sampler,
			.getNativeTextureView	  = &metal4_get_native_texture_view,
			.getNativeSampler		  = &metal4_get_native_sampler,
			.adoptTimeline			  = &metal4_adopt_timeline,
			.adoptBinarySemaphore	  = &metal4_adopt_binary_semaphore,
			.getNativeTimeline		  = &metal4_get_native_timeline,
			.getNativeBinarySemaphore = &metal4_get_native_binary_semaphore,
		};

		return block;
	}

	const InstanceApi & instance_block() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &metal4_instance_api_id,
			.enumerateAdapters = &metal4_enumerate_adapters,
			.createDevice	   = &metal4_instance_create_device,
			.destroyInstance   = &metal4_destroy_instance,
		};

		return block;
	}

	const ExternalCapabilityApi & external_capability_block() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &metal4_query_external_handle_support,
		};

		return block;
	}

	const QueueApi & queue_block() noexcept
	{
		static const QueueApi block{
			.getType		   = &metal4_queue_type_of,
			.submit			   = &metal4_queue_submit,
			.waitIdle		   = &metal4_queue_wait_idle,
			.getCompletedValue = &metal4_queue_get_completed_value,
			.wait			   = &metal4_queue_wait,
			.signal			   = &metal4_queue_signal,
			.beginDebugLabel   = &metal4_queue_begin_debug_label,
			.endDebugLabel	   = &metal4_queue_end_debug_label,
		};

		return block;
	}

	const CommandPoolApi & command_pool_block() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &metal4_command_pool_allocate,
			.reset	  = &metal4_command_pool_reset,
		};

		return block;
	}

	const RenderCommandApi & render_command_block() noexcept
	{
		static const RenderCommandApi block{
			.begin = &metal4_cmd_begin,
			.end   = &metal4_cmd_end,

			.barriers = &metal4_cmd_barriers,

			.beginRendering		 = &metal4_cmd_begin_rendering,
			.endRendering		 = &metal4_cmd_end_rendering,
			.setGraphicsPipeline = &metal4_cmd_set_graphics_pipeline,
			.setComputePipeline	 = &metal4_cmd_set_compute_pipeline,
			.bindDescriptorSet	 = &metal4_cmd_bind_descriptor_set,
			.pushConstants		 = &metal4_cmd_push_constants,
			.setViewport		 = &metal4_cmd_set_viewport,
			.setScissor			 = &metal4_cmd_set_scissor,
			.setBlendConstants	 = &metal4_cmd_set_blend_constants,
			.setStencilReference = &metal4_cmd_set_stencil_reference,
			.setDepthBias		 = &metal4_cmd_set_depth_bias,
			.setVertexBuffer	 = &metal4_cmd_set_vertex_buffer,
			.setIndexBuffer		 = &metal4_cmd_set_index_buffer,
			.draw				 = &metal4_cmd_draw,
			.drawIndexed		 = &metal4_cmd_draw_indexed,
			.dispatch			 = &metal4_cmd_dispatch,

			.copyBuffer			 = &metal4_cmd_copy_buffer,
			.copyBufferToTexture = &metal4_cmd_copy_buffer_to_texture,
			.copyTextureToBuffer = &metal4_cmd_copy_texture_to_buffer,
			.copyTexture		 = &metal4_cmd_copy_texture,
			.clearBuffer		 = &metal4_cmd_clear_buffer,
			.clearTexture		 = &metal4_cmd_clear_texture,
			.resolveTexture		 = &metal4_cmd_resolve_texture,
			.blit				 = &metal4_cmd_blit,
			.generateMips		 = &metal4_cmd_generate_mips,

			.beginDebugLabel = &metal4_cmd_begin_debug_label,
			.endDebugLabel	 = &metal4_cmd_end_debug_label,
		};

		return block;
	}

	const QueryCommandApi & query_command_block() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &metal4_cmd_reset_query_pool,
			.writeTimestamp	  = &metal4_cmd_write_timestamp,
			.beginQuery		  = &metal4_cmd_begin_query,
			.endQuery		  = &metal4_cmd_end_query,
			.resolveQueryData = &metal4_cmd_resolve_query_data,
		};

		return block;
	}

	const AliasingCommandApi & aliasing_command_block() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &metal4_cmd_alias_barriers,
		};

		return block;
	}

	const IndirectApi & indirect_block() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &metal4_cmd_draw_indirect,
			.drawIndexedIndirect = &metal4_cmd_draw_indexed_indirect,
			.dispatchIndirect	 = &metal4_cmd_dispatch_indirect,
		};

		return block;
	}

	const NativeEscapeApi & native_escape_block() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &metal4_begin_native_mutation,
			.endNativeMutation	 = &noop_void,
		};

		return block;
	}

	const DescriptorArenaApi & descriptor_arena_block() noexcept
	{
		static const DescriptorArenaApi block{
			.allocate = &metal4_arena_allocate,
			.reset	  = &metal4_arena_reset,
		};

		return block;
	}

}

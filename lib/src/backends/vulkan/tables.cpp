// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/blocks/command_list.hpp"
#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/blocks/instance.hpp"
#include "azoth/rhi/backend/blocks/queue.hpp"
#include "azoth/rhi/backend/blocks/swapchain.hpp"
#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	const CoreDeviceApi & core_device_block() noexcept
	{
		static const CoreDeviceApi block{
			.getGraphicsApiId			= &vulkan_device_api_id,
			.getGraphicsApiName			= &vulkan_device_api_name,
			.createBuffer				= &vulkan_create_buffer,
			.createTexture				= &vulkan_create_texture,
			.createTextureView			= &vulkan_create_texture_view,
			.createSampler				= &vulkan_create_sampler,
			.createDescriptorSetLayout	= &vulkan_create_descriptor_set_layout,
			.createPipelineLayout		= &vulkan_create_pipeline_layout,
			.createGraphicsPipeline		= &vulkan_create_graphics_pipeline,
			.createComputePipeline		= &vulkan_create_compute_pipeline,
			.createTimeline				= &vulkan_create_timeline,
			.createBinarySemaphore		= &vulkan_create_binary_semaphore,
			.createDescriptorArena		= &vulkan_create_descriptor_arena,
			.createCommandPool			= &vulkan_create_command_pool,
			.getQueue					= &vulkan_get_queue,
			.map						= &vulkan_map,
			.unmap						= &vulkan_unmap,
			.flushMappedRange			= &vulkan_flush_mapped_range,
			.invalidateMappedRange		= &vulkan_invalidate_mapped_range,
			.updateDescriptorsBuffer	= &vulkan_update_descriptors_buffer,
			.updateDescriptorsTexture	= &vulkan_update_descriptors_texture,
			.updateDescriptorsSampler	= &vulkan_update_descriptors_sampler,
			.getCaps					= &vulkan_device_caps,
			.getFormatSupport			= &vulkan_device_format_support,
			.getAdapterInfo				= &vulkan_device_adapter_info,
			.getValidationMessageCounts = &vulkan_device_validation_message_counts,
			.destroy					= &vulkan_destroy,
			.collectGarbage				= &vulkan_collect_garbage,
			.collectGarbageTimeline		= &vulkan_collect_garbage_timeline,
			.destroyDevice				= &vulkan_destroy_device,
		};

		return block;
	}

	const PresentApi & present_block() noexcept
	{
		static const PresentApi block{
			.createSwapchain = &vulkan_create_swapchain,
		};

		return block;
	}

	const PlacedMemoryApi & placed_memory_block() noexcept
	{
		static const PlacedMemoryApi block{
			.createHeap			  = &vulkan_create_heap,
			.createPlacedBuffer	  = &vulkan_create_placed_buffer,
			.createPlacedTexture  = &vulkan_create_placed_texture,
			.getTextureMemoryInfo = &vulkan_get_texture_memory_info,
			.getBufferMemoryInfo  = &vulkan_get_buffer_memory_info,
		};

		return block;
	}

	const ResourceIntrospectionApi & resource_introspection_block() noexcept
	{
		static const ResourceIntrospectionApi block{
			.getTextureInfo = &vulkan_get_texture_info,
			.getBufferInfo	= &vulkan_get_buffer_info,
		};

		return block;
	}

	const QueryApi & query_block() noexcept
	{
		static const QueryApi block{
			.createQueryPool	= &vulkan_create_query_pool,
			.calibrateTimestamp = &vulkan_calibrate_timestamp,
		};

		return block;
	}

	const PipelineCacheApi & pipeline_cache_block() noexcept
	{
		static const PipelineCacheApi block{
			.createPipelineCache  = &vulkan_create_pipeline_cache,
			.getPipelineCacheData = &vulkan_get_pipeline_cache_data,
		};

		return block;
	}

	const ResidencyApi & residency_block() noexcept
	{
		static const ResidencyApi block{
			.queryMemoryBudget	  = &vulkan_query_memory_budget,
			.setResidencyPriority = &vulkan_set_residency_priority,
		};

		return block;
	}

	const InstanceApi & instance_block() noexcept
	{
		static const InstanceApi block{
			.getGraphicsApiId  = &vulkan_instance_api_id,
			.enumerateAdapters = &vulkan_enumerate_adapters,
			.createDevice	   = &vulkan_instance_create_device,
			.destroyInstance   = &vulkan_destroy_instance,
		};

		return block;
	}

	const ExternalCapabilityApi & external_capability_block() noexcept
	{
		static const ExternalCapabilityApi block{
			.queryExternalHandleSupport = &vulkan_query_external_handle_support,
		};

		return block;
	}

	const SwapchainApi & swapchain_block() noexcept
	{
		static const SwapchainApi block{
			.acquireNextImage			 = &vulkan_acquire,
			.present					 = &vulkan_present,
			.getBackBuffer				 = &vulkan_swapchain_back_buffer,
			.getBackBufferView			 = &vulkan_swapchain_back_buffer_view,
			.getPerImagePresentSemaphore = &vulkan_swapchain_present_semaphore,
			.getFormat					 = &vulkan_swapchain_format,
			.getPresentMode				 = &vulkan_swapchain_get_present_mode,
			.getImageCount				 = &vulkan_swapchain_image_count,
			.getWidth					 = &vulkan_swapchain_width,
			.getHeight					 = &vulkan_swapchain_height,
			.resize						 = &vulkan_swapchain_resize,
			.setPresentMode				 = &vulkan_swapchain_set_present_mode,
			.supportsReadback			 = &vulkan_swapchain_supports_readback,
		};

		return block;
	}

	const QueueApi & queue_block() noexcept
	{
		static const QueueApi block{
			.getType		   = &vulkan_queue_type,
			.submit			   = &vulkan_queue_submit,
			.waitIdle		   = &vulkan_queue_wait_idle,
			.getCompletedValue = &vulkan_queue_get_completed_value,
			.wait			   = &vulkan_queue_wait,
			.signal			   = &vulkan_queue_signal,
			.beginDebugLabel   = &vulkan_queue_begin_debug_label,
			.endDebugLabel	   = &vulkan_queue_end_debug_label,
		};

		return block;
	}

	const SparseApi & sparse_block() noexcept
	{
		static const SparseApi block{
			.bindSparse = &vulkan_queue_bind_sparse,
		};

		return block;
	}

	const CommandPoolApi & command_pool_block() noexcept
	{
		static const CommandPoolApi block{
			.allocate = &vulkan_command_pool_allocate,
			.reset	  = &vulkan_command_pool_reset,
		};

		return block;
	}

	const RenderCommandApi & render_command_block() noexcept
	{
		static const RenderCommandApi block{
			.begin				 = &vulkan_command_list_begin,
			.end				 = &vulkan_command_list_end,
			.barriers			 = &vulkan_cmd_barriers,
			.beginRendering		 = &vulkan_cmd_begin_rendering,
			.endRendering		 = &vulkan_cmd_end_rendering,
			.setGraphicsPipeline = &vulkan_cmd_set_graphics_pipeline,
			.setComputePipeline	 = &vulkan_cmd_set_compute_pipeline,
			.bindDescriptorSet	 = &vulkan_cmd_bind_descriptor_set,
			.pushConstants		 = &vulkan_cmd_push_constants,
			.setViewport		 = &vulkan_cmd_set_viewport,
			.setScissor			 = &vulkan_cmd_set_scissor,
			.setBlendConstants	 = &vulkan_cmd_set_blend_constants,
			.setStencilReference = &vulkan_cmd_set_stencil_reference,
			.setDepthBias		 = &vulkan_cmd_set_depth_bias,
			.setVertexBuffer	 = &vulkan_cmd_set_vertex_buffer,
			.setIndexBuffer		 = &vulkan_cmd_set_index_buffer,
			.draw				 = &vulkan_cmd_draw,
			.drawIndexed		 = &vulkan_cmd_draw_indexed,
			.dispatch			 = &vulkan_cmd_dispatch,
			.copyBuffer			 = &vulkan_cmd_copy_buffer,
			.copyBufferToTexture = &vulkan_cmd_copy_buffer_to_texture,
			.copyTextureToBuffer = &vulkan_cmd_copy_texture_to_buffer,
			.copyTexture		 = &vulkan_cmd_copy_texture,
			.clearBuffer		 = &vulkan_cmd_clear_buffer,
			.clearTexture		 = &vulkan_cmd_clear_texture,
			.resolveTexture		 = &vulkan_cmd_resolve_texture,
			.blit				 = &vulkan_cmd_blit,
			.generateMips		 = &vulkan_cmd_generate_mips,
			.beginDebugLabel	 = &vulkan_cmd_begin_debug_label,
			.endDebugLabel		 = &vulkan_cmd_end_debug_label,
		};

		return block;
	}

	const AliasingCommandApi & aliasing_command_block() noexcept
	{
		static const AliasingCommandApi block{
			.aliasBarriers = &vulkan_cmd_alias_barriers,
		};

		return block;
	}

	const QueryCommandApi & query_command_block() noexcept
	{
		static const QueryCommandApi block{
			.resetQueryPool	  = &vulkan_cmd_reset_query_pool,
			.writeTimestamp	  = &vulkan_cmd_write_timestamp,
			.beginQuery		  = &vulkan_cmd_begin_query,
			.endQuery		  = &vulkan_cmd_end_query,
			.resolveQueryData = &vulkan_cmd_resolve_query_data,
		};

		return block;
	}

	const IndirectApi & indirect_block() noexcept
	{
		static const IndirectApi block{
			.drawIndirect		 = &vulkan_cmd_draw_indirect,
			.drawIndexedIndirect = &vulkan_cmd_draw_indexed_indirect,
			.dispatchIndirect	 = &vulkan_cmd_dispatch_indirect,
		};

		return block;
	}

	const IndirectCountApi & indirect_count_block() noexcept
	{
		static const IndirectCountApi block{
			.drawIndirectCount		  = &vulkan_cmd_draw_indirect_count,
			.drawIndexedIndirectCount = &vulkan_cmd_draw_indexed_indirect_count,
		};

		return block;
	}

	const NativeEscapeApi & native_escape_block() noexcept
	{
		static const NativeEscapeApi block{
			.beginNativeMutation = &vulkan_cmd_begin_native_mutation,
			.endNativeMutation	 = &vulkan_cmd_end_native_mutation,
		};

		return block;
	}

}

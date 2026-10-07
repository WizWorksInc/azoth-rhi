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

#include "validation/decorator.hpp"

#include "azoth/rhi/backend/blocks/command_list.hpp"
#include "azoth/rhi/backend/blocks/command_pool.hpp"
#include "azoth/rhi/backend/blocks/descriptor_arena.hpp"
#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/blocks/native_object.hpp"
#include "azoth/rhi/backend/blocks/queue.hpp"
#include "azoth/rhi/backend/blocks/swapchain.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"
#include "azoth/rhi/validation/registry.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint> // NOLINT
#include <limits>
#include <span>
#include <thread>
#include <utility>

namespace azo::rhi::validation
{

	template <>
	[[nodiscard]] const CoreDeviceApi * inner_block<CoreDeviceApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.core;
	}

	template <>
	[[nodiscard]] const PresentApi * inner_block<PresentApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.present;
	}

	template <>
	[[nodiscard]] const PlacedMemoryApi * inner_block<PlacedMemoryApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.placedMemory;
	}

	template <>
	[[nodiscard]] const RayTracingApi * inner_block<RayTracingApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.rayTracing;
	}

	template <>
	[[nodiscard]] const QueryApi * inner_block<QueryApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.query;
	}

	template <>
	[[nodiscard]] const PipelineCacheApi * inner_block<PipelineCacheApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.pipelineCache;
	}

	template <>
	[[nodiscard]] const ResidencyApi * inner_block<ResidencyApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.residency;
	}

	template <>
	[[nodiscard]] const ResourceIntrospectionApi * inner_block<ResourceIntrospectionApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.introspection;
	}

	template <>
	[[nodiscard]] const AdoptionApi * inner_block<AdoptionApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.adoption;
	}

	template <>
	[[nodiscard]] const ExternalSharingApi * inner_block<ExternalSharingApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.externalSharing;
	}

	template <>
	[[nodiscard]] const QueueApi * inner_block<QueueApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedQueue *>(self)->blocks.core;
	}

	template <>
	[[nodiscard]] const SparseApi * inner_block<SparseApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedQueue *>(self)->blocks.sparse;
	}

	template <>
	[[nodiscard]] const CommandPoolApi * inner_block<CommandPoolApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandPool *>(self)->blocks;
	}

	template <>
	[[nodiscard]] const DescriptorArenaApi * inner_block<DescriptorArenaApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDescriptorArena *>(self)->blocks;
	}

	template <>
	[[nodiscard]] const RenderCommandApi * inner_block<RenderCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.render;
	}

	template <>
	[[nodiscard]] const AliasingCommandApi * inner_block<AliasingCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.aliasing;
	}

	template <>
	[[nodiscard]] const RayTracingCommandApi * inner_block<RayTracingCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.rayTracing;
	}

	template <>
	[[nodiscard]] const QueryCommandApi * inner_block<QueryCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.query;
	}

	template <>
	[[nodiscard]] const IndirectApi * inner_block<IndirectApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.indirect;
	}

	template <>
	[[nodiscard]] const IndirectCountApi * inner_block<IndirectCountApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.indirectCount;
	}

	template <>
	[[nodiscard]] const NativeEscapeApi * inner_block<NativeEscapeApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.nativeEscape;
	}

	template <>
	[[nodiscard]] const SwapchainApi * inner_block<SwapchainApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedSwapchain *>(self)->blocks;
	}

	namespace
	{

		void * validated_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
		void * validated_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
		void * validated_create_descriptor_arena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
		void * validated_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
		void * validated_allocate_command_list(void * impl, CString debugName, Error * error) noexcept;
		bool validated_command_pool_reset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		void validated_destroy_device(void * impl) noexcept;
		bool validated_destroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept;
		bool validated_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
		void apply_pending_ownership(DeviceValidator & validator, const WrappedCommandList & list) noexcept;
		bool submitted_ownership_is_legal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept;
		bool submitted_states_are_legal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept;
		bool validated_bind_sparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
		bool validated_arena_reset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		bool validated_barriers(void * impl, const BarrierBatch & batch, Error * error) noexcept;
		bool validated_generate_mips(void * impl, TextureHandle texture, Error * error) noexcept;
		bool validated_alias_barriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
		bool validated_write_timestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
		bool validated_clear_buffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
		bool validated_clear_texture(
			void * impl,
			TextureHandle texture,
			const ClearColor & color,
			std::span<const TextureSubresourceRange> ranges,
			Error * error
		) noexcept;
		bool validated_build_acceleration_structures(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept;
		TextureViewHandle validated_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
		GraphicsPipelineHandle validated_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
		DescriptorSetLayoutHandle validated_create_descriptor_set_layout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
		QueryPoolHandle validated_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
		DescriptorSetHandle validated_allocate_descriptor_set(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
		bool validated_begin(void * impl, Error * error) noexcept;
		bool validated_end(void * impl, Error * error) noexcept;
		bool validated_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
		bool validated_end_rendering(void * impl, Error * error) noexcept;
		bool validated_set_graphics_pipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
		bool validated_set_compute_pipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
		bool validated_set_ray_tracing_pipeline(void * impl, RayTracingPipelineHandle pipeline, Error * error) noexcept;
		bool validated_draw(
			void * impl,
			std::uint32_t vertexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstVertex,
			std::uint32_t firstInstance,
			Error * error
		) noexcept;
		bool validated_draw_indexed(
			void * impl,
			std::uint32_t indexCount,
			std::uint32_t instanceCount,
			std::uint32_t firstIndex,
			std::int32_t vertexOffset,
			std::uint32_t firstInstance,
			Error * error
		) noexcept;
		bool validated_dispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
		bool validated_end_native_mutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept;
		PresentResult validated_present(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept;

		constexpr std::uint32_t kPackedBindings	 = 16;
		constexpr std::uint64_t kBindingTypeMask = 0xFu;

		[[nodiscard]] std::uint64_t pack_binding_types(const DescriptorSetLayoutDesc & desc) noexcept
		{
			std::uint64_t packed = 0;
			for (const DescriptorBinding & binding : desc.bindings)
			{
				if (binding.binding >= kPackedBindings)
				{
					continue;
				}

				packed |= (static_cast<std::uint64_t>(binding.type) + 1u) << (binding.binding * 4u);
			}

			return packed;
		}

		[[nodiscard]] std::uint64_t pack_layout(const DescriptorSetLayoutHandle layout) noexcept
		{
			return static_cast<std::uint64_t>(layout.index) | (static_cast<std::uint64_t>(layout.generation) << 32u);
		}

		GraphicsPipelineHandle validated_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (self->validator->checks_state())
			{
				const std::uint32_t attachments = std::min<std::uint32_t>(desc.blend.attachmentCount, desc.blend.attachments.size());
				for (std::uint32_t slot = 0; slot < attachments; ++slot)
				{
					if (!azo::rhi::detail::at(desc.blend.attachments, slot).blendEnable || slot >= desc.renderTarget.colorFormatCount)
					{
						continue;
					}

					const Format format = azo::rhi::detail::at(desc.renderTarget.colorFormats, slot);
					if (format == Format::eUndefined || self->blocks.core->getFormatSupport(self->inner, format).blendable)
					{
						continue;
					}

					return self->validator->fail_value<GraphicsPipelineHandle>(
						error,
						"a blend enabled on an attachment in a format this device does not advertise as blendable"
					);
				}
			}

			return Recording<ResourceType::eGraphicsPipeline, &CoreDeviceApi::createGraphicsPipeline>::call(impl, desc, error);
		}

		TextureViewHandle validated_create_texture_view(void * impl, const TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (!all_usable(*self->validator, texture))
			{
				return self->validator->fail_value<TextureViewHandle>(error, "a texture view of a texture this device has already taken back");
			}

			Format format = desc.format;
			if (format == Format::eUndefined)
			{
				const ResourceRecord * parent = self->validator->Handles().lookup(
					RegisteredHandle{
						.type		= ResourceType::eTexture,
						.index		= texture.index,
						.generation = texture.generation,
					}
				);
				if (parent != nullptr)
				{
					format = static_cast<Format>(parent->format.load(std::memory_order_relaxed));
				}
			}

			const TextureViewHandle view = self->blocks.core->createTextureView(self->inner, texture, desc, error);
			if (view.is_valid())
			{
				const RegisteredHandle registered{
					.type		= ResourceType::eTextureView,
					.index		= view.index,
					.generation = view.generation,
				};
				if (self->validator->Handles().record(registered) && format != Format::eUndefined)
				{
					self->validator->Handles().lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
				}
			}

			return view;
		}

		DescriptorSetLayoutHandle validated_create_descriptor_set_layout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (!argument_is_usable(*self->validator, desc))
			{
				return self->validator->fail_value<DescriptorSetLayoutHandle>(
					error,
					"a descriptor set layout bakes in an immutable sampler this device has already taken back"
				);
			}

			const DescriptorSetLayoutHandle layout = self->blocks.core->createDescriptorSetLayout(self->inner, desc, error);
			if (!layout.is_valid())
			{
				return layout;
			}

			const RegisteredHandle registered{
				.type		= ResourceType::eDescriptorSetLayout,
				.index		= layout.index,
				.generation = layout.generation,
			};
			if (self->validator->Handles().record(registered))
			{
				self->validator->Handles().lookup(registered)->detail.store(pack_binding_types(desc), std::memory_order_relaxed);
			}

			return layout;
		}

		QueryPoolHandle validated_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (desc.queryCount == 0)
			{
				return self->validator->fail_value<QueryPoolHandle>(error, "query pool creation asked for no queries");
			}

			const QueryPoolHandle pool = self->blocks.query->createQueryPool(self->inner, desc, error);
			if (pool.is_valid())
			{
				static_cast<void>(self->validator->Handles().record(
					RegisteredHandle{
						.type		= ResourceType::eQueryPool,
						.index		= pool.index,
						.generation = pool.generation,
					}
				));
			}

			return pool;
		}

		DescriptorSetHandle validated_allocate_descriptor_set(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDescriptorArena *>(impl);

			if (!all_usable(*self->validator, desc.layout))
			{
				return self->validator->fail_value<DescriptorSetHandle>(
					error,
					"a descriptor set allocated against a layout this device has already taken back"
				);
			}

			const DescriptorSetHandle set = self->blocks->allocate(self->inner, desc, error);
			if (!set.is_valid())
			{
				return set;
			}

			const RegisteredHandle registered{
				.type		= ResourceType::eDescriptorSet,
				.index		= set.index,
				.generation = set.generation,
			};
			if (self->validator->Handles().record(registered))
			{
				ResourceRecord * record = self->validator->Handles().lookup(registered);
				record->detail.store(pack_layout(desc.layout), std::memory_order_relaxed);

				record->origin.store(self->id, std::memory_order_relaxed);
			}

			return set;
		}

		[[nodiscard]] bool write_matches_layout(
			WrappedDevice * self,
			const DescriptorSetHandle set,
			const std::uint32_t binding,
			const DescriptorType type,
			Error * error
		) noexcept
		{
			const ResourceRecord * setRecord = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= ResourceType::eDescriptorSet,
					.index		= set.index,
					.generation = set.generation,
				}
			);
			if (setRecord == nullptr)
			{
				return true;
			}

			const std::uint64_t packedLayout = setRecord->detail.load(std::memory_order_relaxed);
			const ResourceRecord * layout	 = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= ResourceType::eDescriptorSetLayout,
					.index		= static_cast<std::uint32_t>(packedLayout & 0xFFFFFFFFu),
					.generation = static_cast<std::uint32_t>(packedLayout >> 32u),
				}
			);
			if (layout == nullptr || binding >= kPackedBindings)
			{
				return true;
			}

			const std::uint64_t declared = (layout->detail.load(std::memory_order_relaxed) >> (binding * 4u)) & kBindingTypeMask;
			if (declared == 0)
			{
				return self->validator->fail(error, "a descriptor write names a binding its set's layout never declared");
			}

			if (declared - 1u != static_cast<std::uint64_t>(type))
			{
				return self->validator->fail(error, "a descriptor write names a binding its set's layout declared as a different descriptor type");
			}

			return true;
		}

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteBuffer & write) noexcept
		{
			return write.type;
		}

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteTexture & write) noexcept
		{
			return write.type;
		}

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteSampler & /*unused*/) noexcept
		{
			return DescriptorType::eSampler;
		}

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteAccelerationStructure & /*unused*/) noexcept
		{
			return DescriptorType::eAccelerationStructure;
		}

		template <class Write, auto Member>
		struct ValidatedWrites;

		template <class Write, class Block, bool (*Block::*Member)(void *, std::span<const Write>, Error *) noexcept>
		struct ValidatedWrites<Write, Member>
		{
			static bool call(void * impl, std::span<const Write> writes, Error * error) noexcept
			{
				auto * self = static_cast<WrappedDevice *>(impl);

				if (!argument_is_usable(*self->validator, writes))
				{
					return self->validator->fail(error, "a descriptor write names a resource this device has already taken back");
				}

				if (self->validator->checks_state())
				{
					for (const Write & write : writes)
					{
						if (!write_matches_layout(self, write.set, write.binding, WriteType(write), error))
						{
							return false;
						}
					}
				}

				return (inner_block<Block>(self)->*Member)(self->inner, writes, error);
			}
		};

		const CoreDeviceApi & ValidatingCoreDeviceApi() noexcept
		{
			static const CoreDeviceApi block{
				.getGraphicsApiId			= &Forward<&CoreDeviceApi::getGraphicsApiId>::call,
				.getGraphicsApiName			= &Forward<&CoreDeviceApi::getGraphicsApiName>::call,
				.createBuffer				= &Recording<ResourceType::eBuffer, &CoreDeviceApi::createBuffer>::call,
				.createTexture				= &Recording<ResourceType::eTexture, &CoreDeviceApi::createTexture>::call,
				.createTextureView			= &validated_create_texture_view,
				.createSampler				= &Recording<ResourceType::eSampler, &CoreDeviceApi::createSampler>::call,
				.createDescriptorSetLayout	= &validated_create_descriptor_set_layout,
				.createPipelineLayout		= &Recording<ResourceType::ePipelineLayout, &CoreDeviceApi::createPipelineLayout>::call,
				.createGraphicsPipeline		= &validated_create_graphics_pipeline,
				.createComputePipeline		= &Recording<ResourceType::eComputePipeline, &CoreDeviceApi::createComputePipeline>::call,
				.createTimeline				= &Recording<ResourceType::eTimeline, &CoreDeviceApi::createTimeline>::call,
				.createBinarySemaphore		= &Recording<ResourceType::eBinarySemaphore, &CoreDeviceApi::createBinarySemaphore>::call,
				.createDescriptorArena		= &validated_create_descriptor_arena,
				.createCommandPool			= &validated_create_command_pool,
				.getQueue					= &validated_get_queue,
				.map						= &Checked<&CoreDeviceApi::map>::call,
				.unmap						= &Checked<&CoreDeviceApi::unmap>::call,
				.flushMappedRange			= &Checked<&CoreDeviceApi::flushMappedRange>::call,
				.invalidateMappedRange		= &Checked<&CoreDeviceApi::invalidateMappedRange>::call,
				.updateDescriptorsBuffer	= &ValidatedWrites<DescriptorWriteBuffer, &CoreDeviceApi::updateDescriptorsBuffer>::call,
				.updateDescriptorsTexture	= &ValidatedWrites<DescriptorWriteTexture, &CoreDeviceApi::updateDescriptorsTexture>::call,
				.updateDescriptorsSampler	= &ValidatedWrites<DescriptorWriteSampler, &CoreDeviceApi::updateDescriptorsSampler>::call,
				.getCaps					= &Forward<&CoreDeviceApi::getCaps>::call,
				.getFormatSupport			= &Forward<&CoreDeviceApi::getFormatSupport>::call,
				.getAdapterInfo				= &Forward<&CoreDeviceApi::getAdapterInfo>::call,
				.getValidationMessageCounts = &Forward<&CoreDeviceApi::getValidationMessageCounts>::call,
				.destroy					= &validated_destroy,
				.collectGarbage				= &Forward<&CoreDeviceApi::collectGarbage>::call,
				.collectGarbageTimeline		= &Checked<&CoreDeviceApi::collectGarbageTimeline>::call,
				.destroyDevice				= &validated_destroy_device,
			};

			return block;
		}

		const PresentApi & ValidatingPresentApi() noexcept
		{
			static const PresentApi block{
				.createSwapchain = &validated_create_swapchain,
			};

			return block;
		}

		const PlacedMemoryApi & ValidatingPlacedMemoryApi() noexcept
		{
			static const PlacedMemoryApi block{
				.createHeap			  = &Recording<ResourceType::eHeap, &PlacedMemoryApi::createHeap>::call,
				.createPlacedBuffer	  = &Recording<ResourceType::eBuffer, &PlacedMemoryApi::createPlacedBuffer>::call,
				.createPlacedTexture  = &Recording<ResourceType::eTexture, &PlacedMemoryApi::createPlacedTexture>::call,
				.getTextureMemoryInfo = &Forward<&PlacedMemoryApi::getTextureMemoryInfo>::call,
				.getBufferMemoryInfo  = &Forward<&PlacedMemoryApi::getBufferMemoryInfo>::call,
			};

			return block;
		}

		const RayTracingApi & ValidatingRayTracingApi() noexcept
		{
			static const RayTracingApi block{
				.createRayTracingPipeline	 = &Recording<ResourceType::eRayTracingPipeline, &RayTracingApi::createRayTracingPipeline>::call,
				.createAccelerationStructure = &Recording<ResourceType::eAccelerationStructure, &RayTracingApi::createAccelerationStructure>::call,
				.updateDescriptorsAccelerationStructure =
					&ValidatedWrites<DescriptorWriteAccelerationStructure, &RayTracingApi::updateDescriptorsAccelerationStructure>::call,
			};

			return block;
		}

		const QueryApi & ValidatingQueryApi() noexcept
		{
			static const QueryApi block{
				.createQueryPool	= &validated_create_query_pool,
				.calibrateTimestamp = &Forward<&QueryApi::calibrateTimestamp>::call,
			};

			return block;
		}

		const PipelineCacheApi & ValidatingPipelineCacheApi() noexcept
		{
			static const PipelineCacheApi block{
				.createPipelineCache  = &Recording<ResourceType::ePipelineCache, &PipelineCacheApi::createPipelineCache>::call,
				.getPipelineCacheData = &Checked<&PipelineCacheApi::getPipelineCacheData>::call,
			};

			return block;
		}

		const ResourceIntrospectionApi & validating_resource_introspection_api() noexcept
		{
			static const ResourceIntrospectionApi block{
				.getTextureInfo = &Checked<&ResourceIntrospectionApi::getTextureInfo>::call,
				.getBufferInfo	= &Checked<&ResourceIntrospectionApi::getBufferInfo>::call,
			};

			return block;
		}

		const ResidencyApi & ValidatingResidencyApi() noexcept
		{
			static const ResidencyApi block{
				.queryMemoryBudget	  = &Forward<&ResidencyApi::queryMemoryBudget>::call,
				.setResidencyPriority = &Checked<&ResidencyApi::setResidencyPriority>::call,
			};

			return block;
		}

		const AdoptionApi & ValidatingAdoptionApi() noexcept
		{
			static const AdoptionApi block{
				.adoptBuffer			  = &RecordingAdopted<ResourceType::eBuffer, &AdoptionApi::adoptBuffer>::call,
				.adoptTexture			  = &RecordingAdopted<ResourceType::eTexture, &AdoptionApi::adoptTexture>::call,
				.getNativeBuffer		  = &Checked<&AdoptionApi::getNativeBuffer>::call,
				.getNativeTexture		  = &Checked<&AdoptionApi::getNativeTexture>::call,
				.adoptTextureView		  = &RecordingAdopted<ResourceType::eTextureView, &AdoptionApi::adoptTextureView>::call,
				.adoptSampler			  = &RecordingAdopted<ResourceType::eSampler, &AdoptionApi::adoptSampler>::call,
				.getNativeTextureView	  = &Checked<&AdoptionApi::getNativeTextureView>::call,
				.getNativeSampler		  = &Checked<&AdoptionApi::getNativeSampler>::call,
				.adoptTimeline			  = &Recording<ResourceType::eTimeline, &AdoptionApi::adoptTimeline>::call,
				.adoptBinarySemaphore	  = &Recording<ResourceType::eBinarySemaphore, &AdoptionApi::adoptBinarySemaphore>::call,
				.getNativeTimeline		  = &Checked<&AdoptionApi::getNativeTimeline>::call,
				.getNativeBinarySemaphore = &Checked<&AdoptionApi::getNativeBinarySemaphore>::call,
			};

			return block;
		}

		const ExternalSharingApi & ValidatingExternalSharingApi() noexcept
		{
			static const ExternalSharingApi block{
				.exportBuffer		   = &Checked<&ExternalSharingApi::exportBuffer>::call,
				.exportHeap			   = &Checked<&ExternalSharingApi::exportHeap>::call,
				.exportTexture		   = &Checked<&ExternalSharingApi::exportTexture>::call,
				.exportTimeline		   = &Checked<&ExternalSharingApi::exportTimeline>::call,
				.exportBinarySemaphore = &Checked<&ExternalSharingApi::exportBinarySemaphore>::call,
				.importBuffer		   = &Recording<ResourceType::eBuffer, &ExternalSharingApi::importBuffer>::call,
				.importHeap			   = &Recording<ResourceType::eHeap, &ExternalSharingApi::importHeap>::call,
				.importTexture		   = &Recording<ResourceType::eTexture, &ExternalSharingApi::importTexture>::call,
				.importTimeline		   = &Recording<ResourceType::eTimeline, &ExternalSharingApi::importTimeline>::call,
				.importBinarySemaphore = &Recording<ResourceType::eBinarySemaphore, &ExternalSharingApi::importBinarySemaphore>::call,
				.closeExportedHandle   = &Forward<&ExternalSharingApi::closeExportedHandle>::call,
			};

			return block;
		}

		const QueueApi & ValidatingQueueApi() noexcept
		{
			static const QueueApi block{
				.getType		   = &Forward<&QueueApi::getType>::call,
				.submit			   = &validated_submit,
				.waitIdle		   = &Forward<&QueueApi::waitIdle>::call,
				.getCompletedValue = &Checked<&QueueApi::getCompletedValue>::call,
				.wait			   = &Checked<&QueueApi::wait>::call,
				.signal			   = &Checked<&QueueApi::signal>::call,
				.beginDebugLabel   = &Forward<&QueueApi::beginDebugLabel>::call,
				.endDebugLabel	   = &Forward<&QueueApi::endDebugLabel>::call,
			};

			return block;
		}

		const SparseApi & ValidatingSparseApi() noexcept
		{
			static const SparseApi block{
				.bindSparse = &validated_bind_sparse,
			};

			return block;
		}

		const CommandPoolApi & ValidatingCommandPoolApi() noexcept
		{
			static const CommandPoolApi block{
				.allocate = &validated_allocate_command_list,
				.reset	  = &validated_command_pool_reset,
			};

			return block;
		}

		const DescriptorArenaApi & ValidatingDescriptorArenaApi() noexcept
		{
			static const DescriptorArenaApi block{
				.allocate = &validated_allocate_descriptor_set,
				.reset	  = &validated_arena_reset,
			};

			return block;
		}

		template <bool ChecksThread>
		const RenderCommandApi & ValidatingRenderCommandApi() noexcept
		{
			static const RenderCommandApi block{
				.begin				 = &validated_begin,
				.end				 = &validated_end,
				.barriers			 = &validated_barriers,
				.beginRendering		 = &validated_begin_rendering,
				.endRendering		 = &validated_end_rendering,
				.setGraphicsPipeline = &validated_set_graphics_pipeline,
				.setComputePipeline	 = &validated_set_compute_pipeline,
				.bindDescriptorSet	 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::bindDescriptorSet>::call,
				.pushConstants		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::pushConstants>::call,
				.setViewport		 = &RecordedEntry<ChecksThread, &RenderCommandApi::setViewport>::call,
				.setScissor			 = &RecordedEntry<ChecksThread, &RenderCommandApi::setScissor>::call,
				.setBlendConstants	 = &RecordedEntry<ChecksThread, &RenderCommandApi::setBlendConstants>::call,
				.setStencilReference = &RecordedEntry<ChecksThread, &RenderCommandApi::setStencilReference>::call,
				.setDepthBias		 = &RecordedEntry<ChecksThread, &RenderCommandApi::setDepthBias>::call,
				.setVertexBuffer	 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::setVertexBuffer>::call,
				.setIndexBuffer		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::setIndexBuffer>::call,
				.draw				 = &validated_draw,
				.drawIndexed		 = &validated_draw_indexed,
				.dispatch			 = &validated_dispatch,
				.copyBuffer			 = &OutsideRendering<ChecksThread, &RenderCommandApi::copyBuffer>::call,
				.copyBufferToTexture = &OutsideRendering<ChecksThread, &RenderCommandApi::copyBufferToTexture>::call,
				.copyTextureToBuffer = &OutsideRendering<ChecksThread, &RenderCommandApi::copyTextureToBuffer>::call,
				.copyTexture		 = &OutsideRendering<ChecksThread, &RenderCommandApi::copyTexture>::call,
				.clearBuffer		 = &validated_clear_buffer,
				.clearTexture		 = &validated_clear_texture,
				.resolveTexture		 = &OutsideRendering<ChecksThread, &RenderCommandApi::resolveTexture>::call,
				.blit				 = &OutsideRendering<ChecksThread, &RenderCommandApi::blit>::call,
				.generateMips		 = &validated_generate_mips,
				.beginDebugLabel	 = &RecordedEntry<ChecksThread, &RenderCommandApi::beginDebugLabel>::call,
				.endDebugLabel		 = &RecordedEntry<ChecksThread, &RenderCommandApi::endDebugLabel>::call,
			};

			return block;
		}

		template <bool ChecksThread>
		const AliasingCommandApi & ValidatingAliasingCommandApi() noexcept
		{
			static const AliasingCommandApi block{
				.aliasBarriers = &validated_alias_barriers,
			};

			return block;
		}

		template <bool ChecksThread>
		const RayTracingCommandApi & ValidatingRayTracingCommandApi() noexcept
		{
			static const RayTracingCommandApi block{
				.setRayTracingPipeline		  = &validated_set_ray_tracing_pipeline,
				.buildAccelerationStructures  = &validated_build_acceleration_structures,
				.copyAccelerationStructure	  = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::copyAccelerationStructure>::call,
				.compactAccelerationStructure = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::compactAccelerationStructure>::call,
				.traceRays					  = &OutsideRenderingWithRayTracing<ChecksThread, &RayTracingCommandApi::traceRays>::call,
			};

			return block;
		}

		template <bool ChecksThread>
		const QueryCommandApi & ValidatingQueryCommandApi() noexcept
		{
			static const QueryCommandApi block{
				.resetQueryPool	  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::resetQueryPool>::call,
				.writeTimestamp	  = &validated_write_timestamp,
				.beginQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::beginQuery>::call,
				.endQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::endQuery>::call,
				.resolveQueryData = &OutsideRendering<ChecksThread, &QueryCommandApi::resolveQueryData>::call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectApi & ValidatingIndirectApi() noexcept
		{
			static const IndirectApi block{
				.drawIndirect		 = &InsideRenderingWithGraphics<ChecksThread, &IndirectApi::drawIndirect>::call,
				.drawIndexedIndirect = &InsideRenderingWithGraphics<ChecksThread, &IndirectApi::drawIndexedIndirect>::call,
				.dispatchIndirect	 = &OutsideRenderingWithCompute<ChecksThread, &IndirectApi::dispatchIndirect>::call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectCountApi & ValidatingIndirectCountApi() noexcept
		{
			static const IndirectCountApi block{
				.drawIndirectCount		  = &InsideRenderingWithGraphics<ChecksThread, &IndirectCountApi::drawIndirectCount>::call,
				.drawIndexedIndirectCount = &InsideRenderingWithGraphics<ChecksThread, &IndirectCountApi::drawIndexedIndirectCount>::call,
			};

			return block;
		}

		template <bool ChecksThread>
		const NativeEscapeApi & ValidatingNativeEscapeApi() noexcept
		{
			static const NativeEscapeApi block{
				.beginNativeMutation = &RecordedCheckedEntry<ChecksThread, &NativeEscapeApi::beginNativeMutation>::call,
				.endNativeMutation	 = &validated_end_native_mutation,
			};

			return block;
		}

		void record_back_buffer(WrappedSwapchain * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const RegisteredHandle registered{
				.type		= type,
				.index		= index,
				.generation = generation,
			};

			if (self->validator->Handles().lookup(registered) == nullptr && !self->validator->Handles().record(registered))
			{
				return;
			}

			const Format format = self->blocks->getFormat(self->inner);
			if (format != Format::eUndefined)
			{
				self->validator->Handles().lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
			}
		}

		TextureHandle validated_get_back_buffer(void * impl, const std::uint32_t imageIndex) noexcept
		{
			auto * self					= static_cast<WrappedSwapchain *>(impl);
			const TextureHandle texture = self->blocks->getBackBuffer(self->inner, imageIndex);
			if (texture.is_valid())
			{
				record_back_buffer(self, ResourceType::eTexture, texture.index, texture.generation);
			}

			return texture;
		}

		TextureViewHandle validated_get_back_buffer_view(void * impl, const std::uint32_t imageIndex) noexcept
		{
			auto * self					 = static_cast<WrappedSwapchain *>(impl);
			const TextureViewHandle view = self->blocks->getBackBufferView(self->inner, imageIndex);
			if (view.is_valid())
			{
				record_back_buffer(self, ResourceType::eTextureView, view.index, view.generation);
			}

			return view;
		}

		const SwapchainApi & ValidatingSwapchainApi() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage			 = &Forward<&SwapchainApi::acquireNextImage>::call,
				.present					 = &validated_present,
				.getBackBuffer				 = &validated_get_back_buffer,
				.getBackBufferView			 = &validated_get_back_buffer_view,
				.getPerImagePresentSemaphore = &Vending<ResourceType::eBinarySemaphore, &SwapchainApi::getPerImagePresentSemaphore>::call,
				.getFormat					 = &Forward<&SwapchainApi::getFormat>::call,
				.getPresentMode				 = &Forward<&SwapchainApi::getPresentMode>::call,
				.getImageCount				 = &Forward<&SwapchainApi::getImageCount>::call,
				.getWidth					 = &Forward<&SwapchainApi::getWidth>::call,
				.getHeight					 = &Forward<&SwapchainApi::getHeight>::call,
				.resize						 = &Forward<&SwapchainApi::resize>::call,
				.setPresentMode				 = &Forward<&SwapchainApi::setPresentMode>::call,
				.supportsReadback			 = &Forward<&SwapchainApi::supportsReadback>::call,
			};

			return block;
		}

		template <class Block, const Block & (*Table)() noexcept>
		struct WrappedBlock final
		{
			[[nodiscard]] static const void * Match(WrappedObject * self, const InterfaceId id, const std::uint32_t minVersion) noexcept
			{
				if (id != InterfaceTraits<Block>::kId || minVersion > InterfaceTraits<Block>::kVersion || inner_block<Block>(self) == nullptr)
				{
					return nullptr;
				}

				return &Table();
			}
		};

		void * inner_object(void * impl) noexcept
		{
			return static_cast<WrappedObject *>(impl)->inner;
		}

		const NativeObjectApi & validating_native_object_api() noexcept
		{
			static const NativeObjectApi block{ .inner = &inner_object };
			return block;
		}

		struct NativeObjectBlock final
		{
			[[nodiscard]] static const void * Match(WrappedObject * /*unused*/, const InterfaceId id, const std::uint32_t minVersion) noexcept
			{
				if (id != InterfaceTraits<NativeObjectApi>::kId || minVersion > InterfaceTraits<NativeObjectApi>::kVersion)
				{
					return nullptr;
				}

				return &validating_native_object_api();
			}
		};

		template <class... Blocks>
		[[nodiscard]] const void * query_wrapped(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			auto * self		   = static_cast<WrappedObject *>(object);
			const void * found = nullptr;
			((found = found != nullptr ? found : Blocks::Match(self, id, minVersion)), ...);
			return found;
		}

		template <class... Blocks>
		[[nodiscard]] const BackendObject * wrapping_object() noexcept
		{
			static constexpr BackendObject kObject{ .queryInterface = &query_wrapped<Blocks...> };
			return &kObject;
		}

		[[nodiscard]] const BackendObject * device_object() noexcept
		{
			return wrapping_object<
				NativeObjectBlock,
				WrappedBlock<CoreDeviceApi, &ValidatingCoreDeviceApi>,
				WrappedBlock<PresentApi, &ValidatingPresentApi>,
				WrappedBlock<PlacedMemoryApi, &ValidatingPlacedMemoryApi>,
				WrappedBlock<RayTracingApi, &ValidatingRayTracingApi>,
				WrappedBlock<QueryApi, &ValidatingQueryApi>,
				WrappedBlock<PipelineCacheApi, &ValidatingPipelineCacheApi>,
				WrappedBlock<ResidencyApi, &ValidatingResidencyApi>,
				WrappedBlock<ResourceIntrospectionApi, &validating_resource_introspection_api>,
				WrappedBlock<AdoptionApi, &ValidatingAdoptionApi>,
				WrappedBlock<ExternalSharingApi, &ValidatingExternalSharingApi>>();
		}

		[[nodiscard]] const BackendObject * queue_object() noexcept
		{
			return wrapping_object<NativeObjectBlock, WrappedBlock<QueueApi, &ValidatingQueueApi>, WrappedBlock<SparseApi, &ValidatingSparseApi>>();
		}

		[[nodiscard]] const BackendObject * command_pool_object() noexcept
		{
			return wrapping_object<NativeObjectBlock, WrappedBlock<CommandPoolApi, &ValidatingCommandPoolApi>>();
		}

		template <bool ChecksThread>
		[[nodiscard]] const BackendObject * command_list_object() noexcept
		{
			return wrapping_object<
				NativeObjectBlock,
				WrappedBlock<RenderCommandApi, &ValidatingRenderCommandApi<ChecksThread>>,
				WrappedBlock<AliasingCommandApi, &ValidatingAliasingCommandApi<ChecksThread>>,
				WrappedBlock<RayTracingCommandApi, &ValidatingRayTracingCommandApi<ChecksThread>>,
				WrappedBlock<QueryCommandApi, &ValidatingQueryCommandApi<ChecksThread>>,
				WrappedBlock<IndirectApi, &ValidatingIndirectApi<ChecksThread>>,
				WrappedBlock<IndirectCountApi, &ValidatingIndirectCountApi<ChecksThread>>,
				WrappedBlock<NativeEscapeApi, &ValidatingNativeEscapeApi<ChecksThread>>>();
		}

		[[nodiscard]] const BackendObject * descriptor_arena_object() noexcept
		{
			return wrapping_object<NativeObjectBlock, WrappedBlock<DescriptorArenaApi, &ValidatingDescriptorArenaApi>>();
		}

		[[nodiscard]] const BackendObject * swapchain_object() noexcept
		{
			return wrapping_object<NativeObjectBlock, WrappedBlock<SwapchainApi, &ValidatingSwapchainApi>>();
		}

		template <class Wrapper>
		[[nodiscard]] Wrapper * adopt(WrappedDevice * device, HostUniquePtr<Wrapper> child) noexcept
		{
			Wrapper * raw	  = child.get();
			raw->releaseChild = [](WrappedObject * object) noexcept
			{
				HostDeleter{ .size = sizeof(Wrapper), .alignment = alignof(Wrapper) }(static_cast<Wrapper *>(object));
			};

			WrappedObject * head = device->children.load(std::memory_order_relaxed);
			do
			{
				raw->nextChild = head;
			} while (!device->children.compare_exchange_weak(head, raw, std::memory_order_release, std::memory_order_relaxed));

			static_cast<void>(child.release());
			return raw;
		}

		void * validated_get_queue(void * impl, const QueueType type, const std::uint32_t index, Error * error) noexcept
		{
			auto * self		  = static_cast<WrappedDevice *>(impl);
			void * innerQueue = self->blocks.core->getQueue(self->inner, type, index, error);
			if (innerQueue == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedQueue> wrapper = host_new<WrappedQueue>();
			if (wrapper == nullptr)
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused the storage a validated queue needs");
			}

			wrapper->object		   = queue_object();
			wrapper->inner		   = innerQueue;
			wrapper->device		   = self;
			wrapper->validator	   = self->validator;
			wrapper->blocks.core   = detail::query_block<QueueApi>(innerQueue);
			wrapper->blocks.sparse = detail::query_block<SparseApi>(innerQueue);
			wrapper->type		   = type;

			return adopt(self, std::move(wrapper));
		}

		void * validated_create_command_pool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
		{
			auto * self		 = static_cast<WrappedDevice *>(impl);
			void * innerPool = self->blocks.core->createCommandPool(self->inner, desc, error);
			if (innerPool == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedCommandPool> wrapper = host_new<WrappedCommandPool>();
			if (wrapper == nullptr)
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused the storage a validated command pool needs");
			}

			wrapper->object	   = command_pool_object();
			wrapper->inner	   = innerPool;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::query_block<CommandPoolApi>(innerPool);
			wrapper->queueType = desc.queueType;

			return adopt(self, std::move(wrapper));
		}

		void * validated_create_descriptor_arena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept
		{
			auto * self		  = static_cast<WrappedDevice *>(impl);
			void * innerArena = self->blocks.core->createDescriptorArena(self->inner, desc, error);
			if (innerArena == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedDescriptorArena> wrapper = host_new<WrappedDescriptorArena>();
			if (wrapper == nullptr)
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused the storage a validated descriptor arena needs");
			}

			wrapper->object	   = descriptor_arena_object();
			wrapper->inner	   = innerArena;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::query_block<DescriptorArenaApi>(innerArena);
			wrapper->id		   = self->nextArenaId.fetch_add(1, std::memory_order_relaxed);

			return adopt(self, std::move(wrapper));
		}

		void * validated_create_swapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
		{
			auto * self			  = static_cast<WrappedDevice *>(impl);
			void * innerSwapchain = self->blocks.present->createSwapchain(self->inner, desc, error);
			if (innerSwapchain == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedSwapchain> wrapper = host_new<WrappedSwapchain>();
			if (wrapper == nullptr)
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused the storage a validated swapchain needs");
			}

			wrapper->object	   = swapchain_object();
			wrapper->inner	   = innerSwapchain;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::query_block<SwapchainApi>(innerSwapchain);

			return adopt(self, std::move(wrapper));
		}

		bool validated_command_pool_reset(void * impl, const RetirePoint safeAfter, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandPool *>(impl);
			if (!all_usable(*self->validator, safeAfter.timeline))
			{
				return self->validator->fail(error, "command pool reset names a timeline this device did not create or has destroyed");
			}

			if (!self->blocks->reset(self->inner, safeAfter, error))
			{
				return false;
			}

			for (const auto & [inner, list] : self->lists)
			{
				list->recording = false;
				list->rendering = false;
				list->recordedStates.clear();
				list->requiredStates.clear();
				list->pendingOwnership.clear();
				list->pendingArrivals.clear();
			}

			return true;
		}

		void * validated_allocate_command_list(void * impl, const CString debugName, Error * error) noexcept
		{
			auto * self		 = static_cast<WrappedCommandPool *>(impl);
			void * innerList = self->blocks->allocate(self->inner, debugName, error);
			if (innerList == nullptr)
			{
				return nullptr;
			}

			if (const auto recycled = self->lists.find(innerList); recycled != self->lists.end())
			{
				return recycled->second;
			}

			HostUniquePtr<WrappedCommandList> wrapper = host_new<WrappedCommandList>();
			if (wrapper == nullptr)
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused the storage a validated command list needs");
			}

			wrapper->object	   = self->validator->checks_state() ? command_list_object<true>() : command_list_object<false>();
			wrapper->inner	   = innerList;
			wrapper->device	   = self->device;
			wrapper->validator = self->validator;
			wrapper->queueType = self->queueType;

			wrapper->blocks.render		  = detail::query_block<RenderCommandApi>(innerList);
			wrapper->blocks.aliasing	  = detail::query_block<AliasingCommandApi>(innerList);
			wrapper->blocks.rayTracing	  = detail::query_block<RayTracingCommandApi>(innerList);
			wrapper->blocks.query		  = detail::query_block<QueryCommandApi>(innerList);
			wrapper->blocks.indirect	  = detail::query_block<IndirectApi>(innerList);
			wrapper->blocks.indirectCount = detail::query_block<IndirectCountApi>(innerList);
			wrapper->blocks.nativeEscape  = detail::query_block<NativeEscapeApi>(innerList);

			void * adopted = adopt(self->device, std::move(wrapper));

			if (!detail::try_insert_or_assign(self->lists, innerList, static_cast<WrappedCommandList *>(adopted)))
			{
				return self->validator->fail_value<void *>(error, "the host allocator refused to record a validated command list");
			}

			return adopted;
		}

		void validated_destroy_device(void * impl) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			self->blocks.core->destroyDevice(self->inner);

			for (WrappedObject * child = self->children.exchange(nullptr, std::memory_order_acquire); child != nullptr;)
			{
				WrappedObject * next = child->nextChild;
				child->releaseChild(child);
				child = next;
			}

			HostDeleter{ .size = sizeof(WrappedDevice), .alignment = alignof(WrappedDevice) }(self);
		}

		[[nodiscard]] void * unwrap(void * impl) noexcept
		{
			return impl != nullptr ? static_cast<WrappedObject *>(impl)->inner : nullptr;
		}

		bool validated_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedQueue *>(impl);

			if (!argument_is_usable(*self->validator, desc))
			{
				return self->validator->fail(error, "a submit waits on or signals a timeline this device has already taken back");
			}

			detail::HostVector<CommandList> unwrapped;
			detail::HostVector<const CommandList *> pointers;
			if (!detail::try_reserve(unwrapped, desc.commandLists.size()) || !detail::try_reserve(pointers, desc.commandLists.size()))
			{
				return self->validator->fail(error, "the host allocator refused the storage a validated submit needs");
			}

			for (const CommandList * list : desc.commandLists)
			{
				if (list == nullptr)
				{
					return self->validator->fail(error, "submit was given a null command list");
				}

				auto * wrapper = static_cast<WrappedCommandList *>(detail::FacadeBuilder::impl_of(*list));

				if (self->validator->checks_state() && wrapper->queueType != self->type)
				{
					return self->validator->fail(error, "submit of a command list recorded for a different queue type than the queue it was given to");
				}

				if (self->validator->checks_state() && wrapper->recording)
				{
					return self->validator->fail(error, "submit of a command list that is still recording, so End was never called on it");
				}

				unwrapped.push_back(detail::FacadeBuilder::make_command_list(wrapper->inner, &wrapper->blocks));
			}

			for (const CommandList & list : unwrapped)
			{
				pointers.push_back(&list);
			}

			if (self->validator->checks_state() && (!submitted_ownership_is_legal(*self->validator, desc.commandLists, error) ||
													   !submitted_states_are_legal(*self->validator, desc.commandLists, error)))
			{
				return false;
			}

			SubmitDesc inner   = desc;
			inner.commandLists = pointers;
			if (!self->blocks.core->submit(self->inner, inner, error))
			{
				return false;
			}

			for (const CommandList * list : desc.commandLists)
			{
				apply_pending_ownership(*self->validator, *static_cast<const WrappedCommandList *>(detail::FacadeBuilder::impl_of(*list)));
			}

			return true;
		}

		bool validated_bind_sparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedQueue *>(impl);

			if (!argument_is_usable(*self->validator, desc))
			{
				return self->validator->fail(error, "a sparse bind names a resource this device has already taken back");
			}

			if (self->validator->checks_state())
			{
				for (const SparseBufferBind & bind : desc.buffers)
				{
					if (!bind.buffer.is_valid())
					{
						return self->validator->fail(error, "a sparse buffer bind names no buffer");
					}

					if (bind.page.heap.is_valid() && bind.page.size == 0)
					{
						return self->validator->fail(error, "a sparse buffer bind supplies a heap and then binds none of it");
					}
				}

				for (const SparseTextureBind & bind : desc.textures)
				{
					if (!bind.texture.is_valid())
					{
						return self->validator->fail(error, "a sparse texture bind names no texture");
					}

					if (bind.page.heap.is_valid() && bind.page.size == 0)
					{
						return self->validator->fail(error, "a sparse texture bind supplies a heap and then binds none of it");
					}
				}
			}

			return self->blocks.sparse->bindSparse(self->inner, desc, error);
		}

		bool validated_arena_reset(void * impl, const RetirePoint safeAfter, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDescriptorArena *>(impl);

			if (!all_usable(*self->validator, safeAfter))
			{
				return self->validator->fail(error, "an arena reset defers to a timeline this device has already taken back");
			}

			static_cast<void>(self->validator->Handles().retire_from(ResourceType::eDescriptorSet, self->id));
			return self->blocks->reset(self->inner, safeAfter, error);
		}

		[[nodiscard]] std::uint64_t declared_usage(
			DeviceValidator & validator,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation
		) noexcept
		{
			const ResourceRecord * record = validator.Handles().lookup(RegisteredHandle{ .type = type, .index = index, .generation = generation });
			return record != nullptr ? record->detail.load(std::memory_order_relaxed) : 0;
		}

		template <class Usage>
		[[nodiscard]] bool declared_for(const std::uint64_t usage, const Usage bit) noexcept
		{
			return (usage & kUsageDeclared) == 0 || (usage & static_cast<std::uint64_t>(bit)) != 0;
		}

		bool validated_clear_buffer(
			void * impl,
			const BufferHandle buffer,
			const std::uint64_t offset,
			const std::uint64_t size,
			const std::uint32_t value,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, buffer))
			{
				return self->validator->fail(error, "clearBuffer names a buffer this device has already taken back");
			}

			if (self->validator->checks_state() && self->rendering)
			{
				return self->validator->fail(error, "clearBuffer cannot be recorded inside a rendering scope, so record it between passes");
			}

			if (!declared_for(declared_usage(*self->validator, ResourceType::eBuffer, buffer.index, buffer.generation), BufferUsage::eStorage))
			{
				return self->validator->fail(error, "clearBuffer needs BufferUsage::eStorage, which is what Direct3D 12 clears through");
			}

			return self->blocks.render->clearBuffer(self->inner, buffer, offset, size, value, error);
		}

		bool validated_clear_texture(
			void * impl,
			const TextureHandle texture,
			const ClearColor & color,
			const std::span<const TextureSubresourceRange> ranges,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, texture))
			{
				return self->validator->fail(error, "clearTexture names a texture this device has already taken back");
			}

			if (self->validator->checks_state() && self->rendering)
			{
				return self->validator->fail(error, "clearTexture cannot be recorded inside a rendering scope, so record it between passes");
			}

			if (!declared_for(declared_usage(*self->validator, ResourceType::eTexture, texture.index, texture.generation), TextureUsage::eColorAttachment))
			{
				return self->validator->fail(error, "clearTexture needs TextureUsage::eColorAttachment, which is what Direct3D 12 and Metal clear through");
			}

			return self->blocks.render->clearTexture(self->inner, texture, color, ranges, error);
		}

		bool validated_write_timestamp(void * impl, const QueryPoolHandle pool, const std::uint32_t query, const Flags<Stage> stage, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, pool))
			{
				return self->validator->fail(error, "writeTimestamp names a query pool this device has already taken back");
			}

			if (!is_one_timestamp_stage(stage))
			{
				return self->validator->fail(error, "writeTimestamp takes a single stage and this mask names more than one");
			}

			if (!queue_can_name_stage(self->queueType, stage))
			{
				return self->validator->fail(error, "writeTimestamp names a stage the queue this list was allocated for cannot reach");
			}

			return self->blocks.query->writeTimestamp(self->inner, pool, query, stage, error);
		}

		bool validated_build_acceleration_structures(void * impl, const std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, builds))
			{
				return self->validator->fail(error, "an acceleration structure build names a resource this device has already taken back");
			}

			if (self->validator->checks_state())
			{
				for (const AccelerationStructureBuildDesc & build : builds)
				{
					if (!build.dst.is_valid())
					{
						return self->validator->fail(error, "an acceleration structure build has no destination");
					}

					if (!build.scratchBuffer.is_valid())
					{
						return self->validator->fail(error, "an acceleration structure build has no scratch buffer");
					}

					if (build.mode == AccelerationStructureBuildMode::eUpdate && !build.src.is_valid())
					{
						return self->validator->fail(error, "an acceleration structure update has no source to refit from");
					}

					if (build.geometries.empty() && build.instanceCount == 0)
					{
						return self->validator->fail(error, "an acceleration structure build has neither geometry nor instances");
					}
				}
			}

			return self->blocks.rayTracing->buildAccelerationStructures(self->inner, builds, error);
		}

		PresentResult validated_present(
			void * impl,
			const std::uint32_t imageIndex,
			const BinarySemaphoreHandle renderFinished,
			void * queueImpl,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedSwapchain *>(impl);

			if (!all_usable(*self->validator, renderFinished))
			{
				return self->validator->fail_value<PresentResult>(error, "present waits on a semaphore this device has already taken back");
			}

			return self->blocks->present(self->inner, imageIndex, renderFinished, unwrap(queueImpl), error);
		}

		bool validated_destroy(void * impl, const ResourceType type, const RawHandle handle, const DestroyDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			const RegisteredHandle registered{
				.type		= type,
				.index		= handle.index,
				.generation = handle.generation,
			};

			if (!all_usable(*self->validator, desc.safeAfter))
			{
				return self->validator->fail(error, "a destroy defers to a timeline this device has already taken back");
			}

			if (!self->validator->Handles().retire(registered))
			{
				return self->validator->fail(
					error,
					"destroy of a handle this device never handed out, has already taken back, or that belongs to another device"
				);
			}

			if (!self->blocks.core->destroy(self->inner, type, handle, desc, error))
			{
				static_cast<void>(self->validator->Handles().restore(registered));
				return false;
			}

			return true;
		}

		[[nodiscard]] std::uint32_t pack_state(const ResourceState & state) noexcept
		{
			return state.use.bits();
		}

		constexpr std::uint32_t kUnknownState = std::numeric_limits<std::uint32_t>::max();

		[[nodiscard]] bool states_agree(const std::uint32_t tracked, const std::uint32_t wanted) noexcept
		{
			return tracked == wanted || tracked == kUnknownState;
		}

		[[nodiscard]] RegisteredHandle tracked_resource(const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			return RegisteredHandle{
				.type		= type,
				.index		= index,
				.generation = generation,
			};
		}

		[[nodiscard]] std::uint32_t span_end(const std::uint32_t begin, const std::uint32_t count) noexcept
		{
			constexpr std::uint32_t kUnbounded = std::numeric_limits<std::uint32_t>::max();
			return count > kUnbounded - begin ? kUnbounded : begin + count;
		}

		[[nodiscard]] DeclaredExtents extents_of(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation
		) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= type,
					.index		= index,
					.generation = generation,
				}
			);
			return record != nullptr ? extents_from(type, record->detail.load(std::memory_order_relaxed)) : DeclaredExtents{};
		}

		[[nodiscard]] std::uint64_t declared_size_of(WrappedCommandList * self, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= ResourceType::eBuffer,
					.index		= index,
					.generation = generation,
				}
			);
			return record != nullptr ? declared_size_from(record->detail.load(std::memory_order_relaxed)) : 0;
		}

		[[nodiscard]] std::uint32_t bound_or(const std::uint32_t declared, const std::uint32_t fallback) noexcept
		{
			return declared != 0 ? declared : fallback;
		}

		[[nodiscard]] TrackedSubrange whole_resource_span(const DeclaredExtents & extents = {}) noexcept
		{
			constexpr std::uint32_t kUnbounded = std::numeric_limits<std::uint32_t>::max();
			return TrackedSubrange{
				.aspects	= bound_or(extents.aspects, kUnbounded),
				.mipBegin	= 0,
				.mipEnd		= bound_or(extents.mips, kUnbounded),
				.layerBegin = 0,
				.layerEnd	= bound_or(extents.layers, kUnbounded),
				.byteEnd	= extents.bytes != 0 ? extents.bytes : std::numeric_limits<std::uint64_t>::max(),
			};
		}

		[[nodiscard]] TrackedSubrange texture_span(const TextureSubresourceRange & range, const DeclaredExtents & extents) noexcept
		{
			constexpr std::uint32_t kUnbounded = std::numeric_limits<std::uint32_t>::max();
			return TrackedSubrange{
				.aspects	= static_cast<std::uint32_t>(range.aspects.bits()) & bound_or(extents.aspects, kUnbounded),
				.mipBegin	= range.baseMip,
				.mipEnd		= std::min(span_end(range.baseMip, range.mipCount), bound_or(extents.mips, kUnbounded)),
				.layerBegin = range.baseLayer,
				.layerEnd	= std::min(span_end(range.baseLayer, range.layerCount), bound_or(extents.layers, kUnbounded)),
			};
		}

		[[nodiscard]] std::uint64_t byte_span_end(const std::uint64_t begin, const std::uint64_t count) noexcept
		{
			constexpr std::uint64_t kUnbounded = std::numeric_limits<std::uint64_t>::max();
			return count > kUnbounded - begin ? kUnbounded : begin + count;
		}

		[[nodiscard]] TrackedSubrange buffer_span(const BufferBarrier & barrier, const std::uint64_t declared) noexcept
		{
			constexpr std::uint64_t kUnbounded = std::numeric_limits<std::uint64_t>::max();

			TrackedSubrange span = whole_resource_span();
			span.byteBegin		 = barrier.offset;
			span.byteEnd		 = std::min(byte_span_end(barrier.offset, barrier.size), declared != 0 ? declared : kUnbounded);
			return span;
		}

		[[nodiscard]] bool covers_whole_resource(const TrackedSubrange & span, const DeclaredExtents & extents) noexcept
		{
			constexpr std::uint32_t kUnbounded = std::numeric_limits<std::uint32_t>::max();
			const std::uint64_t declaredBytes  = extents.bytes != 0 ? extents.bytes : std::numeric_limits<std::uint64_t>::max();

			return (extents.aspects == 0 || (span.aspects & extents.aspects) == extents.aspects) && span.mipBegin == 0 &&
				   span.mipEnd >= bound_or(extents.mips, kUnbounded) && span.layerBegin == 0 && span.layerEnd >= bound_or(extents.layers, kUnbounded) &&
				   span.byteBegin == 0 && span.byteEnd >= declaredBytes;
		}

		[[nodiscard]] bool overlaps(const TrackedSubrange & lhs, const TrackedSubrange & rhs) noexcept
		{
			return lhs.resource == rhs.resource && (lhs.aspects & rhs.aspects) != 0u && lhs.mipBegin < rhs.mipEnd && rhs.mipBegin < lhs.mipEnd &&
				   lhs.layerBegin < rhs.layerEnd && rhs.layerBegin < lhs.layerEnd && lhs.byteBegin < rhs.byteEnd && rhs.byteBegin < lhs.byteEnd;
		}

		[[nodiscard]] bool subtract_into(detail::HostVector<TrackedSubrange> & into, const TrackedSubrange & from, const TrackedSubrange & cut) noexcept
		{
			if (const std::uint32_t untouched = from.aspects & ~cut.aspects; untouched != 0u)
			{
				TrackedSubrange piece = from;
				piece.aspects		  = untouched;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			TrackedSubrange shared = from;
			shared.aspects		   = from.aspects & cut.aspects;

			if (shared.mipBegin < cut.mipBegin)
			{
				TrackedSubrange piece = shared;
				piece.mipEnd		  = cut.mipBegin;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			if (shared.mipEnd > cut.mipEnd)
			{
				TrackedSubrange piece = shared;
				piece.mipBegin		  = cut.mipEnd;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			TrackedSubrange band = shared;
			band.mipBegin		 = std::max(shared.mipBegin, cut.mipBegin);
			band.mipEnd			 = std::min(shared.mipEnd, cut.mipEnd);

			if (band.layerBegin < cut.layerBegin)
			{
				TrackedSubrange piece = band;
				piece.layerEnd		  = cut.layerBegin;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			if (band.layerEnd > cut.layerEnd)
			{
				TrackedSubrange piece = band;
				piece.layerBegin	  = cut.layerEnd;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			TrackedSubrange strip = band;
			strip.layerBegin	  = std::max(band.layerBegin, cut.layerBegin);
			strip.layerEnd		  = std::min(band.layerEnd, cut.layerEnd);

			if (strip.byteBegin < cut.byteBegin)
			{
				TrackedSubrange piece = strip;
				piece.byteEnd		  = cut.byteBegin;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			if (strip.byteEnd > cut.byteEnd)
			{
				TrackedSubrange piece = strip;
				piece.byteBegin		  = cut.byteEnd;
				if (!detail::try_push_back(into, piece))
				{
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool state_is_usable_as_after(
			WrappedCommandList * self,
			const ResourceState & after,
			const QueueOwnership & ownership,
			Error * error
		) noexcept
		{
			if (after.use.contains(ResourceUse::eDiscard))
			{
				return self->validator->fail(
					error,
					"a barrier names eDiscard as its after-state, which describes contents arriving at a barrier and not leaving one"
				);
			}

			const bool releasing = ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eReleaseToExternal;

			if (after.use.bits() == 0u && !releasing)
			{
				return self->validator->fail(error, "a barrier names no after-state, so it does not say what the resource is being moved into");
			}

			return true;
		}

		void forget(WrappedCommandList * self, const RegisteredHandle resource) noexcept
		{
			static_cast<void>(std::erase_if(
				self->recordedStates,
				[resource](const TrackedSubrange & entry)
				{
					return entry.resource == resource;
				}
			));
		}

		[[nodiscard]] bool mip_chain_is_ready_to_generate(WrappedCommandList * self, const TextureHandle texture, Error * error) noexcept
		{
			const RegisteredHandle resource = tracked_resource(ResourceType::eTexture, texture.index, texture.generation);
			const std::uint32_t source		= pack_state(ResourceState{ .use = ResourceUse::eCopySrc });
			const std::uint32_t target		= pack_state(ResourceState{ .use = ResourceUse::eCopyDst });

			for (const TrackedSubrange & tracked : self->recordedStates)
			{
				if (tracked.resource != resource)
				{
					continue;
				}

				if (tracked.mipBegin == 0 && tracked.state != source)
				{
					return self->validator->fail(error, "generateMips reads level zero as a copy source and this recording left it in another use");
				}

				if (tracked.mipEnd > 1 && tracked.state != target)
				{
					return self->validator->fail(error, "generateMips writes every level below zero whole and this recording left one of them in another use");
				}
			}

			return true;
		}

		bool validated_generate_mips(void * impl, const TextureHandle texture, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, texture))
			{
				return self->validator->fail(error, "generateMips names a texture this device has already taken back");
			}

			if (self->validator->checks_state())
			{
				if (self->rendering)
				{
					return self->validator->fail(error, "a transfer or dispatch recorded inside a rendering scope, which has to be recorded between passes");
				}

				if (!mip_chain_is_ready_to_generate(self, texture, error))
				{
					return false;
				}
			}

			if (!self->blocks.render->generateMips(self->inner, texture, error))
			{
				return false;
			}

			if (self->validator->checks_state())
			{
				const RegisteredHandle resource = tracked_resource(ResourceType::eTexture, texture.index, texture.generation);
				const std::uint32_t exits		= pack_state(ResourceState{ .use = ResourceUse::eCopySrc, .stages = Stage::eCopy });

				bool tracked = false;
				for (TrackedSubrange & span : self->recordedStates)
				{
					if (span.resource == resource)
					{
						span.state = exits;
						tracked	   = true;
					}
				}

				if (!tracked)
				{
					TrackedSubrange written = whole_resource_span(extents_of(self, ResourceType::eTexture, texture.index, texture.generation));
					written.resource		= resource;
					written.state			= exits;
					static_cast<void>(detail::try_push_back(self->recordedStates, written));
				}
			}

			return true;
		}

		[[nodiscard]] bool retrack(detail::HostVector<TrackedSubrange> & states, const TrackedSubrange & box, const std::uint32_t state) noexcept
		{
			const std::size_t existing = states.size();
			for (std::size_t i = 0; i < existing; ++i)
			{
				const TrackedSubrange covered = azo::rhi::detail::at(states, i);
				if (!overlaps(covered, box))
				{
					continue;
				}

				azo::rhi::detail::at(states, i).aspects = 0u;
				if (!subtract_into(states, covered, box))
				{
					return false;
				}
			}

			TrackedSubrange written = box;
			written.state			= state;
			if (!detail::try_push_back(states, written))
			{
				return false;
			}

			static_cast<void>(std::erase_if(
				states,
				[](const TrackedSubrange & entry)
				{
					return entry.aspects == 0u;
				}
			));
			return true;
		}

		[[nodiscard]] bool retrack(WrappedCommandList * self, const TrackedSubrange & box, const std::uint32_t state) noexcept
		{
			return retrack(self->recordedStates, box, state);
		}

		[[nodiscard]] bool untracked_parts(
			const TrackedSubrange & box,
			const detail::HostVector<TrackedSubrange> & states,
			detail::HostVector<TrackedSubrange> & uncovered
		) noexcept
		{
			if (!detail::try_push_back(uncovered, box))
			{
				return false;
			}
			for (const TrackedSubrange & tracked : states)
			{
				const std::size_t existing = uncovered.size();
				for (std::size_t i = 0; i < existing; ++i)
				{
					const TrackedSubrange piece = azo::rhi::detail::at(uncovered, i);
					if (!overlaps(piece, tracked))
					{
						continue;
					}
					azo::rhi::detail::at(uncovered, i).aspects = 0;
					if (!subtract_into(uncovered, piece, tracked))
					{
						return false;
					}
				}
				static_cast<void>(std::erase_if(
					uncovered,
					[](const TrackedSubrange & piece)
					{
						return piece.aspects == 0;
					}
				));
			}
			return true;
		}

		[[nodiscard]] bool submitted_states_are_legal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept
		{
			detail::HostVector<TrackedSubrange> staged;
			for (const CommandList * list : lists)
			{
				const auto * wrapper = static_cast<const WrappedCommandList *>(detail::FacadeBuilder::impl_of(*list));
				for (const TrackedSubrange & arrival : wrapper->requiredStates)
				{
					for (const TrackedSubrange & earlier : staged)
					{
						if (overlaps(arrival, earlier) && !states_agree(earlier.state, arrival.state))
						{
							return validator.fail(error, "a submitted barrier claims a before-state an earlier command list did not leave");
						}
					}
					detail::HostVector<TrackedSubrange> uncovered;
					if (!untracked_parts(arrival, staged, uncovered))
					{
						return validator.fail(error, "the host allocator refused the storage a validated submit needs to check resource states");
					}
					if (!uncovered.empty())
					{
						if (const ResourceRecord * record = validator.Handles().lookup(arrival.resource);
							record && (record->useKnown.load(std::memory_order_relaxed) && record->use.load(std::memory_order_relaxed) != arrival.state))

						{
							return validator.fail(error, "a submitted barrier claims a before-state the resource did not arrive in");
						}
					}
				}
				for (const TrackedSubrange & finalState : wrapper->recordedStates)
				{
					if (!retrack(staged, finalState, finalState.state))
					{
						return validator.fail(error, "the host allocator refused the storage a validated submit needs to check resource states");
					}
				}
			}
			return true;
		}

		[[nodiscard]] PendingOwnership * pending_ownership_of(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation
		) noexcept
		{
			const RegisteredHandle resource = tracked_resource(type, index, generation);
			for (PendingOwnership & pending : self->pendingOwnership)
			{
				if (pending.resource == resource)
				{
					return &pending;
				}
			}

			return nullptr;
		}

		void set_pending_ownership(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const QueueOwnership & ownership,
			const std::uint8_t owner,
			const bool owned
		) noexcept
		{
			if (PendingOwnership * pending = pending_ownership_of(self, type, index, generation))
			{
				pending->owner = owner;
				pending->owned = owned;
				return;
			}

			static_cast<void>(detail::try_push_back(
				self->pendingOwnership,
				PendingOwnership{
					.resource		  = tracked_resource(type, index, generation),
					.firstOp		  = ownership.op,
					.firstCounterpart = ownership.counterpart,
					.recordedOn		  = self->queueType,
					.owner			  = owner,
					.owned			  = owned,
				}
			));
		}

		[[nodiscard]] bool ownership_step_is_legal(
			const PendingOwnership & pending,
			const bool owned,
			const std::uint8_t held,
			Error * error,
			DeviceValidator & validator
		) noexcept
		{
			if (!owned)
			{
				return true;
			}

			const bool releasing = pending.firstOp == OwnershipOp::eRelease || pending.firstOp == OwnershipOp::eReleaseToExternal;
			if (releasing && held != static_cast<std::uint8_t>(pending.recordedOn))
			{
				return validator.fail(error, "a submitted barrier releases a resource from a queue that does not own it");
			}

			if (pending.firstOp == OwnershipOp::eAcquire && held != static_cast<std::uint8_t>(pending.firstCounterpart))
			{
				return validator.fail(error, "a submitted barrier acquires a resource from a queue that does not hold it");
			}

			return true;
		}

		[[nodiscard]] bool submitted_ownership_is_legal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept
		{
			detail::HostVector<PendingOwnership> staged;

			for (const CommandList * list : lists)
			{
				const auto * wrapper = static_cast<const WrappedCommandList *>(detail::FacadeBuilder::impl_of(*list));
				for (const PendingOwnership & pending : wrapper->pendingOwnership)
				{
					const ResourceRecord * record = validator.Handles().lookup(pending.resource);
					if (record == nullptr)
					{
						continue;
					}

					bool owned				   = record->owned.load(std::memory_order_relaxed);
					std::uint8_t held		   = record->owner.load(std::memory_order_relaxed);
					PendingOwnership * earlier = nullptr;
					for (PendingOwnership & seen : staged)
					{
						if (seen.resource == pending.resource)
						{
							earlier = &seen;
							owned	= seen.owned;
							held	= seen.owner;
							break;
						}
					}

					if (!ownership_step_is_legal(pending, owned, held, error, validator))
					{
						return false;
					}

					if (earlier != nullptr)
					{
						earlier->owner = pending.owner;
						earlier->owned = pending.owned;
						continue;
					}

					if (!detail::try_push_back(staged, pending))
					{
						return validator.fail(error, "the host allocator refused the storage a validated submit needs to check queue ownership");
					}
				}
			}

			return true;
		}

		void apply_pending_ownership(DeviceValidator & validator, const WrappedCommandList & list) noexcept
		{
			for (const PendingOwnership & pending : list.pendingOwnership)
			{
				if (ResourceRecord * record = validator.Handles().lookup(pending.resource))
				{
					record->owner.store(pending.owner, std::memory_order_relaxed);
					record->owned.store(pending.owned, std::memory_order_relaxed);
				}
			}

			for (const PendingArrival & pending : list.pendingArrivals)
			{
				if (ResourceRecord * record = validator.Handles().lookup(pending.resource))
				{
					record->use.store(pending.use, std::memory_order_relaxed);
					record->useKnown.store(pending.known, std::memory_order_relaxed);
				}
			}
		}

		void set_pending_arrival(WrappedCommandList * self, const RegisteredHandle resource, const std::uint32_t use, const bool known) noexcept
		{
			for (PendingArrival & pending : self->pendingArrivals)
			{
				if (pending.resource == resource)
				{
					pending.use	  = use;
					pending.known = known;
					return;
				}
			}

			static_cast<void>(detail::try_push_back(
				self->pendingArrivals,
				PendingArrival{
					.resource = resource,
					.use	  = use,
					.known	  = known,
				}
			));
		}

		[[nodiscard]] bool check_and_transfer_ownership(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const QueueOwnership & ownership,
			Error * error
		) noexcept
		{
			if (ownership.op == OwnershipOp::eNone)
			{
				return true;
			}

			const bool namesQueue = ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eAcquire;
			if (namesQueue && ownership.counterpart == self->queueType)
			{
				return self->validator->fail(error, "a barrier transfers queue ownership between one queue and itself, which is not a transfer");
			}

			const ResourceRecord * record = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= type,
					.index		= index,
					.generation = generation,
				}
			);
			if (record == nullptr)
			{
				return true;
			}

			const PendingOwnership * pending = pending_ownership_of(self, type, index, generation);
			const bool owned				 = pending != nullptr && pending->owned;

			if (owned)
			{
				const std::uint8_t held = pending->owner;
				const bool releasing	= ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eReleaseToExternal;

				if (releasing && held != static_cast<std::uint8_t>(self->queueType))
				{
					return self->validator->fail(error, "a barrier releases a resource from a queue that does not own it");
				}
				if (ownership.op == OwnershipOp::eAcquire && held != static_cast<std::uint8_t>(ownership.counterpart))
				{
					return self->validator->fail(error, "a barrier acquires a resource from a queue that does not hold it");
				}
			}

			if (ownership.op == OwnershipOp::eReleaseToExternal)
			{
				set_pending_ownership(self, type, index, generation, ownership, 0, false);
				return true;
			}

			const QueueType holder = ownership.op == OwnershipOp::eRelease ? ownership.counterpart : self->queueType;
			set_pending_ownership(self, type, index, generation, ownership, static_cast<std::uint8_t>(holder), true);
			return true;
		}

		[[nodiscard]] bool check_and_advance(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const TrackedSubrange & span,
			const ResourceState & before,
			const ResourceState & after,
			const QueueOwnership & ownership,
			Error * error
		) noexcept
		{
			TrackedSubrange box		   = span;
			box.resource			   = tracked_resource(type, index, generation);
			const std::uint32_t wanted = pack_state(before);
			const bool discard		   = before.use == ResourceUse::eDiscard;

			for (const TrackedSubrange & tracked : self->recordedStates)
			{
				if (tracked.resource != box.resource)
				{
					continue;
				}

				if (!overlaps(tracked, box))
				{
					continue;
				}

				if (!discard && !states_agree(tracked.state, wanted))
				{
					return self->validator->fail(error, "a barrier claims a before-state the resource was not left in by the last one");
				}
			}

			if (!discard)
			{
				detail::HostVector<TrackedSubrange> uncovered;
				if (!untracked_parts(box, self->recordedStates, uncovered))
				{
					return self->validator->fail(error, "the host allocator refused the storage needed to record resource arrival states");
				}
				for (TrackedSubrange arrival : uncovered)
				{
					arrival.state = wanted;
					if (!detail::try_push_back(self->requiredStates, arrival))
					{
						return self->validator->fail(error, "the host allocator refused the storage needed to record resource arrival states");
					}
				}
			}

			if (!check_and_transfer_ownership(self, type, index, generation, ownership, error))
			{
				return false;
			}

			if (!retrack(self, box, pack_state(after)))
			{
				forget(self, box.resource);
			}

			if (covers_whole_resource(box, extents_of(self, type, index, generation)))
			{
				set_pending_arrival(self, box.resource, after.use.bits(), true);
			}
			else
			{
				set_pending_arrival(self, box.resource, 0, false);
			}

			return true;
		}

		bool validated_barriers(void * impl, const BarrierBatch & batch, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, batch))
			{
				return self->validator->fail(error, "a barrier names a resource this device has already taken back");
			}

			if (self->validator->checks_state())
			{
				if (self->rendering)
				{
					return self->validator->fail(error, "a barrier recorded inside a rendering scope, which has to be recorded between passes");
				}

				for (const MemoryBarrier & barrier : batch.memory)
				{
					if (!state_is_usable_as_after(self, barrier.after, QueueOwnership{}, error))
					{
						return false;
					}
				}

				for (const BufferBarrier & barrier : batch.buffers)
				{
					if (!state_is_usable_as_after(self, barrier.after, barrier.ownership, error))
					{
						return false;
					}

					if (barrier.size == 0)
					{
						return self->validator->fail(error, "a buffer barrier names no bytes, so it describes no range to transition");
					}

					const std::uint64_t declared = declared_size_of(self, barrier.buffer.index, barrier.buffer.generation);
					if (declared != 0)
					{
						const bool wholeBuffer = barrier.size == std::numeric_limits<std::uint64_t>::max();

						if (barrier.offset >= declared || (!wholeBuffer && byte_span_end(barrier.offset, barrier.size) > declared))
						{
							return self->validator->fail(error, "a buffer barrier names bytes past the end of the buffer");
						}
					}

					if (!check_and_advance(
							self,
							ResourceType::eBuffer,
							barrier.buffer.index,
							barrier.buffer.generation,
							buffer_span(barrier, declared),
							barrier.before,
							barrier.after,
							barrier.ownership,
							error
						))
					{
						return false;
					}
				}

				for (const TextureBarrier & barrier : batch.textures)
				{
					if (!state_is_usable_as_after(self, barrier.after, barrier.ownership, error))
					{
						return false;
					}

					if (barrier.range.aspects.bits() == 0u)
					{
						return self->validator->fail(error, "a texture barrier names no aspect, so it describes no subresource to transition");
					}

					if (!check_and_advance(
							self,
							ResourceType::eTexture,
							barrier.texture.index,
							barrier.texture.generation,
							texture_span(barrier.range, extents_of(self, ResourceType::eTexture, barrier.texture.index, barrier.texture.generation)),
							barrier.before,
							barrier.after,
							barrier.ownership,
							error
						))
					{
						return false;
					}
				}
			}

			return self->blocks.render->barriers(self->inner, batch, error);
		}

		bool validated_alias_barriers(void * impl, const std::span<const AliasBarrier> barriers, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, barriers))
			{
				return self->validator->fail(error, "an alias barrier names a resource this device has already taken back");
			}

			if (self->validator->checks_state() && self->rendering)
			{
				return self->validator->fail(error, "aliasBarriers cannot be recorded inside a rendering scope, so record it between passes");
			}

			return self->blocks.aliasing->aliasBarriers(self->inner, barriers, error);
		}

		void reconcile_native_mutation(
			WrappedCommandList * self,
			const ResourceType type,
			const std::uint32_t index,
			const std::uint32_t generation,
			const TrackedSubrange & span,
			const ResourceState & finalState,
			const bool finalStateUnknown
		) noexcept
		{
			TrackedSubrange written = span;
			written.resource		= tracked_resource(type, index, generation);

			if (!retrack(self, written, finalStateUnknown ? kUnknownState : pack_state(finalState)))
			{
				forget(self, written.resource);
			}

			if (self->validator->Handles().lookup(written.resource) == nullptr)
			{
				return;
			}

			if (finalStateUnknown || !covers_whole_resource(written, extents_of(self, type, index, generation)))
			{
				set_pending_arrival(self, written.resource, 0, false);
				return;
			}

			set_pending_arrival(self, written.resource, finalState.use.bits(), true);
		}

		bool validated_end_native_mutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (self->validator->checks_state())
			{
				for (const NativeTouchedBuffer & touched : desc.buffers)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						const TrackedSubrange span =
							whole_resource_span(extents_of(self, ResourceType::eBuffer, touched.buffer.index, touched.buffer.generation));
						reconcile_native_mutation(
							self,
							ResourceType::eBuffer,
							touched.buffer.index,
							touched.buffer.generation,
							span,
							touched.finalState,
							touched.finalStateUnknown
						);
					}
				}

				for (const NativeTouchedTexture & touched : desc.textures)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						const TrackedSubrange span =
							texture_span(touched.range, extents_of(self, ResourceType::eTexture, touched.texture.index, touched.texture.generation));
						reconcile_native_mutation(
							self,
							ResourceType::eTexture,
							touched.texture.index,
							touched.texture.generation,
							span,
							touched.finalState,
							touched.finalStateUnknown
						);
					}
				}
			}

			return self->blocks.nativeEscape->endNativeMutation(self->inner, desc, error);
		}

		bool validated_begin(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (self->validator->checks_state() && self->recording)
			{
				return self->validator->fail(error, "Begin on a command list that is already recording");
			}

			self->recordedStates.clear();
			self->requiredStates.clear();
			self->pendingOwnership.clear();
			self->pendingArrivals.clear();
			self->rendering		  = false;
			self->graphicsBound	  = false;
			self->computeBound	  = false;
			self->rayTracingBound = false;

			self->recordingThread = std::this_thread::get_id();
			self->checksThread	  = self->validator->checks_state();

			if (!self->blocks.render->begin(self->inner, error))
			{
				return false;
			}

			self->recording = true;
			return true;
		}

		bool validated_end(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error))
			{
				return false;
			}

			if (self->validator->checks_state())
			{
				if (!self->recording)
				{
					return self->validator->fail(error, "End on a command list that is not recording");
				}

				if (self->rendering)
				{
					return self->validator->fail(error, "End with a rendering scope still open");
				}
			}

			if (!self->blocks.render->end(self->inner, error))
			{
				return false;
			}

			self->recording = false;
			return true;
		}

		[[nodiscard]] bool attachment_format_is_renderable(WrappedCommandList * self, const RenderingAttachment & attachment, const bool depthStencil) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().lookup(
				RegisteredHandle{
					.type		= ResourceType::eTextureView,
					.index		= attachment.view.index,
					.generation = attachment.view.generation,
				}
			);
			if (record == nullptr)
			{
				return true;
			}

			const auto format = static_cast<Format>(record->format.load(std::memory_order_relaxed));
			if (format == Format::eUndefined || self->device == nullptr || self->device->blocks.core == nullptr)
			{
				return true;
			}

			const FormatSupport support = self->device->blocks.core->getFormatSupport(self->device->inner, format);
			return depthStencil ? support.depthStencilAttachment : support.colorAttachment;
		}

		bool validated_begin_rendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!argument_is_usable(*self->validator, desc))
			{
				return self->validator->fail(error, "a rendering scope names an attachment this device has already taken back");
			}

			if (self->validator->checks_state())
			{
				if (!self->recording)
				{
					return self->validator->fail(error, "a rendering scope opened on a command list that is not recording");
				}

				if (self->rendering)
				{
					return self->validator->fail(error, "a rendering scope opened inside another one, and nothing nests here");
				}

				for (const RenderingAttachment & color : desc.colors)
				{
					if (!attachment_format_is_renderable(self, color, false))
					{
						return self->validator->fail(error, "a colour attachment in a format this device does not advertise as renderable");
					}
				}

				if (desc.depthStencil != nullptr && !attachment_format_is_renderable(self, *desc.depthStencil, true))
				{
					return self->validator->fail(error, "a depth stencil attachment in a format this device does not advertise as renderable");
				}
			}

			if (!self->blocks.render->beginRendering(self->inner, desc, error))
			{
				return false;
			}

			self->rendering = true;
			return true;
		}

		bool validated_end_rendering(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (self->validator->checks_state() && !self->rendering)
			{
				return self->validator->fail(error, "a rendering scope closed without one being open");
			}

			if (!self->blocks.render->endRendering(self->inner, error))
			{
				return false;
			}

			self->rendering		= false;
			self->graphicsBound = false;
			return true;
		}

		bool validated_set_graphics_pipeline(void * impl, const GraphicsPipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!all_usable(*self->validator, pipeline))
			{
				return self->validator->fail(error, "a graphics pipeline bound after this device took it back");
			}

			if (!self->blocks.render->setGraphicsPipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->graphicsBound = true;
			return true;
		}

		bool validated_set_compute_pipeline(void * impl, const ComputePipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!all_usable(*self->validator, pipeline))
			{
				return self->validator->fail(error, "a compute pipeline bound after this device took it back");
			}

			if (!self->blocks.render->setComputePipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->computeBound = true;
			return true;
		}

		bool validated_set_ray_tracing_pipeline(void * impl, const RayTracingPipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!all_usable(*self->validator, pipeline))
			{
				return self->validator->fail(error, "a ray tracing pipeline bound after this device took it back");
			}

			if (!self->blocks.rayTracing->setRayTracingPipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->rayTracingBound = true;
			return true;
		}

		[[nodiscard]] bool draw_is_legal(WrappedCommandList * self, Error * error) noexcept
		{
			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (!self->validator->checks_state())
			{
				return true;
			}

			if (!self->rendering)
			{
				return self->validator->fail(error, "a draw recorded outside a rendering scope");
			}

			return self->graphicsBound ? true : self->validator->fail(error, "a draw recorded with no graphics pipeline bound");
		}

		bool validated_draw(
			void * impl,
			const std::uint32_t vertexCount,
			const std::uint32_t instanceCount,
			const std::uint32_t firstVertex,
			const std::uint32_t firstInstance,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			return draw_is_legal(self, error) ? self->blocks.render->draw(self->inner, vertexCount, instanceCount, firstVertex, firstInstance, error) : false;
		}

		bool validated_draw_indexed(
			void * impl,
			const std::uint32_t indexCount,
			const std::uint32_t instanceCount,
			const std::uint32_t firstIndex,
			const std::int32_t vertexOffset,
			const std::uint32_t firstInstance,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			return draw_is_legal(self, error)
					   ? self->blocks.render->drawIndexed(self->inner, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, error)
					   : false;
		}

		bool validated_dispatch(
			void * impl,
			const std::uint32_t groupCountX,
			const std::uint32_t groupCountY,
			const std::uint32_t groupCountZ,
			Error * error
		) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!recorded_on_its_own_thread(self, error) || !recorded_into_an_open_list(self, error))
			{
				return false;
			}

			if (self->validator->checks_state())
			{
				if (self->rendering)
				{
					return self->validator->fail(error, "a dispatch recorded inside a rendering scope");
				}

				if (!self->computeBound)
				{
					return self->validator->fail(error, "a dispatch recorded with no compute pipeline bound");
				}
			}

			return self->blocks.render->dispatch(self->inner, groupCountX, groupCountY, groupCountZ, error);
		}

	} // namespace

	void * wrap_device(void * deviceImpl, const ValidationMode mode) noexcept
	{
		if (deviceImpl == nullptr || mode == ValidationMode::eOff)
		{
			return deviceImpl;
		}

		const auto * core = detail::query_block<CoreDeviceApi>(deviceImpl);
		if (core == nullptr)
		{
			return deviceImpl;
		}

		HostUniquePtr<WrappedDevice> wrapper = host_new<WrappedDevice>();
		if (wrapper == nullptr)
		{
			return deviceImpl;
		}

		wrapper->ownedValidator.set_mode(mode);

		wrapper->object	   = device_object();
		wrapper->inner	   = deviceImpl;
		wrapper->device	   = wrapper.get();
		wrapper->validator = &wrapper->ownedValidator;

		wrapper->blocks.core			= core;
		wrapper->blocks.present			= detail::query_block<PresentApi>(deviceImpl);
		wrapper->blocks.placedMemory	= detail::query_block<PlacedMemoryApi>(deviceImpl);
		wrapper->blocks.rayTracing		= detail::query_block<RayTracingApi>(deviceImpl);
		wrapper->blocks.query			= detail::query_block<QueryApi>(deviceImpl);
		wrapper->blocks.pipelineCache	= detail::query_block<PipelineCacheApi>(deviceImpl);
		wrapper->blocks.residency		= detail::query_block<ResidencyApi>(deviceImpl);
		wrapper->blocks.introspection	= detail::query_block<ResourceIntrospectionApi>(deviceImpl);
		wrapper->blocks.adoption		= detail::query_block<AdoptionApi>(deviceImpl);
		wrapper->blocks.externalSharing = detail::query_block<ExternalSharingApi>(deviceImpl);

		return wrapper.release();
	}

	DeviceValidator * validator_of(void * deviceImpl) noexcept
	{
		if (deviceImpl == nullptr || detail::object_of(deviceImpl) != device_object())
		{
			return nullptr;
		}

		return static_cast<WrappedDevice *>(deviceImpl)->validator;
	}

} // namespace azo::rhi::validation

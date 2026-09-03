// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "validation/decorator.hpp"

#include <mutex>
#include <utility>

namespace azo::rhi::validation
{

	template <>
	[[nodiscard]] const CoreDeviceApi * InnerBlock<CoreDeviceApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.core;
	}

	template <>
	[[nodiscard]] const PresentApi * InnerBlock<PresentApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.present;
	}

	template <>
	[[nodiscard]] const PlacedMemoryApi * InnerBlock<PlacedMemoryApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.placedMemory;
	}

	template <>
	[[nodiscard]] const RayTracingApi * InnerBlock<RayTracingApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.rayTracing;
	}

	template <>
	[[nodiscard]] const QueryApi * InnerBlock<QueryApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.query;
	}

	template <>
	[[nodiscard]] const PipelineCacheApi * InnerBlock<PipelineCacheApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.pipelineCache;
	}

	template <>
	[[nodiscard]] const ResidencyApi * InnerBlock<ResidencyApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.residency;
	}

	template <>
	[[nodiscard]] const ResourceIntrospectionApi * InnerBlock<ResourceIntrospectionApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.introspection;
	}

	template <>
	[[nodiscard]] const AdoptionApi * InnerBlock<AdoptionApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.adoption;
	}

	template <>
	[[nodiscard]] const ExternalSharingApi * InnerBlock<ExternalSharingApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDevice *>(self)->blocks.externalSharing;
	}

	template <>
	[[nodiscard]] const QueueApi * InnerBlock<QueueApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedQueue *>(self)->blocks.core;
	}

	template <>
	[[nodiscard]] const SparseApi * InnerBlock<SparseApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedQueue *>(self)->blocks.sparse;
	}

	template <>
	[[nodiscard]] const CommandPoolApi * InnerBlock<CommandPoolApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandPool *>(self)->blocks;
	}

	template <>
	[[nodiscard]] const DescriptorArenaApi * InnerBlock<DescriptorArenaApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedDescriptorArena *>(self)->blocks;
	}

	template <>
	[[nodiscard]] const RenderCommandApi * InnerBlock<RenderCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.render;
	}

	template <>
	[[nodiscard]] const AliasingCommandApi * InnerBlock<AliasingCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.aliasing;
	}

	template <>
	[[nodiscard]] const RayTracingCommandApi * InnerBlock<RayTracingCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.rayTracing;
	}

	template <>
	[[nodiscard]] const QueryCommandApi * InnerBlock<QueryCommandApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.query;
	}

	template <>
	[[nodiscard]] const IndirectApi * InnerBlock<IndirectApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.indirect;
	}

	template <>
	[[nodiscard]] const IndirectCountApi * InnerBlock<IndirectCountApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.indirectCount;
	}

	template <>
	[[nodiscard]] const NativeEscapeApi * InnerBlock<NativeEscapeApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedCommandList *>(self)->blocks.nativeEscape;
	}

	template <>
	[[nodiscard]] const SwapchainApi * InnerBlock<SwapchainApi>(WrappedObject * self) noexcept
	{
		return static_cast<WrappedSwapchain *>(self)->blocks;
	}

	namespace
	{

		void * ValidatedGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
		void * ValidatedCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
		void * ValidatedCreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
		void * ValidatedCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
		void * ValidatedAllocateCommandList(void * impl, CString debugName, Error * error) noexcept;
		bool ValidatedCommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		void ValidatedDestroyDevice(void * impl) noexcept;
		bool ValidatedDestroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept;
		bool ValidatedSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
		void ApplyPendingOwnership(DeviceValidator & validator, const WrappedCommandList & list) noexcept;
		bool SubmittedOwnershipIsLegal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept;
		bool ValidatedBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
		bool ValidatedArenaReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		bool ValidatedBarriers(void * impl, const BarrierBatch & batch, Error * error) noexcept;
		bool ValidatedGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
		bool ValidatedAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
		bool ValidatedWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<Stage> stage, Error * error) noexcept;
		bool ValidatedClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
		bool ValidatedClearTexture(
			void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
		bool ValidatedBuildAccelerationStructures(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept;
		TextureViewHandle ValidatedCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
		GraphicsPipelineHandle ValidatedCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
		DescriptorSetLayoutHandle ValidatedCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
		QueryPoolHandle ValidatedCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
		DescriptorSetHandle ValidatedAllocateDescriptorSet(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
		bool ValidatedBegin(void * impl, Error * error) noexcept;
		bool ValidatedEnd(void * impl, Error * error) noexcept;
		bool ValidatedBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
		bool ValidatedEndRendering(void * impl, Error * error) noexcept;
		bool ValidatedSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
		bool ValidatedSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
		bool ValidatedSetRayTracingPipeline(void * impl, RayTracingPipelineHandle pipeline, Error * error) noexcept;
		bool ValidatedDraw(void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance,
			Error * error) noexcept;
		bool ValidatedDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, Error * error) noexcept;
		bool ValidatedDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
		bool ValidatedEndNativeMutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept;
		PresentResult ValidatedPresent(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept;

		constexpr std::uint32_t kPackedBindings	 = 16;
		constexpr std::uint64_t kBindingTypeMask = 0xFu;

		[[nodiscard]] std::uint64_t PackBindingTypes(const DescriptorSetLayoutDesc & desc) noexcept
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

		[[nodiscard]] std::uint64_t PackLayout(const DescriptorSetLayoutHandle layout) noexcept
		{
			return static_cast<std::uint64_t>(layout.index) | (static_cast<std::uint64_t>(layout.generation) << 32u);
		}

		GraphicsPipelineHandle ValidatedCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (self->validator->ChecksState())
			{
				const std::uint32_t attachments = std::min<std::uint32_t>(desc.blend.attachmentCount, desc.blend.attachments.size());
				for (std::uint32_t slot = 0; slot < attachments; ++slot)
				{
					if (!desc.blend.attachments[slot].blendEnable || slot >= desc.renderTarget.colorFormatCount)
					{
						continue;
					}

					const Format format = desc.renderTarget.colorFormats[slot];
					if (format == Format::eUndefined || self->blocks.core->getFormatSupport(self->inner, format).blendable)
					{
						continue;
					}

					return self->validator->FailValue<GraphicsPipelineHandle>(
						error, "a blend enabled on an attachment in a format this device does not advertise as blendable");
				}
			}

			return Recording<ResourceType::eGraphicsPipeline, &CoreDeviceApi::createGraphicsPipeline>::Call(impl, desc, error);
		}

		TextureViewHandle ValidatedCreateTextureView(void * impl, const TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (!AllUsable(*self->validator, texture))
			{
				return self->validator->FailValue<TextureViewHandle>(error, "a texture view of a texture this device has already taken back");
			}

			Format format = desc.format;
			if (format == Format::eUndefined)
			{
				const ResourceRecord * parent = self->validator->Handles().Lookup(RegisteredHandle{
					.type		= ResourceType::eTexture,
					.index		= texture.index,
					.generation = texture.generation,
				});
				if (parent != nullptr)
				{
					format = static_cast<Format>(parent->format.load(std::memory_order_relaxed));
				}
			}

			const TextureViewHandle view = self->blocks.core->createTextureView(self->inner, texture, desc, error);
			if (view.IsValid())
			{
				const RegisteredHandle registered{
					.type		= ResourceType::eTextureView,
					.index		= view.index,
					.generation = view.generation,
				};
				if (self->validator->Handles().Record(registered) && format != Format::eUndefined)
				{
					self->validator->Handles().Lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
				}
			}

			return view;
		}

		DescriptorSetLayoutHandle ValidatedCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (!ArgumentIsUsable(*self->validator, desc))
			{
				return self->validator->FailValue<DescriptorSetLayoutHandle>(
					error, "a descriptor set layout bakes in an immutable sampler this device has already taken back");
			}

			const DescriptorSetLayoutHandle layout = self->blocks.core->createDescriptorSetLayout(self->inner, desc, error);
			if (!layout.IsValid())
			{
				return layout;
			}

			const RegisteredHandle registered{
				.type		= ResourceType::eDescriptorSetLayout,
				.index		= layout.index,
				.generation = layout.generation,
			};
			if (self->validator->Handles().Record(registered))
			{
				self->validator->Handles().Lookup(registered)->detail.store(PackBindingTypes(desc), std::memory_order_relaxed);
			}

			return layout;
		}

		QueryPoolHandle ValidatedCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (desc.queryCount == 0)
			{
				return self->validator->FailValue<QueryPoolHandle>(error, "query pool creation asked for no queries");
			}

			const QueryPoolHandle pool = self->blocks.query->createQueryPool(self->inner, desc, error);
			if (pool.IsValid())
			{
				static_cast<void>(self->validator->Handles().Record(RegisteredHandle{
					.type		= ResourceType::eQueryPool,
					.index		= pool.index,
					.generation = pool.generation,
				}));
			}

			return pool;
		}

		DescriptorSetHandle ValidatedAllocateDescriptorSet(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDescriptorArena *>(impl);

			if (!AllUsable(*self->validator, desc.layout))
			{
				return self->validator->FailValue<DescriptorSetHandle>(error, "a descriptor set allocated against a layout this device has already taken back");
			}

			const DescriptorSetHandle set = self->blocks->allocate(self->inner, desc, error);
			if (!set.IsValid())
			{
				return set;
			}

			const RegisteredHandle registered{
				.type		= ResourceType::eDescriptorSet,
				.index		= set.index,
				.generation = set.generation,
			};
			if (self->validator->Handles().Record(registered))
			{
				ResourceRecord * record = self->validator->Handles().Lookup(registered);
				record->detail.store(PackLayout(desc.layout), std::memory_order_relaxed);

				record->origin.store(self->id, std::memory_order_relaxed);
			}

			return set;
		}

		[[nodiscard]] bool WriteMatchesLayout(
			WrappedDevice * self, const DescriptorSetHandle set, const std::uint32_t binding, const DescriptorType type, Error * error) noexcept
		{
			const ResourceRecord * setRecord = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= ResourceType::eDescriptorSet,
				.index		= set.index,
				.generation = set.generation,
			});
			if (setRecord == nullptr)
			{
				return true;
			}

			const std::uint64_t packedLayout = setRecord->detail.load(std::memory_order_relaxed);
			const ResourceRecord * layout	 = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= ResourceType::eDescriptorSetLayout,
				.index		= static_cast<std::uint32_t>(packedLayout & 0xFFFFFFFFu),
				.generation = static_cast<std::uint32_t>(packedLayout >> 32u),
			});
			if (layout == nullptr || binding >= kPackedBindings)
			{
				return true;
			}

			const std::uint64_t declared = (layout->detail.load(std::memory_order_relaxed) >> (binding * 4u)) & kBindingTypeMask;
			if (declared == 0)
			{
				return self->validator->Fail(error, "a descriptor write names a binding its set's layout never declared");
			}

			if (declared - 1u != static_cast<std::uint64_t>(type))
			{
				return self->validator->Fail(error, "a descriptor write names a binding its set's layout declared as a different descriptor type");
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

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteSampler &) noexcept
		{
			return DescriptorType::eSampler;
		}

		[[nodiscard]] DescriptorType WriteType(const DescriptorWriteAccelerationStructure &) noexcept
		{
			return DescriptorType::eAccelerationStructure;
		}

		template <class Write, auto Member>
		struct ValidatedWrites;

		template <class Write, class Block, bool (*Block::*Member)(void *, std::span<const Write>, Error *) noexcept>
		struct ValidatedWrites<Write, Member>
		{
			static bool Call(void * impl, std::span<const Write> writes, Error * error) noexcept
			{
				auto * self = static_cast<WrappedDevice *>(impl);

				if (!ArgumentIsUsable(*self->validator, writes))
				{
					return self->validator->Fail(error, "a descriptor write names a resource this device has already taken back");
				}

				if (self->validator->ChecksState())
				{
					for (const Write & write : writes)
					{
						if (!WriteMatchesLayout(self, write.set, write.binding, WriteType(write), error))
						{
							return false;
						}
					}
				}

				return (InnerBlock<Block>(self)->*Member)(self->inner, writes, error);
			}
		};

		const CoreDeviceApi & ValidatingCoreDeviceApi() noexcept
		{
			static const CoreDeviceApi block{
				.getGraphicsApiId			= &Forward<&CoreDeviceApi::getGraphicsApiId>::Call,
				.getGraphicsApiName			= &Forward<&CoreDeviceApi::getGraphicsApiName>::Call,
				.createBuffer				= &Recording<ResourceType::eBuffer, &CoreDeviceApi::createBuffer>::Call,
				.createTexture				= &Recording<ResourceType::eTexture, &CoreDeviceApi::createTexture>::Call,
				.createTextureView			= &ValidatedCreateTextureView,
				.createSampler				= &Recording<ResourceType::eSampler, &CoreDeviceApi::createSampler>::Call,
				.createDescriptorSetLayout	= &ValidatedCreateDescriptorSetLayout,
				.createPipelineLayout		= &Recording<ResourceType::ePipelineLayout, &CoreDeviceApi::createPipelineLayout>::Call,
				.createGraphicsPipeline		= &ValidatedCreateGraphicsPipeline,
				.createComputePipeline		= &Recording<ResourceType::eComputePipeline, &CoreDeviceApi::createComputePipeline>::Call,
				.createTimeline				= &Recording<ResourceType::eTimeline, &CoreDeviceApi::createTimeline>::Call,
				.createBinarySemaphore		= &Recording<ResourceType::eBinarySemaphore, &CoreDeviceApi::createBinarySemaphore>::Call,
				.createDescriptorArena		= &ValidatedCreateDescriptorArena,
				.createCommandPool			= &ValidatedCreateCommandPool,
				.getQueue					= &ValidatedGetQueue,
				.map						= &Checked<&CoreDeviceApi::map>::Call,
				.unmap						= &Checked<&CoreDeviceApi::unmap>::Call,
				.flushMappedRange			= &Checked<&CoreDeviceApi::flushMappedRange>::Call,
				.invalidateMappedRange		= &Checked<&CoreDeviceApi::invalidateMappedRange>::Call,
				.updateDescriptorsBuffer	= &ValidatedWrites<DescriptorWriteBuffer, &CoreDeviceApi::updateDescriptorsBuffer>::Call,
				.updateDescriptorsTexture	= &ValidatedWrites<DescriptorWriteTexture, &CoreDeviceApi::updateDescriptorsTexture>::Call,
				.updateDescriptorsSampler	= &ValidatedWrites<DescriptorWriteSampler, &CoreDeviceApi::updateDescriptorsSampler>::Call,
				.getCaps					= &Forward<&CoreDeviceApi::getCaps>::Call,
				.getFormatSupport			= &Forward<&CoreDeviceApi::getFormatSupport>::Call,
				.getAdapterInfo				= &Forward<&CoreDeviceApi::getAdapterInfo>::Call,
				.getValidationMessageCounts = &Forward<&CoreDeviceApi::getValidationMessageCounts>::Call,
				.destroy					= &ValidatedDestroy,
				.collectGarbage				= &Forward<&CoreDeviceApi::collectGarbage>::Call,
				.collectGarbageTimeline		= &Checked<&CoreDeviceApi::collectGarbageTimeline>::Call,
				.destroyDevice				= &ValidatedDestroyDevice,
			};

			return block;
		}

		const PresentApi & ValidatingPresentApi() noexcept
		{
			static const PresentApi block{
				.createSwapchain = &ValidatedCreateSwapchain,
			};

			return block;
		}

		const PlacedMemoryApi & ValidatingPlacedMemoryApi() noexcept
		{
			static const PlacedMemoryApi block{
				.createHeap			  = &Recording<ResourceType::eHeap, &PlacedMemoryApi::createHeap>::Call,
				.createPlacedBuffer	  = &Recording<ResourceType::eBuffer, &PlacedMemoryApi::createPlacedBuffer>::Call,
				.createPlacedTexture  = &Recording<ResourceType::eTexture, &PlacedMemoryApi::createPlacedTexture>::Call,
				.getTextureMemoryInfo = &Forward<&PlacedMemoryApi::getTextureMemoryInfo>::Call,
				.getBufferMemoryInfo  = &Forward<&PlacedMemoryApi::getBufferMemoryInfo>::Call,
			};

			return block;
		}

		const RayTracingApi & ValidatingRayTracingApi() noexcept
		{
			static const RayTracingApi block{
				.createRayTracingPipeline	 = &Recording<ResourceType::eRayTracingPipeline, &RayTracingApi::createRayTracingPipeline>::Call,
				.createAccelerationStructure = &Recording<ResourceType::eAccelerationStructure, &RayTracingApi::createAccelerationStructure>::Call,
				.updateDescriptorsAccelerationStructure =
					&ValidatedWrites<DescriptorWriteAccelerationStructure, &RayTracingApi::updateDescriptorsAccelerationStructure>::Call,
			};

			return block;
		}

		const QueryApi & ValidatingQueryApi() noexcept
		{
			static const QueryApi block{
				.createQueryPool	= &ValidatedCreateQueryPool,
				.calibrateTimestamp = &Forward<&QueryApi::calibrateTimestamp>::Call,
			};

			return block;
		}

		const PipelineCacheApi & ValidatingPipelineCacheApi() noexcept
		{
			static const PipelineCacheApi block{
				.createPipelineCache  = &Recording<ResourceType::ePipelineCache, &PipelineCacheApi::createPipelineCache>::Call,
				.getPipelineCacheData = &Checked<&PipelineCacheApi::getPipelineCacheData>::Call,
			};

			return block;
		}

		const ResourceIntrospectionApi & ValidatingResourceIntrospectionApi() noexcept
		{
			static const ResourceIntrospectionApi block{
				.getTextureInfo = &Checked<&ResourceIntrospectionApi::getTextureInfo>::Call,
				.getBufferInfo	= &Checked<&ResourceIntrospectionApi::getBufferInfo>::Call,
			};

			return block;
		}

		const ResidencyApi & ValidatingResidencyApi() noexcept
		{
			static const ResidencyApi block{
				.queryMemoryBudget	  = &Forward<&ResidencyApi::queryMemoryBudget>::Call,
				.setResidencyPriority = &Checked<&ResidencyApi::setResidencyPriority>::Call,
			};

			return block;
		}

		const AdoptionApi & ValidatingAdoptionApi() noexcept
		{
			static const AdoptionApi block{
				.adoptBuffer			  = &RecordingAdopted<ResourceType::eBuffer, &AdoptionApi::adoptBuffer>::Call,
				.adoptTexture			  = &RecordingAdopted<ResourceType::eTexture, &AdoptionApi::adoptTexture>::Call,
				.getNativeBuffer		  = &Checked<&AdoptionApi::getNativeBuffer>::Call,
				.getNativeTexture		  = &Checked<&AdoptionApi::getNativeTexture>::Call,
				.adoptTextureView		  = &RecordingAdopted<ResourceType::eTextureView, &AdoptionApi::adoptTextureView>::Call,
				.adoptSampler			  = &RecordingAdopted<ResourceType::eSampler, &AdoptionApi::adoptSampler>::Call,
				.getNativeTextureView	  = &Checked<&AdoptionApi::getNativeTextureView>::Call,
				.getNativeSampler		  = &Checked<&AdoptionApi::getNativeSampler>::Call,
				.adoptTimeline			  = &Recording<ResourceType::eTimeline, &AdoptionApi::adoptTimeline>::Call,
				.adoptBinarySemaphore	  = &Recording<ResourceType::eBinarySemaphore, &AdoptionApi::adoptBinarySemaphore>::Call,
				.getNativeTimeline		  = &Checked<&AdoptionApi::getNativeTimeline>::Call,
				.getNativeBinarySemaphore = &Checked<&AdoptionApi::getNativeBinarySemaphore>::Call,
			};

			return block;
		}

		const ExternalSharingApi & ValidatingExternalSharingApi() noexcept
		{
			static const ExternalSharingApi block{
				.exportBuffer		   = &Checked<&ExternalSharingApi::exportBuffer>::Call,
				.exportHeap			   = &Checked<&ExternalSharingApi::exportHeap>::Call,
				.exportTexture		   = &Checked<&ExternalSharingApi::exportTexture>::Call,
				.exportTimeline		   = &Checked<&ExternalSharingApi::exportTimeline>::Call,
				.exportBinarySemaphore = &Checked<&ExternalSharingApi::exportBinarySemaphore>::Call,
				.importBuffer		   = &Recording<ResourceType::eBuffer, &ExternalSharingApi::importBuffer>::Call,
				.importHeap			   = &Recording<ResourceType::eHeap, &ExternalSharingApi::importHeap>::Call,
				.importTexture		   = &Recording<ResourceType::eTexture, &ExternalSharingApi::importTexture>::Call,
				.importTimeline		   = &Recording<ResourceType::eTimeline, &ExternalSharingApi::importTimeline>::Call,
				.importBinarySemaphore = &Recording<ResourceType::eBinarySemaphore, &ExternalSharingApi::importBinarySemaphore>::Call,
				.closeExportedHandle   = &Forward<&ExternalSharingApi::closeExportedHandle>::Call,
			};

			return block;
		}

		const QueueApi & ValidatingQueueApi() noexcept
		{
			static const QueueApi block{
				.getType		   = &Forward<&QueueApi::getType>::Call,
				.submit			   = &ValidatedSubmit,
				.waitIdle		   = &Forward<&QueueApi::waitIdle>::Call,
				.getCompletedValue = &Checked<&QueueApi::getCompletedValue>::Call,
				.wait			   = &Checked<&QueueApi::wait>::Call,
				.signal			   = &Checked<&QueueApi::signal>::Call,
				.beginDebugLabel   = &Forward<&QueueApi::beginDebugLabel>::Call,
				.endDebugLabel	   = &Forward<&QueueApi::endDebugLabel>::Call,
			};

			return block;
		}

		const SparseApi & ValidatingSparseApi() noexcept
		{
			static const SparseApi block{
				.bindSparse = &ValidatedBindSparse,
			};

			return block;
		}

		const CommandPoolApi & ValidatingCommandPoolApi() noexcept
		{
			static const CommandPoolApi block{
				.allocate = &ValidatedAllocateCommandList,
				.reset	  = &ValidatedCommandPoolReset,
			};

			return block;
		}

		const DescriptorArenaApi & ValidatingDescriptorArenaApi() noexcept
		{
			static const DescriptorArenaApi block{
				.allocate = &ValidatedAllocateDescriptorSet,
				.reset	  = &ValidatedArenaReset,
			};

			return block;
		}

		template <bool ChecksThread>
		const RenderCommandApi & ValidatingRenderCommandApi() noexcept
		{
			static const RenderCommandApi block{
				.begin				 = &ValidatedBegin,
				.end				 = &ValidatedEnd,
				.barriers			 = &ValidatedBarriers,
				.beginRendering		 = &ValidatedBeginRendering,
				.endRendering		 = &ValidatedEndRendering,
				.setGraphicsPipeline = &ValidatedSetGraphicsPipeline,
				.setComputePipeline	 = &ValidatedSetComputePipeline,
				.bindDescriptorSet	 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::bindDescriptorSet>::Call,
				.pushConstants		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::pushConstants>::Call,
				.setViewport		 = &RecordedEntry<ChecksThread, &RenderCommandApi::setViewport>::Call,
				.setScissor			 = &RecordedEntry<ChecksThread, &RenderCommandApi::setScissor>::Call,
				.setBlendConstants	 = &RecordedEntry<ChecksThread, &RenderCommandApi::setBlendConstants>::Call,
				.setStencilReference = &RecordedEntry<ChecksThread, &RenderCommandApi::setStencilReference>::Call,
				.setDepthBias		 = &RecordedEntry<ChecksThread, &RenderCommandApi::setDepthBias>::Call,
				.setVertexBuffer	 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::setVertexBuffer>::Call,
				.setIndexBuffer		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::setIndexBuffer>::Call,
				.draw				 = &ValidatedDraw,
				.drawIndexed		 = &ValidatedDrawIndexed,
				.dispatch			 = &ValidatedDispatch,
				.copyBuffer			 = &OutsideRendering<ChecksThread, &RenderCommandApi::copyBuffer>::Call,
				.copyBufferToTexture = &OutsideRendering<ChecksThread, &RenderCommandApi::copyBufferToTexture>::Call,
				.copyTextureToBuffer = &OutsideRendering<ChecksThread, &RenderCommandApi::copyTextureToBuffer>::Call,
				.copyTexture		 = &OutsideRendering<ChecksThread, &RenderCommandApi::copyTexture>::Call,
				.clearBuffer		 = &ValidatedClearBuffer,
				.clearTexture		 = &ValidatedClearTexture,
				.resolveTexture		 = &OutsideRendering<ChecksThread, &RenderCommandApi::resolveTexture>::Call,
				.blit				 = &OutsideRendering<ChecksThread, &RenderCommandApi::blit>::Call,
				.generateMips		 = &ValidatedGenerateMips,
				.beginDebugLabel	 = &RecordedEntry<ChecksThread, &RenderCommandApi::beginDebugLabel>::Call,
				.endDebugLabel		 = &RecordedEntry<ChecksThread, &RenderCommandApi::endDebugLabel>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const AliasingCommandApi & ValidatingAliasingCommandApi() noexcept
		{
			static const AliasingCommandApi block{
				.aliasBarriers = &ValidatedAliasBarriers,
			};

			return block;
		}

		template <bool ChecksThread>
		const RayTracingCommandApi & ValidatingRayTracingCommandApi() noexcept
		{
			static const RayTracingCommandApi block{
				.setRayTracingPipeline		  = &ValidatedSetRayTracingPipeline,
				.buildAccelerationStructures  = &ValidatedBuildAccelerationStructures,
				.copyAccelerationStructure	  = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::copyAccelerationStructure>::Call,
				.compactAccelerationStructure = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::compactAccelerationStructure>::Call,
				.traceRays					  = &OutsideRenderingWithRayTracing<ChecksThread, &RayTracingCommandApi::traceRays>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const QueryCommandApi & ValidatingQueryCommandApi() noexcept
		{
			static const QueryCommandApi block{
				.resetQueryPool	  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::resetQueryPool>::Call,
				.writeTimestamp	  = &ValidatedWriteTimestamp,
				.beginQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::beginQuery>::Call,
				.endQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::endQuery>::Call,
				.resolveQueryData = &OutsideRendering<ChecksThread, &QueryCommandApi::resolveQueryData>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectApi & ValidatingIndirectApi() noexcept
		{
			static const IndirectApi block{
				.drawIndirect		 = &InsideRenderingWithGraphics<ChecksThread, &IndirectApi::drawIndirect>::Call,
				.drawIndexedIndirect = &InsideRenderingWithGraphics<ChecksThread, &IndirectApi::drawIndexedIndirect>::Call,
				.dispatchIndirect	 = &OutsideRenderingWithCompute<ChecksThread, &IndirectApi::dispatchIndirect>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectCountApi & ValidatingIndirectCountApi() noexcept
		{
			static const IndirectCountApi block{
				.drawIndirectCount		  = &InsideRenderingWithGraphics<ChecksThread, &IndirectCountApi::drawIndirectCount>::Call,
				.drawIndexedIndirectCount = &InsideRenderingWithGraphics<ChecksThread, &IndirectCountApi::drawIndexedIndirectCount>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const NativeEscapeApi & ValidatingNativeEscapeApi() noexcept
		{
			static const NativeEscapeApi block{
				.beginNativeMutation = &RecordedCheckedEntry<ChecksThread, &NativeEscapeApi::beginNativeMutation>::Call,
				.endNativeMutation	 = &ValidatedEndNativeMutation,
			};

			return block;
		}

		void RecordBackBuffer(WrappedSwapchain * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const RegisteredHandle registered{
				.type		= type,
				.index		= index,
				.generation = generation,
			};

			if (self->validator->Handles().Lookup(registered) == nullptr && !self->validator->Handles().Record(registered))
			{
				return;
			}

			const Format format = self->blocks->getFormat(self->inner);
			if (format != Format::eUndefined)
			{
				self->validator->Handles().Lookup(registered)->format.store(static_cast<std::uint16_t>(format), std::memory_order_relaxed);
			}
		}

		TextureHandle ValidatedGetBackBuffer(void * impl, const std::uint32_t imageIndex) noexcept
		{
			auto * self					= static_cast<WrappedSwapchain *>(impl);
			const TextureHandle texture = self->blocks->getBackBuffer(self->inner, imageIndex);
			if (texture.IsValid())
			{
				RecordBackBuffer(self, ResourceType::eTexture, texture.index, texture.generation);
			}

			return texture;
		}

		TextureViewHandle ValidatedGetBackBufferView(void * impl, const std::uint32_t imageIndex) noexcept
		{
			auto * self					 = static_cast<WrappedSwapchain *>(impl);
			const TextureViewHandle view = self->blocks->getBackBufferView(self->inner, imageIndex);
			if (view.IsValid())
			{
				RecordBackBuffer(self, ResourceType::eTextureView, view.index, view.generation);
			}

			return view;
		}

		const SwapchainApi & ValidatingSwapchainApi() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage			 = &Forward<&SwapchainApi::acquireNextImage>::Call,
				.present					 = &ValidatedPresent,
				.getBackBuffer				 = &ValidatedGetBackBuffer,
				.getBackBufferView			 = &ValidatedGetBackBufferView,
				.getPerImagePresentSemaphore = &Vending<ResourceType::eBinarySemaphore, &SwapchainApi::getPerImagePresentSemaphore>::Call,
				.getFormat					 = &Forward<&SwapchainApi::getFormat>::Call,
				.getPresentMode				 = &Forward<&SwapchainApi::getPresentMode>::Call,
				.getImageCount				 = &Forward<&SwapchainApi::getImageCount>::Call,
				.getWidth					 = &Forward<&SwapchainApi::getWidth>::Call,
				.getHeight					 = &Forward<&SwapchainApi::getHeight>::Call,
				.resize						 = &Forward<&SwapchainApi::resize>::Call,
				.setPresentMode				 = &Forward<&SwapchainApi::setPresentMode>::Call,
				.supportsReadback			 = &Forward<&SwapchainApi::supportsReadback>::Call,
			};

			return block;
		}

		template <class Block, const Block & (*Table)() noexcept>
		struct WrappedBlock final
		{
			[[nodiscard]] static const void * Match(WrappedObject * self, const InterfaceId id, const std::uint32_t minVersion) noexcept
			{
				if (id != InterfaceTraits<Block>::kId || minVersion > InterfaceTraits<Block>::kVersion || InnerBlock<Block>(self) == nullptr)
				{
					return nullptr;
				}

				return &Table();
			}
		};

		void * InnerObject(void * impl) noexcept
		{
			return static_cast<WrappedObject *>(impl)->inner;
		}

		const NativeObjectApi & ValidatingNativeObjectApi() noexcept
		{
			static const NativeObjectApi block{ .inner = &InnerObject };
			return block;
		}

		struct NativeObjectBlock final
		{
			[[nodiscard]] static const void * Match(WrappedObject *, const InterfaceId id, const std::uint32_t minVersion) noexcept
			{
				if (id != InterfaceTraits<NativeObjectApi>::kId || minVersion > InterfaceTraits<NativeObjectApi>::kVersion)
				{
					return nullptr;
				}

				return &ValidatingNativeObjectApi();
			}
		};

		template <class... Blocks>
		[[nodiscard]] const void * QueryWrapped(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			auto * self		   = static_cast<WrappedObject *>(object);
			const void * found = nullptr;
			((found = found != nullptr ? found : Blocks::Match(self, id, minVersion)), ...);
			return found;
		}

		template <class... Blocks>
		[[nodiscard]] const BackendObject * WrappingObject() noexcept
		{
			static constexpr BackendObject object{ .queryInterface = &QueryWrapped<Blocks...> };
			return &object;
		}

		[[nodiscard]] const BackendObject * DeviceObject() noexcept
		{
			return WrappingObject<NativeObjectBlock,
				WrappedBlock<CoreDeviceApi, &ValidatingCoreDeviceApi>,
				WrappedBlock<PresentApi, &ValidatingPresentApi>,
				WrappedBlock<PlacedMemoryApi, &ValidatingPlacedMemoryApi>,
				WrappedBlock<RayTracingApi, &ValidatingRayTracingApi>,
				WrappedBlock<QueryApi, &ValidatingQueryApi>,
				WrappedBlock<PipelineCacheApi, &ValidatingPipelineCacheApi>,
				WrappedBlock<ResidencyApi, &ValidatingResidencyApi>,
				WrappedBlock<ResourceIntrospectionApi, &ValidatingResourceIntrospectionApi>,
				WrappedBlock<AdoptionApi, &ValidatingAdoptionApi>,
				WrappedBlock<ExternalSharingApi, &ValidatingExternalSharingApi>>();
		}

		[[nodiscard]] const BackendObject * QueueObject() noexcept
		{
			return WrappingObject<NativeObjectBlock, WrappedBlock<QueueApi, &ValidatingQueueApi>, WrappedBlock<SparseApi, &ValidatingSparseApi>>();
		}

		[[nodiscard]] const BackendObject * CommandPoolObject() noexcept
		{
			return WrappingObject<NativeObjectBlock, WrappedBlock<CommandPoolApi, &ValidatingCommandPoolApi>>();
		}

		template <bool ChecksThread>
		[[nodiscard]] const BackendObject * CommandListObject() noexcept
		{
			return WrappingObject<NativeObjectBlock,
				WrappedBlock<RenderCommandApi, &ValidatingRenderCommandApi<ChecksThread>>,
				WrappedBlock<AliasingCommandApi, &ValidatingAliasingCommandApi<ChecksThread>>,
				WrappedBlock<RayTracingCommandApi, &ValidatingRayTracingCommandApi<ChecksThread>>,
				WrappedBlock<QueryCommandApi, &ValidatingQueryCommandApi<ChecksThread>>,
				WrappedBlock<IndirectApi, &ValidatingIndirectApi<ChecksThread>>,
				WrappedBlock<IndirectCountApi, &ValidatingIndirectCountApi<ChecksThread>>,
				WrappedBlock<NativeEscapeApi, &ValidatingNativeEscapeApi<ChecksThread>>>();
		}

		[[nodiscard]] const BackendObject * DescriptorArenaObject() noexcept
		{
			return WrappingObject<NativeObjectBlock, WrappedBlock<DescriptorArenaApi, &ValidatingDescriptorArenaApi>>();
		}

		[[nodiscard]] const BackendObject * SwapchainObject() noexcept
		{
			return WrappingObject<NativeObjectBlock, WrappedBlock<SwapchainApi, &ValidatingSwapchainApi>>();
		}

		template <class Wrapper>
		[[nodiscard]] Wrapper * Adopt(WrappedDevice * device, HostUniquePtr<Wrapper> child) noexcept
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

		void * ValidatedGetQueue(void * impl, const QueueType type, const std::uint32_t index, Error * error) noexcept
		{
			auto * self		  = static_cast<WrappedDevice *>(impl);
			void * innerQueue = self->blocks.core->getQueue(self->inner, type, index, error);
			if (innerQueue == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedQueue> wrapper = HostNew<WrappedQueue>();
			if (wrapper == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused the storage a validated queue needs");
			}

			wrapper->object		   = QueueObject();
			wrapper->inner		   = innerQueue;
			wrapper->device		   = self;
			wrapper->validator	   = self->validator;
			wrapper->blocks.core   = detail::QueryBlock<QueueApi>(innerQueue);
			wrapper->blocks.sparse = detail::QueryBlock<SparseApi>(innerQueue);
			wrapper->type		   = type;

			return Adopt(self, std::move(wrapper));
		}

		void * ValidatedCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept
		{
			auto * self		 = static_cast<WrappedDevice *>(impl);
			void * innerPool = self->blocks.core->createCommandPool(self->inner, desc, error);
			if (innerPool == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedCommandPool> wrapper = HostNew<WrappedCommandPool>();
			if (wrapper == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused the storage a validated command pool needs");
			}

			wrapper->object	   = CommandPoolObject();
			wrapper->inner	   = innerPool;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::QueryBlock<CommandPoolApi>(innerPool);
			wrapper->queueType = desc.queueType;

			return Adopt(self, std::move(wrapper));
		}

		void * ValidatedCreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept
		{
			auto * self		  = static_cast<WrappedDevice *>(impl);
			void * innerArena = self->blocks.core->createDescriptorArena(self->inner, desc, error);
			if (innerArena == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedDescriptorArena> wrapper = HostNew<WrappedDescriptorArena>();
			if (wrapper == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused the storage a validated descriptor arena needs");
			}

			wrapper->object	   = DescriptorArenaObject();
			wrapper->inner	   = innerArena;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::QueryBlock<DescriptorArenaApi>(innerArena);
			wrapper->id		   = self->nextArenaId.fetch_add(1, std::memory_order_relaxed);

			return Adopt(self, std::move(wrapper));
		}

		void * ValidatedCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept
		{
			auto * self			  = static_cast<WrappedDevice *>(impl);
			void * innerSwapchain = self->blocks.present->createSwapchain(self->inner, desc, error);
			if (innerSwapchain == nullptr)
			{
				return nullptr;
			}

			HostUniquePtr<WrappedSwapchain> wrapper = HostNew<WrappedSwapchain>();
			if (wrapper == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused the storage a validated swapchain needs");
			}

			wrapper->object	   = SwapchainObject();
			wrapper->inner	   = innerSwapchain;
			wrapper->device	   = self;
			wrapper->validator = self->validator;
			wrapper->blocks	   = detail::QueryBlock<SwapchainApi>(innerSwapchain);

			return Adopt(self, std::move(wrapper));
		}

		bool ValidatedCommandPoolReset(void * impl, const RetirePoint safeAfter, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandPool *>(impl);
			if (!AllUsable(*self->validator, safeAfter.timeline))
			{
				return self->validator->Fail(error, "command pool reset names a timeline this device did not create or has destroyed");
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
				list->pendingOwnership.clear();
				list->pendingArrivals.clear();
			}

			return true;
		}

		void * ValidatedAllocateCommandList(void * impl, const CString debugName, Error * error) noexcept
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

			HostUniquePtr<WrappedCommandList> wrapper = HostNew<WrappedCommandList>();
			if (wrapper == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused the storage a validated command list needs");
			}

			wrapper->object	   = self->validator->ChecksState() ? CommandListObject<true>() : CommandListObject<false>();
			wrapper->inner	   = innerList;
			wrapper->device	   = self->device;
			wrapper->validator = self->validator;
			wrapper->queueType = self->queueType;

			wrapper->blocks.render		  = detail::QueryBlock<RenderCommandApi>(innerList);
			wrapper->blocks.aliasing	  = detail::QueryBlock<AliasingCommandApi>(innerList);
			wrapper->blocks.rayTracing	  = detail::QueryBlock<RayTracingCommandApi>(innerList);
			wrapper->blocks.query		  = detail::QueryBlock<QueryCommandApi>(innerList);
			wrapper->blocks.indirect	  = detail::QueryBlock<IndirectApi>(innerList);
			wrapper->blocks.indirectCount = detail::QueryBlock<IndirectCountApi>(innerList);
			wrapper->blocks.nativeEscape  = detail::QueryBlock<NativeEscapeApi>(innerList);

			void * adopted = Adopt(self->device, std::move(wrapper));

			if (!detail::TryInsertOrAssign(self->lists, innerList, static_cast<WrappedCommandList *>(adopted)))
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused to record a validated command list");
			}

			return adopted;
		}

		void ValidatedDestroyDevice(void * impl) noexcept
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

		[[nodiscard]] void * Unwrap(void * impl) noexcept
		{
			return impl != nullptr ? static_cast<WrappedObject *>(impl)->inner : nullptr;
		}

		bool ValidatedSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedQueue *>(impl);

			if (!ArgumentIsUsable(*self->validator, desc))
			{
				return self->validator->Fail(error, "a submit waits on or signals a timeline this device has already taken back");
			}

			detail::HostVector<CommandList> unwrapped;
			detail::HostVector<const CommandList *> pointers;
			if (!detail::TryReserve(unwrapped, desc.commandLists.size()) || !detail::TryReserve(pointers, desc.commandLists.size()))
			{
				return self->validator->Fail(error, "the host allocator refused the storage a validated submit needs");
			}

			for (const CommandList * list : desc.commandLists)
			{
				if (list == nullptr)
				{
					return self->validator->Fail(error, "submit was given a null command list");
				}

				auto * wrapper = static_cast<WrappedCommandList *>(detail::FacadeBuilder::ImplOf(*list));

				if (self->validator->ChecksState() && wrapper->queueType != self->type)
				{
					return self->validator->Fail(error, "submit of a command list recorded for a different queue type than the queue it was given to");
				}

				if (self->validator->ChecksState() && wrapper->recording)
				{
					return self->validator->Fail(error, "submit of a command list that is still recording, so End was never called on it");
				}

				unwrapped.push_back(detail::FacadeBuilder::MakeCommandList(wrapper->inner, &wrapper->blocks));
			}

			for (const CommandList & list : unwrapped)
			{
				pointers.push_back(&list);
			}

			if (self->validator->ChecksState() && !SubmittedOwnershipIsLegal(*self->validator, desc.commandLists, error))
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
				ApplyPendingOwnership(*self->validator, *static_cast<const WrappedCommandList *>(detail::FacadeBuilder::ImplOf(*list)));
			}

			return true;
		}

		bool ValidatedBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedQueue *>(impl);

			if (!ArgumentIsUsable(*self->validator, desc))
			{
				return self->validator->Fail(error, "a sparse bind names a resource this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				for (const SparseBufferBind & bind : desc.buffers)
				{
					if (!bind.buffer.IsValid())
					{
						return self->validator->Fail(error, "a sparse buffer bind names no buffer");
					}

					if (bind.page.heap.IsValid() && bind.page.size == 0)
					{
						return self->validator->Fail(error, "a sparse buffer bind supplies a heap and then binds none of it");
					}
				}

				for (const SparseTextureBind & bind : desc.textures)
				{
					if (!bind.texture.IsValid())
					{
						return self->validator->Fail(error, "a sparse texture bind names no texture");
					}

					if (bind.page.heap.IsValid() && bind.page.size == 0)
					{
						return self->validator->Fail(error, "a sparse texture bind supplies a heap and then binds none of it");
					}
				}
			}

			return self->blocks.sparse->bindSparse(self->inner, desc, error);
		}

		bool ValidatedArenaReset(void * impl, const RetirePoint safeAfter, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDescriptorArena *>(impl);

			if (!AllUsable(*self->validator, safeAfter))
			{
				return self->validator->Fail(error, "an arena reset defers to a timeline this device has already taken back");
			}

			static_cast<void>(self->validator->Handles().RetireFrom(ResourceType::eDescriptorSet, self->id));
			return self->blocks->reset(self->inner, safeAfter, error);
		}

		[[nodiscard]] std::uint64_t DeclaredUsage(
			DeviceValidator & validator, const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const ResourceRecord * record = validator.Handles().Lookup(RegisteredHandle{ .type = type, .index = index, .generation = generation });
			return record != nullptr ? record->detail.load(std::memory_order_relaxed) : 0;
		}

		template <class Usage>
		[[nodiscard]] bool DeclaredFor(const std::uint64_t usage, const Usage bit) noexcept
		{
			return (usage & kUsageDeclared) == 0 || (usage & static_cast<std::uint64_t>(bit)) != 0;
		}

		bool ValidatedClearBuffer(
			void * impl, const BufferHandle buffer, const std::uint64_t offset, const std::uint64_t size, const std::uint32_t value, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, buffer))
			{
				return self->validator->Fail(error, "clearBuffer names a buffer this device has already taken back");
			}

			if (self->validator->ChecksState() && self->rendering)
			{
				return self->validator->Fail(error, "clearBuffer cannot be recorded inside a rendering scope, so record it between passes");
			}

			if (!DeclaredFor(DeclaredUsage(*self->validator, ResourceType::eBuffer, buffer.index, buffer.generation), BufferUsage::eStorage))
			{
				return self->validator->Fail(error, "clearBuffer needs BufferUsage::eStorage, which is what Direct3D 12 clears through");
			}

			return self->blocks.render->clearBuffer(self->inner, buffer, offset, size, value, error);
		}

		bool ValidatedClearTexture(
			void * impl, const TextureHandle texture, const ClearColor & color, const std::span<const TextureSubresourceRange> ranges, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, texture))
			{
				return self->validator->Fail(error, "clearTexture names a texture this device has already taken back");
			}

			if (self->validator->ChecksState() && self->rendering)
			{
				return self->validator->Fail(error, "clearTexture cannot be recorded inside a rendering scope, so record it between passes");
			}

			if (!DeclaredFor(DeclaredUsage(*self->validator, ResourceType::eTexture, texture.index, texture.generation), TextureUsage::eColorAttachment))
			{
				return self->validator->Fail(error, "clearTexture needs TextureUsage::eColorAttachment, which is what Direct3D 12 and Metal clear through");
			}

			return self->blocks.render->clearTexture(self->inner, texture, color, ranges, error);
		}

		bool ValidatedWriteTimestamp(void * impl, const QueryPoolHandle pool, const std::uint32_t query, const Flags<Stage> stage, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, pool))
			{
				return self->validator->Fail(error, "writeTimestamp names a query pool this device has already taken back");
			}

			if (!IsOneTimestampStage(stage))
			{
				return self->validator->Fail(error, "writeTimestamp takes a single stage and this mask names more than one");
			}

			if (!QueueCanNameStage(self->queueType, stage))
			{
				return self->validator->Fail(error, "writeTimestamp names a stage the queue this list was allocated for cannot reach");
			}

			return self->blocks.query->writeTimestamp(self->inner, pool, query, stage, error);
		}

		bool ValidatedBuildAccelerationStructures(void * impl, const std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, builds))
			{
				return self->validator->Fail(error, "an acceleration structure build names a resource this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				for (const AccelerationStructureBuildDesc & build : builds)
				{
					if (!build.dst.IsValid())
					{
						return self->validator->Fail(error, "an acceleration structure build has no destination");
					}

					if (!build.scratchBuffer.IsValid())
					{
						return self->validator->Fail(error, "an acceleration structure build has no scratch buffer");
					}

					if (build.mode == AccelerationStructureBuildMode::eUpdate && !build.src.IsValid())
					{
						return self->validator->Fail(error, "an acceleration structure update has no source to refit from");
					}

					if (build.geometries.empty() && build.instanceCount == 0)
					{
						return self->validator->Fail(error, "an acceleration structure build has neither geometry nor instances");
					}
				}
			}

			return self->blocks.rayTracing->buildAccelerationStructures(self->inner, builds, error);
		}

		PresentResult ValidatedPresent(
			void * impl, const std::uint32_t imageIndex, const BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedSwapchain *>(impl);

			if (!AllUsable(*self->validator, renderFinished))
			{
				return self->validator->FailValue<PresentResult>(error, "present waits on a semaphore this device has already taken back");
			}

			return self->blocks->present(self->inner, imageIndex, renderFinished, Unwrap(queueImpl), error);
		}

		bool ValidatedDestroy(void * impl, const ResourceType type, const RawHandle handle, const DestroyDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			const RegisteredHandle registered{
				.type		= type,
				.index		= handle.index,
				.generation = handle.generation,
			};

			if (!AllUsable(*self->validator, desc.safeAfter))
			{
				return self->validator->Fail(error, "a destroy defers to a timeline this device has already taken back");
			}

			if (!self->validator->Handles().Retire(registered))
			{
				return self->validator->Fail(
					error, "destroy of a handle this device never handed out, has already taken back, or that belongs to another device");
			}

			if (!self->blocks.core->destroy(self->inner, type, handle, desc, error))
			{
				static_cast<void>(self->validator->Handles().Restore(registered));
				return false;
			}

			return true;
		}

		[[nodiscard]] std::uint32_t PackState(const ResourceState & state) noexcept
		{
			return state.use.Bits();
		}

		[[nodiscard]] RegisteredHandle TrackedResource(const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			return RegisteredHandle{
				.type		= type,
				.index		= index,
				.generation = generation,
			};
		}

		[[nodiscard]] std::uint32_t SpanEnd(const std::uint32_t begin, const std::uint32_t count) noexcept
		{
			constexpr std::uint32_t unbounded = std::numeric_limits<std::uint32_t>::max();
			return count > unbounded - begin ? unbounded : begin + count;
		}

		[[nodiscard]] DeclaredExtents ExtentsOf(
			WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= type,
				.index		= index,
				.generation = generation,
			});
			return record != nullptr ? ExtentsFrom(type, record->detail.load(std::memory_order_relaxed)) : DeclaredExtents{};
		}

		[[nodiscard]] std::uint64_t DeclaredSizeOf(WrappedCommandList * self, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= ResourceType::eBuffer,
				.index		= index,
				.generation = generation,
			});
			return record != nullptr ? DeclaredSizeFrom(record->detail.load(std::memory_order_relaxed)) : 0;
		}

		[[nodiscard]] std::uint32_t BoundOr(const std::uint32_t declared, const std::uint32_t fallback) noexcept
		{
			return declared != 0 ? declared : fallback;
		}

		[[nodiscard]] TrackedSubrange WholeResourceSpan(const DeclaredExtents & extents = {}) noexcept
		{
			constexpr std::uint32_t unbounded = std::numeric_limits<std::uint32_t>::max();
			return TrackedSubrange{
				.aspects	= BoundOr(extents.aspects, unbounded),
				.mipBegin	= 0,
				.mipEnd		= BoundOr(extents.mips, unbounded),
				.layerBegin = 0,
				.layerEnd	= BoundOr(extents.layers, unbounded),
				.byteEnd	= extents.bytes != 0 ? extents.bytes : std::numeric_limits<std::uint64_t>::max(),
			};
		}

		[[nodiscard]] TrackedSubrange TextureSpan(const TextureSubresourceRange & range, const DeclaredExtents & extents) noexcept
		{
			constexpr std::uint32_t unbounded = std::numeric_limits<std::uint32_t>::max();
			return TrackedSubrange{
				.aspects	= static_cast<std::uint32_t>(range.aspects.Bits()) & BoundOr(extents.aspects, unbounded),
				.mipBegin	= range.baseMip,
				.mipEnd		= std::min(SpanEnd(range.baseMip, range.mipCount), BoundOr(extents.mips, unbounded)),
				.layerBegin = range.baseLayer,
				.layerEnd	= std::min(SpanEnd(range.baseLayer, range.layerCount), BoundOr(extents.layers, unbounded)),
			};
		}

		[[nodiscard]] std::uint64_t ByteSpanEnd(const std::uint64_t begin, const std::uint64_t count) noexcept
		{
			constexpr std::uint64_t unbounded = std::numeric_limits<std::uint64_t>::max();
			return count > unbounded - begin ? unbounded : begin + count;
		}

		[[nodiscard]] TrackedSubrange BufferSpan(const BufferBarrier & barrier, const std::uint64_t declared) noexcept
		{
			constexpr std::uint64_t unbounded = std::numeric_limits<std::uint64_t>::max();

			TrackedSubrange span = WholeResourceSpan();
			span.byteBegin		 = barrier.offset;
			span.byteEnd		 = std::min(ByteSpanEnd(barrier.offset, barrier.size), declared != 0 ? declared : unbounded);
			return span;
		}

		[[nodiscard]] bool CoversWholeResource(const TrackedSubrange & span, const DeclaredExtents & extents) noexcept
		{
			constexpr std::uint32_t unbounded = std::numeric_limits<std::uint32_t>::max();
			const std::uint64_t declaredBytes = extents.bytes != 0 ? extents.bytes : std::numeric_limits<std::uint64_t>::max();

			return (extents.aspects == 0 || (span.aspects & extents.aspects) == extents.aspects) && span.mipBegin == 0 &&
				   span.mipEnd >= BoundOr(extents.mips, unbounded) && span.layerBegin == 0 && span.layerEnd >= BoundOr(extents.layers, unbounded) &&
				   span.byteBegin == 0 && span.byteEnd >= declaredBytes;
		}

		[[nodiscard]] bool Overlaps(const TrackedSubrange & lhs, const TrackedSubrange & rhs) noexcept
		{
			return lhs.resource == rhs.resource && (lhs.aspects & rhs.aspects) != 0u && lhs.mipBegin < rhs.mipEnd && rhs.mipBegin < lhs.mipEnd &&
				   lhs.layerBegin < rhs.layerEnd && rhs.layerBegin < lhs.layerEnd && lhs.byteBegin < rhs.byteEnd && rhs.byteBegin < lhs.byteEnd;
		}

		[[nodiscard]] bool SubtractInto(detail::HostVector<TrackedSubrange> & into, const TrackedSubrange & from, const TrackedSubrange & cut) noexcept
		{
			if (const std::uint32_t untouched = from.aspects & ~cut.aspects; untouched != 0u)
			{
				TrackedSubrange piece = from;
				piece.aspects		  = untouched;
				if (!detail::TryPushBack(into, piece))
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
				if (!detail::TryPushBack(into, piece))
				{
					return false;
				}
			}

			if (shared.mipEnd > cut.mipEnd)
			{
				TrackedSubrange piece = shared;
				piece.mipBegin		  = cut.mipEnd;
				if (!detail::TryPushBack(into, piece))
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
				if (!detail::TryPushBack(into, piece))
				{
					return false;
				}
			}

			if (band.layerEnd > cut.layerEnd)
			{
				TrackedSubrange piece = band;
				piece.layerBegin	  = cut.layerEnd;
				if (!detail::TryPushBack(into, piece))
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
				if (!detail::TryPushBack(into, piece))
				{
					return false;
				}
			}

			if (strip.byteEnd > cut.byteEnd)
			{
				TrackedSubrange piece = strip;
				piece.byteBegin		  = cut.byteEnd;
				if (!detail::TryPushBack(into, piece))
				{
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool StateIsUsableAsAfter(
			WrappedCommandList * self, const ResourceState & after, const QueueOwnership & ownership, Error * error) noexcept
		{
			if (after.use.Contains(ResourceUse::eDiscard))
			{
				return self->validator->Fail(
					error, "a barrier names eDiscard as its after-state, which describes contents arriving at a barrier and not leaving one");
			}

			const bool releasing = ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eReleaseToExternal;

			if (after.use.Bits() == 0u && !releasing)
			{
				return self->validator->Fail(error, "a barrier names no after-state, so it does not say what the resource is being moved into");
			}

			return true;
		}

		void Forget(WrappedCommandList * self, const RegisteredHandle resource) noexcept
		{
			static_cast<void>(std::erase_if(self->recordedStates,
				[resource](const TrackedSubrange & entry)
				{
					return entry.resource == resource;
				}));
		}

		[[nodiscard]] bool MipChainIsReadyToGenerate(WrappedCommandList * self, const TextureHandle texture, Error * error) noexcept
		{
			const RegisteredHandle resource = TrackedResource(ResourceType::eTexture, texture.index, texture.generation);
			const std::uint32_t source		= PackState(ResourceState{ .use = ResourceUse::eCopySrc });
			const std::uint32_t target		= PackState(ResourceState{ .use = ResourceUse::eCopyDst });

			for (const TrackedSubrange & tracked : self->recordedStates)
			{
				if (tracked.resource != resource)
				{
					continue;
				}

				if (tracked.mipBegin == 0 && tracked.state != source)
				{
					return self->validator->Fail(error, "generateMips reads level zero as a copy source and this recording left it in another use");
				}

				if (tracked.mipEnd > 1 && tracked.state != target)
				{
					return self->validator->Fail(error, "generateMips writes every level below zero whole and this recording left one of them in another use");
				}
			}

			return true;
		}

		bool ValidatedGenerateMips(void * impl, const TextureHandle texture, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, texture))
			{
				return self->validator->Fail(error, "generateMips names a texture this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				if (self->rendering)
				{
					return self->validator->Fail(error, "a transfer or dispatch recorded inside a rendering scope, which has to be recorded between passes");
				}

				if (!MipChainIsReadyToGenerate(self, texture, error))
				{
					return false;
				}
			}

			if (!self->blocks.render->generateMips(self->inner, texture, error))
			{
				return false;
			}

			if (self->validator->ChecksState())
			{
				const RegisteredHandle resource = TrackedResource(ResourceType::eTexture, texture.index, texture.generation);
				const std::uint32_t exits		= PackState(ResourceState{ .use = ResourceUse::eCopySrc, .stages = Stage::eCopy });

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
					TrackedSubrange written = WholeResourceSpan(ExtentsOf(self, ResourceType::eTexture, texture.index, texture.generation));
					written.resource		= resource;
					written.state			= exits;
					static_cast<void>(detail::TryPushBack(self->recordedStates, written));
				}
			}

			return true;
		}

		[[nodiscard]] bool Retrack(WrappedCommandList * self, const TrackedSubrange & box, const std::uint32_t state) noexcept
		{
			const std::size_t existing = self->recordedStates.size();
			for (std::size_t i = 0; i < existing; ++i)
			{
				const TrackedSubrange covered = self->recordedStates[i];
				if (!Overlaps(covered, box))
				{
					continue;
				}

				self->recordedStates[i].aspects = 0u;
				if (!SubtractInto(self->recordedStates, covered, box))
				{
					return false;
				}
			}

			TrackedSubrange written = box;
			written.state			= state;
			if (!detail::TryPushBack(self->recordedStates, written))
			{
				return false;
			}

			static_cast<void>(std::erase_if(self->recordedStates,
				[](const TrackedSubrange & entry)
				{
					return entry.aspects == 0u;
				}));
			return true;
		}

		[[nodiscard]] PendingOwnership * PendingOwnershipOf(
			WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			const RegisteredHandle resource = TrackedResource(type, index, generation);
			for (PendingOwnership & pending : self->pendingOwnership)
			{
				if (pending.resource == resource)
				{
					return &pending;
				}
			}

			return nullptr;
		}

		void SetPendingOwnership(WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const QueueOwnership & ownership, const std::uint8_t owner, const bool owned) noexcept
		{
			if (PendingOwnership * pending = PendingOwnershipOf(self, type, index, generation))
			{
				pending->owner = owner;
				pending->owned = owned;
				return;
			}

			static_cast<void>(detail::TryPushBack(self->pendingOwnership,
				PendingOwnership{
					.resource		  = TrackedResource(type, index, generation),
					.firstOp		  = ownership.op,
					.firstCounterpart = ownership.counterpart,
					.recordedOn		  = self->queueType,
					.owner			  = owner,
					.owned			  = owned,
				}));
		}

		[[nodiscard]] bool OwnershipStepIsLegal(
			const PendingOwnership & pending, const bool owned, const std::uint8_t held, Error * error, DeviceValidator & validator) noexcept
		{
			if (!owned)
			{
				return true;
			}

			const bool releasing = pending.firstOp == OwnershipOp::eRelease || pending.firstOp == OwnershipOp::eReleaseToExternal;
			if (releasing && held != static_cast<std::uint8_t>(pending.recordedOn))
			{
				return validator.Fail(error, "a submitted barrier releases a resource from a queue that does not own it");
			}

			if (pending.firstOp == OwnershipOp::eAcquire && held != static_cast<std::uint8_t>(pending.firstCounterpart))
			{
				return validator.Fail(error, "a submitted barrier acquires a resource from a queue that does not hold it");
			}

			return true;
		}

		[[nodiscard]] bool SubmittedOwnershipIsLegal(DeviceValidator & validator, std::span<const CommandList * const> lists, Error * error) noexcept
		{
			detail::HostVector<PendingOwnership> staged;

			for (const CommandList * list : lists)
			{
				const auto * wrapper = static_cast<const WrappedCommandList *>(detail::FacadeBuilder::ImplOf(*list));
				for (const PendingOwnership & pending : wrapper->pendingOwnership)
				{
					const ResourceRecord * record = validator.Handles().Lookup(pending.resource);
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

					if (!OwnershipStepIsLegal(pending, owned, held, error, validator))
					{
						return false;
					}

					if (earlier != nullptr)
					{
						earlier->owner = pending.owner;
						earlier->owned = pending.owned;
						continue;
					}

					if (!detail::TryPushBack(staged, pending))
					{
						return validator.Fail(error, "the host allocator refused the storage a validated submit needs to check queue ownership");
					}
				}
			}

			return true;
		}

		void ApplyPendingOwnership(DeviceValidator & validator, const WrappedCommandList & list) noexcept
		{
			for (const PendingOwnership & pending : list.pendingOwnership)
			{
				if (ResourceRecord * record = validator.Handles().Lookup(pending.resource))
				{
					record->owner.store(pending.owner, std::memory_order_relaxed);
					record->owned.store(pending.owned, std::memory_order_relaxed);
				}
			}

			for (const PendingArrival & pending : list.pendingArrivals)
			{
				if (ResourceRecord * record = validator.Handles().Lookup(pending.resource))
				{
					record->use.store(pending.use, std::memory_order_relaxed);
					record->useKnown.store(pending.known, std::memory_order_relaxed);
				}
			}
		}

		[[nodiscard]] const PendingArrival * PendingArrivalOf(const WrappedCommandList * self, const RegisteredHandle resource) noexcept
		{
			for (const PendingArrival & pending : self->pendingArrivals)
			{
				if (pending.resource == resource)
				{
					return &pending;
				}
			}

			return nullptr;
		}

		void SetPendingArrival(WrappedCommandList * self, const RegisteredHandle resource, const std::uint32_t use, const bool known) noexcept
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

			static_cast<void>(detail::TryPushBack(self->pendingArrivals,
				PendingArrival{
					.resource = resource,
					.use	  = use,
					.known	  = known,
				}));
		}

		[[nodiscard]] bool CheckAndTransferOwnership(WrappedCommandList * self, const ResourceType type, const std::uint32_t index,
			const std::uint32_t generation, const QueueOwnership & ownership, Error * error) noexcept
		{
			if (ownership.op == OwnershipOp::eNone)
			{
				return true;
			}

			const bool namesQueue = ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eAcquire;
			if (namesQueue && ownership.counterpart == self->queueType)
			{
				return self->validator->Fail(error, "a barrier transfers queue ownership between one queue and itself, which is not a transfer");
			}

			const ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= type,
				.index		= index,
				.generation = generation,
			});
			if (record == nullptr)
			{
				return true;
			}

			const PendingOwnership * pending = PendingOwnershipOf(self, type, index, generation);
			const bool owned				 = pending != nullptr ? pending->owned : record->owned.load(std::memory_order_relaxed);

			if (owned)
			{
				const std::uint8_t held = pending != nullptr ? pending->owner : record->owner.load(std::memory_order_relaxed);
				const bool releasing	= ownership.op == OwnershipOp::eRelease || ownership.op == OwnershipOp::eReleaseToExternal;

				if (releasing && held != static_cast<std::uint8_t>(self->queueType))
				{
					return self->validator->Fail(error, "a barrier releases a resource from a queue that does not own it");
				}
				if (ownership.op == OwnershipOp::eAcquire && held != static_cast<std::uint8_t>(ownership.counterpart))
				{
					return self->validator->Fail(error, "a barrier acquires a resource from a queue that does not hold it");
				}
			}

			if (ownership.op == OwnershipOp::eReleaseToExternal)
			{
				SetPendingOwnership(self, type, index, generation, ownership, 0, false);
				return true;
			}

			const QueueType holder = ownership.op == OwnershipOp::eRelease ? ownership.counterpart : self->queueType;
			SetPendingOwnership(self, type, index, generation, ownership, static_cast<std::uint8_t>(holder), true);
			return true;
		}

		[[nodiscard]] bool CheckAndAdvance(WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const TrackedSubrange & span, const ResourceState & before, const ResourceState & after, const QueueOwnership & ownership, Error * error) noexcept
		{
			TrackedSubrange box		   = span;
			box.resource			   = TrackedResource(type, index, generation);
			const std::uint32_t wanted = PackState(before);

			bool trackedHere = false;
			for (const TrackedSubrange & tracked : self->recordedStates)
			{
				if (tracked.resource != box.resource)
				{
					continue;
				}

				if (!Overlaps(tracked, box))
				{
					continue;
				}

				trackedHere = true;
				if (tracked.state != wanted)
				{
					return self->validator->Fail(error, "a barrier claims a before-state the resource was not left in by the last one");
				}
			}

			if (!trackedHere)
			{
				if (const PendingArrival * pending = PendingArrivalOf(self, box.resource))
				{
					if (pending->known && pending->use != before.use.Bits())
					{
						return self->validator->Fail(error, "a barrier claims a before-state the adopted resource did not arrive in");
					}
				}
				else if (const ResourceRecord * record = self->validator->Handles().Lookup(box.resource))
				{
					if (record->useKnown.load(std::memory_order_relaxed) && record->use.load(std::memory_order_relaxed) != before.use.Bits())
					{
						return self->validator->Fail(error, "a barrier claims a before-state the adopted resource did not arrive in");
					}
				}
			}

			if (!CheckAndTransferOwnership(self, type, index, generation, ownership, error))
			{
				return false;
			}

			if (!Retrack(self, box, PackState(after)))
			{
				Forget(self, box.resource);
			}

			if (CoversWholeResource(box, ExtentsOf(self, type, index, generation)))
			{
				SetPendingArrival(self, box.resource, after.use.Bits(), true);
			}
			else
			{
				SetPendingArrival(self, box.resource, 0, false);
			}

			return true;
		}

		bool ValidatedBarriers(void * impl, const BarrierBatch & batch, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, batch))
			{
				return self->validator->Fail(error, "a barrier names a resource this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				if (self->rendering)
				{
					return self->validator->Fail(error, "a barrier recorded inside a rendering scope, which has to be recorded between passes");
				}

				for (const MemoryBarrier & barrier : batch.memory)
				{
					if (!StateIsUsableAsAfter(self, barrier.after, QueueOwnership{}, error))
					{
						return false;
					}
				}

				for (const BufferBarrier & barrier : batch.buffers)
				{
					if (!StateIsUsableAsAfter(self, barrier.after, barrier.ownership, error))
					{
						return false;
					}

					if (barrier.size == 0)
					{
						return self->validator->Fail(error, "a buffer barrier names no bytes, so it describes no range to transition");
					}

					const std::uint64_t declared = DeclaredSizeOf(self, barrier.buffer.index, barrier.buffer.generation);
					if (declared != 0)
					{
						const bool wholeBuffer = barrier.size == std::numeric_limits<std::uint64_t>::max();

						if (barrier.offset >= declared || (!wholeBuffer && ByteSpanEnd(barrier.offset, barrier.size) > declared))
						{
							return self->validator->Fail(error, "a buffer barrier names bytes past the end of the buffer");
						}
					}

					if (!CheckAndAdvance(self,
							ResourceType::eBuffer,
							barrier.buffer.index,
							barrier.buffer.generation,
							BufferSpan(barrier, declared),
							barrier.before,
							barrier.after,
							barrier.ownership,
							error))
					{
						return false;
					}
				}

				for (const TextureBarrier & barrier : batch.textures)
				{
					if (!StateIsUsableAsAfter(self, barrier.after, barrier.ownership, error))
					{
						return false;
					}

					if (barrier.range.aspects.Bits() == 0u)
					{
						return self->validator->Fail(error, "a texture barrier names no aspect, so it describes no subresource to transition");
					}

					if (!CheckAndAdvance(self,
							ResourceType::eTexture,
							barrier.texture.index,
							barrier.texture.generation,
							TextureSpan(barrier.range, ExtentsOf(self, ResourceType::eTexture, barrier.texture.index, barrier.texture.generation)),
							barrier.before,
							barrier.after,
							barrier.ownership,
							error))
					{
						return false;
					}
				}
			}

			return self->blocks.render->barriers(self->inner, batch, error);
		}

		bool ValidatedAliasBarriers(void * impl, const std::span<const AliasBarrier> barriers, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, barriers))
			{
				return self->validator->Fail(error, "an alias barrier names a resource this device has already taken back");
			}

			if (self->validator->ChecksState() && self->rendering)
			{
				return self->validator->Fail(error, "aliasBarriers cannot be recorded inside a rendering scope, so record it between passes");
			}

			return self->blocks.aliasing->aliasBarriers(self->inner, barriers, error);
		}

		void ReconcileNativeMutation(WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const TrackedSubrange & span, const ResourceState & finalState) noexcept
		{
			TrackedSubrange written = span;
			written.resource		= TrackedResource(type, index, generation);

			if (!Retrack(self, written, PackState(finalState)))
			{
				Forget(self, written.resource);
			}

			if (self->validator->Handles().Lookup(written.resource) == nullptr)
			{
				return;
			}

			if (!CoversWholeResource(written, ExtentsOf(self, type, index, generation)))
			{
				SetPendingArrival(self, written.resource, 0, false);
				return;
			}

			SetPendingArrival(self, written.resource, finalState.use.Bits(), true);
		}

		bool ValidatedEndNativeMutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (self->validator->ChecksState())
			{
				for (const NativeTouchedBuffer & touched : desc.buffers)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						const TrackedSubrange span = WholeResourceSpan(ExtentsOf(self, ResourceType::eBuffer, touched.buffer.index, touched.buffer.generation));
						ReconcileNativeMutation(self, ResourceType::eBuffer, touched.buffer.index, touched.buffer.generation, span, touched.finalState);
					}
				}

				for (const NativeTouchedTexture & touched : desc.textures)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						const TrackedSubrange span =
							TextureSpan(touched.range, ExtentsOf(self, ResourceType::eTexture, touched.texture.index, touched.texture.generation));
						ReconcileNativeMutation(self, ResourceType::eTexture, touched.texture.index, touched.texture.generation, span, touched.finalState);
					}
				}
			}

			return self->blocks.nativeEscape->endNativeMutation(self->inner, desc, error);
		}

		bool ValidatedBegin(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (self->validator->ChecksState() && self->recording)
			{
				return self->validator->Fail(error, "Begin on a command list that is already recording");
			}

			self->recordedStates.clear();
			self->pendingOwnership.clear();
			self->pendingArrivals.clear();
			self->rendering		  = false;
			self->graphicsBound	  = false;
			self->computeBound	  = false;
			self->rayTracingBound = false;

			self->recordingThread = std::this_thread::get_id();
			self->checksThread	  = self->validator->ChecksState();

			if (!self->blocks.render->begin(self->inner, error))
			{
				return false;
			}

			self->recording = true;
			return true;
		}

		bool ValidatedEnd(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
			{
				return false;
			}

			if (self->validator->ChecksState())
			{
				if (!self->recording)
				{
					return self->validator->Fail(error, "End on a command list that is not recording");
				}

				if (self->rendering)
				{
					return self->validator->Fail(error, "End with a rendering scope still open");
				}
			}

			if (!self->blocks.render->end(self->inner, error))
			{
				return false;
			}

			self->recording = false;
			return true;
		}

		[[nodiscard]] bool AttachmentFormatIsRenderable(WrappedCommandList * self, const RenderingAttachment & attachment, const bool depthStencil) noexcept
		{
			const ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= ResourceType::eTextureView,
				.index		= attachment.view.index,
				.generation = attachment.view.generation,
			});
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

		bool ValidatedBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, desc))
			{
				return self->validator->Fail(error, "a rendering scope names an attachment this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				if (!self->recording)
				{
					return self->validator->Fail(error, "a rendering scope opened on a command list that is not recording");
				}

				if (self->rendering)
				{
					return self->validator->Fail(error, "a rendering scope opened inside another one, and nothing nests here");
				}

				for (const RenderingAttachment & color : desc.colors)
				{
					if (!AttachmentFormatIsRenderable(self, color, false))
					{
						return self->validator->Fail(error, "a colour attachment in a format this device does not advertise as renderable");
					}
				}

				if (desc.depthStencil != nullptr && !AttachmentFormatIsRenderable(self, *desc.depthStencil, true))
				{
					return self->validator->Fail(error, "a depth stencil attachment in a format this device does not advertise as renderable");
				}
			}

			if (!self->blocks.render->beginRendering(self->inner, desc, error))
			{
				return false;
			}

			self->rendering = true;
			return true;
		}

		bool ValidatedEndRendering(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (self->validator->ChecksState() && !self->rendering)
			{
				return self->validator->Fail(error, "a rendering scope closed without one being open");
			}

			if (!self->blocks.render->endRendering(self->inner, error))
			{
				return false;
			}

			self->rendering		= false;
			self->graphicsBound = false;
			return true;
		}

		bool ValidatedSetGraphicsPipeline(void * impl, const GraphicsPipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!AllUsable(*self->validator, pipeline))
			{
				return self->validator->Fail(error, "a graphics pipeline bound after this device took it back");
			}

			if (!self->blocks.render->setGraphicsPipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->graphicsBound = true;
			return true;
		}

		bool ValidatedSetComputePipeline(void * impl, const ComputePipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!AllUsable(*self->validator, pipeline))
			{
				return self->validator->Fail(error, "a compute pipeline bound after this device took it back");
			}

			if (!self->blocks.render->setComputePipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->computeBound = true;
			return true;
		}

		bool ValidatedSetRayTracingPipeline(void * impl, const RayTracingPipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!AllUsable(*self->validator, pipeline))
			{
				return self->validator->Fail(error, "a ray tracing pipeline bound after this device took it back");
			}

			if (!self->blocks.rayTracing->setRayTracingPipeline(self->inner, pipeline, error))
			{
				return false;
			}

			self->rayTracingBound = true;
			return true;
		}

		[[nodiscard]] bool DrawIsLegal(WrappedCommandList * self, Error * error) noexcept
		{
			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (!self->validator->ChecksState())
			{
				return true;
			}

			if (!self->rendering)
			{
				return self->validator->Fail(error, "a draw recorded outside a rendering scope");
			}

			return self->graphicsBound ? true : self->validator->Fail(error, "a draw recorded with no graphics pipeline bound");
		}

		bool ValidatedDraw(void * impl, const std::uint32_t vertexCount, const std::uint32_t instanceCount, const std::uint32_t firstVertex,
			const std::uint32_t firstInstance, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			return DrawIsLegal(self, error) ? self->blocks.render->draw(self->inner, vertexCount, instanceCount, firstVertex, firstInstance, error) : false;
		}

		bool ValidatedDrawIndexed(void * impl, const std::uint32_t indexCount, const std::uint32_t instanceCount, const std::uint32_t firstIndex,
			const std::int32_t vertexOffset, const std::uint32_t firstInstance, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);
			return DrawIsLegal(self, error)
					   ? self->blocks.render->drawIndexed(self->inner, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, error)
					   : false;
		}

		bool ValidatedDispatch(
			void * impl, const std::uint32_t groupCountX, const std::uint32_t groupCountY, const std::uint32_t groupCountZ, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error) || !RecordedIntoAnOpenList(self, error))
			{
				return false;
			}

			if (self->validator->ChecksState())
			{
				if (self->rendering)
				{
					return self->validator->Fail(error, "a dispatch recorded inside a rendering scope");
				}

				if (!self->computeBound)
				{
					return self->validator->Fail(error, "a dispatch recorded with no compute pipeline bound");
				}
			}

			return self->blocks.render->dispatch(self->inner, groupCountX, groupCountY, groupCountZ, error);
		}

	}

	void * WrapDevice(void * deviceImpl, const ValidationMode mode) noexcept
	{
		if (deviceImpl == nullptr || mode == ValidationMode::eOff)
		{
			return deviceImpl;
		}

		const CoreDeviceApi * core = detail::QueryBlock<CoreDeviceApi>(deviceImpl);
		if (core == nullptr)
		{
			return deviceImpl;
		}

		HostUniquePtr<WrappedDevice> wrapper = HostNew<WrappedDevice>();
		if (wrapper == nullptr)
		{
			return deviceImpl;
		}

		wrapper->ownedValidator.SetMode(mode);

		wrapper->object	   = DeviceObject();
		wrapper->inner	   = deviceImpl;
		wrapper->device	   = wrapper.get();
		wrapper->validator = &wrapper->ownedValidator;

		wrapper->blocks.core			= core;
		wrapper->blocks.present			= detail::QueryBlock<PresentApi>(deviceImpl);
		wrapper->blocks.placedMemory	= detail::QueryBlock<PlacedMemoryApi>(deviceImpl);
		wrapper->blocks.rayTracing		= detail::QueryBlock<RayTracingApi>(deviceImpl);
		wrapper->blocks.query			= detail::QueryBlock<QueryApi>(deviceImpl);
		wrapper->blocks.pipelineCache	= detail::QueryBlock<PipelineCacheApi>(deviceImpl);
		wrapper->blocks.residency		= detail::QueryBlock<ResidencyApi>(deviceImpl);
		wrapper->blocks.introspection	= detail::QueryBlock<ResourceIntrospectionApi>(deviceImpl);
		wrapper->blocks.adoption		= detail::QueryBlock<AdoptionApi>(deviceImpl);
		wrapper->blocks.externalSharing = detail::QueryBlock<ExternalSharingApi>(deviceImpl);

		return wrapper.release();
	}

	DeviceValidator * ValidatorOf(void * deviceImpl) noexcept
	{
		if (deviceImpl == nullptr || detail::ObjectOf(deviceImpl) != DeviceObject())
		{
			return nullptr;
		}

		return static_cast<WrappedDevice *>(deviceImpl)->validator;
	}

}

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

#include <mutex>
#include <utility>

namespace azo::rhi::validation
{
	// Which inner block an entry reaches through, one per block. The block type says which wrapper the impl pointer is, since only an entry reached through that
	// wrapper can name the block.

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

		// The entries that hand out another object, declared here because the tables name them and they are defined after the tables they publish through.
		void * ValidatedGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
		void * ValidatedCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
		void * ValidatedCreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
		void * ValidatedCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
		void * ValidatedAllocateCommandList(void * impl, CString debugName, Error * error) noexcept;
		bool ValidatedCommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		void ValidatedDestroyDevice(void * impl) noexcept;
		bool ValidatedDestroy(void * impl, ResourceType type, RawHandle handle, const DestroyDesc & desc, Error * error) noexcept;
		bool ValidatedSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
		bool ValidatedBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
		bool ValidatedArenaReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
		bool ValidatedBarriers(void * impl, const BarrierBatch & batch, Error * error) noexcept;
		bool ValidatedBuildAccelerationStructures(void * impl, std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept;
		TextureViewHandle ValidatedCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
		DescriptorSetLayoutHandle ValidatedCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
		DescriptorSetHandle ValidatedAllocateDescriptorSet(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
		bool ValidatedBegin(void * impl, Error * error) noexcept;
		bool ValidatedEnd(void * impl, Error * error) noexcept;
		bool ValidatedBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
		bool ValidatedEndRendering(void * impl, Error * error) noexcept;
		bool ValidatedSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
		bool ValidatedSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
		bool ValidatedDraw(void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance,
			Error * error) noexcept;
		bool ValidatedDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
			std::uint32_t firstInstance, Error * error) noexcept;
		bool ValidatedDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
		bool ValidatedEndNativeMutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept;
		PresentResult ValidatedPresent(void * impl, std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished, void * queueImpl, Error * error) noexcept;

		/*
		 * Descriptor compatibility needs a layout's binding types, the layout a set came from and the set a write names. Each is recorded in the registry record that
		 * handle already has, so there is no second table to keep in step.
		 *
		 * Bindings pack four bits each, low binding first, holding the type plus one so zero reads as a binding the layout never declared. One word covers sixteen
		 * bindings and a layout with more is checked only that far.
		 */
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

		// A set remembers the layout it was allocated against, both words of the handle so a layout destroyed and its slot reused is not mistaken for it.
		[[nodiscard]] std::uint64_t PackLayout(const DescriptorSetLayoutHandle layout) noexcept
		{
			return static_cast<std::uint64_t>(layout.index) | (static_cast<std::uint64_t>(layout.generation) << 32u);
		}

		TextureViewHandle ValidatedCreateTextureView(void * impl, const TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			if (!AllUsable(*self->validator, texture))
			{
				return self->validator->FailValue<TextureViewHandle>(error, "a texture view of a texture this device has already taken back");
			}

			const TextureViewHandle view = self->blocks.core->createTextureView(self->inner, texture, desc, error);
			if (view.IsValid())
			{
				static_cast<void>(self->validator->Handles().Record(RegisteredHandle{
					.type		= ResourceType::eTextureView,
					.index		= view.index,
					.generation = view.generation,
				}));
			}

			return view;
		}

		DescriptorSetLayoutHandle ValidatedCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

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

				// Stamped with the arena so the reset that frees this set can find it, there being no handle for the reset to name it by.
				record->origin.store(self->id, std::memory_order_relaxed);
			}

			return set;
		}

		/*
		 * Whether one write names a binding its set's layout declared and declared as this type.
		 *
		 * Anything the chain cannot be followed through is passed, not refused: a set this layer never saw, a layout it never saw or a binding past the sixteen a
		 * word holds. Refusing those would be reporting a gap in what is known as a mistake by the caller.
		 */
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

		// A sampler write names no type, there being one kind of sampler binding. The other two carry theirs.
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

		// The three write entries differ only in what they carry alongside the set, the binding and the type, which is all this looks at.
		template <class Write, auto Member>
		struct ValidatedWrites;

		template <class Write, bool (*CoreDeviceApi::*Member)(void *, std::span<const Write>, Error *) noexcept>
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

				return (self->blocks.core->*Member)(self->inner, writes, error);
			}
		};

		/*
		 * The blocks a wrapper publishes.
		 *
		 * Every entry is generated from the block's own declaration so nothing here is a signature retyped by hand and an entry that changes shape does not need a
		 * thunk changed to match. The entries that check something replace themselves below, which keeps what is checked in one list, not scattered through
		 * twenty tables.
		 */

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
				.createGraphicsPipeline		= &Recording<ResourceType::eGraphicsPipeline, &CoreDeviceApi::createGraphicsPipeline>::Call,
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
				.createRayTracingPipeline				= &Recording<ResourceType::eRayTracingPipeline, &RayTracingApi::createRayTracingPipeline>::Call,
				.createAccelerationStructure			= &Recording<ResourceType::eAccelerationStructure, &RayTracingApi::createAccelerationStructure>::Call,
				.updateDescriptorsAccelerationStructure = &Checked<&RayTracingApi::updateDescriptorsAccelerationStructure>::Call,
			};

			return block;
		}

		const QueryApi & ValidatingQueryApi() noexcept
		{
			static const QueryApi block{
				.createQueryPool	= &Recording<ResourceType::eQueryPool, &QueryApi::createQueryPool>::Call,
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

		/*
		 * Every import is a create, so each one writes the handle it produced into the registry the same way an ordinary create does. The exports name a handle this
		 * device owns and check it. closeExportedHandle names none, its argument being an operating system handle and not an RHI one.
		 */
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
				.getFamilyIndex	   = &Forward<&QueueApi::getFamilyIndex>::Call,
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
				.copyBuffer			 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::copyBuffer>::Call,
				.copyBufferToTexture = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::copyBufferToTexture>::Call,
				.copyTextureToBuffer = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::copyTextureToBuffer>::Call,
				.copyTexture		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::copyTexture>::Call,
				.clearBuffer		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::clearBuffer>::Call,
				.clearTexture		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::clearTexture>::Call,
				.resolveTexture		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::resolveTexture>::Call,
				.blit				 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::blit>::Call,
				.generateMips		 = &RecordedCheckedEntry<ChecksThread, &RenderCommandApi::generateMips>::Call,
				.beginDebugLabel	 = &RecordedEntry<ChecksThread, &RenderCommandApi::beginDebugLabel>::Call,
				.endDebugLabel		 = &RecordedEntry<ChecksThread, &RenderCommandApi::endDebugLabel>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const AliasingCommandApi & ValidatingAliasingCommandApi() noexcept
		{
			static const AliasingCommandApi block{
				.aliasBarriers = &RecordedCheckedEntry<ChecksThread, &AliasingCommandApi::aliasBarriers>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const RayTracingCommandApi & ValidatingRayTracingCommandApi() noexcept
		{
			static const RayTracingCommandApi block{
				.setRayTracingPipeline		  = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::setRayTracingPipeline>::Call,
				.buildAccelerationStructures  = &ValidatedBuildAccelerationStructures,
				.copyAccelerationStructure	  = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::copyAccelerationStructure>::Call,
				.compactAccelerationStructure = &RecordedCheckedEntry<ChecksThread, &RayTracingCommandApi::compactAccelerationStructure>::Call,
				.traceRays					  = &RecordedEntry<ChecksThread, &RayTracingCommandApi::traceRays>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const QueryCommandApi & ValidatingQueryCommandApi() noexcept
		{
			static const QueryCommandApi block{
				.resetQueryPool	  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::resetQueryPool>::Call,
				.writeTimestamp	  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::writeTimestamp>::Call,
				.beginQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::beginQuery>::Call,
				.endQuery		  = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::endQuery>::Call,
				.resolveQueryData = &RecordedCheckedEntry<ChecksThread, &QueryCommandApi::resolveQueryData>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectApi & ValidatingIndirectApi() noexcept
		{
			static const IndirectApi block{
				.drawIndirect		 = &RecordedCheckedEntry<ChecksThread, &IndirectApi::drawIndirect>::Call,
				.drawIndexedIndirect = &RecordedCheckedEntry<ChecksThread, &IndirectApi::drawIndexedIndirect>::Call,
				.dispatchIndirect	 = &RecordedCheckedEntry<ChecksThread, &IndirectApi::dispatchIndirect>::Call,
			};

			return block;
		}

		template <bool ChecksThread>
		const IndirectCountApi & ValidatingIndirectCountApi() noexcept
		{
			static const IndirectCountApi block{
				.drawIndirectCount		  = &RecordedCheckedEntry<ChecksThread, &IndirectCountApi::drawIndirectCount>::Call,
				.drawIndexedIndirectCount = &RecordedCheckedEntry<ChecksThread, &IndirectCountApi::drawIndexedIndirectCount>::Call,
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

		const SwapchainApi & ValidatingSwapchainApi() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage			 = &Forward<&SwapchainApi::acquireNextImage>::Call,
				.present					 = &ValidatedPresent,
				.getBackBuffer				 = &Vending<ResourceType::eTexture, &SwapchainApi::getBackBuffer>::Call,
				.getBackBufferView			 = &Vending<ResourceType::eTextureView, &SwapchainApi::getBackBufferView>::Call,
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

		/*
		 * A wrapper publishes exactly what the object behind it published and nothing it did not.
		 *
		 * Declining a block is how a caller learns a capability is absent so a wrapper that published one its inner declined would be inventing a capability without
		 * checking one. That is why this consults the wrapper instead of being the static match the backends use.
		 */
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

		// The object a wrapper stands in front of, which is what the native handle accessors resolve through.
		void * InnerObject(void * impl) noexcept
		{
			return static_cast<WrappedObject *>(impl)->inner;
		}

		const NativeObjectApi & ValidatingNativeObjectApi() noexcept
		{
			static const NativeObjectApi block{ .inner = &InnerObject };
			return block;
		}

		/*
		 * The one block a wrapper publishes that the object behind it does not.
		 *
		 * Every other block here is published only where the inner object published it because declining is how a caller learns a capability is absent and a wrapper
		 * inventing one would be answering for a backend. This is the exception and has to be, since what it answers is that there is a layer here at all, which is
		 * the one thing the object behind it cannot say.
		 */
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

		/*
		 * Hands a child wrapper to the device that made it.
		 *
		 * Every child outlives its own create and dies with the device because that is when the backend frees the object behind it: a pool, an arena, a swapchain and
		 * the lists a pool allocates all live until the device goes. Holding them here without inventing a shorter lifetime is what keeps a wrapper from outliving
		 * its inner object or dying before it.
		 */
		template <class Wrapper>
		[[nodiscard]] Wrapper * Adopt(WrappedDevice * device, HostUniquePtr<Wrapper> child) noexcept
		{
			Wrapper * raw = child.get();

			const std::scoped_lock lock(device->childLock);
			if (!detail::TryPushBack(device->children,
					WrappedDevice::Child{
						.wrapper = raw,
						.release =
							[](WrappedObject * object) noexcept
						{
							HostDeleter{ .size = sizeof(Wrapper), .alignment = alignof(Wrapper) }(static_cast<Wrapper *>(object));
						},
					}))
			{
				return nullptr;
			}

			static_cast<void>(child.release());
			return raw;
		}

		/*
		 * The entries that hand out another object and not a handle.
		 *
		 * Each has to come back wrapped or the caller would hold the backend's object directly and everything reached through it would be unchecked. A wrapper that
		 * could not be allocated is reported as the create failing, since handing back the bare inner object would silently drop validation for that object and
		 * everything under it.
		 */

		void * ValidatedGetQueue(void * impl, const QueueType type, const std::uint32_t index, Error * error) noexcept
		{
			auto * self		  = static_cast<WrappedDevice *>(impl);
			void * innerQueue = self->blocks.core->getQueue(self->inner, type, index, error);
			if (innerQueue == nullptr)
			{
				return nullptr;
			}

			/*
			 * One wrapper per call and not one per queue because a backend hands back a fresh object each time this is asked and not the same one twice. Deduplicating
			 * on the inner pointer would find nothing to match and would cost a scan of every child the device has, which is every command list it ever allocated.
			 */
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

			void * adopted = Adopt(self, std::move(wrapper));
			return adopted != nullptr ? adopted : self->validator->FailValue<void *>(error, "the host allocator refused to record a validated queue");
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

			void * adopted = Adopt(self, std::move(wrapper));
			return adopted != nullptr ? adopted : self->validator->FailValue<void *>(error, "the host allocator refused to record a validated command pool");
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

			void * adopted = Adopt(self, std::move(wrapper));
			return adopted != nullptr ? adopted
									  : self->validator->FailValue<void *>(error, "the host allocator refused to record a validated descriptor arena");
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

			void * adopted = Adopt(self, std::move(wrapper));
			return adopted != nullptr ? adopted : self->validator->FailValue<void *>(error, "the host allocator refused to record a validated swapchain");
		}

		/*
		 * A reset ends every recording the pool has out, so the wrappers in front of those lists go back to where End leaves one.
		 *
		 * A list is handed out under the wrapper it had the first time, so a recording nothing ever ended would otherwise still read as open on the frame after the
		 * one that abandoned it, and the next Begin would be refused for a recording that the reset already ended.
		 */
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

			/*
			 * A list this pool built before and has since taken back, which arrives as the pointer it was handed out under the first time. Its wrapper is still the
			 * one standing in front of it and everything a recording leaves behind is rewound at Begin, so there is nothing here to remake.
			 */
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
			if (adopted == nullptr)
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused to record a validated command list");
			}

			// The device owns the wrapper from here whether or not this succeeds, so a refusal costs this allocate and leaks nothing.
			if (!detail::TryInsertOrAssign(self->lists, innerList, static_cast<WrappedCommandList *>(adopted)))
			{
				return self->validator->FailValue<void *>(error, "the host allocator refused to record a validated command list");
			}

			return adopted;
		}

		/*
		 * Teardown. The inner device goes first, since the objects the children stand in front of belong to it and freeing a wrapper while its inner object is still
		 * live would leave the backend holding something nothing points at.
		 */
		void ValidatedDestroyDevice(void * impl) noexcept
		{
			auto * self = static_cast<WrappedDevice *>(impl);

			self->blocks.core->destroyDevice(self->inner);

			for (const WrappedDevice::Child & child : self->children)
			{
				child.release(child.wrapper);
			}
			self->children.clear();

			HostDeleter{ .size = sizeof(WrappedDevice), .alignment = alignof(WrappedDevice) }(self);
		}

		/*
		 * The two entries that receive another object, not only their own. Above this layer a wrapper stands in front of its inner object so what arrives here names
		 * the wrapper. A backend handed one would read this layer's fields as its own. Both put the inner object back before forwarding. Everything else takes
		 * handles, which the backend minted and this layer never rewrites.
		 */

		[[nodiscard]] void * Unwrap(void * impl) noexcept
		{
			return impl != nullptr ? static_cast<WrappedObject *>(impl)->inner : nullptr;
		}

		bool ValidatedSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedQueue *>(impl);

			/*
			 * The lists travel as facades so each one has to be rebuilt naming the object behind it. Allocated, not kept on the stack because the count is the caller's
			 * and a submit is a per-frame call and not a recorded command, which is the path that may not allocate.
			 */
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

				/*
				 * A list is recorded against the pool's queue type and can only be run on a queue of that type. Backends catch this in their own terms, a family index on
				 * Vulkan and a list type on D3D12, both of which are the same rule wearing the native spelling.
				 */
				if (self->validator->ChecksState() && wrapper->queueType != self->type)
				{
					return self->validator->Fail(error, "submit of a command list recorded for a different queue type than the queue it was given to");
				}

				unwrapped.push_back(detail::FacadeBuilder::MakeCommandList(wrapper->inner, &wrapper->blocks));
			}

			for (const CommandList & list : unwrapped)
			{
				pointers.push_back(&list);
			}

			SubmitDesc inner   = desc;
			inner.commandLists = pointers;
			return self->blocks.core->submit(self->inner, inner, error);
		}

		/*
		 * A sparse bind names memory pages without transitioning anything so the arithmetic is what it gets wrong. Handles are checked as anywhere else. What is left
		 * is the page itself: a bind that supplies a heap and then binds nothing of it means the caller wanted an unbind and left the heap in. That maps a
		 * zero-length tile range on backends that take it and is refused on the ones that do not.
		 */
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

		/*
		 * An arena reset is the one place handles stop being live without a destroy naming them. The sets are taken back before the backend frees them so nothing can
		 * bind one in between. A set bound after this reads as a handle the device already took back, because the descriptor memory behind it belongs to whatever the
		 * arena hands out next. No native validation layer reports it, since from its side the set was legally recycled.
		 */
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

		/*
		 * An acceleration structure build, whose desc carries what it needs without naming it in the signature.
		 *
		 * A build with no destination, no scratch or nothing to build from is a desc that was filled in partly and the native APIs answer it with anything from a
		 * silent no-op to a device loss.
		 */
		bool ValidatedBuildAccelerationStructures(void * impl, const std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
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

		/*
		 * Destroy is where a handle stops being one. It catches the three ways a handle goes wrong, for every backend at once: a handle this device never handed out,
		 * one belonging to another device and one already destroyed. Retire answers no to all three so the retire is the check. It is refused before the backend sees
		 * it, since a backend handed a stale handle reads whatever moved into the slot.
		 */
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
				/*
				 * The backend refused what the registry let through. A swapchain back buffer is that case: the device hands it out and keeps owning it so the backend
				 * refuses the destroy. Put the handle back or the next frame would be refused for a destroy that never happened. Its tracked state is deliberately not
				 * restored, since a failed destroy should leave less known about a resource, not more.
				 */
				static_cast<void>(self->validator->Handles().Record(registered));
				return false;
			}

			return true;
		}

		/*
		 * A resource's state, packed into the word the registry keeps for it.
		 *
		 * Stages are left out. They say when an access happens, not what state the resource is in and two barriers that agree on the access and the layout describe
		 * the same state whichever stage each named.
		 */
		[[nodiscard]] std::uint32_t PackState(const ResourceState & state) noexcept
		{
			return static_cast<std::uint32_t>(state.access.Bits()) | (static_cast<std::uint32_t>(state.layout) << 24u);
		}

		// One key for a resource within a recording. The generation is in it so a slot reused after a destroy is not the resource that held it before.
		[[nodiscard]] std::uint64_t StateKey(const ResourceType type, const std::uint32_t index, const std::uint32_t generation) noexcept
		{
			return (static_cast<std::uint64_t>(type) << 56u) ^ (static_cast<std::uint64_t>(index) << 32u) ^ generation;
		}

		/*
		 * Queue ownership is declared by a barrier too. Unlike a resource state it is not per recording: a queue that releases a resource stays released from it
		 * until another queue acquires it. It lives on the registry record where it outlives any one list. A barrier naming no transfer leaves ownership alone. One
		 * that does has to name the family that actually holds it, since releasing from a family that never had it produces a resource no queue owns.
		 */
		[[nodiscard]] bool CheckAndTransferOwnership(WrappedCommandList * self, const ResourceType type, const std::uint32_t index,
			const std::uint32_t generation, const QueueFamilyTransfer & ownership, Error * error) noexcept
		{
			if (ownership.src == kIgnoreQueueFamily && ownership.dst == kIgnoreQueueFamily)
			{
				return true;
			}

			/*
			 * A transfer names both ends or neither and the two ends differ. One end named and the other ignored is neither a transfer nor the absence of one. A
			 * transfer to the family that already holds it asks for a matched release and acquire on one queue. Only Vulkan notices, since the pair goes straight into
			 * the barrier it builds while the other two do nothing with it. That difference is why this check moved up here.
			 */
			if (ownership.src == kIgnoreQueueFamily || ownership.dst == kIgnoreQueueFamily || ownership.src == ownership.dst)
			{
				return self->validator->Fail(
					error, "a barrier names a queue family ownership transfer that is not one, either half-filled or from a family to itself");
			}

			ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
				.type		= type,
				.index		= index,
				.generation = generation,
			});
			if (record == nullptr)
			{
				return true;
			}

			if (record->owned.load(std::memory_order_relaxed) && record->owner.load(std::memory_order_relaxed) != static_cast<std::uint8_t>(ownership.src))
			{
				return self->validator->Fail(error, "a barrier releases a resource from a queue family that does not own it");
			}

			record->owner.store(static_cast<std::uint8_t>(ownership.dst), std::memory_order_relaxed);
			record->owned.store(true, std::memory_order_relaxed);
			return true;
		}

		/*
		 * Checks one barrier's before-state against where this recording last left the resource, then records the after-state. A resource not yet touched here is
		 * taken, not checked, since nothing earlier in the list said what state it was in and the caller is entitled to declare it. An adopted resource is the
		 * exception: it arrived in a declared state and the first barrier is checked against that declaration. Getting it wrong discards the contents the object was
		 * adopted for.
		 */
		[[nodiscard]] bool CheckAndAdvance(WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const ResourceState & before, const ResourceState & after, const QueueFamilyTransfer & ownership, Error * error) noexcept
		{
			const std::uint64_t key = StateKey(type, index, generation);
			const auto tracked		= self->recordedStates.find(key);
			const bool trackedHere	= tracked != self->recordedStates.end();

			if (trackedHere && tracked->second != PackState(before))
			{
				return self->validator->Fail(error, "a barrier claims a before-state the resource was not left in by the last one");
			}

			/*
			 * The declared arrival state, checked once and then given up.
			 *
			 * Consumed, not kept because it stops being true the moment this barrier moves the resource, and because keeping it would mean claiming to track state
			 * across command lists, which nothing here does. The exchange is what makes exactly one recording consume it when two are running at once.
			 */
			if (!trackedHere)
			{
				if (ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
						.type		= type,
						.index		= index,
						.generation = generation,
					}))
				{
					if (record->accessKnown.exchange(false, std::memory_order_relaxed) &&
						record->access.load(std::memory_order_relaxed) != static_cast<std::uint32_t>(before.access.Bits()))
					{
						return self->validator->Fail(error, "a barrier claims a before-state the adopted resource did not arrive in");
					}
				}
			}

			if (!CheckAndTransferOwnership(self, type, index, generation, ownership, error))
			{
				return false;
			}

			if (!detail::TryInsertOrAssign(self->recordedStates, key, PackState(after)))
			{
				// Nothing tracked is better than half tracked: the entry is simply absent so the next barrier naming it is taken, not checked.
				self->recordedStates.erase(key);
			}

			return true;
		}

		/*
		 * A barrier declares the state it believes a resource is in. Getting that wrong stays silent on hardware that does not care. Tracking it here, not per
		 * backend makes every backend answer the same way, including external ones that track nothing.
		 *
		 * The first barrier a resource sees is taken as truth, since nothing before it said what state it was created in. An undefined before-state is likewise
		 * taken, meaning the caller does not care what was there.
		 */
		bool ValidatedBarriers(void * impl, const BarrierBatch & batch, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
			{
				return false;
			}

			if (!ArgumentIsUsable(*self->validator, batch))
			{
				return self->validator->Fail(error, "a barrier names a resource this device has already taken back");
			}

			if (self->validator->ChecksState())
			{
				for (const BufferBarrier & barrier : batch.buffers)
				{
					if (!CheckAndAdvance(self,
							ResourceType::eBuffer,
							barrier.buffer.index,
							barrier.buffer.generation,
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
					if (!CheckAndAdvance(self,
							ResourceType::eTexture,
							barrier.texture.index,
							barrier.texture.generation,
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

		/*
		 * Where one resource a native mutation moved is written back, into both of the places a barrier is checked against.
		 *
		 * Both, because which one answers depends on where the next barrier is: this recording's own tracking, and the registry record for the first barrier in
		 * a list that has not named the resource. Writing one and leaving the other refuses a correct barrier.
		 *
		 * Queue ownership is left alone, a touched resource declaring no queue family.
		 */
		void ReconcileNativeMutation(WrappedCommandList * self, const ResourceType type, const std::uint32_t index, const std::uint32_t generation,
			const ResourceState & finalState) noexcept
		{
			const std::uint64_t key = StateKey(type, index, generation);
			if (!detail::TryInsertOrAssign(self->recordedStates, key, PackState(finalState)))
			{
				// The same answer a barrier gives when the map cannot grow: nothing tracked, so the next barrier naming it is taken and not checked.
				self->recordedStates.erase(key);
			}

			if (ResourceRecord * record = self->validator->Handles().Lookup(RegisteredHandle{
					.type		= type,
					.index		= index,
					.generation = generation,
				}))
			{
				record->access.store(static_cast<std::uint32_t>(finalState.access.Bits()), std::memory_order_relaxed);
				record->accessKnown.store(true, std::memory_order_relaxed);
			}
		}

		/*
		 * The end of a native mutation scope, where the states the caller declared become the states this layer believes.
		 *
		 * It belongs here and not in a backend. A backend's own recording already targets the list it was handed, so reconciling against tracking only this
		 * layer keeps is the whole reason the caller was asked to fill it in.
		 *
		 * A read-only touch is skipped, a read leaving the resource where it was.
		 */
		bool ValidatedEndNativeMutation(void * impl, const NativeMutationDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
			{
				return false;
			}

			if (self->validator->ChecksState())
			{
				for (const NativeTouchedBuffer & touched : desc.buffers)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						ReconcileNativeMutation(self, ResourceType::eBuffer, touched.buffer.index, touched.buffer.generation, touched.finalState);
					}
				}

				for (const NativeTouchedTexture & touched : desc.textures)
				{
					if (touched.access == NativeMutationAccess::eReadWrite)
					{
						ReconcileNativeMutation(self, ResourceType::eTexture, touched.texture.index, touched.texture.generation, touched.finalState);
					}
				}
			}

			return self->blocks.nativeEscape->endNativeMutation(self->inner, desc, error);
		}

		/*
		 * A command list is a state machine every backend enforces differently or not at all. Tracking it here gives one answer even on a backend whose native API
		 * tolerates the mistake. The flags live on the list and not the registry and need no lock, since a list belongs to one thread.
		 *
		 * The checks are gated but the bookkeeping is not. Turning validation up mid-run would otherwise start from a lie.
		 */
		bool ValidatedBegin(void * impl, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (self->validator->ChecksState() && self->recording)
			{
				return self->validator->Fail(error, "Begin on a command list that is already recording");
			}

			// Cleared before the backend is called so a list whose Begin the backend refuses is still left carrying nothing from a recording that is over.
			self->recordedStates.clear();
			self->rendering		= false;
			self->graphicsBound = false;
			self->computeBound	= false;

			// Where the list is claimed by a thread. Every command until End has to arrive on this one, in the modes that say they check.
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

		bool ValidatedBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
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

			if (!RecordedOnItsOwnThread(self, error))
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

			// A pipeline binding does not outlive the scope it was made in so what was bound stops counting here.
			self->rendering		= false;
			self->graphicsBound = false;
			return true;
		}

		bool ValidatedSetGraphicsPipeline(void * impl, const GraphicsPipelineHandle pipeline, Error * error) noexcept
		{
			auto * self = static_cast<WrappedCommandList *>(impl);

			if (!RecordedOnItsOwnThread(self, error))
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

			if (!RecordedOnItsOwnThread(self, error))
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

		// What a draw needs to be legal, shared by both spellings of it.
		[[nodiscard]] bool DrawIsLegal(WrappedCommandList * self, Error * error) noexcept
		{
			if (!RecordedOnItsOwnThread(self, error))
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

			if (!RecordedOnItsOwnThread(self, error))
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

	} // namespace

	/*
	 * Puts a device behind a wrapper, which is what installs the layer.
	 *
	 * Everything above this holds the wrapper from here on, including the block set the facades resolve so nothing has to know a decorator exists. The inner
	 * device is handed back if the wrapper cannot be built, since a device that came up is better returned than leaked.
	 */
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
			// Unwrapped, not refused. A device with no validation is worse than one with it and better than none at all.
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

} // namespace azo::rhi::validation

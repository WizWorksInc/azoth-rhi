// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/native_access.hpp"

#include "backends/null/internal.hpp"
#include "backends/registration.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

namespace azo::rhi
{
	namespace
	{

		const CoreDeviceApi & CoreDeviceBlock() noexcept;
		const PresentApi & PresentBlock() noexcept;
		const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
		const RayTracingApi & RayTracingBlock() noexcept;
		const ResourceIntrospectionApi & resource_introspection_block() noexcept;
		const QueryApi & QueryBlock() noexcept;
		const PipelineCacheApi & PipelineCacheBlock() noexcept;
		const ResidencyApi & ResidencyBlock() noexcept;
		const AdoptionApi & AdoptionBlock() noexcept;
		const InstanceApi & InstanceBlock() noexcept;
		const QueueApi & QueueBlock() noexcept;
		const SparseApi & SparseBlock() noexcept;
		const CommandPoolApi & CommandPoolBlock() noexcept;
		const RenderCommandApi & RenderCommandBlock() noexcept;
		const AliasingCommandApi & AliasingCommandBlock() noexcept;
		const RayTracingCommandApi & RayTracingCommandBlock() noexcept;
		const QueryCommandApi & QueryCommandBlock() noexcept;
		const IndirectApi & IndirectBlock() noexcept;
		const IndirectCountApi & IndirectCountBlock() noexcept;
		const NativeEscapeApi & NativeEscapeBlock() noexcept;
		const SwapchainApi & SwapchainBlock() noexcept;
		const DescriptorArenaApi & DescriptorArenaBlock() noexcept;

		template <typename HandleT>
		[[nodiscard]] HandleT mint_handle(null::NullDevice * device, const SlotLifetime lifetime = SlotLifetime::eOwned)
		{
			return device->handles.store<HandleT>(null::NullHandleRecord{ .lifetime = lifetime });
		}

		template <typename HandleT>
		[[nodiscard]] bool resolves(null::NullDevice * device, HandleT handle) noexcept
		{
			return device->handles.resolve(handle, kHandleAlreadyChecked) != nullptr;
		}

		[[nodiscard]] void * alloc_object(null::NullDevice * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics)
		{
			null::NullObject * object = device->objects.New();
			if (object == nullptr)
			{
				return nullptr;
			}

			object->object	  = published;
			object->owner	  = device;
			object->queueType = queueType;
			return object;
		}

		struct NullBackendOwner final
		{
			detail::HostVector<HostUniquePtr<null::NullInstance>> instances;
			detail::HostVector<HostUniquePtr<null::NullDevice>> devices;
		};

		[[nodiscard]] NullBackendOwner & backend_owner()
		{
			static NullBackendOwner s_Owner;
			return s_Owner;
		}

		[[nodiscard]] null::NullDevice * make_owned_device(null::NullInstance * instance, const DeviceDesc & desc, Error * error)
		{
			const QueuePlan plan = plan_queues(desc.queues);
			if ((plan.computeDedicated && plan.computeCount > 0) || (plan.copyDedicated && plan.copyCount > 0))
			{
				if (error != nullptr)
				{
					*error = Error{
						.code	 = ErrorCode::eUnsupportedFeature,
						.message = "the Null backend has no dedicated compute or copy queue",
					};
				}

				return nullptr;
			}

			auto device = host_new<null::NullDevice>();
			if (device == nullptr)
			{
				return nullptr;
			}

			device->object					= publishing_object<Published<CoreDeviceApi, &CoreDeviceBlock>,
				Published<PresentApi, &PresentBlock>,
				Published<PlacedMemoryApi, &PlacedMemoryBlock>,
				Published<RayTracingApi, &RayTracingBlock>,
				Published<QueryApi, &QueryBlock>,
				Published<PipelineCacheApi, &PipelineCacheBlock>,
				Published<ResidencyApi, &ResidencyBlock>,
				Published<ResourceIntrospectionApi, &resource_introspection_block>,
				Published<AdoptionApi, &AdoptionBlock>>();
			device->instanceWrapper			= instance;
			device->validation				= desc.validation;
			device->caps.apiId				= NullApi::kId;
			device->caps.shaderBinaryFormat = ShaderBinaryFormat::eBackendNative;
			device->caps.graphicsQueueCount = plan.graphicsCount;
			device->caps.computeQueueCount	= plan.computeCount;
			device->caps.copyQueueCount		= plan.copyCount;

			device->caps.hasDedicatedComputeQueue = false;

			device->caps.hasDedicatedTransferQueue = false;

			device->caps.maxOpenCommandListsPerQueue = kUnlimitedOpenCommandLists;

			// NullQueueSubmit records no work and waits on nothing, so a list is never pending and can always go again.
			device->caps.supportsCommandListResubmit = true;

			device->caps.supportsTextureViewSwizzle = true;
			device->caps.supportsScalarBlockLayout	= true;

			device->caps.supportsMultiPlanarFormats = true;

			device->caps.supportsScaledBlit = true;

			device->caps.supportsTimestampWritesInScope = true;

			// Writing and resolving a timestamp both record nothing here, so there is never a pair of values to come back in order.
			device->caps.supportsOrderedTimestamps = false;

			device->caps.sparseTier			 = SparseTier::eResidentVolumes;
			device->caps.sparseTileSizeBytes = std::uint64_t{ 64 } * 1024;

			device->caps.maxDescriptorSets					= 8;
			device->caps.maxDescriptorsPerSet				= 65536;
			device->caps.minUniformBufferOffsetAlignment	= 1;
			device->caps.minStorageBufferOffsetAlignment	= 1;
			device->caps.minTexelBufferOffsetAlignment		= 1;
			device->caps.optimalBufferCopyOffsetAlignment	= 1;
			device->caps.optimalBufferCopyRowPitchAlignment = 1;

			device->adapter.type					  = AdapterType::eCpu;
			device->adapter.apiId					  = NullApi::kId;
			device->adapter.name					  = "Null Adapter";
			device->adapter.unifiedMemoryArchitecture = true;

			null::NullDevice * raw	 = device.get();
			NullBackendOwner & owner = backend_owner();

			std::uint32_t deviceTag = 0;
			if (!detail::device_tags().acquire(deviceTag))
			{
				return nullptr;
			}
			raw->deviceTag = deviceTag;
			raw->handles.rebind(deviceTag);

			if (!detail::try_push_back(owner.devices, std::move(device)))
			{
				detail::device_tags().release(deviceTag);
				return nullptr;
			}

			return raw;
		}

		[[nodiscard]] null::NullInstance * make_owned_instance()
		{
			auto instance = host_new<null::NullInstance>();
			if (instance == nullptr)
			{
				return nullptr;
			}

			instance->object = publishing_object<Published<InstanceApi, &InstanceBlock>>();

			null::NullInstance * raw = instance.get();
			NullBackendOwner & owner = backend_owner();
			if (!detail::try_push_back(owner.instances, std::move(instance)))
			{
				return nullptr;
			}

			return raw;
		}

		void null_destroy_device(void * impl) noexcept
		{
			NullBackendOwner & owner = backend_owner();

			null::NullInstance * owningInstance = nullptr;
			std::uint32_t releasedTag			= 0;
			for (const HostUniquePtr<null::NullDevice> & device : owner.devices)
			{
				if (device.get() == impl)
				{
					owningInstance = device->instanceWrapper;
					releasedTag	   = device->deviceTag;
					break;
				}
			}

			std::erase_if(owner.devices,
				[impl](const HostUniquePtr<null::NullDevice> & device)
				{
					return device.get() == impl;
				});
			detail::device_tags().release(releasedTag);

			if (owningInstance != nullptr)
			{
				bool stillUsed = false;
				for (const HostUniquePtr<null::NullDevice> & device : owner.devices)
				{
					if (device->instanceWrapper == owningInstance)
					{
						stillUsed = true;
						break;
					}
				}
				if (!stillUsed)
				{
					std::erase_if(owner.instances,
						[owningInstance](const HostUniquePtr<null::NullInstance> & instance)
						{
							return instance.get() == owningInstance;
						});
				}
			}
		}

		void null_destroy_instance(void * impl) noexcept
		{
			NullBackendOwner & owner = backend_owner();
			std::erase_if(owner.instances,
				[impl](const HostUniquePtr<null::NullInstance> & instance)
				{
					return instance.get() == impl;
				});
		}

		bool succeed(Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = {};
			}

			return true;
		}

		bool fail(Error * error, ErrorCode code, const char * message) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{
					.code	 = code,
					.message = message,
				};
			}
			return false;
		}

		template <typename T>
		[[nodiscard]] bool store(T * out, T value, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
			}

			*out = std::move(value);
			return succeed(error);
		}

		template <typename T>
		[[nodiscard]] T return_value(T value, Error * error) noexcept
		{
			succeed(error);
			return value;
		}

		template <typename T>
		[[nodiscard]] T fail_value(Error * error, ErrorCode code, const char * message) noexcept
		{
			fail(error, code, message);
			return {};
		}

		template <typename... Args>
		[[nodiscard]] Error * last_error(Args &&... args) noexcept
		{
			static_assert(sizeof...(Args) > 0);
			auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
			return std::get<sizeof...(Args) - 1>(tuple);
		}

		template <typename T, typename... Args>
		[[nodiscard]] T * output_before_error(Args &&... args) noexcept
		{
			static_assert(sizeof...(Args) > 1);
			auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
			return std::get<sizeof...(Args) - 2>(tuple);
		}

		template <typename... Args>
		bool noop_void([[maybe_unused]] void * impl, Args... args) noexcept
		{
			return succeed(last_error(args...));
		}

		template <typename HandleT>
		[[nodiscard]] HandleT mint_created(null::NullDevice * device, Error * error) noexcept
		{
			const auto handle = mint_handle<HandleT>(device);
			if (!handle.is_valid())
			{
				return fail_value<HandleT>(error, ErrorCode::eOutOfHostMemory, "Null backend handle allocation failed");
			}

			return return_value(handle, error);
		}

		template <typename HandleT, typename... Args>
		HandleT null_create_handle(void * impl, Args... args) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			return mint_created<HandleT>(static_cast<null::NullDevice *>(impl), last_error(args...));
		}

		template <typename HandleT, typename DescT>
		HandleT null_create_exportable(void * impl, const DescT & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			if (!desc.exportableHandleTypes.empty())
			{
				return fail_value<HandleT>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			return mint_created<HandleT>(static_cast<null::NullDevice *>(impl), error);
		}

		TextureHandle null_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (!desc.exportableHandleTypes.empty())
			{
				return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}

			if (desc.mipLevels > detail::max_mip_levels(desc.width, desc.height, desc.type == TextureType::eTex3D ? desc.depth : 1))
			{
				return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}

			const auto handle = mint_created<TextureHandle>(device, error);
			if (!handle.is_valid())
			{
				return handle;
			}

			if (null::NullHandleRecord * record = device->handles.resolve(handle, kHandleAlreadyChecked); record != nullptr)
			{
				record->desc = detail::recorded(desc);
			}

			return handle;
		}

		bool null_get_texture_info(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.getTextureInfo");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
			}

			const null::NullHandleRecord * const record = device->handles.resolve(texture, false);
			if (record == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
			}

			if (record->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
			}

			*out = TextureInfo{ .desc = record->desc };
			return true;
		}

		BufferHandle null_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (!desc.exportableHandleTypes.empty())
			{
				return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			if (desc.size == 0)
			{
				return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}

			const auto handle = mint_created<BufferHandle>(device, error);
			if (!handle.is_valid())
			{
				return handle;
			}

			if (null::NullHandleRecord * record = device->handles.resolve(handle, kHandleAlreadyChecked); record != nullptr)
			{
				record->bufferDesc = detail::recorded(desc);
			}

			return handle;
		}

		bool null_get_buffer_info(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.getBufferInfo");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
			}

			const null::NullHandleRecord * const record = device->handles.resolve(buffer, false);
			if (record == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
			}

			const MemoryAccess access = record->bufferDesc.memory == MemoryUsage::eGpuOnly || record->bufferDesc.memory == MemoryUsage::eTransient ||
												record->bufferDesc.memory == MemoryUsage::eReserved
											? MemoryAccess::eGpuOnly
											: MemoryAccess::eCpuVisibleCoherent;

			*out = BufferInfo{ .desc = record->bufferDesc, .memoryAccess = access };
			return true;
		}

		template <typename T, typename... Args>
		bool null_default([[maybe_unused]] void * impl, Args... args) noexcept
		{
			T * out = output_before_error<T>(args...);
			if (out == nullptr)
			{
				return fail(last_error(args...), ErrorCode::eInvalidArgument, "operation called with a null output");
			}

			return store(out, T{}, last_error(args...));
		}

		MappedMemory null_map([[maybe_unused]] void * impl, [[maybe_unused]] BufferHandle buffer, [[maybe_unused]] const MapDesc & desc, Error * error) noexcept
		{
			fail(error, ErrorCode::eUnsupportedFeature, "Null backend does not expose mapped memory");
			return {};
		}

		bool null_unmap([[maybe_unused]] void * impl, [[maybe_unused]] BufferHandle buffer, Error * error) noexcept
		{
			return fail(error, ErrorCode::eInvalidState, "unmap of a buffer with no map outstanding");
		}

		GraphicsApiId null_device_api_id([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::kId;
		}

		std::string_view null_device_api_name([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::kDisplayName;
		}

		const DeviceCaps & null_device_caps(void * impl) noexcept
		{
			return static_cast<null::NullDevice *>(impl)->caps;
		}

		const AdapterInfo & null_device_adapter_info(void * impl) noexcept
		{
			return static_cast<null::NullDevice *>(impl)->adapter;
		}

		ValidationMessageCounts null_device_validation_message_counts([[maybe_unused]] void * impl) noexcept
		{
			return {};
		}

		FormatSupport null_device_format_support([[maybe_unused]] void * impl, const Format format) noexcept
		{
			if (format == Format::eUndefined)
			{
				return FormatSupport{ .format = format };
			}

			const bool depth	  = is_depth_format(format);
			const bool compressed = detail::is_compressed_format(format);
			const bool integer	  = detail::is_integer_format(format);

			return FormatSupport{
				.format					= format,
				.sampled				= true,
				.storage				= !depth && !compressed,
				.colorAttachment		= !depth && !compressed,
				.depthStencilAttachment = depth,
				.copySrc				= true,
				.copyDst				= true,
				.linearFiltering		= !depth && !integer,
				.blendable				= !depth && !compressed && !integer,
			};
		}

		TextureViewHandle null_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createTextureView");

			auto * device									   = static_cast<null::NullDevice *>(impl);
			const null::NullHandleRecord * const sourceTexture = device->handles.resolve(texture, kHandleAlreadyChecked);
			if (sourceTexture == nullptr)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of an invalid or stale texture handle");
			}

			const TextureSubresourceRange & r = desc.range;
			if (r.mipCount == kAllMips || r.layerCount == kAllLayers)
			{
				return fail_value<TextureViewHandle>(error,
					ErrorCode::eInvalidArgument,
					"kAllMips and kAllLayers are barrier counts, so a texture view has to name how many levels and layers it takes");
			}

			const TextureDesc & source = sourceTexture->desc;
			if (r.baseMip >= source.mipLevels || r.mipCount > source.mipLevels - r.baseMip)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view mip range is outside the source texture");
			}
			if (r.baseLayer >= source.arrayLayers || r.layerCount > source.arrayLayers - r.baseLayer)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view layer range is outside the source texture");
			}

			return mint_created<TextureViewHandle>(device, error);
		}

		QueryPoolHandle null_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createQueryPool");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (desc.queryCount == 0)
			{
				return fail_value<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
			}

			return mint_created<QueryPoolHandle>(device, error);
		}

		BufferHandle null_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPlacedBuffer");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!resolves(device, desc.heap))
			{
				return fail_value<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer names an invalid or stale heap handle");
			}

			return mint_created<BufferHandle>(device, error);
		}

		TextureHandle null_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPlacedTexture");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!resolves(device, desc.heap))
			{
				return fail_value<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture names an invalid or stale heap handle");
			}

			return mint_created<TextureHandle>(device, error);
		}

		PipelineLayoutHandle null_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPipelineLayout");

			auto * device = static_cast<null::NullDevice *>(impl);
			for (const DescriptorSetLayoutHandle set : desc.sets)
			{
				if (!resolves(device, set))
				{
					return fail_value<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
				}
			}

			return mint_created<PipelineLayoutHandle>(device, error);
		}

		GraphicsPipelineHandle null_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createGraphicsPipeline");
			if (desc.vertexInput == nullptr)
			{
				return fail_value<GraphicsPipelineHandle>(error,
					ErrorCode::eUnsupportedFeature,
					"graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
			}

			const VertexInputDesc & vertexInput = *desc.vertexInput;
			if (desc.raster.conservativeRasterEnable && static_cast<null::NullDevice *>(impl)->caps.conservativeRasterTier == ConservativeRasterTier::eNone)
			{
				return fail_value<GraphicsPipelineHandle>(
					error, ErrorCode::eUnsupportedFeature, "conservative rasterization was requested on a device that reports none");
			}

			if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
			}

			if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
			{
				return fail_value<GraphicsPipelineHandle>(
					error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
			}

			if (desc.shaders.empty())
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline has no shader stages");
			}

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!resolves(device, desc.layout))
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidHandle, "graphics pipeline with an invalid layout handle");
			}

			return mint_created<GraphicsPipelineHandle>(device, error);
		}

		ComputePipelineHandle null_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createComputePipeline");

			if (desc.shader.isSource)
			{
				return fail_value<ComputePipelineHandle>(error, ErrorCode::eUnsupportedFormat, "the Null backend has no shader compiler");
			}

			if (!desc.shader.threadgroupSize.is_stated())
			{
				return fail_value<ComputePipelineHandle>(error,
					ErrorCode::eInvalidArgument,
					"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
			}

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!resolves(device, desc.layout))
			{
				return fail_value<ComputePipelineHandle>(error, ErrorCode::eInvalidHandle, "compute pipeline with an invalid layout handle");
			}

			return mint_created<ComputePipelineHandle>(device, error);
		}

		AccelerationStructureHandle null_create_acceleration_structure(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createAccelerationStructure");

			auto * device = static_cast<null::NullDevice *>(impl);

			if (desc.storage.is_valid() && !resolves(device, desc.storage))
			{
				return fail_value<AccelerationStructureHandle>(error, ErrorCode::eInvalidHandle, "acceleration structure with an invalid storage buffer handle");
			}

			return mint_created<AccelerationStructureHandle>(device, error);
		}

		void * null_create_descriptor_arena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createDescriptorArena");

			void * arena = alloc_object(static_cast<null::NullDevice *>(impl), publishing_object<Published<DescriptorArenaApi, &DescriptorArenaBlock>>());
			if (arena == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null descriptor arena allocation failed");
			}

			return return_value(arena, error);
		}

		void * null_create_command_pool(void * impl, [[maybe_unused]] const CommandPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createCommandPool");

			void * pool = alloc_object(static_cast<null::NullDevice *>(impl), publishing_object<Published<CommandPoolApi, &CommandPoolBlock>>());
			if (pool == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null command pool allocation failed");
			}

			return return_value(pool, error);
		}

		void * null_create_swapchain(void * impl, [[maybe_unused]] const SwapchainDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createSwapchain");

			auto * device	 = static_cast<null::NullDevice *>(impl);
			auto * swapchain = static_cast<null::NullObject *>(alloc_object(device, publishing_object<Published<SwapchainApi, &SwapchainBlock>>()));
			if (swapchain == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null swapchain allocation failed");
			}

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			for (std::uint32_t i = 0; i < null::kNullSwapchainImages; ++i)
			{
				swapchain->backBuffers[i]		= mint_handle<TextureHandle>(device, SlotLifetime::eSwapchainBorrowed);
				swapchain->backBufferViews[i]	= mint_handle<TextureViewHandle>(device, SlotLifetime::eSwapchainBorrowed);
				swapchain->presentSemaphores[i] = mint_handle<BinarySemaphoreHandle>(device, SlotLifetime::eSwapchainBorrowed);

				if (!swapchain->backBuffers[i].is_valid() || !swapchain->backBufferViews[i].is_valid() || !swapchain->presentSemaphores[i].is_valid())
				{
					return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null swapchain allocation failed");
				}
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			return return_value(static_cast<void *>(swapchain), error);
		}

		void * null_get_queue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
		{
			auto * device			  = static_cast<null::NullDevice *>(impl);
			const std::uint32_t count = queue_count_for_type(device->caps, type);
			if (index >= count)
			{
				return fail_value<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
			}

			void * queue = alloc_object(device, publishing_object<Published<QueueApi, &QueueBlock>, Published<SparseApi, &SparseBlock>>(), type);
			if (queue == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null queue allocation failed");
			}

			return return_value(queue, error);
		}

		bool null_destroy(void * impl, [[maybe_unused]] ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.destroy");

			auto * device = static_cast<null::NullDevice *>(impl);

			const null::NullHandleRecord * record = device->handles.resolve(type, handle, true);
			if (record == nullptr)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed handle");
			}

			if (record->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eValidationFailed, "destroy of a handle the device lends out and does not own, such as a back buffer");
			}

			static_cast<void>(device->handles.retire(type, handle, true));

			[[maybe_unused]] const std::uint64_t pending = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
			AZO_RHI_PROFILE_PLOT("rhi.null.pendingRetire", static_cast<std::int64_t>(pending));
			return succeed(error);
		}

		bool null_collect_garbage(void * impl, ResourceType type, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.collectGarbage");

			if (type == ResourceType::eBuffer)
			{
				static_cast<null::NullDevice *>(impl)->pendingRetire.store(0, std::memory_order_relaxed);
				AZO_RHI_PROFILE_PLOT("rhi.null.pendingRetire", static_cast<std::int64_t>(0));
			}
			return succeed(error);
		}

		bool null_collect_garbage_timeline(
			void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept
		{
			return null_collect_garbage(impl, type, error);
		}

		TimelineHandle null_adopt_timeline([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTimelineDesc & desc, Error * error) noexcept
		{
			return fail_value<TimelineHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native timeline to adopt");
		}

		BinarySemaphoreHandle null_adopt_binary_semaphore([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api,
			[[maybe_unused]] const void * nativeImport, [[maybe_unused]] const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
		{
			return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native binary semaphore to adopt");
		}

		bool null_get_native_timeline([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TimelineHandle timeline,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native timeline to hand back");
		}

		bool null_get_native_binary_semaphore([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] BinarySemaphoreHandle semaphore,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native binary semaphore to hand back");
		}

		TextureViewHandle null_adopt_texture_view([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTextureViewDesc & desc, Error * error) noexcept
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native texture view to adopt");
		}

		SamplerHandle null_adopt_sampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedSamplerDesc & desc, Error * error) noexcept
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native sampler to adopt");
		}

		bool null_get_native_texture_view([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureViewHandle view,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native texture view to hand back");
		}

		bool null_get_native_sampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] SamplerHandle sampler,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native sampler to hand back");
		}

		BufferHandle null_adopt_buffer(void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.importBuffer");
			return return_value(mint_handle<BufferHandle>(static_cast<null::NullDevice *>(impl)), error);
		}

		TextureHandle null_adopt_texture(void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.importTexture");
			return return_value(mint_handle<TextureHandle>(static_cast<null::NullDevice *>(impl)), error);
		}

		bool null_get_native_buffer([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] BufferHandle buffer,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return succeed(error);
		}

		bool null_get_native_texture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return succeed(error);
		}

		QueueType null_queue_type(void * impl) noexcept
		{
			return static_cast<null::NullObject *>(impl)->queueType;
		}

		bool null_queue_submit(void * impl, const SubmitDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.submit");

			const null::NullDevice * device = static_cast<null::NullObject *>(impl)->owner;

			// Nothing is ever in flight here, so a submitted list is never pending and may go again.
			if (const char * refusal = submit_refusal_for_lists(
					desc.commandLists,
					device->caps.supportsCommandListResubmit,
					[](const CommandList & list)
					{
						return static_cast<const null::NullObject *>(detail::unwrapped_impl_of(list));
					},
					[](const null::NullObject &)
					{
						return false;
					});
				refusal != nullptr)
			{
				return fail(error, ErrorCode::eInvalidState, refusal);
			}

			for (const CommandList * list : desc.commandLists)
			{
				if (list == nullptr)
				{
					continue;
				}

				auto * record	  = static_cast<null::NullObject *>(detail::unwrapped_impl_of(*list));
				record->lifecycle = ListLifecycle::eSubmitted;
			}

			return succeed(error);
		}

		bool null_queue_bind_sparse([[maybe_unused]] void * impl, [[maybe_unused]] const SparseBindDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.bindSparse");
			return succeed(error);
		}

		void * null_command_pool_allocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.commandPool.allocate");

			auto * poolObject		  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = poolObject->owner;

			if (poolObject->handedOut < poolObject->lists.size())
			{
				null::NullObject * recycled = azo::rhi::detail::at(poolObject->lists, poolObject->handedOut);
				++poolObject->handedOut;
				return return_value(static_cast<void *>(recycled), error);
			}

			void * listObject = alloc_object(device,
				publishing_object<Published<RenderCommandApi, &RenderCommandBlock>,
					Published<AliasingCommandApi, &AliasingCommandBlock>,
					Published<RayTracingCommandApi, &RayTracingCommandBlock>,
					Published<QueryCommandApi, &QueryCommandBlock>,
					Published<IndirectApi, &IndirectBlock>,
					Published<IndirectCountApi, &IndirectCountBlock>,
					Published<NativeEscapeApi, &NativeEscapeBlock>>());
			if (listObject == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null command list allocation failed");
			}

			if (!detail::try_push_back(poolObject->lists, static_cast<null::NullObject *>(listObject)))
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null command list allocation failed");
			}
			++poolObject->handedOut;

			return return_value(listObject, error);
		}

		bool null_command_pool_reset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.commandPool.reset");

			auto * poolObject = static_cast<null::NullObject *>(impl);
			for (null::NullObject * listObject : poolObject->lists)
			{
				listObject->lifecycle = ListLifecycle::eFresh;
			}
			poolObject->handedOut = 0;

			return succeed(error);
		}

		bool null_begin_native_mutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
		{
			if (api != NullApi::kId)
			{
				return fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
			}

			return succeed(error);
		}

		bool null_command_list_begin(void * impl, Error * error) noexcept
		{
			static_cast<null::NullObject *>(impl)->lifecycle = ListLifecycle::eRecording;
			return succeed(error);
		}

		bool null_command_list_end(void * impl, Error * error) noexcept
		{
			static_cast<null::NullObject *>(impl)->lifecycle = ListLifecycle::eEnded;
			return succeed(error);
		}

		bool null_command_list_barriers([[maybe_unused]] void * impl, [[maybe_unused]] const BarrierBatch & barriers, Error * error) noexcept
		{
			return succeed(error);
		}

		DescriptorSetHandle null_arena_allocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.descriptorArena.allocate");

			auto * arena			  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = arena->owner;
			if (!resolves(device, desc.layout))
			{
				return fail_value<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocated from an invalid or stale layout handle");
			}

			const auto handle = device->handles.store<DescriptorSetHandle>(null::NullHandleRecord{});
			if (!handle.is_valid())
			{
				return fail_value<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Null descriptor set allocation failed");
			}

			return return_value(handle, error);
		}

		bool null_arena_reset([[maybe_unused]] void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.descriptorArena.reset");
			return succeed(error);
		}

		bool null_bind_descriptor_set([[maybe_unused]] void * impl, [[maybe_unused]] PipelineLayoutHandle layout, [[maybe_unused]] std::uint32_t setIndex,
			[[maybe_unused]] DescriptorSetHandle set, [[maybe_unused]] std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
		{
			return succeed(error);
		}

		bool null_command_list_build_acceleration_structures(
			[[maybe_unused]] void * impl, [[maybe_unused]] std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept
		{
			return succeed(error);
		}

		AcquireResult null_acquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return return_value(
				AcquireResult{
					.status		= SwapchainStatus::eOk,
					.imageIndex = 0,
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					.imageAvailable = swapchain->presentSemaphores[0],
				},
				error);
		}

		PresentResult null_present([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t imageIndex,
			[[maybe_unused]] BinarySemaphoreHandle renderFinished, [[maybe_unused]] void * queueImpl, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.present");
			return return_value(PresentResult{ .status = SwapchainStatus::eOk }, error);
		}

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		TextureHandle null_swapchain_back_buffer(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->backBuffers[imageIndex] : TextureHandle{};
		}

		TextureViewHandle null_swapchain_back_buffer_view(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->backBufferViews[imageIndex] : TextureViewHandle{};
		}

		BinarySemaphoreHandle null_swapchain_present_semaphore(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->presentSemaphores[imageIndex] : BinarySemaphoreHandle{};
		}

		// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		Format null_swapchain_format([[maybe_unused]] void * impl) noexcept
		{
			return Format::eBGRA8UNorm;
		}

		PresentMode null_swapchain_present_mode([[maybe_unused]] void * impl) noexcept
		{
			return PresentMode::eFifo;
		}

		bool null_swapchain_supports_readback([[maybe_unused]] void * impl) noexcept
		{
			return false;
		}

		std::uint32_t null_swapchain_image_count([[maybe_unused]] void * impl) noexcept
		{
			return 3;
		}

		std::uint32_t null_swapchain_extent([[maybe_unused]] void * impl) noexcept
		{
			return 1;
		}

		GraphicsApiId null_instance_api_id([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::kId;
		}

		bool null_enumerate_adapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
			}

			*out = 0;

			if (!adapters.empty())
			{
				adapters.front() = AdapterInfo{
					.type					   = AdapterType::eCpu,
					.apiId					   = NullApi::kId,
					.unifiedMemoryArchitecture = true,
					.name					   = "Null Adapter",
				};
			}

			*out = 1;
			return succeed(error);
		}

		void * null_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept
		{
			null::NullDevice * device = make_owned_device(static_cast<null::NullInstance *>(impl), desc, error);
			if (device == nullptr)
			{
				return nullptr;
			}

			return return_value(static_cast<void *>(device), error);
		}

		void * null_create_instance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
		{
			null::NullInstance * instance = make_owned_instance();
			if (instance == nullptr)
			{
				return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Null instance creation failed");
			}

			return return_value(static_cast<void *>(instance), error);
		}

		const CoreDeviceApi & CoreDeviceBlock() noexcept
		{
			static const CoreDeviceApi block{
				.getGraphicsApiId			= &null_device_api_id,
				.getGraphicsApiName			= &null_device_api_name,
				.createBuffer				= &null_create_buffer,
				.createTexture				= &null_create_texture,
				.createTextureView			= &null_create_texture_view,
				.createSampler				= &null_create_handle<SamplerHandle>,
				.createDescriptorSetLayout	= &null_create_handle<DescriptorSetLayoutHandle>,
				.createPipelineLayout		= &null_create_pipeline_layout,
				.createGraphicsPipeline		= &null_create_graphics_pipeline,
				.createComputePipeline		= &null_create_compute_pipeline,
				.createTimeline				= &null_create_exportable<TimelineHandle, TimelineDesc>,
				.createBinarySemaphore		= &null_create_exportable<BinarySemaphoreHandle, BinarySemaphoreDesc>,
				.createDescriptorArena		= &null_create_descriptor_arena,
				.createCommandPool			= &null_create_command_pool,
				.getQueue					= &null_get_queue,
				.map						= &null_map,
				.unmap						= &null_unmap,
				.flushMappedRange			= &noop_void,
				.invalidateMappedRange		= &noop_void,
				.updateDescriptorsBuffer	= &noop_void,
				.updateDescriptorsTexture	= &noop_void,
				.updateDescriptorsSampler	= &noop_void,
				.getCaps					= &null_device_caps,
				.getFormatSupport			= &null_device_format_support,
				.getAdapterInfo				= &null_device_adapter_info,
				.getValidationMessageCounts = &null_device_validation_message_counts,
				.destroy					= &null_destroy,
				.collectGarbage				= &null_collect_garbage,
				.collectGarbageTimeline		= &null_collect_garbage_timeline,
				.destroyDevice				= &null_destroy_device,
			};

			return block;
		}

		const PresentApi & PresentBlock() noexcept
		{
			static const PresentApi block{
				.createSwapchain = &null_create_swapchain,
			};

			return block;
		}

		const PlacedMemoryApi & PlacedMemoryBlock() noexcept
		{
			static const PlacedMemoryApi block{
				.createHeap			  = &null_create_exportable<HeapHandle, HeapDesc>,
				.createPlacedBuffer	  = &null_create_placed_buffer,
				.createPlacedTexture  = &null_create_placed_texture,
				.getTextureMemoryInfo = &null_default<MemoryInfo>,
				.getBufferMemoryInfo  = &null_default<MemoryInfo>,
			};

			return block;
		}

		const RayTracingApi & RayTracingBlock() noexcept
		{
			static const RayTracingApi block{
				.createRayTracingPipeline				= &null_create_handle<RayTracingPipelineHandle>,
				.createAccelerationStructure			= &null_create_acceleration_structure,
				.updateDescriptorsAccelerationStructure = &noop_void,
			};

			return block;
		}

		const ResourceIntrospectionApi & resource_introspection_block() noexcept
		{
			static const ResourceIntrospectionApi block{
				.getTextureInfo = &null_get_texture_info,
				.getBufferInfo	= &null_get_buffer_info,
			};

			return block;
		}

		const QueryApi & QueryBlock() noexcept
		{
			static const QueryApi block{
				.createQueryPool	= &null_create_query_pool,
				.calibrateTimestamp = &null_default<TimestampCalibration>,
			};

			return block;
		}

		const PipelineCacheApi & PipelineCacheBlock() noexcept
		{
			static const PipelineCacheApi block{
				.createPipelineCache  = &null_create_handle<PipelineCacheHandle>,
				.getPipelineCacheData = &null_default<PipelineCacheData>,
			};

			return block;
		}

		const ResidencyApi & ResidencyBlock() noexcept
		{
			static const ResidencyApi block{
				.queryMemoryBudget	  = &null_default<MemoryBudgetInfo>,
				.setResidencyPriority = &noop_void,
			};

			return block;
		}

		const AdoptionApi & AdoptionBlock() noexcept
		{
			static const AdoptionApi block{
				.adoptBuffer			  = &null_adopt_buffer,
				.adoptTexture			  = &null_adopt_texture,
				.getNativeBuffer		  = &null_get_native_buffer,
				.getNativeTexture		  = &null_get_native_texture,
				.adoptTextureView		  = &null_adopt_texture_view,
				.adoptSampler			  = &null_adopt_sampler,
				.getNativeTextureView	  = &null_get_native_texture_view,
				.getNativeSampler		  = &null_get_native_sampler,
				.adoptTimeline			  = &null_adopt_timeline,
				.adoptBinarySemaphore	  = &null_adopt_binary_semaphore,
				.getNativeTimeline		  = &null_get_native_timeline,
				.getNativeBinarySemaphore = &null_get_native_binary_semaphore,
			};

			return block;
		}

		const InstanceApi & InstanceBlock() noexcept
		{
			static const InstanceApi block{
				.getGraphicsApiId  = &null_instance_api_id,
				.enumerateAdapters = &null_enumerate_adapters,
				.createDevice	   = &null_instance_create_device,
				.destroyInstance   = &null_destroy_instance,
			};

			return block;
		}

		const QueueApi & QueueBlock() noexcept
		{
			static const QueueApi block{
				.getType		   = &null_queue_type,
				.submit			   = &null_queue_submit,
				.waitIdle		   = &noop_void,
				.getCompletedValue = &null_default<std::uint64_t>,
				.wait			   = &noop_void,
				.signal			   = &noop_void,
				.beginDebugLabel   = &noop_void,
				.endDebugLabel	   = &noop_void,
			};

			return block;
		}

		const SparseApi & SparseBlock() noexcept
		{
			static const SparseApi block{
				.bindSparse = &null_queue_bind_sparse,
			};

			return block;
		}

		const CommandPoolApi & CommandPoolBlock() noexcept
		{
			static const CommandPoolApi block{
				.allocate = &null_command_pool_allocate,
				.reset	  = &null_command_pool_reset,
			};

			return block;
		}

		const RenderCommandApi & RenderCommandBlock() noexcept
		{
			static const RenderCommandApi block{
				.begin				 = &null_command_list_begin,
				.end				 = &null_command_list_end,
				.barriers			 = &null_command_list_barriers,
				.beginRendering		 = &noop_void,
				.endRendering		 = &noop_void,
				.setGraphicsPipeline = &noop_void,
				.setComputePipeline	 = &noop_void,
				.bindDescriptorSet	 = &null_bind_descriptor_set,
				.pushConstants		 = &noop_void,
				.setViewport		 = &noop_void,
				.setScissor			 = &noop_void,
				.setBlendConstants	 = &noop_void,
				.setStencilReference = &noop_void,
				.setDepthBias		 = &noop_void,
				.setVertexBuffer	 = &noop_void,
				.setIndexBuffer		 = &noop_void,
				.draw				 = &noop_void,
				.drawIndexed		 = &noop_void,
				.dispatch			 = &noop_void,
				.copyBuffer			 = &noop_void,
				.copyBufferToTexture = &noop_void,
				.copyTextureToBuffer = &noop_void,
				.copyTexture		 = &noop_void,
				.clearBuffer		 = &noop_void,
				.clearTexture		 = &noop_void,
				.resolveTexture		 = &noop_void,
				.blit				 = &noop_void,
				.generateMips		 = &noop_void,
				.beginDebugLabel	 = &noop_void,
				.endDebugLabel		 = &noop_void,
			};

			return block;
		}

		bool null_alias_barriers(void * impl, const std::span<const AliasBarrier> barriers, Error * error) noexcept
		{
			auto * list				  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = list->owner;

			for (const AliasBarrier & barrier : barriers)
			{
				if ((barrier.beforeBuffer.is_valid() && device->handles.resolve(barrier.beforeBuffer, true) == nullptr) ||
					(barrier.afterBuffer.is_valid() && device->handles.resolve(barrier.afterBuffer, true) == nullptr) ||
					(barrier.beforeTexture.is_valid() && device->handles.resolve(barrier.beforeTexture, true) == nullptr) ||
					(barrier.afterTexture.is_valid() && device->handles.resolve(barrier.afterTexture, true) == nullptr))
				{
					return fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
				}
			}

			return succeed(error);
		}

		const AliasingCommandApi & AliasingCommandBlock() noexcept
		{
			static const AliasingCommandApi block{
				.aliasBarriers = &null_alias_barriers,
			};

			return block;
		}

		const RayTracingCommandApi & RayTracingCommandBlock() noexcept
		{
			static const RayTracingCommandApi block{
				.setRayTracingPipeline		  = &noop_void,
				.buildAccelerationStructures  = &null_command_list_build_acceleration_structures,
				.copyAccelerationStructure	  = &noop_void,
				.compactAccelerationStructure = &noop_void,
				.traceRays					  = &noop_void,
			};

			return block;
		}

		const QueryCommandApi & QueryCommandBlock() noexcept
		{
			static const QueryCommandApi block{
				.resetQueryPool	  = &noop_void,
				.writeTimestamp	  = &noop_void,
				.beginQuery		  = &noop_void,
				.endQuery		  = &noop_void,
				.resolveQueryData = &noop_void,
			};

			return block;
		}

		const IndirectApi & IndirectBlock() noexcept
		{
			static const IndirectApi block{
				.drawIndirect		 = &noop_void,
				.drawIndexedIndirect = &noop_void,
				.dispatchIndirect	 = &noop_void,
			};

			return block;
		}

		const IndirectCountApi & IndirectCountBlock() noexcept
		{
			static const IndirectCountApi block{
				.drawIndirectCount		  = &noop_void,
				.drawIndexedIndirectCount = &noop_void,
			};

			return block;
		}

		const NativeEscapeApi & NativeEscapeBlock() noexcept
		{
			static const NativeEscapeApi block{
				.beginNativeMutation = &null_begin_native_mutation,
				.endNativeMutation	 = &noop_void,
			};

			return block;
		}

		const SwapchainApi & SwapchainBlock() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage			 = &null_acquire,
				.present					 = &null_present,
				.getBackBuffer				 = &null_swapchain_back_buffer,
				.getBackBufferView			 = &null_swapchain_back_buffer_view,
				.getPerImagePresentSemaphore = &null_swapchain_present_semaphore,
				.getFormat					 = &null_swapchain_format,
				.getPresentMode				 = &null_swapchain_present_mode,
				.getImageCount				 = &null_swapchain_image_count,
				.getWidth					 = &null_swapchain_extent,
				.getHeight					 = &null_swapchain_extent,
				.resize						 = &noop_void,
				.setPresentMode				 = &noop_void,
				.supportsReadback			 = &null_swapchain_supports_readback,
			};

			return block;
		}

		const DescriptorArenaApi & DescriptorArenaBlock() noexcept
		{
			static const DescriptorArenaApi block{
				.allocate = &null_arena_allocate,
				.reset	  = &null_arena_reset,
			};

			return block;
		}

	}

	Result<void> register_null_backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName		   = NullApi::kCanonicalName;
		info.info.displayName		   = NullApi::kDisplayName;
		info.info.apiVersionMajor	   = 1;
		info.info.supportsSurfaces	   = true;
		info.info.supportsDebugMarkers = true;
		info.createInstance			   = &null_create_instance;

		return registry.Register<NullApi>(info);
	}

	template <>
	Result<UniqueDevice> create_device<NullApi>(const DeviceDesc & desc)
	{
		if (const Result<void> checked = detail::check_device_desc(desc); !checked)
		{
			return checked.get_error();
		}

		Error error{};

		null::NullDevice * device = make_owned_device(nullptr, desc, &error);
		if (device == nullptr)
		{
			return error;
		}

		void * deviceImpl		 = device;
		BackendBlockSet * blocks = detail::resolve_device_blocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::make_unique_device(deviceImpl, blocks);
	}

}

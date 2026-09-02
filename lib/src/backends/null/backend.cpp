// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/profiling.hpp"

#include "backends/null/internal.hpp"
#include "backends/registration.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace azo::rhi
{
	namespace
	{

		const CoreDeviceApi & CoreDeviceBlock() noexcept;
		const PresentApi & PresentBlock() noexcept;
		const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
		const RayTracingApi & RayTracingBlock() noexcept;
		const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
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
		[[nodiscard]] HandleT MintHandle(null::NullDevice * device, const SlotLifetime lifetime = SlotLifetime::eOwned)
		{
			return device->handles.Store<HandleT>(null::NullHandleRecord{ .lifetime = lifetime });
		}

		template <typename HandleT>
		[[nodiscard]] bool Resolves(null::NullDevice * device, HandleT handle) noexcept
		{
			return device->handles.Resolve(handle, kHandleAlreadyChecked) != nullptr;
		}

		[[nodiscard]] void * AllocObject(null::NullDevice * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics)
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

		[[nodiscard]] NullBackendOwner & Owner()
		{
			static NullBackendOwner owner;
			return owner;
		}

		[[nodiscard]] null::NullDevice * MakeOwnedDevice(null::NullInstance * instance, const DeviceDesc & desc, Error * error)
		{
			const QueuePlan plan = PlanQueues(desc.queues);
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

			auto device = HostNew<null::NullDevice>();
			if (device == nullptr)
			{
				return nullptr;
			}

			device->object					= PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
				Published<PresentApi, &PresentBlock>,
				Published<PlacedMemoryApi, &PlacedMemoryBlock>,
				Published<RayTracingApi, &RayTracingBlock>,
				Published<QueryApi, &QueryBlock>,
				Published<PipelineCacheApi, &PipelineCacheBlock>,
				Published<ResidencyApi, &ResidencyBlock>,
				Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
				Published<AdoptionApi, &AdoptionBlock>>();
			device->instanceWrapper			= instance;
			device->validation				= desc.validation;
			device->caps.apiId				= NullApi::id;
			device->caps.shaderBinaryFormat = ShaderBinaryFormat::eBackendNative;
			device->caps.graphicsQueueCount = plan.graphicsCount;
			device->caps.computeQueueCount	= plan.computeCount;
			device->caps.copyQueueCount		= plan.copyCount;

			device->caps.hasDedicatedComputeQueue = false;

			device->caps.hasDedicatedTransferQueue = false;

			device->caps.supportsTextureViewSwizzle = true;

			device->caps.supportsMultiPlanarFormats = true;

			device->caps.supportsScaledBlit = true;

			device->caps.supportsTimestampWritesInScope = true;

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
			device->adapter.apiId					  = NullApi::id;
			device->adapter.name					  = "Null Adapter";
			device->adapter.unifiedMemoryArchitecture = true;

			null::NullDevice * raw	 = device.get();
			NullBackendOwner & owner = Owner();

			std::uint32_t deviceTag = 0;
			if (!detail::DeviceTags().Acquire(deviceTag))
			{
				return nullptr;
			}
			raw->deviceTag = deviceTag;
			raw->handles.Rebind(deviceTag);

			if (!detail::TryPushBack(owner.devices, std::move(device)))
			{
				detail::DeviceTags().Release(deviceTag);
				return nullptr;
			}

			return raw;
		}

		[[nodiscard]] null::NullInstance * MakeOwnedInstance()
		{
			auto instance = HostNew<null::NullInstance>();
			if (instance == nullptr)
			{
				return nullptr;
			}

			instance->object = PublishingObject<Published<InstanceApi, &InstanceBlock>>();

			null::NullInstance * raw = instance.get();
			NullBackendOwner & owner = Owner();
			if (!detail::TryPushBack(owner.instances, std::move(instance)))
			{
				return nullptr;
			}

			return raw;
		}

		void NullDestroyDevice(void * impl) noexcept
		{
			NullBackendOwner & owner = Owner();

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
			detail::DeviceTags().Release(releasedTag);

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

		void NullDestroyInstance(void * impl) noexcept
		{
			NullBackendOwner & owner = Owner();
			std::erase_if(owner.instances,
				[impl](const HostUniquePtr<null::NullInstance> & instance)
				{
					return instance.get() == impl;
				});
		}

		bool Succeed(Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = {};
			}

			return true;
		}

		bool Fail(Error * error, ErrorCode code, const char * message) noexcept
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
		[[nodiscard]] bool Store(T * out, T value, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "backend output pointer is null");
			}

			*out = std::move(value);
			return Succeed(error);
		}

		template <typename T>
		[[nodiscard]] T ReturnValue(T value, Error * error) noexcept
		{
			Succeed(error);
			return value;
		}

		template <typename T>
		[[nodiscard]] T FailValue(Error * error, ErrorCode code, const char * message) noexcept
		{
			Fail(error, code, message);
			return {};
		}

		template <typename... Args>
		[[nodiscard]] Error * LastError(Args &&... args) noexcept
		{
			static_assert(sizeof...(Args) > 0);
			auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
			return std::get<sizeof...(Args) - 1>(tuple);
		}

		template <typename T, typename... Args>
		[[nodiscard]] T * OutputBeforeError(Args &&... args) noexcept
		{
			static_assert(sizeof...(Args) > 1);
			auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
			return std::get<sizeof...(Args) - 2>(tuple);
		}

		template <typename... Args>
		bool NoopVoid([[maybe_unused]] void * impl, Args... args) noexcept
		{
			return Succeed(LastError(args...));
		}

		template <typename HandleT>
		[[nodiscard]] HandleT MintCreated(null::NullDevice * device, Error * error) noexcept
		{
			const HandleT handle = MintHandle<HandleT>(device);
			if (!handle.IsValid())
			{
				return FailValue<HandleT>(error, ErrorCode::eOutOfHostMemory, "Null backend handle allocation failed");
			}

			return ReturnValue(handle, error);
		}

		template <typename HandleT, typename... Args>
		HandleT NullCreateHandle(void * impl, Args... args) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			return MintCreated<HandleT>(static_cast<null::NullDevice *>(impl), LastError(args...));
		}

		template <typename HandleT, typename DescT>
		HandleT NullCreateExportable(void * impl, const DescT & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			if (!desc.exportableHandleTypes.Empty())
			{
				return FailValue<HandleT>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			return MintCreated<HandleT>(static_cast<null::NullDevice *>(impl), error);
		}

		TextureHandle NullCreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (!desc.exportableHandleTypes.Empty())
			{
				return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}

			if (desc.mipLevels > detail::MaxMipLevels(desc.width, desc.height, desc.type == TextureType::eTex3D ? desc.depth : 1))
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}

			const TextureHandle handle = MintCreated<TextureHandle>(device, error);
			if (!handle.IsValid())
			{
				return handle;
			}

			if (null::NullHandleRecord * record = device->handles.Resolve(handle, kHandleAlreadyChecked); record != nullptr)
			{
				record->desc = detail::Recorded(desc);
			}

			return handle;
		}

		bool NullGetTextureInfo(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.getTextureInfo");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
			}

			const null::NullHandleRecord * const record = device->handles.Resolve(texture, false);
			if (record == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
			}

			if (record->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
			}

			*out = TextureInfo{ .desc = record->desc };
			return true;
		}

		BufferHandle NullCreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.create");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (!desc.exportableHandleTypes.Empty())
			{
				return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend exports nothing, so nothing it creates is exportable");
			}

			if (desc.size == 0)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}

			const BufferHandle handle = MintCreated<BufferHandle>(device, error);
			if (!handle.IsValid())
			{
				return handle;
			}

			if (null::NullHandleRecord * record = device->handles.Resolve(handle, kHandleAlreadyChecked); record != nullptr)
			{
				record->bufferDesc = detail::Recorded(desc);
			}

			return handle;
		}

		bool NullGetBufferInfo(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.getBufferInfo");
			auto * device = static_cast<null::NullDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
			}

			const null::NullHandleRecord * const record = device->handles.Resolve(buffer, false);
			if (record == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
			}

			const MemoryAccess access = record->bufferDesc.memory == MemoryUsage::eGpuOnly || record->bufferDesc.memory == MemoryUsage::eTransient ||
												record->bufferDesc.memory == MemoryUsage::eReserved
											? MemoryAccess::eGpuOnly
											: MemoryAccess::eCpuVisibleCoherent;

			*out = BufferInfo{ .desc = record->bufferDesc, .memoryAccess = access };
			return true;
		}

		template <typename T, typename... Args>
		bool NullDefault([[maybe_unused]] void * impl, Args... args) noexcept
		{
			T * out = OutputBeforeError<T>(args...);
			if (out == nullptr)
			{
				return Fail(LastError(args...), ErrorCode::eInvalidArgument, "operation called with a null output");
			}

			return Store(out, T{}, LastError(args...));
		}

		MappedMemory NullMap([[maybe_unused]] void * impl, [[maybe_unused]] BufferHandle buffer, [[maybe_unused]] const MapDesc & desc, Error * error) noexcept
		{
			Fail(error, ErrorCode::eUnsupportedFeature, "Null backend does not expose mapped memory");
			return {};
		}

		GraphicsApiId NullDeviceApiId([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::id;
		}

		std::string_view NullDeviceApiName([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::displayName;
		}

		const DeviceCaps & NullDeviceCaps(void * impl) noexcept
		{
			return static_cast<null::NullDevice *>(impl)->caps;
		}

		const AdapterInfo & NullDeviceAdapterInfo(void * impl) noexcept
		{
			return static_cast<null::NullDevice *>(impl)->adapter;
		}

		ValidationMessageCounts NullDeviceValidationMessageCounts([[maybe_unused]] void * impl) noexcept
		{
			return {};
		}

		FormatSupport NullDeviceFormatSupport([[maybe_unused]] void * impl, const Format format) noexcept
		{
			if (format == Format::eUndefined)
			{
				return FormatSupport{ .format = format };
			}

			const bool depth	  = IsDepthFormat(format);
			const bool compressed = detail::IsCompressedFormat(format);
			const bool integer	  = detail::IsIntegerFormat(format);

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

		TextureViewHandle NullCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createTextureView");

			auto * device									   = static_cast<null::NullDevice *>(impl);
			const null::NullHandleRecord * const sourceTexture = device->handles.Resolve(texture, kHandleAlreadyChecked);
			if (sourceTexture == nullptr)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of an invalid or stale texture handle");
			}

			const TextureSubresourceRange & r = desc.range;
			if (r.mipCount == kAllMips || r.layerCount == kAllLayers)
			{
				return FailValue<TextureViewHandle>(error,
					ErrorCode::eInvalidArgument,
					"kAllMips and kAllLayers are barrier counts, so a texture view has to name how many levels and layers it takes");
			}

			const TextureDesc & source = sourceTexture->desc;
			if (r.baseMip >= source.mipLevels || r.mipCount > source.mipLevels - r.baseMip)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view mip range is outside the source texture");
			}
			if (r.baseLayer >= source.arrayLayers || r.layerCount > source.arrayLayers - r.baseLayer)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view layer range is outside the source texture");
			}

			return MintCreated<TextureViewHandle>(device, error);
		}

		QueryPoolHandle NullCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createQueryPool");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (desc.queryCount == 0)
			{
				return FailValue<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
			}

			return MintCreated<QueryPoolHandle>(device, error);
		}

		BufferHandle NullCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPlacedBuffer");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!Resolves(device, desc.heap))
			{
				return FailValue<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer names an invalid or stale heap handle");
			}

			return MintCreated<BufferHandle>(device, error);
		}

		TextureHandle NullCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPlacedTexture");

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!Resolves(device, desc.heap))
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture names an invalid or stale heap handle");
			}

			return MintCreated<TextureHandle>(device, error);
		}

		PipelineLayoutHandle NullCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createPipelineLayout");

			auto * device = static_cast<null::NullDevice *>(impl);
			for (const DescriptorSetLayoutHandle set : desc.sets)
			{
				if (!Resolves(device, set))
				{
					return FailValue<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
				}
			}

			return MintCreated<PipelineLayoutHandle>(device, error);
		}

		GraphicsPipelineHandle NullCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createGraphicsPipeline");
			if (desc.vertexInput == nullptr)
			{
				return FailValue<GraphicsPipelineHandle>(error,
					ErrorCode::eUnsupportedFeature,
					"graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
			}

			const VertexInputDesc & vertexInput = *desc.vertexInput;
			if (desc.raster.conservativeRasterEnable && static_cast<null::NullDevice *>(impl)->caps.conservativeRasterTier == ConservativeRasterTier::eNone)
			{
				return FailValue<GraphicsPipelineHandle>(
					error, ErrorCode::eUnsupportedFeature, "conservative rasterization was requested on a device that reports none");
			}

			if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
			}

			if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
			{
				return FailValue<GraphicsPipelineHandle>(
					error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
			}

			if (desc.shaders.empty())
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline has no shader stages");
			}

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!Resolves(device, desc.layout))
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidHandle, "graphics pipeline with an invalid layout handle");
			}

			return MintCreated<GraphicsPipelineHandle>(device, error);
		}

		ComputePipelineHandle NullCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createComputePipeline");

			if (desc.shader.isSource)
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eUnsupportedFormat, "the Null backend has no shader compiler");
			}

			if (!desc.shader.threadgroupSize.IsStated())
			{
				return FailValue<ComputePipelineHandle>(error,
					ErrorCode::eInvalidArgument,
					"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
			}

			auto * device = static_cast<null::NullDevice *>(impl);
			if (!Resolves(device, desc.layout))
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eInvalidHandle, "compute pipeline with an invalid layout handle");
			}

			return MintCreated<ComputePipelineHandle>(device, error);
		}

		AccelerationStructureHandle NullCreateAccelerationStructure(void * impl, const AccelerationStructureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createAccelerationStructure");

			auto * device = static_cast<null::NullDevice *>(impl);

			if (desc.storage.IsValid() && !Resolves(device, desc.storage))
			{
				return FailValue<AccelerationStructureHandle>(error, ErrorCode::eInvalidHandle, "acceleration structure with an invalid storage buffer handle");
			}

			return MintCreated<AccelerationStructureHandle>(device, error);
		}

		void * NullCreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createDescriptorArena");

			void * arena = AllocObject(static_cast<null::NullDevice *>(impl), PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>());
			if (arena == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null descriptor arena allocation failed");
			}

			return ReturnValue(arena, error);
		}

		void * NullCreateCommandPool(void * impl, [[maybe_unused]] const CommandPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createCommandPool");

			void * pool = AllocObject(static_cast<null::NullDevice *>(impl), PublishingObject<Published<CommandPoolApi, &CommandPoolBlock>>());
			if (pool == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null command pool allocation failed");
			}

			return ReturnValue(pool, error);
		}

		void * NullCreateSwapchain(void * impl, [[maybe_unused]] const SwapchainDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.createSwapchain");

			auto * device	 = static_cast<null::NullDevice *>(impl);
			auto * swapchain = static_cast<null::NullObject *>(AllocObject(device, PublishingObject<Published<SwapchainApi, &SwapchainBlock>>()));
			if (swapchain == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null swapchain allocation failed");
			}

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			for (std::uint32_t i = 0; i < null::kNullSwapchainImages; ++i)
			{
				swapchain->backBuffers[i]		= MintHandle<TextureHandle>(device, SlotLifetime::eSwapchainBorrowed);
				swapchain->backBufferViews[i]	= MintHandle<TextureViewHandle>(device, SlotLifetime::eSwapchainBorrowed);
				swapchain->presentSemaphores[i] = MintHandle<BinarySemaphoreHandle>(device, SlotLifetime::eSwapchainBorrowed);

				if (!swapchain->backBuffers[i].IsValid() || !swapchain->backBufferViews[i].IsValid() || !swapchain->presentSemaphores[i].IsValid())
				{
					return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null swapchain allocation failed");
				}
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			return ReturnValue(static_cast<void *>(swapchain), error);
		}

		void * NullGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
		{
			auto * device			  = static_cast<null::NullDevice *>(impl);
			const std::uint32_t count = QueueCountForType(device->caps, type);
			if (index >= count)
			{
				return FailValue<void *>(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
			}

			void * queue = AllocObject(device, PublishingObject<Published<QueueApi, &QueueBlock>, Published<SparseApi, &SparseBlock>>(), type);
			if (queue == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null queue allocation failed");
			}

			return ReturnValue(queue, error);
		}

		bool NullDestroy(void * impl, [[maybe_unused]] ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.destroy");

			auto * device = static_cast<null::NullDevice *>(impl);

			const null::NullHandleRecord * record = device->handles.Resolve(type, handle, true);
			if (record == nullptr)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a stale, foreign, or already destroyed handle");
			}

			if (record->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eValidationFailed, "destroy of a handle the device lends out and does not own, such as a back buffer");
			}

			static_cast<void>(device->handles.Retire(type, handle, true));

			[[maybe_unused]] const std::uint64_t pending = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
			AZO_RHI_PROFILE_PLOT("rhi.null.pendingRetire", static_cast<std::int64_t>(pending));
			return Succeed(error);
		}

		bool NullCollectGarbage(void * impl, ResourceType type, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.collectGarbage");

			if (type == ResourceType::eBuffer)
			{
				static_cast<null::NullDevice *>(impl)->pendingRetire.store(0, std::memory_order_relaxed);
				AZO_RHI_PROFILE_PLOT("rhi.null.pendingRetire", static_cast<std::int64_t>(0));
			}
			return Succeed(error);
		}

		bool NullCollectGarbageTimeline(
			void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept
		{
			return NullCollectGarbage(impl, type, error);
		}

		TimelineHandle NullAdoptTimeline([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTimelineDesc & desc, Error * error) noexcept
		{
			return FailValue<TimelineHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native timeline to adopt");
		}

		BinarySemaphoreHandle NullAdoptBinarySemaphore([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api,
			[[maybe_unused]] const void * nativeImport, [[maybe_unused]] const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept
		{
			return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native binary semaphore to adopt");
		}

		bool NullGetNativeTimeline([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TimelineHandle timeline,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native timeline to hand back");
		}

		bool NullGetNativeBinarySemaphore([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] BinarySemaphoreHandle semaphore,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native binary semaphore to hand back");
		}

		TextureViewHandle NullAdoptTextureView([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTextureViewDesc & desc, Error * error) noexcept
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native texture view to adopt");
		}

		SamplerHandle NullAdoptSampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedSamplerDesc & desc, Error * error) noexcept
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native sampler to adopt");
		}

		bool NullGetNativeTextureView([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureViewHandle view,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native texture view to hand back");
		}

		bool NullGetNativeSampler([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] SamplerHandle sampler,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "the Null backend has no native sampler to hand back");
		}

		BufferHandle NullAdoptBuffer(void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.importBuffer");
			return ReturnValue(MintHandle<BufferHandle>(static_cast<null::NullDevice *>(impl)), error);
		}

		TextureHandle NullAdoptTexture(void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
			[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.importTexture");
			return ReturnValue(MintHandle<TextureHandle>(static_cast<null::NullDevice *>(impl)), error);
		}

		bool NullGetNativeBuffer([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] BufferHandle buffer,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Succeed(error);
		}

		bool NullGetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
			[[maybe_unused]] void * outNativeImport, Error * error) noexcept
		{
			return Succeed(error);
		}

		QueueType NullQueueType(void * impl) noexcept
		{
			return static_cast<null::NullObject *>(impl)->queueType;
		}

		bool NullQueueSubmit([[maybe_unused]] void * impl, [[maybe_unused]] const SubmitDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.submit");
			return Succeed(error);
		}

		bool NullQueueBindSparse([[maybe_unused]] void * impl, [[maybe_unused]] const SparseBindDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.bindSparse");
			return Succeed(error);
		}

		void * NullCommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.commandPool.allocate");

			auto * poolObject		  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = poolObject->owner;

			if (poolObject->handedOut < poolObject->lists.size())
			{
				null::NullObject * recycled = poolObject->lists[poolObject->handedOut];
				++poolObject->handedOut;
				return ReturnValue(static_cast<void *>(recycled), error);
			}

			void * listObject = AllocObject(device,
				PublishingObject<Published<RenderCommandApi, &RenderCommandBlock>,
					Published<AliasingCommandApi, &AliasingCommandBlock>,
					Published<RayTracingCommandApi, &RayTracingCommandBlock>,
					Published<QueryCommandApi, &QueryCommandBlock>,
					Published<IndirectApi, &IndirectBlock>,
					Published<IndirectCountApi, &IndirectCountBlock>,
					Published<NativeEscapeApi, &NativeEscapeBlock>>());
			if (listObject == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null command list allocation failed");
			}

			if (!detail::TryPushBack(poolObject->lists, static_cast<null::NullObject *>(listObject)))
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null command list allocation failed");
			}
			++poolObject->handedOut;

			return ReturnValue(listObject, error);
		}

		bool NullCommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.commandPool.reset");

			static_cast<null::NullObject *>(impl)->handedOut = 0;

			return Succeed(error);
		}

		bool NullBeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept
		{
			if (api != NullApi::id)
			{
				return Fail(error, ErrorCode::eUnsupportedApi, "native mutation API does not match the device backend");
			}

			return Succeed(error);
		}

		bool NullCommandListBegin([[maybe_unused]] void * impl, Error * error) noexcept
		{
			return Succeed(error);
		}

		bool NullCommandListEnd([[maybe_unused]] void * impl, Error * error) noexcept
		{
			return Succeed(error);
		}

		bool NullCommandListBarriers([[maybe_unused]] void * impl, [[maybe_unused]] const BarrierBatch & barriers, Error * error) noexcept
		{
			return Succeed(error);
		}

		DescriptorSetHandle NullArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.descriptorArena.allocate");

			auto * arena			  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = arena->owner;
			if (!Resolves(device, desc.layout))
			{
				return FailValue<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocated from an invalid or stale layout handle");
			}

			const DescriptorSetHandle handle = device->handles.Store<DescriptorSetHandle>(null::NullHandleRecord{});
			if (!handle.IsValid())
			{
				return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfHostMemory, "Null descriptor set allocation failed");
			}

			return ReturnValue(handle, error);
		}

		bool NullArenaReset([[maybe_unused]] void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.descriptorArena.reset");
			return Succeed(error);
		}

		bool NullBindDescriptorSet([[maybe_unused]] void * impl, [[maybe_unused]] PipelineLayoutHandle layout, [[maybe_unused]] std::uint32_t setIndex,
			[[maybe_unused]] DescriptorSetHandle set, [[maybe_unused]] std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
		{
			return Succeed(error);
		}

		bool NullCommandListBuildAccelerationStructures(
			[[maybe_unused]] void * impl, [[maybe_unused]] std::span<const AccelerationStructureBuildDesc> builds, Error * error) noexcept
		{
			return Succeed(error);
		}

		AcquireResult NullAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return ReturnValue(
				AcquireResult{
					.status		= SwapchainStatus::eOk,
					.imageIndex = 0,
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					.imageAvailable = swapchain->presentSemaphores[0],
				},
				error);
		}

		PresentResult NullPresent([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t imageIndex,
			[[maybe_unused]] BinarySemaphoreHandle renderFinished, [[maybe_unused]] void * queueImpl, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.null.present");
			return ReturnValue(PresentResult{ .status = SwapchainStatus::eOk }, error);
		}

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		TextureHandle NullSwapchainBackBuffer(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->backBuffers[imageIndex] : TextureHandle{};
		}

		TextureViewHandle NullSwapchainBackBufferView(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->backBufferViews[imageIndex] : TextureViewHandle{};
		}

		BinarySemaphoreHandle NullSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept
		{
			const auto * swapchain = static_cast<null::NullObject *>(impl);
			return imageIndex < null::kNullSwapchainImages ? swapchain->presentSemaphores[imageIndex] : BinarySemaphoreHandle{};
		}

		// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		Format NullSwapchainFormat([[maybe_unused]] void * impl) noexcept
		{
			return Format::eBGRA8UNorm;
		}

		PresentMode NullSwapchainPresentMode([[maybe_unused]] void * impl) noexcept
		{
			return PresentMode::eFifo;
		}

		bool NullSwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept
		{
			return false;
		}

		std::uint32_t NullSwapchainImageCount([[maybe_unused]] void * impl) noexcept
		{
			return 3;
		}

		std::uint32_t NullSwapchainExtent([[maybe_unused]] void * impl) noexcept
		{
			return 1;
		}

		GraphicsApiId NullInstanceApiId([[maybe_unused]] void * impl) noexcept
		{
			return NullApi::id;
		}

		bool NullEnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
			}

			*out = 0;

			if (!adapters.empty())
			{
				adapters.front() = AdapterInfo{
					.type					   = AdapterType::eCpu,
					.apiId					   = NullApi::id,
					.unifiedMemoryArchitecture = true,
					.name					   = "Null Adapter",
				};
			}

			*out = 1;
			return Succeed(error);
		}

		void * NullInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
		{
			null::NullDevice * device = MakeOwnedDevice(static_cast<null::NullInstance *>(impl), desc, error);
			if (device == nullptr)
			{
				return nullptr;
			}

			return ReturnValue(static_cast<void *>(device), error);
		}

		void * NullCreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
		{
			null::NullInstance * instance = MakeOwnedInstance();
			if (instance == nullptr)
			{
				return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Null instance creation failed");
			}

			return ReturnValue(static_cast<void *>(instance), error);
		}

		const CoreDeviceApi & CoreDeviceBlock() noexcept
		{
			static const CoreDeviceApi block{
				.getGraphicsApiId			= &NullDeviceApiId,
				.getGraphicsApiName			= &NullDeviceApiName,
				.createBuffer				= &NullCreateBuffer,
				.createTexture				= &NullCreateTexture,
				.createTextureView			= &NullCreateTextureView,
				.createSampler				= &NullCreateHandle<SamplerHandle>,
				.createDescriptorSetLayout	= &NullCreateHandle<DescriptorSetLayoutHandle>,
				.createPipelineLayout		= &NullCreatePipelineLayout,
				.createGraphicsPipeline		= &NullCreateGraphicsPipeline,
				.createComputePipeline		= &NullCreateComputePipeline,
				.createTimeline				= &NullCreateExportable<TimelineHandle, TimelineDesc>,
				.createBinarySemaphore		= &NullCreateExportable<BinarySemaphoreHandle, BinarySemaphoreDesc>,
				.createDescriptorArena		= &NullCreateDescriptorArena,
				.createCommandPool			= &NullCreateCommandPool,
				.getQueue					= &NullGetQueue,
				.map						= &NullMap,
				.unmap						= &NoopVoid,
				.flushMappedRange			= &NoopVoid,
				.invalidateMappedRange		= &NoopVoid,
				.updateDescriptorsBuffer	= &NoopVoid,
				.updateDescriptorsTexture	= &NoopVoid,
				.updateDescriptorsSampler	= &NoopVoid,
				.getCaps					= &NullDeviceCaps,
				.getFormatSupport			= &NullDeviceFormatSupport,
				.getAdapterInfo				= &NullDeviceAdapterInfo,
				.getValidationMessageCounts = &NullDeviceValidationMessageCounts,
				.destroy					= &NullDestroy,
				.collectGarbage				= &NullCollectGarbage,
				.collectGarbageTimeline		= &NullCollectGarbageTimeline,
				.destroyDevice				= &NullDestroyDevice,
			};

			return block;
		}

		const PresentApi & PresentBlock() noexcept
		{
			static const PresentApi block{
				.createSwapchain = &NullCreateSwapchain,
			};

			return block;
		}

		const PlacedMemoryApi & PlacedMemoryBlock() noexcept
		{
			static const PlacedMemoryApi block{
				.createHeap			  = &NullCreateExportable<HeapHandle, HeapDesc>,
				.createPlacedBuffer	  = &NullCreatePlacedBuffer,
				.createPlacedTexture  = &NullCreatePlacedTexture,
				.getTextureMemoryInfo = &NullDefault<MemoryInfo>,
				.getBufferMemoryInfo  = &NullDefault<MemoryInfo>,
			};

			return block;
		}

		const RayTracingApi & RayTracingBlock() noexcept
		{
			static const RayTracingApi block{
				.createRayTracingPipeline				= &NullCreateHandle<RayTracingPipelineHandle>,
				.createAccelerationStructure			= &NullCreateAccelerationStructure,
				.updateDescriptorsAccelerationStructure = &NoopVoid,
			};

			return block;
		}

		const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept
		{
			static const ResourceIntrospectionApi block{
				.getTextureInfo = &NullGetTextureInfo,
				.getBufferInfo	= &NullGetBufferInfo,
			};

			return block;
		}

		const QueryApi & QueryBlock() noexcept
		{
			static const QueryApi block{
				.createQueryPool	= &NullCreateQueryPool,
				.calibrateTimestamp = &NullDefault<TimestampCalibration>,
			};

			return block;
		}

		const PipelineCacheApi & PipelineCacheBlock() noexcept
		{
			static const PipelineCacheApi block{
				.createPipelineCache  = &NullCreateHandle<PipelineCacheHandle>,
				.getPipelineCacheData = &NullDefault<PipelineCacheData>,
			};

			return block;
		}

		const ResidencyApi & ResidencyBlock() noexcept
		{
			static const ResidencyApi block{
				.queryMemoryBudget	  = &NullDefault<MemoryBudgetInfo>,
				.setResidencyPriority = &NoopVoid,
			};

			return block;
		}

		const AdoptionApi & AdoptionBlock() noexcept
		{
			static const AdoptionApi block{
				.adoptBuffer			  = &NullAdoptBuffer,
				.adoptTexture			  = &NullAdoptTexture,
				.getNativeBuffer		  = &NullGetNativeBuffer,
				.getNativeTexture		  = &NullGetNativeTexture,
				.adoptTextureView		  = &NullAdoptTextureView,
				.adoptSampler			  = &NullAdoptSampler,
				.getNativeTextureView	  = &NullGetNativeTextureView,
				.getNativeSampler		  = &NullGetNativeSampler,
				.adoptTimeline			  = &NullAdoptTimeline,
				.adoptBinarySemaphore	  = &NullAdoptBinarySemaphore,
				.getNativeTimeline		  = &NullGetNativeTimeline,
				.getNativeBinarySemaphore = &NullGetNativeBinarySemaphore,
			};

			return block;
		}

		const InstanceApi & InstanceBlock() noexcept
		{
			static const InstanceApi block{
				.getGraphicsApiId  = &NullInstanceApiId,
				.enumerateAdapters = &NullEnumerateAdapters,
				.createDevice	   = &NullInstanceCreateDevice,
				.destroyInstance   = &NullDestroyInstance,
			};

			return block;
		}

		const QueueApi & QueueBlock() noexcept
		{
			static const QueueApi block{
				.getType		   = &NullQueueType,
				.submit			   = &NullQueueSubmit,
				.waitIdle		   = &NoopVoid,
				.getCompletedValue = &NullDefault<std::uint64_t>,
				.wait			   = &NoopVoid,
				.signal			   = &NoopVoid,
				.beginDebugLabel   = &NoopVoid,
				.endDebugLabel	   = &NoopVoid,
			};

			return block;
		}

		const SparseApi & SparseBlock() noexcept
		{
			static const SparseApi block{
				.bindSparse = &NullQueueBindSparse,
			};

			return block;
		}

		const CommandPoolApi & CommandPoolBlock() noexcept
		{
			static const CommandPoolApi block{
				.allocate = &NullCommandPoolAllocate,
				.reset	  = &NullCommandPoolReset,
			};

			return block;
		}

		const RenderCommandApi & RenderCommandBlock() noexcept
		{
			static const RenderCommandApi block{
				.begin				 = &NullCommandListBegin,
				.end				 = &NullCommandListEnd,
				.barriers			 = &NullCommandListBarriers,
				.beginRendering		 = &NoopVoid,
				.endRendering		 = &NoopVoid,
				.setGraphicsPipeline = &NoopVoid,
				.setComputePipeline	 = &NoopVoid,
				.bindDescriptorSet	 = &NullBindDescriptorSet,
				.pushConstants		 = &NoopVoid,
				.setViewport		 = &NoopVoid,
				.setScissor			 = &NoopVoid,
				.setBlendConstants	 = &NoopVoid,
				.setStencilReference = &NoopVoid,
				.setDepthBias		 = &NoopVoid,
				.setVertexBuffer	 = &NoopVoid,
				.setIndexBuffer		 = &NoopVoid,
				.draw				 = &NoopVoid,
				.drawIndexed		 = &NoopVoid,
				.dispatch			 = &NoopVoid,
				.copyBuffer			 = &NoopVoid,
				.copyBufferToTexture = &NoopVoid,
				.copyTextureToBuffer = &NoopVoid,
				.copyTexture		 = &NoopVoid,
				.clearBuffer		 = &NoopVoid,
				.clearTexture		 = &NoopVoid,
				.resolveTexture		 = &NoopVoid,
				.blit				 = &NoopVoid,
				.generateMips		 = &NoopVoid,
				.beginDebugLabel	 = &NoopVoid,
				.endDebugLabel		 = &NoopVoid,
			};

			return block;
		}

		bool NullAliasBarriers(void * impl, const std::span<const AliasBarrier> barriers, Error * error) noexcept
		{
			auto * list				  = static_cast<null::NullObject *>(impl);
			null::NullDevice * device = list->owner;

			for (const AliasBarrier & barrier : barriers)
			{
				if ((barrier.beforeBuffer.IsValid() && device->handles.Resolve(barrier.beforeBuffer, true) == nullptr) ||
					(barrier.afterBuffer.IsValid() && device->handles.Resolve(barrier.afterBuffer, true) == nullptr) ||
					(barrier.beforeTexture.IsValid() && device->handles.Resolve(barrier.beforeTexture, true) == nullptr) ||
					(barrier.afterTexture.IsValid() && device->handles.Resolve(barrier.afterTexture, true) == nullptr))
				{
					return Fail(error, ErrorCode::eInvalidHandle, "aliasBarriers with an invalid resource handle");
				}
			}

			return Succeed(error);
		}

		const AliasingCommandApi & AliasingCommandBlock() noexcept
		{
			static const AliasingCommandApi block{
				.aliasBarriers = &NullAliasBarriers,
			};

			return block;
		}

		const RayTracingCommandApi & RayTracingCommandBlock() noexcept
		{
			static const RayTracingCommandApi block{
				.setRayTracingPipeline		  = &NoopVoid,
				.buildAccelerationStructures  = &NullCommandListBuildAccelerationStructures,
				.copyAccelerationStructure	  = &NoopVoid,
				.compactAccelerationStructure = &NoopVoid,
				.traceRays					  = &NoopVoid,
			};

			return block;
		}

		const QueryCommandApi & QueryCommandBlock() noexcept
		{
			static const QueryCommandApi block{
				.resetQueryPool	  = &NoopVoid,
				.writeTimestamp	  = &NoopVoid,
				.beginQuery		  = &NoopVoid,
				.endQuery		  = &NoopVoid,
				.resolveQueryData = &NoopVoid,
			};

			return block;
		}

		const IndirectApi & IndirectBlock() noexcept
		{
			static const IndirectApi block{
				.drawIndirect		 = &NoopVoid,
				.drawIndexedIndirect = &NoopVoid,
				.dispatchIndirect	 = &NoopVoid,
			};

			return block;
		}

		const IndirectCountApi & IndirectCountBlock() noexcept
		{
			static const IndirectCountApi block{
				.drawIndirectCount		  = &NoopVoid,
				.drawIndexedIndirectCount = &NoopVoid,
			};

			return block;
		}

		const NativeEscapeApi & NativeEscapeBlock() noexcept
		{
			static const NativeEscapeApi block{
				.beginNativeMutation = &NullBeginNativeMutation,
				.endNativeMutation	 = &NoopVoid,
			};

			return block;
		}

		const SwapchainApi & SwapchainBlock() noexcept
		{
			static const SwapchainApi block{
				.acquireNextImage			 = &NullAcquire,
				.present					 = &NullPresent,
				.getBackBuffer				 = &NullSwapchainBackBuffer,
				.getBackBufferView			 = &NullSwapchainBackBufferView,
				.getPerImagePresentSemaphore = &NullSwapchainPresentSemaphore,
				.getFormat					 = &NullSwapchainFormat,
				.getPresentMode				 = &NullSwapchainPresentMode,
				.getImageCount				 = &NullSwapchainImageCount,
				.getWidth					 = &NullSwapchainExtent,
				.getHeight					 = &NullSwapchainExtent,
				.resize						 = &NoopVoid,
				.setPresentMode				 = &NoopVoid,
				.supportsReadback			 = &NullSwapchainSupportsReadback,
			};

			return block;
		}

		const DescriptorArenaApi & DescriptorArenaBlock() noexcept
		{
			static const DescriptorArenaApi block{
				.allocate = &NullArenaAllocate,
				.reset	  = &NullArenaReset,
			};

			return block;
		}

	}

	Result<void> RegisterNullBackend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName		   = NullApi::canonicalName;
		info.info.displayName		   = NullApi::displayName;
		info.info.apiVersionMajor	   = 1;
		info.info.supportsSurfaces	   = true;
		info.info.supportsDebugMarkers = true;
		info.createInstance			   = &NullCreateInstance;

		return registry.Register<NullApi>(info);
	}

	template <>
	Result<UniqueDevice> CreateDevice<NullApi>(const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		Error error{};

		null::NullDevice * device = MakeOwnedDevice(nullptr, desc, &error);
		if (device == nullptr)
		{
			return error;
		}

		void * deviceImpl		 = device;
		BackendBlockSet * blocks = detail::ResolveDeviceBlocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueDevice(deviceImpl, blocks);
	}

}

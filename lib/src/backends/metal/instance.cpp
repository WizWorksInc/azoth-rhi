// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/native/metal_config.hpp"

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	GraphicsApiId MetalInstanceApiId([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::id;
	}

	bool MetalEnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
		}

		*out = 0;

		NS::SharedPtr<NS::Array> all = NS::TransferPtr(MTL::CopyAllDevices());
		const std::uint32_t count	 = (all.get() != nullptr) ? static_cast<std::uint32_t>(all->count()) : 0;
		const std::uint32_t fill	 = std::min(count, static_cast<std::uint32_t>(adapters.size()));

		for (std::uint32_t i = 0; i < fill; ++i)
		{
			auto * device = static_cast<MTL::Device *>(all->object(i));
			adapters[i]	  = AdapterInfo{
				.type					   = device->hasUnifiedMemory() ? AdapterType::eIntegrated : AdapterType::eDiscrete,
				.apiId					   = MetalApi::id,
				.adapterIndex			   = i,
				.dedicatedVideoMemoryBytes = device->recommendedMaxWorkingSetSize(),
				.unifiedMemoryArchitecture = device->hasUnifiedMemory(),
				.name = nullptr,
			};
		}

		*out = count;
		return Succeed(error);
	}

	void PopulateCaps(MetalDevice * device)
	{
		MTL::Device * mtl		 = device->device.get();
		const bool unifiedMemory = mtl->hasUnifiedMemory();

		device->adapterName.clear();
		if (const NS::String * name = mtl->name(); name != nullptr && name->utf8String() != nullptr)
		{
			device->adapterName = name->utf8String();
		}

		NS::ProcessInfo * processInfo		 = NS::ProcessInfo::processInfo();
		const NS::OperatingSystemVersion osv = processInfo->operatingSystemVersion();
		device->driverVersion				 = std::to_string(osv.majorVersion) + "." + std::to_string(osv.minorVersion);
		device->driverInfo.clear();
		if (const NS::String * s = processInfo->operatingSystemVersionString(); s != nullptr && s->utf8String() != nullptr)
		{
			device->driverInfo = s->utf8String();
		}

		device->adapter = AdapterInfo{
			.type					   = unifiedMemory ? AdapterType::eIntegrated : AdapterType::eDiscrete,
			.apiId					   = MetalApi::id,
			.adapterIndex			   = 0,
			.dedicatedVideoMemoryBytes = mtl->recommendedMaxWorkingSetSize(),
			.unifiedMemoryArchitecture = unifiedMemory,
			.name					   = device->adapterName.empty() ? nullptr : device->adapterName.c_str(),
			.driverVersion			   = device->driverVersion.empty() ? nullptr : device->driverVersion.c_str(),
			.driverInfo				   = device->driverInfo.empty() ? nullptr : device->driverInfo.c_str(),
		};

		DeviceCaps caps{};
		caps.apiId		= MetalApi::id;
		caps.apiVersion = ApiVersion{ .major = 3, .minor = 0 };
		const bool apple3 = mtl->supportsFamily(MTL::GPUFamilyApple3);
		const bool apple5 = mtl->supportsFamily(MTL::GPUFamilyApple5);
		const bool apple7 = mtl->supportsFamily(MTL::GPUFamilyApple7);
		const bool mac2	  = mtl->supportsFamily(MTL::GPUFamilyMac2);

		caps.supportsTimelineSync	  = apple7 || mac2;

		const MTL::ArgumentBuffersTier argumentTier = mtl->argumentBuffersSupport();
		caps.bindingTier							= argumentTier >= MTL::ArgumentBuffersTier2 ? BindingTier::eUnbounded : BindingTier::eDynamicIndexing;
		caps.shaderBinaryFormat						= ShaderBinaryFormat::eBackendNative;
		caps.supportsShaderSource					= true;

		caps.supportsUpdateAfterBind		   = caps.bindingTier >= BindingTier::eDynamicIndexing;
		caps.supportsPartiallyBoundDescriptors = caps.bindingTier >= BindingTier::eDynamicIndexing;

		caps.maxSamplerDescriptors = static_cast<std::uint32_t>(mtl->maxArgumentBufferSamplerCount());

		caps.maxDescriptorSets = caps.bindingTier >= BindingTier::eUnbounded ? kMetalMaxBufferArguments - 1 : 1;

		caps.maxDescriptorsPerSet = std::min({ kMetalMaxBufferArguments, kMetalMaxTextureArguments, kMetalMaxSamplerArguments });

		caps.maxBindlessSampledTextures = 0;
		caps.maxBindlessStorageBuffers	= 0;

		caps.supportsAnisotropy		  = true;
		caps.supportsIndependentBlend = true;

		caps.supportsTextureViewSwizzle	  = true;
		caps.supportsShaderDrawParameters = true;

		caps.supportsShaderFloat16 = true;

		caps.supportsDrawIndirectFirstInstance = true;
		caps.supportsDynamicBufferOffsets	   = true;

		device->samplesAtStageBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary);
		device->samplesAtDrawBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtDrawBoundary);
		device->samplesAtDispatchBoundary = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtDispatchBoundary);
		device->samplesAtBlitBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtBlitBoundary);

		if (NS::Array * counterSets = mtl->counterSets(); counterSets != nullptr)
		{
			for (NS::UInteger i = 0; i < counterSets->count(); ++i)
			{
				auto * set = counterSets->object<MTL::CounterSet>(i);
				if (set != nullptr && set->name() != nullptr && set->name()->isEqualToString(MTL::CommonCounterSetTimestamp))
				{
					device->timestampCounterSet = NS::RetainPtr(set);
					break;
				}
			}
		}

		const bool canWriteTimestamps = device->timestampCounterSet.get() != nullptr && (device->samplesAtStageBoundary || device->samplesAtBlitBoundary);

		caps.supportsTimestampQueries = canWriteTimestamps;

		caps.supportsTimestampWritesInScope = canWriteTimestamps && device->samplesAtDrawBoundary && device->samplesAtDispatchBoundary;

		MTL::Timestamp probedCpu = 0;
		MTL::Timestamp probedGpu = 0;
		mtl->sampleTimestamps(&probedCpu, &probedGpu);
		caps.supportsTimestampCalibration = (device->samplesAtStageBoundary || device->samplesAtDrawBoundary) && (probedCpu != 0 || probedGpu != 0);

		caps.supportsRayTracing = false;

		caps.maxColorAttachments = 8;
		caps.maxRenderTargets	 = caps.maxColorAttachments;
		caps.maxVertexBindings	 = 31;
		caps.maxVertexAttributes = 31;
		caps.maxViewports		 = apple5 || mac2 ? 16 : 1;

		const bool bigTextures	   = apple3 || mac2;
		caps.maxTextureDimension1D = bigTextures ? 16384 : 8192;
		caps.maxTextureDimension2D = bigTextures ? 16384 : 8192;
		caps.maxTextureDimension3D = 2048;
		caps.maxTextureArrayLayers = 2048;
		caps.maxPushConstantBytes  = 4096;

		caps.minUniformBufferOffsetAlignment	= 32;
		caps.minStorageBufferOffsetAlignment	= 32;
		caps.minTexelBufferOffsetAlignment		= 16;
		caps.optimalBufferCopyOffsetAlignment	= 16;
		caps.optimalBufferCopyRowPitchAlignment = std::max<std::uint64_t>(mtl->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm), 256);

		caps.timestampPeriodNanoseconds = 1.0f;

		caps.timestampValidBits = 64;

		caps.sparseTileSizeBytes = 0;

		device->caps = caps;
	}

	[[nodiscard]] bool VersionIsOurs(const ApiVersion requested, Error & refusal) noexcept
	{
		if (requested.major >= 4)
		{
			refusal = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "this is the Metal 3 backend; ask for azoth.rhi.metal4 to get Metal 4" };
			return false;
		}

		return true;
	}

	[[nodiscard]] MetalDevice * MakeOwnedDevice(MetalInstance * instance, const DeviceDesc & desc, Error & refusal)
	{
		NS::SharedPtr<MTL::Device> mtlDevice;

		if (desc.preferredAdapterIndex != kInvalidIndex)
		{
			NS::SharedPtr<NS::Array> all = NS::TransferPtr(MTL::CopyAllDevices());
			if (all.get() != nullptr && desc.preferredAdapterIndex < static_cast<std::uint32_t>(all->count()))
			{
				mtlDevice = NS::RetainPtr(static_cast<MTL::Device *>(all->object(desc.preferredAdapterIndex)));
			}
		}

		if (mtlDevice.get() == nullptr)
		{
			mtlDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
		}

		if (mtlDevice.get() == nullptr)
		{
			return nullptr;
		}

		const auto config = native::FindDeviceConfig<MetalApi>(desc.backendConfigs);
		if (config.malformed)
		{
			refusal =
				Error{ .code = ErrorCode::eInvalidArgument, .message = "the Metal 3 configuration block declares a size or version this backend cannot read" };
			return nullptr;
		}
		if (!VersionIsOurs(config.block != nullptr ? config.block->generation : desc.apiVersion, refusal))
		{
			return nullptr;
		}

		auto device	   = HostNew<MetalDevice>();
		device->object = PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
			Published<PresentApi, &PresentBlock>,
			Published<PlacedMemoryApi, &PlacedMemoryBlock>,
			Published<RayTracingApi, &RayTracingBlock>,
			Published<QueryApi, &QueryBlock>,
			Published<ResidencyApi, &ResidencyBlock>,
			Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
			Published<AdoptionApi, &AdoptionBlock>,
			Published<ExternalSharingApi, &ExternalSharingBlock>>();

		device->instanceWrapper = instance;
		device->validation		= desc.validation;
		device->debugLabels		= desc.enableDebugLabels;
		device->device			= std::move(mtlDevice);

		device->caps.deviceLocalMemoryIsHostVisible = device->device->hasUnifiedMemory();
		device->allowDeviceLocalMapping				= desc.allowDeviceLocalMapping && device->caps.deviceLocalMemoryIsHostVisible;

		const QueuePlan plan  = PlanQueues(desc.queues);
		MTL::Device * mtl	  = device->device.get();
		const auto makeQueues = [mtl](detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & out, std::uint32_t count) -> bool
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				NS::SharedPtr<MTL::CommandQueue> commandQueue = NS::TransferPtr(mtl->newCommandQueue());
				if (commandQueue.get() == nullptr)
				{
					return false;
				}

				out.push_back(std::move(commandQueue));
			}

			return true;
		};
		if (!makeQueues(device->graphicsQueues, plan.graphicsCount) || !makeQueues(device->computeQueues, plan.computeCount) ||
			!makeQueues(device->copyQueues, plan.copyCount))
		{
			return nullptr;
		}

		{
			const NS::SharedPtr<MTL::ResidencySetDescriptor> residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
			for (NS::SharedPtr<MTL::ResidencySet> & set : device->residencySets)
			{
				NS::Error * residencyError = nullptr;
				MTL::ResidencySet * made   = mtl->newResidencySet(residencyDesc.get(), &residencyError);
				if (made == nullptr)
				{
					break;
				}

				set = NS::TransferPtr(made);
				for (const QueueType type : { QueueType::eGraphics, QueueType::eCompute, QueueType::eCopy })
				{
					if (MTL::CommandQueue * queue = device->CommandQueueFor(type); queue != nullptr)
					{
						queue->addResidencySet(set.get());
					}
				}
			}
		}

		PopulateCaps(device.get());

		const auto queueCount = [&device](const QueueType type) -> std::uint32_t
		{
			return static_cast<std::uint32_t>(device->QueuesForType(type).size());
		};

		device->caps.graphicsQueueCount		   = queueCount(QueueType::eGraphics);
		device->caps.computeQueueCount		   = queueCount(QueueType::eCompute);
		device->caps.copyQueueCount			   = queueCount(QueueType::eCopy);
		device->caps.hasDedicatedComputeQueue  = device->caps.computeQueueCount != 0;
		device->caps.hasDedicatedTransferQueue = device->caps.copyQueueCount != 0;

		MetalDevice * raw		  = device.get();
		MetalBackendOwner & owner = Owner();

		std::uint32_t deviceTag = 0;
		if (!detail::DeviceTags().Acquire(deviceTag))
		{
			return nullptr;
		}
		raw->deviceTag = deviceTag;
		raw->tracked.Rebind(deviceTag);
		raw->buffers.Rebind(deviceTag);
		raw->textures.Rebind(deviceTag);
		raw->textureViews.Rebind(deviceTag);
		raw->samplers.Rebind(deviceTag);
		raw->heaps.Rebind(deviceTag);
		raw->timelines.Rebind(deviceTag);
		raw->binarySemaphores.Rebind(deviceTag);
		raw->graphicsPipelines.Rebind(deviceTag);
		raw->computePipelines.Rebind(deviceTag);
		raw->descriptorSets.Rebind(deviceTag);

		owner.devices.push_back(std::move(device));
		return raw;
	}

	[[nodiscard]] MetalInstance * MakeOwnedInstance()
	{
		auto instance = HostNew<MetalInstance>();
		if (instance == nullptr)
		{
			return nullptr;
		}

		instance->object = PublishingObject<Published<InstanceApi, &InstanceBlock>, Published<ExternalCapabilityApi, &ExternalCapabilityBlock>>();

		MetalInstance * raw		  = instance.get();
		MetalBackendOwner & owner = Owner();
		if (!detail::TryPushBack(owner.instances, std::move(instance)))
		{
			return nullptr;
		}

		return raw;
	}

	void MetalDestroyDevice(void * impl) noexcept
	{
		MetalBackendOwner & owner = Owner();

		MetalInstance * owningInstance = nullptr;
		std::uint32_t releasedTag	   = 0;
		for (const HostUniquePtr<MetalDevice> & device : owner.devices)
		{
			if (device.get() == impl)
			{
				owningInstance = device->instanceWrapper;
				releasedTag	   = device->deviceTag;
				break;
			}
		}

		std::erase_if(owner.devices,
			[impl](const HostUniquePtr<MetalDevice> & device)
			{
				return device.get() == impl;
			});
		detail::DeviceTags().Release(releasedTag);

		if (owningInstance != nullptr)
		{
			bool stillUsed = false;
			for (const HostUniquePtr<MetalDevice> & device : owner.devices)
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
					[owningInstance](const HostUniquePtr<MetalInstance> & instance)
					{
						return instance.get() == owningInstance;
					});
			}
		}
	}

	void MetalDestroyInstance(void * impl) noexcept
	{
		MetalBackendOwner & owner = Owner();
		std::erase_if(owner.instances,
			[impl](const HostUniquePtr<MetalInstance> & instance)
			{
				return instance.get() == impl;
			});
	}

	bool MetalQueryExternalHandleSupport(
		[[maybe_unused]] void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support needs somewhere to write the result");
		}

		*out = {};

		NS::SharedPtr<NS::Array> all = NS::TransferPtr(MTL::CopyAllDevices());
		const std::uint32_t count	 = (all.get() != nullptr) ? static_cast<std::uint32_t>(all->count()) : 0;
		if (desc.adapterIndex >= count)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support asked about an adapter index this instance does not have");
		}

		switch (desc.kind)
		{
		case ExternalObjectKind::eTexture:
			if (desc.handleType == ExternalHandleType::eMtlSharedTexture && desc.format != Format::eUndefined)
			{
				out->exportable		 = true;
				out->importable		 = true;
				out->compatibleTypes = ExternalHandleType::eMtlSharedTexture;
			}
			break;

		case ExternalObjectKind::eTimeline:
		case ExternalObjectKind::eBinarySemaphore:
			if (desc.handleType == ExternalHandleType::eMtlSharedEvent)
			{
				out->exportable		 = true;
				out->importable		 = true;
				out->compatibleTypes = ExternalHandleType::eMtlSharedEvent;
			}
			break;

		case ExternalObjectKind::eBuffer:
		case ExternalObjectKind::eHeap:	  break;
		}

		return Succeed(error);
	}

	void * MetalInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		Error refusal{};
		MetalDevice * device = MakeOwnedDevice(static_cast<MetalInstance *>(impl), desc, refusal);
		if (device == nullptr)
		{
			return refusal.code != ErrorCode::eOk ? FailValue<void *>(error, refusal.code, refusal.message)
												  : FailValue<void *>(error, ErrorCode::eNativeApiError, "no Metal device available");
		}
		return ReturnValue(static_cast<void *>(device), error);
	}

	void * MetalCreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
	{
		MetalInstance * instance = MakeOwnedInstance();
		if (instance == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal instance creation failed");
		}

		return ReturnValue(static_cast<void *>(instance), error);
	}

}

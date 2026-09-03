// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/native/metal_config.hpp"

#include "backends/metal4/internal.hpp"

#include <format>
#include <iterator>

namespace azo::rhi::metal4
{
	GraphicsApiId Metal4InstanceApiId([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::id;
	}

	bool Metal4EnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
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
				.apiId					   = Metal4Api::id,
				.adapterIndex			   = i,
				.dedicatedVideoMemoryBytes = device->recommendedMaxWorkingSetSize(),
				.unifiedMemoryArchitecture = device->hasUnifiedMemory(),
				.name					   = nullptr,
			};
		}

		*out = count;
		return Succeed(error);
	}

	void PopulateCaps(Metal4Device * device)
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
			.apiId					   = Metal4Api::id,
			.adapterIndex			   = 0,
			.dedicatedVideoMemoryBytes = mtl->recommendedMaxWorkingSetSize(),
			.unifiedMemoryArchitecture = unifiedMemory,
			.name					   = device->adapterName.empty() ? nullptr : device->adapterName.c_str(),
			.driverVersion			   = device->driverVersion.empty() ? nullptr : device->driverVersion.c_str(),
			.driverInfo				   = device->driverInfo.empty() ? nullptr : device->driverInfo.c_str(),
		};

		DeviceCaps caps{};
		caps.apiId		  = Metal4Api::id;
		caps.apiVersion	  = ApiVersion{ .major = 4, .minor = 0 };
		const bool apple3 = mtl->supportsFamily(MTL::GPUFamilyApple3);
		const bool apple5 = mtl->supportsFamily(MTL::GPUFamilyApple5);
		const bool apple7 = mtl->supportsFamily(MTL::GPUFamilyApple7);
		const bool mac2	  = mtl->supportsFamily(MTL::GPUFamilyMac2);

		caps.supportsTimelineSync = apple7 || mac2;

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

		caps.supportsTimestampWritesInScope = canWriteTimestamps;

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
		if (const std::uint64_t frequency = mtl->queryTimestampFrequency(); frequency != 0)
		{
			caps.timestampPeriodNanoseconds = 1'000'000'000.0f / static_cast<float>(frequency);
		}

		caps.timestampValidBits = 64;

		caps.sparseTileSizeBytes = 0;

		device->caps = caps;
	}

	bool AdapterHasMetal4(MTL::Device * device) noexcept
	{
		return device != nullptr && device->supportsFamily(MTL::GPUFamilyMetal4);
	}

	[[nodiscard]] bool VersionIsOurs(const ApiVersion requested, Error & refusal) noexcept
	{
		if (requested.major != 0 && requested.major < 4)
		{
			refusal = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "this is the Metal 4 backend; ask for azoth.rhi.metal to get Metal 3" };
			return false;
		}

		return true;
	}

	[[nodiscard]] const char * NoMetal4FamilyMessage(MTL::Device * mtlDevice)
	{
		static constexpr const char * kPlain = "this adapter does not report the Metal 4 family";

		const NS::String * name = mtlDevice != nullptr ? mtlDevice->name() : nullptr;
		if (name == nullptr || name->utf8String() == nullptr)
		{
			return kPlain;
		}

		thread_local detail::HostString message;
		message.clear();
		std::format_to(std::back_inserter(message), "{}: {}", kPlain, name->utf8String());
		return message.c_str();
	}

	[[nodiscard]] Metal4Device * MakeOwnedDevice(Metal4Instance * instance, const DeviceDesc & desc, Error & refusal)
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

		const auto config = native::FindDeviceConfig<Metal4Api>(desc.backendConfigs);
		if (config.malformed)
		{
			refusal =
				Error{ .code = ErrorCode::eInvalidArgument, .message = "the Metal 4 configuration block declares a size or version this backend cannot read" };
			return nullptr;
		}
		if (!VersionIsOurs(config.block != nullptr ? config.block->generation : desc.apiVersion, refusal))
		{
			return nullptr;
		}

		if (!AdapterHasMetal4(mtlDevice.get()))
		{
			refusal = Error{ .code = ErrorCode::eUnsupportedFeature, .message = NoMetal4FamilyMessage(mtlDevice.get()) };
			return nullptr;
		}

		auto device	   = HostNew<Metal4Device>();
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
		const auto makeQueues = [mtl](detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & out, std::uint32_t count) -> bool
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				NS::SharedPtr<MTL4::CommandQueue> commandQueue = NS::TransferPtr(mtl->newMTL4CommandQueue());
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

		device->drainEvent = NS::TransferPtr(mtl->newSharedEvent());
		if (device->drainEvent.get() == nullptr)
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
					return nullptr;
				}

				set = NS::TransferPtr(made);
				for (const QueueType type : { QueueType::eGraphics, QueueType::eCompute, QueueType::eCopy })
				{
					if (MTL4::CommandQueue * queue = device->CommandQueueFor(type); queue != nullptr)
					{
						queue->addResidencySet(set.get());
					}
				}
			}

			const NS::SharedPtr<MTL4::CompilerDescriptor> compilerDesc = NS::TransferPtr(MTL4::CompilerDescriptor::alloc()->init());

			NS::Error * compilerError = nullptr;
			MTL4::Compiler * compiler = mtl->newCompiler(compilerDesc.get(), &compilerError);
			if (compiler == nullptr)
			{
				return nullptr;
			}

			device->compiler = NS::TransferPtr(compiler);
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

		Metal4Device * raw		   = device.get();
		Metal4BackendOwner & owner = Owner();

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

	[[nodiscard]] Metal4Instance * MakeOwnedInstance()
	{
		auto instance = HostNew<Metal4Instance>();
		if (instance == nullptr)
		{
			return nullptr;
		}

		instance->object = PublishingObject<Published<InstanceApi, &InstanceBlock>, Published<ExternalCapabilityApi, &ExternalCapabilityBlock>>();

		Metal4Instance * raw	   = instance.get();
		Metal4BackendOwner & owner = Owner();
		if (!detail::TryPushBack(owner.instances, std::move(instance)))
		{
			return nullptr;
		}

		return raw;
	}

	void Metal4DestroyDevice(void * impl) noexcept
	{
		Metal4BackendOwner & owner = Owner();

		Metal4Instance * owningInstance = nullptr;
		std::uint32_t releasedTag		= 0;
		for (const HostUniquePtr<Metal4Device> & device : owner.devices)
		{
			if (device.get() == impl)
			{
				owningInstance = device->instanceWrapper;
				releasedTag	   = device->deviceTag;
				break;
			}
		}

		std::erase_if(owner.devices,
			[impl](const HostUniquePtr<Metal4Device> & device)
			{
				return device.get() == impl;
			});
		detail::DeviceTags().Release(releasedTag);

		if (owningInstance != nullptr)
		{
			bool stillUsed = false;
			for (const HostUniquePtr<Metal4Device> & device : owner.devices)
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
					[owningInstance](const HostUniquePtr<Metal4Instance> & instance)
					{
						return instance.get() == owningInstance;
					});
			}
		}
	}

	void Metal4DestroyInstance(void * impl) noexcept
	{
		Metal4BackendOwner & owner = Owner();
		std::erase_if(owner.instances,
			[impl](const HostUniquePtr<Metal4Instance> & instance)
			{
				return instance.get() == impl;
			});
	}

	bool Metal4QueryExternalHandleSupport(
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

	void * Metal4InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		Error refusal{};
		Metal4Device * device = MakeOwnedDevice(static_cast<Metal4Instance *>(impl), desc, refusal);
		if (device == nullptr)
		{
			return refusal.code != ErrorCode::eOk ? FailValue<void *>(error, refusal.code, refusal.message)
												  : FailValue<void *>(error, ErrorCode::eNativeApiError, "no Metal device available");
		}
		return ReturnValue(static_cast<void *>(device), error);
	}

	void * Metal4CreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
	{
		Metal4Instance * instance = MakeOwnedInstance();
		if (instance == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal instance creation failed");
		}

		return ReturnValue(static_cast<void *>(instance), error);
	}

}

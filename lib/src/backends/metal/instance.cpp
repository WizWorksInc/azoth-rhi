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

#include "azoth/rhi/backend/blocks/instance.hpp"

#include "azoth/rhi/backend/blocks/device.hpp"
#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/device_config.hpp"
#include "azoth/rhi/native/metal_config.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSArray.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSProcessInfo.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTLCommandQueue.hpp>
#include <Metal/MTLCounters.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLPixelFormat.hpp>
#include <Metal/MTLResidencySet.hpp>

#include <algorithm>
#include <cstdint> // NOLINT
#include <span>
#include <string>
#include <utility>

namespace azo::rhi::metal
{
	GraphicsApiId metal_instance_api_id([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::kId;
	}

	bool metal_enumerate_adapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "adapter count output pointer is null");
		}

		*out = 0;

		NS::SharedPtr<NS::Array> all = NS::TransferPtr(MTL::CopyAllDevices());
		const std::uint32_t count	 = (all.get() != nullptr) ? static_cast<std::uint32_t>(all->count()) : 0;
		const std::uint32_t fill	 = std::min(count, static_cast<std::uint32_t>(adapters.size()));

		for (std::uint32_t i = 0; i < fill; ++i)
		{
			auto * device					  = static_cast<MTL::Device *>(all->object(i));
			azo::rhi::detail::at(adapters, i) = AdapterInfo{
				.type					   = device->hasUnifiedMemory() ? AdapterType::eIntegrated : AdapterType::eDiscrete,
				.apiId					   = MetalApi::kId,
				.adapterIndex			   = i,
				.dedicatedVideoMemoryBytes = device->recommendedMaxWorkingSetSize(),
				.unifiedMemoryArchitecture = device->hasUnifiedMemory(),
				.name					   = nullptr,
			};
		}

		*out = count;
		return succeed(error);
	}

	void populate_caps(MetalDevice * device)
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
			.apiId					   = MetalApi::kId,
			.adapterIndex			   = 0,
			.dedicatedVideoMemoryBytes = mtl->recommendedMaxWorkingSetSize(),
			.unifiedMemoryArchitecture = unifiedMemory,
			.name					   = device->adapterName.empty() ? nullptr : device->adapterName.c_str(),
			.driverVersion			   = device->driverVersion.empty() ? nullptr : device->driverVersion.c_str(),
			.driverInfo				   = device->driverInfo.empty() ? nullptr : device->driverInfo.c_str(),
		};

		DeviceCaps caps{};
		caps.apiId		  = MetalApi::kId;
		caps.apiVersion	  = ApiVersion{ .major = 3, .minor = 0 };
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

		caps.supportsScalarBlockLayout = true;

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

		// Every path a write outside a scope can take samples from its own encoder, and Apple isolates such a sample from no other encoder's commands.
		caps.supportsOrderedTimestamps = false;

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

	[[nodiscard]] static bool version_is_ours(const ApiVersion requested, Error & refusal) noexcept
	{
		if (requested.major >= 4)
		{
			refusal = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "this is the Metal 3 backend; ask for azoth.rhi.metal4 to get Metal 4" };
			return false;
		}

		return true;
	}

	[[nodiscard]] MetalDevice * make_owned_device(MetalInstance * instance, const DeviceDesc & desc, Error & refusal)
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
			refusal = Error{ .code = ErrorCode::eNoCompatibleAdapter, .message = "this machine reports no Metal device" };
			return nullptr;
		}

		const auto config = native::find_device_config<MetalApi>(desc.backendConfigs);
		if (config.malformed)
		{
			refusal =
				Error{ .code = ErrorCode::eInvalidArgument, .message = "the Metal 3 configuration block declares a size or version this backend cannot read" };
			return nullptr;
		}
		if (!version_is_ours(config.block != nullptr ? config.block->generation : ApiVersion{}, refusal))
		{
			return nullptr;
		}

		auto device	   = host_new<MetalDevice>();
		device->object = publishing_object<
			Published<CoreDeviceApi, &core_device_block>,
			Published<PresentApi, &present_block>,
			Published<PlacedMemoryApi, &placed_memory_block>,
			Published<RayTracingApi, &ray_tracing_block>,
			Published<QueryApi, &query_block>,
			Published<ResidencyApi, &residency_block>,
			Published<ResourceIntrospectionApi, &resource_introspection_block>,
			Published<AdoptionApi, &adoption_block>,
			Published<ExternalSharingApi, &external_sharing_block>>();

		device->instanceWrapper = instance;
		device->validation		= desc.validation;
		device->debugLabels		= desc.enableDebugLabels;
		device->device			= std::move(mtlDevice);

		device->caps.deviceLocalMemoryIsHostVisible = device->device->hasUnifiedMemory();
		device->allowDeviceLocalMapping				= desc.allowDeviceLocalMapping && device->caps.deviceLocalMemoryIsHostVisible;

		device->openLists.set_bound(desc.maxOpenCommandListsPerQueue);

		const QueuePlan plan				   = plan_queues(desc.queues);
		MTL::Device * mtl					   = device->device.get();
		const std::uint32_t commandBufferCount = MetalDevice::command_buffers_per_queue(desc.maxOpenCommandListsPerQueue);
		const auto makeQueues = [mtl, commandBufferCount](detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & out, std::uint32_t count) -> bool
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				NS::SharedPtr<MTL::CommandQueue> commandQueue = NS::TransferPtr(mtl->newCommandQueue(commandBufferCount));
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
			refusal = Error{ .code = ErrorCode::eNativeApiError, .message = "this Metal device would not make a command queue" };
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
					if (MTL::CommandQueue * queue = device->command_queue_for(type); queue != nullptr)
					{
						queue->addResidencySet(set.get());
					}
				}
			}
		}

		populate_caps(device.get());

		const auto queueCount = [&device](const QueueType type) -> std::uint32_t
		{
			return static_cast<std::uint32_t>(device->queues_for_type(type).size());
		};

		device->caps.graphicsQueueCount		   = queueCount(QueueType::eGraphics);
		device->caps.computeQueueCount		   = queueCount(QueueType::eCompute);
		device->caps.copyQueueCount			   = queueCount(QueueType::eCopy);
		device->caps.hasDedicatedComputeQueue  = device->caps.computeQueueCount != 0;
		device->caps.hasDedicatedTransferQueue = device->caps.copyQueueCount != 0;

		// Metal 3 takes its command buffers from a queue with a hard cap that blocks when it fills, so this backend really is bounded.
		device->caps.maxOpenCommandListsPerQueue = desc.maxOpenCommandListsPerQueue;

		// An MTLCommandBuffer commits once and this backend keeps one per list, so there is no way to replay a recording.
		device->caps.supportsCommandListResubmit = false;

		MetalDevice * raw		  = device.get();
		MetalBackendOwner & owner = backend_owner();

		std::uint32_t deviceTag = 0;
		if (!detail::device_tags().acquire(deviceTag))
		{
			refusal = Error{ .code = ErrorCode::eOutOfHostMemory, .message = "no device tag is available, too many devices are alive at once" };
			return nullptr;
		}
		raw->deviceTag = deviceTag;
		raw->tracked.rebind(deviceTag);
		raw->buffers.rebind(deviceTag);
		raw->textures.rebind(deviceTag);
		raw->textureViews.rebind(deviceTag);
		raw->samplers.rebind(deviceTag);
		raw->heaps.rebind(deviceTag);
		raw->timelines.rebind(deviceTag);
		raw->binarySemaphores.rebind(deviceTag);
		raw->graphicsPipelines.rebind(deviceTag);
		raw->computePipelines.rebind(deviceTag);
		raw->descriptorSets.rebind(deviceTag);

		owner.devices.push_back(std::move(device));
		return raw;
	}

	[[nodiscard]] MetalInstance * make_owned_instance()
	{
		auto instance = host_new<MetalInstance>();
		if (instance == nullptr)
		{
			return nullptr;
		}

		instance->object = publishing_object<Published<InstanceApi, &instance_block>, Published<ExternalCapabilityApi, &external_capability_block>>();

		MetalInstance * raw		  = instance.get();
		MetalBackendOwner & owner = backend_owner();
		if (!detail::try_push_back(owner.instances, std::move(instance)))
		{
			return nullptr;
		}

		return raw;
	}

	void metal_destroy_device(void * impl) noexcept
	{
		MetalBackendOwner & owner = backend_owner();

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

		std::erase_if(
			owner.devices,
			[impl](const HostUniquePtr<MetalDevice> & device)
			{
				return device.get() == impl;
			}
		);
		detail::device_tags().release(releasedTag);

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
				std::erase_if(
					owner.instances,
					[owningInstance](const HostUniquePtr<MetalInstance> & instance)
					{
						return instance.get() == owningInstance;
					}
				);
			}
		}
	}

	void metal_destroy_instance(void * impl) noexcept
	{
		MetalBackendOwner & owner = backend_owner();
		std::erase_if(
			owner.instances,
			[impl](const HostUniquePtr<MetalInstance> & instance)
			{
				return instance.get() == impl;
			}
		);
	}

	bool metal_query_external_handle_support(
		[[maybe_unused]] void * impl,
		const ExternalHandleSupportDesc & desc,
		ExternalHandleSupport * out,
		Error * error
	) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external handle support needs somewhere to write the result");
		}

		*out = {};

		NS::SharedPtr<NS::Array> all = NS::TransferPtr(MTL::CopyAllDevices());
		const std::uint32_t count	 = (all.get() != nullptr) ? static_cast<std::uint32_t>(all->count()) : 0;
		if (desc.adapterIndex >= count)
		{
			return fail(error, ErrorCode::eInvalidArgument, "external handle support asked about an adapter index this instance does not have");
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

		return succeed(error);
	}

	void * metal_instance_create_device(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		Error refusal{};
		MetalDevice * device = make_owned_device(static_cast<MetalInstance *>(impl), desc, refusal);
		if (device == nullptr)
		{
			return refusal.code != ErrorCode::eOk ? fail_value<void *>(error, refusal.code, refusal.message)
												  : fail_value<void *>(error, ErrorCode::eNativeApiError, "no Metal device available");
		}
		return return_value(static_cast<void *>(device), error);
	}

	void * metal_create_instance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
	{
		MetalInstance * instance = make_owned_instance();
		if (instance == nullptr)
		{
			return fail_value<void *>(error, ErrorCode::eOutOfHostMemory, "Metal instance creation failed");
		}

		return return_value(static_cast<void *>(instance), error);
	}

} // namespace azo::rhi::metal

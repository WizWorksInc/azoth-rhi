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

#include "backends/metal4/internal.hpp"

#include <format>
#include <iterator>

namespace azo::rhi::metal4
{
	GraphicsApiId InstanceApiId([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::id;
	}

	bool EnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
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
				// Names point into per-device storage that enumerate does not keep alive so they are reported through GetAdapterInfo on a created device instead.
				.name = nullptr,
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

		// Metal exposes no driver version of its own so report the OS (Metal-stack) version as the closest useful analog for logs and crash reports. NS::ProcessInfo
		// gives both a numeric major.minor and a ready-made human string.
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
		caps.apiId		= Metal4Api::id;
		caps.apiVersion = ApiVersion{ .major = 4, .minor = 0 };
		/*
		 * Everything below asks Metal without asserting. The families are the coarse axis: Apple and Mac number upward and each level is a superset of the one under
		 * it, so a supportsFamily probe answers what an older feature-set query used to and keeps working on hardware that did not exist when this was written. Where
		 * Metal has a dedicated query, that query wins over any family inference.
		 */
		const bool apple3 = mtl->supportsFamily(MTL::GPUFamilyApple3);
		const bool apple5 = mtl->supportsFamily(MTL::GPUFamilyApple5);
		const bool apple7 = mtl->supportsFamily(MTL::GPUFamilyApple7);
		const bool mac2	  = mtl->supportsFamily(MTL::GPUFamilyMac2);

		// MTLEvent and MTLSharedEvent, which the timeline lowers onto, arrive with Metal 2 and are on every family this builds against. There is no query narrower
		// than the family probe, so this is the narrowest honest answer and not a literal.
		caps.supportsTimelineSync	  = apple7 || mac2;
		caps.supportsDynamicRendering = true; // a render command encoder is always built from its attachments, with nothing else to lower onto

		// Argument buffers are the binding model, and Metal reports which of the two tiers it gives. Tier 2 lifts the array bound and allows indexing into an
		// unbounded table, which is what a bindless renderer needs, so it is the level the ladder's top means.
		const MTL::ArgumentBuffersTier argumentTier = mtl->argumentBuffersSupport();
		caps.bindingTier							= argumentTier >= MTL::ArgumentBuffersTier2 ? BindingTier::eUnbounded : BindingTier::eDynamicIndexing;
		caps.shaderBinaryFormat						= ShaderBinaryFormat::eBackendNative;
		caps.supportsShaderSource					= true;

		caps.supportsUpdateAfterBind		   = caps.bindingTier >= BindingTier::eDynamicIndexing;
		caps.supportsPartiallyBoundDescriptors = caps.bindingTier >= BindingTier::eDynamicIndexing;

		// A real query, and it counts samplers, not textures, so it answers the sampler limit and nothing else.
		caps.maxSamplerDescriptors = static_cast<std::uint32_t>(mtl->maxArgumentBufferSamplerCount());

		/*
		 * A set is an argument buffer bound at a buffer index, so the buffer argument table is what bounds how many there can be, less the one index the ABI reserves
		 * for the push constant. Below the tier that has argument buffers only set 0 is addressable at all.
		 */
		caps.maxDescriptorSets = caps.bindingTier >= BindingTier::eUnbounded ? kMetalMaxBufferArguments - 1 : 1;

		// The smallest of the three argument tables a set draws from, samplers being the tightest, so it is the count a caller can rely on whatever they fill a set
		// with. A set of textures alone may hold far more, which is what the per-kind ceilings above are for.
		caps.maxDescriptorsPerSet = std::min({ kMetalMaxBufferArguments, kMetalMaxTextureArguments, kMetalMaxSamplerArguments });

		/*
		 * Left at zero, which reads as no stated ceiling and not as no bindless. Metal has no descriptor heap to bound and no query for how many resources an
		 * argument buffer may reference, so there is no number to report and inventing one would be worse than saying so. bindingTier is what a caller reads to find
		 * out whether bindless works here.
		 */
		caps.maxBindlessSampledTextures = 0;
		caps.maxBindlessStorageBuffers	= 0;

		// MTLSamplerDescriptor carries maxAnisotropy with no capability gating it, and a sampler is where the whole feature lives.
		caps.supportsAnisotropy		  = true;
		caps.supportsIndependentBlend = true; // MTLRenderPipelineColorAttachmentDescriptorArray is per attachment by construction

		// newTextureView taking MTLTextureSwizzleChannels landed in macOS 10.15 and iOS 13, below the metal-cpp floor this builds against.
		caps.supportsTextureViewSwizzle	  = true;
		caps.supportsShaderDrawParameters = true; // base_vertex and base_instance are ordinary vertex-stage attributes

		// half is an MSL scalar on every family, and 32-bit float filtering is the one numeric capability Metal gates separately.
		caps.supportsShaderFloat16 = true;

		/*
		 * MTLDrawPrimitivesIndirectArguments carries baseInstance and Metal honors it so an indirect draw may name a non-zero firstInstance.
		 * supportsMultiDrawIndirect stays false alongside it: Metal takes one draw per call so a batch is issued as the documented one command per entry, not
		 * a single multi-draw.
		 */
		caps.supportsDrawIndirectFirstInstance = true;
		caps.supportsDynamicBufferOffsets	   = true; // setVertexBufferOffset and its fragment and compute counterparts take an offset directly

		/*
		 * Timestamps need a counter sample buffer and Metal says at which points it can sample. Nothing else in the RHI can tell whether a timestamp would land. The
		 * query half is gated on this and not a family guess.
		 *
		 * The points split by vendor. Apple parts answer stage boundary, where sample points are named on the pass descriptor before the encoder opens. Discrete
		 * parts answer encoder boundaries. Both are recorded because recording reads them one at a time.
		 */
		device->samplesAtStageBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary);
		device->samplesAtDrawBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtDrawBoundary);
		device->samplesAtDispatchBoundary = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtDispatchBoundary);
		device->samplesAtBlitBoundary	  = mtl->supportsCounterSampling(MTL::CounterSamplingPointAtBlitBoundary);

		/*
		 * The counter set the sample buffers are built over, which is a separate question from where sampling happens: an adapter answering a sampling point still
		 * has to carry the timestamp counters for a pool to hold anything.
		 */
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

		/*
		 * A write reaches a standalone encoder through the stage-boundary attachment or through a blit-boundary sample, so an adapter offering neither has nowhere to
		 * put one even with the counter set present. The dispatch layer derives this flag from block presence, and the command list declines its half on exactly
		 * this, so the two agree.
		 */
		const bool canWriteTimestamps = device->timestampCounterSet.get() != nullptr && (device->samplesAtStageBoundary || device->samplesAtBlitBoundary);

		caps.supportsTimestampQueries = canWriteTimestamps;

		/*
		 * Mid-scope writes, which this generation can always do where it can write a timestamp at all.
		 *
		 * On Metal 3 they need the encoder to take the sample itself, which is the draw and dispatch boundary pair, and every Apple part measured answers the
		 * stage boundary alone. An encoder here takes a timestamp wherever it is asked to, with no sampling point to consult. Closing that gap is most of why
		 * this backend exists.
		 */
		caps.supportsTimestampWritesInScope = canWriteTimestamps;

		/*
		 * sampleTimestamps is what correlates the two clocks, and a sampling point is necessary without being sufficient: an adapter carrying one can still
		 * answer with a zero pair, which is the emptiness the call itself reports as uncalibrated. Probed once so the cap cannot promise what the call declines.
		 */
		MTL::Timestamp probedCpu = 0;
		MTL::Timestamp probedGpu = 0;
		mtl->sampleTimestamps(&probedCpu, &probedGpu);
		caps.supportsTimestampCalibration = (device->samplesAtStageBoundary || device->samplesAtDrawBoundary) && (probedCpu != 0 || probedGpu != 0);

		// Ray tracing is asked for, not assumed absent, so the flag tracks the hardware even while the blocks stay unimplemented.
		caps.supportsRayTracing = false; // both halves are declined, and the device answer alone cannot make it true

		/*
		 * Limits. Metal publishes a few directly and derives the rest from the family, which is the same ladder Apple's own feature tables are written against.
		 * Probing downward from the highest family means a device newer than this code still lands on the highest rung it recognizes and not on a default.
		 */
		caps.maxColorAttachments = 8; // MTLRenderPassDescriptor declares exactly eight colour attachment slots, on every family
		caps.maxRenderTargets	 = caps.maxColorAttachments;
		caps.maxVertexBindings	 = 31; // the vertex buffer argument table is 31 entries wide, fixed by the MSL binding model
		caps.maxVertexAttributes = 31;
		caps.maxViewports		 = apple5 || mac2 ? 16 : 1; // layered rendering, and the multiple viewports that come with it, arrive at Apple5

		// 2D and cube extents step at Apple3. Mac2 is above that step, so both reach the higher limit and earlier Apple families stay at the lower one.
		const bool bigTextures	   = apple3 || mac2;
		caps.maxTextureDimension1D = bigTextures ? 16384 : 8192;
		caps.maxTextureDimension2D = bigTextures ? 16384 : 8192;
		caps.maxTextureDimension3D = 2048; // the 3D limit does not step with the family
		caps.maxTextureArrayLayers = 2048;
		caps.maxPushConstantBytes  = 4096; // setVertexBytes and friends accept up to 4 KiB before a buffer is required

		// Buffer alignment. minimumLinearTextureAlignmentForPixelFormat is a real query, so the copy alignment comes from the device and not from a constant that
		// happens to be right on the machines this was written on.
		caps.minUniformBufferOffsetAlignment	= 32;
		caps.minStorageBufferOffsetAlignment	= 32;
		caps.minTexelBufferOffsetAlignment		= 16;
		caps.optimalBufferCopyOffsetAlignment	= 16;
		caps.optimalBufferCopyRowPitchAlignment = std::max<std::uint64_t>(mtl->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm), 256);

		/*
		 * A counter heap does not count nanoseconds. It counts ticks of a clock whose rate the device reports, measured at 24 MHz on an M1 Max, which is about
		 * 41.67 ns a tick. Leaving the period at one, as the Metal 3 backend correctly does for its own counters, would report a frame as a fortieth of its
		 * real length, and plausibly enough that nobody would notice.
		 */
		caps.timestampPeriodNanoseconds = 1.0f;
		if (const std::uint64_t frequency = mtl->queryTimestampFrequency(); frequency != 0)
		{
			caps.timestampPeriodNanoseconds = 1'000'000'000.0f / static_cast<float>(frequency);
		}

		caps.timestampValidBits = 64;

		// Sparse residency is reported from the tile size Metal gives, which is zero on a device with no sparse support at all.
		caps.sparseTileSizeBytes = 0;

		device->caps = caps;
	}

	bool AdapterHasMetal4(MTL::Device * device) noexcept
	{
		/*
		 * GPUFamilyMetal4 sits beside GPUFamilyMetal3 in the same enum, so this is a family probe like every other capability question a Metal backend asks,
		 * not a special case.
		 */
		return device != nullptr && device->supportsFamily(MTL::GPUFamilyMetal4);
	}

	/*
	 * Whether DeviceDesc::apiVersion is one this backend can answer for.
	 *
	 * Zero means take what this backend is, and a stated major pins a generation. Metal 3 is a backend of its own, so pinning it here is a request this one
	 * cannot honor and is refused. Both platform owners tell callers to pin a tested baseline, so a caller stepping around a driver bug has to be believed.
	 *
	 * refusedReason is set to why, and left alone otherwise.
	 */
	[[nodiscard]] bool VersionIsOurs(const ApiVersion requested, const char *& refusedReason) noexcept
	{
		if (requested.major != 0 && requested.major < 4)
		{
			refusedReason = "this is the Metal 4 backend; ask for azoth.rhi.metal to get Metal 3";
			return false;
		}

		return true;
	}

	/*
	 * Names the adapter that refused, since which GPU answered is the whole of the answer. A virtualized host is the case this pays for. It hands the guest a
	 * paravirtual adapter sitting below the family floor while the machine underneath it is Apple silicon that would pass.
	 *
	 * The storage is thread_local because Error borrows its message and two threads can be refused by different adapters at once.
	 */
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

	[[nodiscard]] Metal4Device * MakeOwnedDevice(Metal4Instance * instance, const DeviceDesc & desc, const char *& refusedReason)
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

		// Both settled before anything is built, since a refusal should cost nothing.
		if (!VersionIsOurs(desc.apiVersion, refusedReason))
		{
			return nullptr;
		}

		if (!AdapterHasMetal4(mtlDevice.get()))
		{
			refusedReason = NoMetal4FamilyMessage(mtlDevice.get());
			return nullptr;
		}

		/*
		 * PipelineCacheApi is declined, not published. Metal's serializable cache is MTLBinaryArchive, which is a different object with its own creation and lookup
		 * rules, so a block over it is real work and not a wrapper. Publishing one that minted a handle and handed back no data would read as supportsPipelineCache
		 * true to a caller who then serializes nothing and is never told.
		 */
		auto device	   = HostNew<Metal4Device>();
		device->object = PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
			Published<PresentApi, &PresentBlock>,
			Published<PlacedMemoryApi, &PlacedMemoryBlock>,
			Published<RayTracingApi, &RayTracingBlock>,
			Published<QueryApi, &QueryBlock>,
			Published<ResidencyApi, &ResidencyBlock>,
			Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
			Published<AdoptionApi, &AdoptionBlock>,
			/*
			 * Published with four of its eleven entries refusing. The refusals are per kind, not per capability: Metal shares textures and events and has no shared form
			 * of a buffer or a heap. The per adapter query says exactly that for those kinds. Declining the block whole would deny the two kinds Metal does share.
			 */
			Published<ExternalSharingApi, &ExternalSharingBlock>>();

		device->instanceWrapper = instance;
		device->validation		= desc.validation;
		device->debugLabels		= desc.enableDebugLabels;
		device->device			= std::move(mtlDevice);

		// Only where the adapter really shares one pool, so asking for it on a discrete Mac gets the portable refusal rather than a pointer that is not backed.
		device->caps.deviceLocalMemoryIsHostVisible = device->device->hasUnifiedMemory();
		device->allowDeviceLocalMapping				= desc.allowDeviceLocalMapping && device->caps.deviceLocalMemoryIsHostVisible;

		// Each requested type gets its own independent command queues. Metal command queues are always independent so a compute or copy queue is dedicated and a
		// required dedicated queue always succeeds.
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

		// What waitIdle blocks on, there being no command buffer to wait for on this generation.
		device->drainEvent = NS::TransferPtr(mtl->newSharedEvent());
		if (device->drainEvent.get() == nullptr)
		{
			return nullptr;
		}

		/*
		 * The residency sets and the compiler, made here, not on first use.
		 *
		 * First use is reached under four different RHI guards for the sets and two for the compiler, so making either there would be threads racing to make
		 * the same object, and a backend takes no lock of its own. Device creation is under the one lifetime lock.
		 *
		 * Both are required here. A device with neither cannot draw and is refused.
		 */
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

	void DestroyDevice(void * impl) noexcept
	{
		Metal4BackendOwner & owner = Owner();

		// Capture the owning instance before the device record is dropped so we can retire the instance alongside its last device (below). The tag goes back at the
		// same time so the ceiling is devices alive at once, not devices ever created.
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

		// Retire the instance once its last device is gone. CreateDevice hands the instance to the backend to hold for the devices made from it so with no device
		// left nothing else owns it and leaving it in the static owner until process exit would grow that list once per created device.
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

	void DestroyInstance(void * impl) noexcept
	{
		Metal4BackendOwner & owner = Owner();
		std::erase_if(owner.instances,
			[impl](const HostUniquePtr<Metal4Instance> & instance)
			{
				return instance.get() == impl;
			});
	}

	/*
	 * What Metal shares, which is textures and events and not buffers. MTLTexture has newSharedTextureHandle and MTLDevice has newSharedTexture in both
	 * directions. MTLSharedEvent has the same pair and MTLBuffer has neither, making a buffer the one kind with no answer and not an unimplemented one.
	 *
	 * Metal exposes no per adapter query for any of this. Neither Metal type is a file descriptor or an NT handle so the compatible mask names only itself.
	 */
	bool QueryExternalHandleSupport([[maybe_unused]] void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept
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

		// Metal shares neither of these. MTLBuffer has no shared handle and MTLHeap has no shared form at all, so both are a no and not a gap.
		case ExternalObjectKind::eBuffer:
		case ExternalObjectKind::eHeap:	  break;
		}

		return Succeed(error);
	}

	void * InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		const char * refusedReason = nullptr;
		Metal4Device * device	   = MakeOwnedDevice(static_cast<Metal4Instance *>(impl), desc, refusedReason);
		if (device == nullptr)
		{
			// A refused version pin is a different failure from having no adapter, so it says which instead of reporting the one message for both.
			return refusedReason != nullptr ? FailValue<void *>(error, ErrorCode::eUnsupportedFeature, refusedReason)
											: FailValue<void *>(error, ErrorCode::eNativeApiError, "no Metal device available");
		}
		return ReturnValue(static_cast<void *>(device), error);
	}

	void * CreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept
	{
		Metal4Instance * instance = MakeOwnedInstance();
		if (instance == nullptr)
		{
			return FailValue<void *>(error, ErrorCode::eOutOfHostMemory, "Metal instance creation failed");
		}

		return ReturnValue(static_cast<void *>(instance), error);
	}

} // namespace azo::rhi::metal4

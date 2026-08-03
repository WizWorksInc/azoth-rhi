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

#ifdef _WIN32

	#include "backends/d3d12/internal.hpp"

namespace azo::rhi::d3d12
{
	/*
	 * What a queue from this device publishes.
	 *
	 * Sparse binding needs a tiled resources tier the adapter may not have so a queue on a device without it declines the block. That leaves whether sparse works
	 * stated once, in the one place DeviceCaps reads it back from.
	 */
	const void * D3D12QueueQueryInterface(void * object, const InterfaceId id, const std::uint32_t minVersion) noexcept
	{
		const auto * queue = static_cast<const D3D12Queue *>(object);

		if (id == InterfaceTraits<SparseApi>::kId && queue->owner->caps.sparseTier == SparseTier::eNone)
		{
			return nullptr;
		}

		return QueryPublished<Published<QueueApi, &QueueBlock>, Published<SparseApi, &SparseBlock>>(object, id, minVersion);
	}

	const BackendObject * QueueObject() noexcept
	{
		static constexpr BackendObject object{ .queryInterface = &D3D12QueueQueryInterface };
		return &object;
	}

	[[nodiscard]] AdapterType ClassifyAdapter(const DXGI_ADAPTER_DESC3 & desc, bool unifiedMemory) noexcept
	{
		if ((desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0)
		{
			return AdapterType::eCpu;
		}
		return unifiedMemory ? AdapterType::eIntegrated : AdapterType::eDiscrete;
	}

	// Fills AdapterInfo from a DXGI adapter description (name is filled by the caller from stable storage).
	void FillAdapterInfo(AdapterInfo & info, const DXGI_ADAPTER_DESC3 & desc, std::uint32_t index, bool unifiedMemory) noexcept
	{
		info.type						= ClassifyAdapter(desc, unifiedMemory);
		info.apiId						= D3D12Api::id;
		info.adapterIndex				= index;
		info.vendorId					= desc.VendorId;
		info.deviceId					= desc.DeviceId;
		info.dedicatedVideoMemoryBytes	= desc.DedicatedVideoMemory;
		info.dedicatedSystemMemoryBytes = desc.DedicatedSystemMemory;
		info.sharedSystemMemoryBytes	= desc.SharedSystemMemory;
		info.unifiedMemoryArchitecture	= unifiedMemory;
		info.linkedAdapter				= false;

		/*
		 * The LUID is the identity Direct3D 12 has, and it is the same value Vulkan reports for this adapter on this machine, which is what lets a caller pair an RHI
		 * device with the one another API is holding.
		 *
		 * deviceUUID and driverUUID are left zeroed because DXGI has no equivalent to report and a fabricated one would compare unequal to the Vulkan value for the
		 * same adapter, which is worse than an obvious absence.
		 */
		info.deviceLUID		 = std::bit_cast<std::array<std::uint8_t, sizeof(LUID)>>(desc.AdapterLuid);
		info.deviceLUIDValid = true;
	}

	[[nodiscard]] D3D12DriverVersion QueryDriverVersion(IDXGIAdapter * adapter) noexcept
	{
		LARGE_INTEGER umd{};
		if (adapter == nullptr || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
		{
			return {};
		}
		const std::uint64_t raw = static_cast<std::uint64_t>(umd.QuadPart);
		return { FormatD3D12DriverVersion(raw), raw };
	}

	// Negotiates the DeviceCaps for a created device from D3D12 feature queries.
	void NegotiateCaps(D3D12Device & dev, D3D_FEATURE_LEVEL level) noexcept
	{
		DeviceCaps & caps = dev.caps;
		caps.apiId		  = D3D12Api::id;
		caps.apiVersion	  = FeatureLevelToApiVersion(level);

		ID3D12Device * device = dev.device.Get();

		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
		device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

		D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
		arch.NodeIndex = 0;
		device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch));

		D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
		const bool haveOptions12 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)));

		D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2{};
		const bool haveOptions2 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2)));

		D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4{};
		const bool haveOptions4 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options4, sizeof(options4)));

		// Core to the API tier so always available: timeline fences, dynamic rendering, placed resources, pipeline caching, timestamps.
		caps.supportsTimelineSync	  = true;
		caps.supportsDynamicRendering = true;
		caps.supportsTimestampQueries = true;
		// A timestamp is an EndQuery wherever a command can be recorded, this path binding render targets without opening a pass object, so there is no scope for one
		// to be inside of.
		caps.supportsTimestampWritesInScope = true;
		// D3D12_FILTER_ANISOTROPIC and MaxAnisotropy are part of the sampler description at every feature level, with no capability gating them.
		caps.supportsAnisotropy = true;
		// Every binding lowers to a descriptor table here, never to a root CBV/SRV/UAV so root descriptors are not offered.
		caps.supportsRootDescriptors = false;

		// One resource binding tier decides every level, which is why these cannot vary independently and are read back as one ladder.
		caps.bindingTier					   = BindingTierFromResourceBindingTier(options.ResourceBindingTier);
		caps.shaderBinaryFormat				   = ShaderBinaryFormat::eDxil;
		caps.supportsPartiallyBoundDescriptors = caps.bindingTier >= BindingTier::eDynamicIndexing;
		caps.supportsUpdateAfterBind		   = caps.bindingTier >= BindingTier::eDynamicIndexing;

		// A Tier 2 shader-visible heap holds up to a million descriptors, which is the ceiling a bindless array is planned against. Left at zero below the tier that
		// allows one, which reads as no stated ceiling and not as no bindless, since bindingTier already carries that.
		if (caps.bindingTier >= BindingTier::eUnbounded)
		{
			caps.maxBindlessSampledTextures = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
			caps.maxBindlessStorageBuffers	= D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
		}

		// A sampler heap is a fixed size here whatever the tier, unlike the resource heap, so this is the ceiling on a sampler array.
		if (caps.bindingTier >= BindingTier::eUnbounded)
		{
			caps.maxSamplerDescriptors = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
		}

		/*
		 * A set lowers to descriptor tables in the root signature and the signature is a fixed budget of DWORDs. Each set costs up to two, one table for the
		 * resources and one for the samplers, so the budget divided by two is the number of sets a layout can carry before the signature will not build. Push
		 * constants come out of the same budget, which is why this is a ceiling and not a promise.
		 */
		caps.maxDescriptorSets = D3D12_MAX_ROOT_COST / 2;

		// One table can address the whole shader-visible heap, so the heap this device created is what bounds a single set.
		caps.maxDescriptorsPerSet = kD3D12GlobalResourceCapacity;

		// Structured and raw buffer views start on a 16 byte boundary. Typed texel buffers additionally align to their element, which is a per-format rule and not a
		// device limit, so the raw alignment is what a caller can budget against without knowing the format.
		caps.minTexelBufferOffsetAlignment = D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;

		caps.conservativeRasterTier	  = ConservativeRasterTierFrom(options.ConservativeRasterizationTier);
		caps.supportsEnhancedBarriers = haveOptions12 && options12.EnhancedBarriersSupported != FALSE;
		caps.supportsDepthBounds	  = haveOptions2 && options2.DepthBoundsTestSupported != FALSE;
		// Native 16-bit shader math, distinct from R16Float storage. Needs shader model 6.2 compiled with -enable-16bit-types.
		caps.supportsShaderFloat16 = haveOptions4 && options4.Native16BitShaderOpsSupported != FALSE;

		// A tile is a fixed 64 KiB here, whatever the tier, and means nothing when there is no tier.
		caps.sparseTier			 = SparseTierFromTiledResourcesTier(options.TiledResourcesTier);
		caps.sparseTileSizeBytes = caps.sparseTier > SparseTier::eNone ? kD3D12TileSizeBytes : 0;

		// Implemented here so reported, not left at the false defaults a caller would gate working code on.
		caps.supportsIndependentBlend	= true; // the graphics pipeline always writes per-RT blend state
		caps.supportsTextureViewSwizzle = true; // Shader4ComponentMapping has been on every shader resource view since the first release
		// NV12 and P010 are DXGI formats every D3D12 device declares. Whether a given adapter can sample one is a per-format support query, not a device feature,
		// which getFormatSupport answers.
		caps.supportsMultiPlanarFormats		   = true;
		caps.supportsIndirectCount			   = true; // drawIndirectCount uses ExecuteIndirect with a count buffer
		caps.supportsMultiDrawIndirect		   = true; // one ExecuteIndirect issues MaxCommandCount draws, with no feature to gate it
		caps.supportsDrawIndirectFirstInstance = true; // StartInstanceLocation is a field of the draw argument struct
		caps.supportsShaderDrawParameters	   = true; // SV_VertexID and SV_InstanceID are always readable, with no feature to gate them
		caps.supportsPipelineStatisticsQueries = true; // pipeline-statistics query heaps are created and resolved
		caps.supportsTimestampCalibration	   = true; // calibrateTimestamp uses ID3D12CommandQueue::GetClockCalibration

		// Common D3D12 limits. These are fixed by the API spec, not queried.
		caps.maxColorAttachments   = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
		caps.maxRenderTargets	   = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
		caps.maxVertexBindings	   = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
		caps.maxVertexAttributes   = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
		caps.maxViewports		   = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		caps.maxTextureDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
		caps.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		caps.maxTextureDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
		caps.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
		caps.maxPushConstantBytes  = D3D12_MAX_ROOT_COST * sizeof(std::uint32_t);

		caps.minUniformBufferOffsetAlignment	= D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		caps.minStorageBufferOffsetAlignment	= D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
		caps.optimalBufferCopyOffsetAlignment	= D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
		caps.optimalBufferCopyRowPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

		// D3D12 timestamps are full 64-bit counters with no valid-bits concept so report 64.
		caps.timestampValidBits			   = 64;
		UINT64 frequency				   = 0;
		ID3D12CommandQueue * graphicsQueue = dev.graphicsQueues.empty() ? nullptr : dev.graphicsQueues.front().queue.Get();
		if (graphicsQueue != nullptr && SUCCEEDED(graphicsQueue->GetTimestampFrequency(&frequency)) && frequency != 0)
		{
			caps.timestampPeriodNanoseconds = 1.0e9f / static_cast<float>(frequency);
		}

		caps.supportsMultiAdapter = false;
	}

	void DestroyDeviceObject(D3D12Device * device) noexcept
	{
		if (device == nullptr)
		{
			return;
		}

		D3D12BackendOwner & owner = Owner();

		// Captured before the record drops so the instance it came from can be retired alongside it below.
		D3D12Instance * owningInstance = nullptr;
		bool found					   = false;
		for (auto it = owner.devices.begin(); it != owner.devices.end(); ++it)
		{
			if (it->get() == device)
			{
				owningInstance = (*it)->instanceWrapper;

				// The tag goes back before the record drops so the ceiling is devices alive at once, not devices ever created.
				const std::uint32_t releasedTag = (*it)->deviceTag;
				owner.devices.erase(it);
				detail::DeviceTags().Release(releasedTag);
				found = true;
				break;
			}
		}

		if (!found)
		{
			return;
		}

		/*
		 * Retire the instance once its last device is gone.
		 *
		 * On the dynamic form the instance lives in this owner and the device only borrows it so without this nothing ever releases it and every create-and-destroy
		 * round leaves one behind for the life of the process. The static form owns its instance in the device itself and has already released it above, which is why
		 * it leaves instanceWrapper null.
		 */
		if (owningInstance == nullptr)
		{
			return;
		}

		for (const HostUniquePtr<D3D12Device> & remaining : owner.devices)
		{
			if (remaining->instanceWrapper == owningInstance)
			{
				return;
			}
		}

		std::erase_if(owner.instances,
			[owningInstance](const HostUniquePtr<D3D12Instance> & instance)
			{
				return instance.get() == owningInstance;
			});
	}

	// Device identity, caps, adapter and queue access. The resource, descriptor, pipeline and sync entries live in their own translation units.
	GraphicsApiId D3D12DeviceApiId([[maybe_unused]] void * impl) noexcept
	{
		return D3D12Api::id;
	}

	std::string_view D3D12DeviceApiName([[maybe_unused]] void * impl) noexcept
	{
		return D3D12Api::displayName;
	}

	const DeviceCaps & D3D12DeviceCaps(void * impl) noexcept
	{
		return static_cast<D3D12Device *>(impl)->caps;
	}

	const AdapterInfo & D3D12DeviceAdapterInfo(void * impl) noexcept
	{
		return static_cast<D3D12Device *>(impl)->adapterInfo;
	}

	ValidationMessageCounts D3D12DeviceValidationMessageCounts(void * impl) noexcept
	{
		const auto * device = static_cast<D3D12Device *>(impl);
		return ValidationMessageCounts{ .errors = device->validationErrors.load(std::memory_order_relaxed),
			.warnings							= device->validationWarnings.load(std::memory_order_relaxed) };
	}

	FormatSupport D3D12DeviceFormatSupport(void * impl, Format format) noexcept
	{
		FormatSupport support{ .format = format };
		const DXGI_FORMAT dxgi = MapFormat(format);
		if (dxgi == DXGI_FORMAT_UNKNOWN)
		{
			return support;
		}

		auto * device = static_cast<D3D12Device *>(impl);
		D3D12_FEATURE_DATA_FORMAT_SUPPORT query{
			.Format	  = dxgi,
			.Support1 = D3D12_FORMAT_SUPPORT1_NONE,
			.Support2 = D3D12_FORMAT_SUPPORT2_NONE,
		};
		if (FAILED(device->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &query, sizeof(query))))
		{
			return support;
		}

		const auto has = [&](D3D12_FORMAT_SUPPORT1 bit) noexcept
		{
			return (query.Support1 & bit) != 0;
		};
		support.sampled				   = has(D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) || has(D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
		support.storage				   = has(D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
		support.colorAttachment		   = has(D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
		support.depthStencilAttachment = has(D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
		support.linearFiltering		   = has(D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
		support.blendable			   = has(D3D12_FORMAT_SUPPORT1_BLENDABLE);
		// D3D12 has no copy-capability bit: any format realizable as a texture or buffer can be either end of a copy.
		const bool usableResource = has(D3D12_FORMAT_SUPPORT1_TEXTURE1D) || has(D3D12_FORMAT_SUPPORT1_TEXTURE2D) || has(D3D12_FORMAT_SUPPORT1_TEXTURE3D) ||
									has(D3D12_FORMAT_SUPPORT1_TEXTURECUBE) || has(D3D12_FORMAT_SUPPORT1_BUFFER);
		support.copySrc			  = usableResource;
		support.copyDst			  = usableResource;
		return support;
	}

	void * D3D12GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept
	{
		auto * device						  = static_cast<D3D12Device *>(impl);
		detail::HostVector<D3D12Queue> & pool = device->QueuesForType(type);
		if (index >= pool.size())
		{
			Fail(error, ErrorCode::eInvalidArgument, "queue index is out of range for the requested queue type");
			return nullptr;
		}

		Succeed(error);
		return &pool[index];
	}

	void D3D12DestroyDevice(void * impl) noexcept
	{
		DestroyDeviceObject(static_cast<D3D12Device *>(impl));
	}

	QueueType D3D12QueueType(void * impl) noexcept
	{
		return static_cast<D3D12Queue *>(impl)->type;
	}

	std::uint32_t D3D12QueueFamilyIndex([[maybe_unused]] void * impl) noexcept
	{
		// D3D12 has no queue-family index. Report the queue kind ordinal for a stable identifier.
		return static_cast<std::uint32_t>(static_cast<D3D12Queue *>(impl)->type);
	}

	GraphicsApiId D3D12InstanceApiId([[maybe_unused]] void * impl) noexcept
	{
		return D3D12Api::id;
	}

	/*
	 * What Direct3D 12 shares, which is nearly everything and cannot be asked about. ID3D12Device::CreateSharedHandle is core and takes both a resource and a
	 * fence. Direct3D 12 exposes no capability to ask an adapter first, so this reports what the API offers, not what a query answered.
	 *
	 * A shared handle is scoped to the adapter that created it and opening it on another is disallowed, which is the cross adapter case this leaves out.
	 */
	bool D3D12QueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support needs somewhere to write the result");
		}

		*out			= {};
		auto * instance = static_cast<D3D12Instance *>(impl);

		std::uint32_t adapterCount = 0;
		if (!D3D12EnumerateAdapters(instance, {}, &adapterCount, error))
		{
			return false;
		}
		if (desc.adapterIndex >= adapterCount)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "external handle support asked about an adapter index this instance does not have");
		}

		// CreateSharedHandle produces an NT handle either way. eD3D12Resource and eD3D12Fence are the names Vulkan gives that same handle when importing it, so both
		// are reported alongside the plain NT type and the mask says they alias each other.
		switch (desc.kind)
		{
		// A heap is shared under its own name and not the resource one, which is the distinction Vulkan and CUDA both draw when importing one.
		case ExternalObjectKind::eHeap:
			if (desc.handleType == ExternalHandleType::eOpaqueWin32 || desc.handleType == ExternalHandleType::eD3D12Heap)
			{
				out->exportable		 = true;
				out->importable		 = true;
				out->compatibleTypes = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Heap;
			}
			break;

		case ExternalObjectKind::eBuffer:
		case ExternalObjectKind::eTexture:
			if (desc.handleType == ExternalHandleType::eOpaqueWin32 || desc.handleType == ExternalHandleType::eD3D12Resource)
			{
				out->exportable		 = true;
				out->importable		 = true;
				out->compatibleTypes = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Resource;
			}
			break;

		case ExternalObjectKind::eTimeline:
		case ExternalObjectKind::eBinarySemaphore:
			if (desc.handleType == ExternalHandleType::eOpaqueWin32 || desc.handleType == ExternalHandleType::eD3D12Fence)
			{
				out->exportable		 = true;
				out->importable		 = true;
				out->compatibleTypes = Flags<ExternalHandleType>(ExternalHandleType::eOpaqueWin32) | ExternalHandleType::eD3D12Fence;
			}
			break;
		}

		return Succeed(error);
	}

	bool D3D12EnumerateAdapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept
	{
		auto * instance = static_cast<D3D12Instance *>(impl);
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "adapter count output is null");
		}
		*out = 0;

		instance->adapterNames.clear();
		instance->driverVersions.clear();
		detail::HostVector<DXGI_ADAPTER_DESC3> descs;
		detail::HostVector<std::uint64_t> driverRaws;
		for (UINT i = 0;; ++i)
		{
			ComPtr<IDXGIAdapter4> adapter;
			if (instance->factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			DXGI_ADAPTER_DESC3 desc{};
			if (FAILED(adapter->GetDesc3(&desc)))
			{
				continue;
			}
			// Skip the Basic Render Driver (software) adapter unless it is the only thing present.
			descs.push_back(desc);
			instance->adapterNames.push_back(NarrowAdapterName(desc.Description));
			const D3D12DriverVersion dv = QueryDriverVersion(adapter.Get());
			instance->driverVersions.push_back(dv.text);
			driverRaws.push_back(dv.raw);
		}

		*out = static_cast<std::uint32_t>(descs.size());

		const std::size_t count = std::min<std::size_t>(adapters.size(), descs.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			FillAdapterInfo(adapters[i], descs[i], static_cast<std::uint32_t>(i), false);
			adapters[i].name			 = instance->adapterNames[i].c_str();
			adapters[i].driverVersionRaw = driverRaws[i];
			adapters[i].driverVersion	 = instance->driverVersions[i].empty() ? nullptr : instance->driverVersions[i].c_str();
		}

		return Succeed(error);
	}

	void * D3D12InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;

	[[nodiscard]] HostUniquePtr<D3D12Instance> BuildInstance(const InstanceDesc & desc, Error * error)
	{
		auto instance			  = HostNew<D3D12Instance>();
		instance->object		  = PublishingObject<Published<InstanceApi, &InstanceBlock>, Published<ExternalCapabilityApi, &ExternalCapabilityBlock>>();
		instance->onMessage		  = desc.nativeValidation.onMessage;
		instance->messageUserData = desc.nativeValidation.messageUserData;

		UINT factoryFlags	 = 0;
		const bool wantDebug = desc.validation == ValidationMode::eDeveloper || desc.validation == ValidationMode::eCapture ||
							   desc.nativeValidation.apiValidation == NativeValidationPolicy::eEnabled;
		if (wantDebug)
		{
			ComPtr<ID3D12Debug> debug;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			{
				debug->EnableDebugLayer();
				instance->debugLayer = true;
				factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

				if (desc.nativeValidation.gpuBasedValidation == NativeValidationPolicy::eEnabled)
				{
					ComPtr<ID3D12Debug1> debug1;
					if (SUCCEEDED(debug.As(&debug1)))
					{
						debug1->SetEnableGPUBasedValidation(TRUE);
					}
				}
			}
		}

		if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&instance->factory))))
		{
			Fail(error, ErrorCode::eNativeApiError, "CreateDXGIFactory2 failed");
			return nullptr;
		}

		Succeed(error);
		return instance;
	}

	// Creates a command queue of the given type on the device.
	[[nodiscard]] ComPtr<ID3D12CommandQueue> CreateQueue(ID3D12Device * device, D3D12_COMMAND_LIST_TYPE type)
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type	= type;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		ComPtr<ID3D12CommandQueue> queue;
		if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))))
		{
			return nullptr;
		}
		return queue;
	}

	/*
	 * Selects an adapter, creates the device, its queues and the D3D12MA allocator, negotiates caps and registers with the process owner. Null with *error set on
	 * failure. ownedInstance is non-null in the static CreateDevice form, where the device owns its factory. Runs once under the owner mutex so it needs no lock
	 * of its own.
	 */
	[[nodiscard]] D3D12Device * MakeOwnedDevice(D3D12Instance * instance, HostUniquePtr<D3D12Instance> ownedInstance, const DeviceDesc & desc, Error * error)
	{
		D3D12BackendOwner & owner = Owner();

		const D3D_FEATURE_LEVEL floor = ApiVersionToFloor(desc.apiVersion);

		// Walk adapters in high-performance order and pick the first that creates a device at the floor.
		ComPtr<IDXGIAdapter4> chosenAdapter;
		ComPtr<ID3D12Device> chosenDevice;
		DXGI_ADAPTER_DESC3 chosenDesc{};
		for (UINT i = 0;; ++i)
		{
			ComPtr<IDXGIAdapter4> adapter;
			if (instance->factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			DXGI_ADAPTER_DESC3 adapterDesc{};
			if (FAILED(adapter->GetDesc3(&adapterDesc)))
			{
				continue;
			}
			if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0 && !desc.allowSoftwareAdapter)
			{
				continue;
			}
			if (desc.preferredAdapterIndex != kInvalidIndex && desc.preferredAdapterIndex != i)
			{
				continue;
			}

			ComPtr<ID3D12Device> device;
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), floor, IID_PPV_ARGS(&device))))
			{
				chosenAdapter = adapter;
				chosenDevice  = device;
				chosenDesc	  = adapterDesc;
				break;
			}
		}

		if (!chosenDevice)
		{
			Fail(error, ErrorCode::eNativeApiError, "no Direct3D 12 adapter satisfied the requested feature level");
			return nullptr;
		}

		auto dev			 = HostNew<D3D12Device>();
		dev->object			 = PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
			Published<PresentApi, &PresentBlock>,
			Published<PlacedMemoryApi, &PlacedMemoryBlock>,
			Published<QueryApi, &QueryBlock>,
			Published<PipelineCacheApi, &PipelineCacheBlock>,
			Published<ResidencyApi, &ResidencyBlock>,
			Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
			Published<AdoptionApi, &AdoptionBlock>,
			// Unconditional, unlike the Vulkan device's. CreateSharedHandle is core Direct3D 12 with no extension to enable and no capability to ask first, so a device
			// that came up at all can share.
			Published<ExternalSharingApi, &ExternalSharingBlock>>();
		dev->factory		 = instance->factory;
		dev->adapter		 = chosenAdapter;
		dev->device			 = chosenDevice;
		dev->validation		 = desc.validation;
		dev->debugNames		 = desc.enableDebugNames;
		dev->debugLabels	 = desc.enableDebugLabels;
		dev->onMessage		 = instance->onMessage;
		dev->messageUserData = instance->messageUserData;

		/*
		 * Count validation messages as the debug layer emits them, which makes GetValidationMessageCounts mean the same thing here as on Vulkan. Reading the info
		 * queue's stored-message count instead measures what has not been discarded yet and drops back down once the queue fills.
		 *
		 * Conditional twice over: the callback interface arrived after the original info queue and the queue only exists while the debug layer is installed. Either
		 * one missing leaves the counters at zero, which the cap reports as not measured.
		 */
	#ifdef __ID3D12InfoQueue1_INTERFACE_DEFINED__
		if (instance->debugLayer && SUCCEEDED(chosenDevice.As(&dev->infoQueue)))
		{
			const auto onMessage = [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void * context)
			{
				auto * counted = static_cast<D3D12Device *>(context);

				// Info and message severities reach neither the counters nor the callback, which keeps this the same two classes the Vulkan messenger subscribes to and not
				// a channel that says more on one backend than the other.
				ValidationMessageSeverity classified{};
				if (severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || severity == D3D12_MESSAGE_SEVERITY_ERROR)
				{
					counted->validationErrors.fetch_add(1, std::memory_order_relaxed);
					classified = ValidationMessageSeverity::eError;
				}
				else if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
				{
					counted->validationWarnings.fetch_add(1, std::memory_order_relaxed);
					classified = ValidationMessageSeverity::eWarning;
				}
				else
				{
					return;
				}

				// No stderr fallback opposite this one. The debug layer already reports through the platform, so printing here would be a second copy of what a debugger is
				// showing and not the only copy, which is what the Vulkan echo is.
				if (counted->onMessage != nullptr && description != nullptr)
				{
					counted->onMessage(classified, description, counted->messageUserData);
				}
			};

			if (SUCCEEDED(dev->infoQueue->RegisterMessageCallback(onMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, dev.get(), &dev->infoQueueCookie)))
			{
				dev->caps.reportsValidationMessageCounts = true;
			}
			else
			{
				dev->infoQueue.Reset();
			}
		}
	#endif

		// A distinct tag per live device, carried in the top bits of every handle's index word so another device rejects it outright. This backend used to leave
		// every registry on the default so two D3D12 devices handed out interchangeable handles from the very first resource.
		std::uint32_t deviceTag = 0;
		if (!detail::DeviceTags().Acquire(deviceTag))
		{
			Fail(error, ErrorCode::eOutOfHostMemory, "no device tag is available, too many devices are alive at once");
			return nullptr;
		}
		dev->deviceTag = deviceTag;

		/*
		 * The tag goes back on every way out of here that does not reach the owner list. The pool is process-global with a ceiling of 255 and is shared by every
		 * backend, so a caller probing adapters in a loop would otherwise drop that ceiling by one per failed create until no backend can make a device at all.
		 * Dismissed once the record is owned, from which point the destroy path releases it.
		 */
		auto tagGuard = detail::MakeScopeGuard(
			[deviceTag]() noexcept
			{
				detail::DeviceTags().Release(deviceTag);
			});

		// Seed each handle registry with the device tag.
		dev->bufferSlots.Rebind(deviceTag);
		dev->textureSlots.Rebind(deviceTag);
		dev->textureViewSlots.Rebind(deviceTag);
		dev->samplerSlots.Rebind(deviceTag);
		dev->heapSlots.Rebind(deviceTag);
		dev->descriptorSetLayoutSlots.Rebind(deviceTag);
		dev->pipelineLayoutSlots.Rebind(deviceTag);
		dev->graphicsPipelineSlots.Rebind(deviceTag);
		dev->computePipelineSlots.Rebind(deviceTag);
		dev->pipelineCacheSlots.Rebind(deviceTag);
		dev->queryPoolSlots.Rebind(deviceTag);
		dev->timelineSlots.Rebind(deviceTag);
		dev->binarySemaphoreSlots.Rebind(deviceTag);
		dev->descriptorSetSlots.Rebind(deviceTag);

		// The highest feature level the chosen adapter actually supports, recorded in caps.apiVersion.
		D3D_FEATURE_LEVEL achieved = floor;
		D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
		levels.NumFeatureLevels		   = static_cast<UINT>(kProbeLevels.size());
		levels.pFeatureLevelsRequested = kProbeLevels.data();
		if (SUCCEEDED(chosenDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))))
		{
			achieved = levels.MaxSupportedFeatureLevel;
		}

		// One queue per requested count, each carrying a fence used only for WaitIdle. An empty request list defaults to one of each type.
		const QueuePlan plan  = PlanQueues(desc.queues);
		const auto makeQueues = [&](detail::HostVector<D3D12Queue> & out, QueueType type, D3D12_COMMAND_LIST_TYPE d3dType, std::uint32_t count) -> bool
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				D3D12Queue queue{
					.object = QueueObject(),
					.queue	= CreateQueue(chosenDevice.Get(), d3dType),
					.type	= type,
					.owner	= dev.get(),
				};
				if (!queue.queue)
				{
					return false;
				}

				chosenDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(queue.idleFence.GetAddressOf()));
				out.push_back(std::move(queue));
			}

			return true;
		};
		if (!makeQueues(dev->graphicsQueues, QueueType::eGraphics, D3D12_COMMAND_LIST_TYPE_DIRECT, plan.graphicsCount) ||
			!makeQueues(dev->computeQueues, QueueType::eCompute, D3D12_COMMAND_LIST_TYPE_COMPUTE, plan.computeCount) ||
			!makeQueues(dev->copyQueues, QueueType::eCopy, D3D12_COMMAND_LIST_TYPE_COPY, plan.copyCount))
		{
			Fail(error, ErrorCode::eNativeApiError, "failed to create a D3D12 command queue");
			return nullptr;
		}

		D3D12MA::ALLOCATOR_DESC allocatorDesc{};
		allocatorDesc.pDevice  = chosenDevice.Get();
		allocatorDesc.pAdapter = chosenAdapter.Get();
		if (FAILED(D3D12MA::CreateAllocator(&allocatorDesc, dev->allocator.GetAddressOf())))
		{
			Fail(error, ErrorCode::eNativeApiError, "D3D12MA::CreateAllocator failed");
			return nullptr;
		}

		if (!dev->rtvHeap.Init(chosenDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256) ||
			!dev->dsvHeap.Init(chosenDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64))
		{
			Fail(error, ErrorCode::eNativeApiError, "failed to create the D3D12 RTV or DSV descriptor heap");
			return nullptr;
		}

		// The shared shader-visible heaps every arena bumps from, plus their CPU staging heaps. These cannot grow so the resource heap is sized for the bindless
		// table and every pass's sets and the sampler heap sits at the Tier 2 limit.
		{
			const auto makeGlobalHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t count, bool shaderVisible, ComPtr<ID3D12DescriptorHeap> & out)
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
				heapDesc.Type			= type;
				heapDesc.NumDescriptors = count;
				heapDesc.Flags			= shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				return SUCCEEDED(chosenDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(out.GetAddressOf())));
			};
			constexpr std::uint32_t kGlobalResourceCapacity = kD3D12GlobalResourceCapacity;
			constexpr std::uint32_t kGlobalSamplerCapacity	= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
			if (!makeGlobalHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kGlobalResourceCapacity, true, dev->globalResourceHeap) ||
				!makeGlobalHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kGlobalSamplerCapacity, true, dev->globalSamplerHeap) ||
				!makeGlobalHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kGlobalResourceCapacity, false, dev->globalResourceStaging) ||
				!makeGlobalHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kGlobalSamplerCapacity, false, dev->globalSamplerStaging))
			{
				Fail(error, ErrorCode::eNativeApiError, "failed to create the shared D3D12 descriptor heaps");
				return nullptr;
			}
			dev->globalResourceIncrement = chosenDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			dev->globalSamplerIncrement	 = chosenDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			dev->globalResourceCapacity	 = kGlobalResourceCapacity;
			dev->globalSamplerCapacity	 = kGlobalSamplerCapacity;
		}

		D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
		chosenDevice->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch));
		const bool unifiedMemory = arch.UMA != FALSE;

		dev->adapterName = NarrowAdapterName(chosenDesc.Description);
		FillAdapterInfo(dev->adapterInfo, chosenDesc, 0, unifiedMemory);
		dev->adapterInfo.name			  = dev->adapterName.c_str();
		const D3D12DriverVersion dv		  = QueryDriverVersion(chosenAdapter.Get());
		dev->driverVersion				  = dv.text;
		dev->adapterInfo.driverVersionRaw = dv.raw;
		dev->adapterInfo.driverVersion	  = dev->driverVersion.empty() ? nullptr : dev->driverVersion.c_str();

		NegotiateCaps(*dev, achieved);
		dev->caps.graphicsQueueCount		= static_cast<std::uint32_t>(dev->graphicsQueues.size());
		dev->caps.computeQueueCount			= static_cast<std::uint32_t>(dev->computeQueues.size());
		dev->caps.copyQueueCount			= static_cast<std::uint32_t>(dev->copyQueues.size());
		dev->caps.hasDedicatedComputeQueue	= !dev->computeQueues.empty();
		dev->caps.hasDedicatedTransferQueue = !dev->copyQueues.empty();

		dev->ownedInstance	 = std::move(ownedInstance);
		dev->instanceWrapper = dev->ownedInstance == nullptr ? instance : nullptr;

		D3D12Device * raw = dev.get();
		owner.devices.push_back(std::move(dev));
		tagGuard.Dismiss();
		Succeed(error);
		return raw;
	}

	void * D3D12InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept
	{
		return MakeOwnedDevice(static_cast<D3D12Instance *>(impl), nullptr, desc, error);
	}

	void * D3D12CreateInstance(const void * instanceDesc, Error * error) noexcept
	{
		D3D12BackendOwner & owner			  = Owner();
		HostUniquePtr<D3D12Instance> instance = BuildInstance(*static_cast<const InstanceDesc *>(instanceDesc), error);
		if (!instance)
		{
			return nullptr;
		}

		D3D12Instance * raw = instance.get();
		owner.instances.push_back(std::move(instance));
		return raw;
	}

	// Releases the factory here and not from the owner's destructor at process exit, where the debug layer it may have installed has already reported and torn
	// itself down.
	void D3D12DestroyInstance(void * impl) noexcept
	{
		D3D12BackendOwner & owner = Owner();
		for (auto it = owner.instances.begin(); it != owner.instances.end(); ++it)
		{
			if (it->get() == impl)
			{
				owner.instances.erase(it);
				return;
			}
		}
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

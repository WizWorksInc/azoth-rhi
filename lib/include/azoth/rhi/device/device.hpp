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

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/threading.hpp"
#include "azoth/rhi/native/device_config.hpp"
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace azo::rhi
{

	class DeviceMemoryAllocator;
	class Profiler;

	namespace detail
	{
		struct FacadeBuilder;
		struct RegistryAccess;
	}

	class UniqueDevice;
	class UniqueInstance;

	enum class ValidationMode : std::uint8_t
	{
		eOff,

		eReleaseLight,

		eDeveloper,

		eCapture,
	};

	enum class NativeValidationPolicy : std::uint8_t
	{
		eFollowValidationMode,

		eDisabled,

		eEnabled,
	};

	enum class ValidationMessageSeverity : std::uint8_t
	{
		eWarning,
		eError,
	};

	using ValidationMessageCallback = void (*)(ValidationMessageSeverity severity, const char * message, void * userData) noexcept;

	struct NativeValidationDesc final
	{
		NativeValidationPolicy apiValidation			 = NativeValidationPolicy::eFollowValidationMode;
		NativeValidationPolicy synchronizationValidation = NativeValidationPolicy::eFollowValidationMode;
		NativeValidationPolicy gpuBasedValidation		 = NativeValidationPolicy::eDisabled;
		NativeValidationPolicy bestPractices			 = NativeValidationPolicy::eFollowValidationMode;
		bool breakOnError								 = false;
		bool breakOnWarning								 = false;

		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;

		std::span<const char * const> extraNativeLayers;
		std::span<const char * const> extraNativeExtensions;
	};

	struct ApiVersion final
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
	};

	struct InstanceDesc final
	{
		const char * applicationName		  = "Azoth Application";
		const char * engineName				  = "Azoth";
		std::uint32_t applicationVersionMajor = 0;
		std::uint32_t applicationVersionMinor = 0;
		std::uint32_t engineVersionMajor	  = 0;
		std::uint32_t engineVersionMinor	  = 0;

		ValidationMode validation = ValidationMode::eReleaseLight;
		NativeValidationDesc nativeValidation{};
		std::span<const InstanceConfigEntry> backendConfigs;
	};

	struct QueueRequest final
	{
		QueueType type			   = QueueType::eGraphics;
		std::uint32_t minCount	   = 1;
		bool requireDedicatedQueue = false;
	};

	enum class SparseTier : std::uint8_t
	{
		eNone,

		eBuffers,

		eResidentTextures,

		eResidentVolumes,
	};

	enum class ConservativeRasterTier : std::uint8_t
	{
		eNone,

		eBasic,

		eDegenerateCulling,

		eInnerCoverage,
	};

	enum class BindingTier : std::uint8_t
	{
		eBasic,

		eDynamicIndexing,

		eUnbounded,
	};

	enum class DeviceFeature : std::uint8_t
	{
		eTimestampQueries,
		eSamplerAnisotropy,
		eIndependentBlend,
		eDepthBounds,
		ePipelineStatisticsQueries,
		eMultiDrawIndirect,
		eDrawIndirectFirstInstance,
		eShaderDrawParameters,
		eSparseResources,
		eSparseBuffers,
		eSparseTextures,
		eSparseVolumes,
		eTextureViewSwizzle,
		eMultiPlanarFormats,
		eSamplerYcbcrConversion,
	};

	inline constexpr std::uint32_t kDefaultMaxOpenCommandListsPerQueue = 1024;

	inline constexpr std::uint32_t kUnlimitedOpenCommandLists = std::numeric_limits<std::uint32_t>::max();

	struct DeviceDesc final
	{
		std::span<const QueueRequest> queues;

		ValidationMode validation = ValidationMode::eReleaseLight;

		NativeValidationDesc nativeValidation{};

		bool enableDebugNames = true;

		bool enableDebugLabels = true;
		bool preferDiscreteGpu = true;

		bool requireSwapchain = true;

		bool allowDeviceLocalMapping = false;

		std::uint32_t maxOpenCommandListsPerQueue = kDefaultMaxOpenCommandListsPerQueue;

		ThreadingMode threading = ThreadingMode::eThreads;

		SyncOps sync{};

		DeviceMemoryAllocator * allocator = nullptr;

		Profiler * profiler = nullptr;

		bool allowSoftwareAdapter = false;

		bool allowLinkedAdapters = false;

		std::uint32_t preferredAdapterIndex = kInvalidIndex;

		std::span<const DeviceFeature> requiredFeatures;

		std::span<const DeviceFeature> preferredFeatures;

		std::span<const DeviceConfigEntry> backendConfigs;
		std::span<const InstanceConfigEntry> instanceConfigs;

		const char * debugName = nullptr;
	};

	[[nodiscard]] constexpr InstanceDesc instance_desc_for_device(const DeviceDesc & desc) noexcept
	{
		InstanceDesc instance{};
		instance.validation		  = desc.validation;
		instance.nativeValidation = desc.nativeValidation;
		instance.backendConfigs	  = desc.instanceConfigs;

		return instance;
	}

	struct FormatSupport final
	{
		Format format				= Format::eUndefined;
		bool sampled				= false;
		bool storage				= false;
		bool colorAttachment		= false;
		bool depthStencilAttachment = false;
		bool copySrc				= false;
		bool copyDst				= false;
		bool linearFiltering		= false;
		bool blendable				= false;

		bool blitSrc = false;

		bool blitDst = false;
	};

	struct DeviceCaps final
	{
		GraphicsApiId apiId{};

		ApiVersion apiVersion{};

		bool supportsTimelineSync = false;

		BindingTier bindingTier = BindingTier::eBasic;

		bool supportsUpdateAfterBind = false;

		bool supportsPartiallyBoundDescriptors = false;

		bool supportsSurfaces = false;

		bool supportsAnisotropy		  = false;
		bool supportsIndependentBlend = false;

		bool supportsTextureViewSwizzle = false;

		bool supportsMultiPlanarFormats = false;

		bool supportsSamplerYcbcrConversion = false;

		ConservativeRasterTier conservativeRasterTier = ConservativeRasterTier::eNone;

		bool supportsDepthBounds			   = false;
		bool supportsTimestampQueries		   = false;
		bool supportsPipelineStatisticsQueries = false;
		bool supportsIndirectCount			   = false;

		bool supportsTimestampWritesInScope = false;

		// Two timestamps written outside a rendering scope, in recording order on one list and both naming the same stage, resolve in that order.
		bool supportsOrderedTimestamps = false;

		bool supportsMultiDrawIndirect = false;

		bool supportsDrawIndirectFirstInstance = false;

		bool supportsShaderDrawParameters = false;

		bool supportsShaderFloat16 = false;

		bool supportsEnhancedBarriers = false;

		bool supportsPlacedResources = false;

		bool deviceLocalMemoryIsHostVisible = false;

		bool supportsScaledBlit = false;

		bool supportsResourceAdoption = false;

		bool supportsMemoryBudget = false;

		SparseTier sparseTier = SparseTier::eNone;

		bool supportsDynamicBufferOffsets = false;

		bool supportsRootDescriptors = false;
		bool supportsPipelineCache	 = false;

		ShaderBinaryFormat shaderBinaryFormat = ShaderBinaryFormat::eBackendNative;

		bool supportsShaderSource = false;

		bool supportsTimestampCalibration = false;

		bool reportsValidationMessageCounts = false;

		bool supportsRayTracing = false;

		bool supportsAccelerationStructureUpdate = false;

		bool supportsAccelerationStructureCompaction = false;

		bool supportsShaderBindingTable = false;

		bool supportsMultiAdapter = false;

		std::uint32_t graphicsQueueCount = 0;
		std::uint32_t computeQueueCount	 = 0;
		std::uint32_t copyQueueCount	 = 0;

		std::uint32_t maxOpenCommandListsPerQueue = 0;

		bool supportsCommandListResubmit = false;

		bool hasDedicatedComputeQueue  = false;
		bool hasDedicatedTransferQueue = false;

		std::uint32_t maxColorAttachments				= 0;
		std::uint32_t maxRenderTargets					= 0;
		std::uint32_t maxDescriptorSets					= 0;
		std::uint32_t maxDescriptorsPerSet				= 0;
		std::uint32_t maxBindlessSampledTextures		= 0;
		std::uint32_t maxBindlessStorageBuffers			= 0;
		std::uint32_t maxBindlessAccelerationStructures = 0;
		std::uint32_t maxSamplerDescriptors				= 0;
		std::uint32_t maxPushConstantBytes				= 0;
		std::uint32_t maxVertexBindings					= 0;
		std::uint32_t maxVertexAttributes				= 0;
		std::uint32_t maxViewports						= 0;
		std::uint32_t maxTextureDimension1D				= 0;
		std::uint32_t maxTextureDimension2D				= 0;
		std::uint32_t maxTextureDimension3D				= 0;
		std::uint32_t maxTextureArrayLayers				= 0;

		std::uint64_t minUniformBufferOffsetAlignment	 = 0;
		std::uint64_t minStorageBufferOffsetAlignment	 = 0;
		std::uint64_t minTexelBufferOffsetAlignment		 = 0;
		std::uint64_t optimalBufferCopyOffsetAlignment	 = 0;
		std::uint64_t optimalBufferCopyRowPitchAlignment = 0;
		std::uint64_t shaderGroupHandleSize				 = 0;
		std::uint64_t shaderGroupBaseAlignment			 = 0;
		std::uint64_t shaderBindingTableAlignment		 = 0;
		std::uint64_t sparseTileSizeBytes				 = 0;

		float timestampPeriodNanoseconds = 1.0f;

		std::uint32_t timestampValidBits = 0;

		bool supportsScalarBlockLayout = false;

		[[nodiscard]] bool supports(const DeviceFeature feature) const noexcept
		{
			switch (feature)
			{
			case DeviceFeature::eTimestampQueries:			return supportsTimestampQueries;
			case DeviceFeature::eSamplerAnisotropy:			return supportsAnisotropy;
			case DeviceFeature::eIndependentBlend:			return supportsIndependentBlend;
			case DeviceFeature::eDepthBounds:				return supportsDepthBounds;
			case DeviceFeature::ePipelineStatisticsQueries: return supportsPipelineStatisticsQueries;
			case DeviceFeature::eMultiDrawIndirect:			return supportsMultiDrawIndirect;
			case DeviceFeature::eDrawIndirectFirstInstance: return supportsDrawIndirectFirstInstance;
			case DeviceFeature::eShaderDrawParameters:		return supportsShaderDrawParameters;
			case DeviceFeature::eSparseResources:
			case DeviceFeature::eSparseBuffers:				return sparseTier >= SparseTier::eBuffers;
			case DeviceFeature::eSparseTextures:			return sparseTier >= SparseTier::eResidentTextures;
			case DeviceFeature::eSparseVolumes:				return sparseTier >= SparseTier::eResidentVolumes;
			case DeviceFeature::eTextureViewSwizzle:		return supportsTextureViewSwizzle;
			case DeviceFeature::eMultiPlanarFormats:		return supportsMultiPlanarFormats;
			case DeviceFeature::eSamplerYcbcrConversion:	return supportsSamplerYcbcrConversion;
			}

			return false;
		}
	};

	enum class AdapterType : std::uint8_t
	{
		eUnknown,
		eIntegrated,
		eDiscrete,
		eVirtual,
		eCpu,
	};

	enum class DriverId : std::uint32_t // NOLINT(performance-enum-size): Intentionally larger than required for future IDs.
	{
		eUnknown				   = 0,
		eAmdProprietary			   = 1,
		eAmdOpenSource			   = 2,
		eMesaRadv				   = 3,
		eNvidiaProprietary		   = 4,
		eIntelProprietaryWindows   = 5,
		eIntelOpenSourceMesa	   = 6,
		eImaginationProprietary	   = 7,
		eQualcommProprietary	   = 8,
		eArmProprietary			   = 9,
		eGoogleSwiftshader		   = 10,
		eGgpProprietary			   = 11,
		eBroadcomProprietary	   = 12,
		eMesaLlvmpipe			   = 13,
		eMoltenvk				   = 14,
		eCoreaviProprietary		   = 15,
		eJuiceProprietary		   = 16,
		eVerisiliconProprietary	   = 17,
		eMesaTurnip				   = 18,
		eMesaV3dv				   = 19,
		eMesaPanvk				   = 20,
		eSamsungProprietary		   = 21,
		eMesaVenus				   = 22,
		eMesaDozen				   = 23,
		eMesaNvk				   = 24,
		eImaginationOpenSourceMesa = 25,
		eMesaHoneykrisp			   = 26,
		eMesaKosmickrisp		   = 28,
	};

	struct AdapterInfo final
	{
		AdapterType type = AdapterType::eUnknown;

		GraphicsApiId apiId{};

		std::uint32_t adapterIndex = 0;

		std::uint32_t vendorId = 0;

		std::uint32_t deviceId = 0;

		std::array<std::uint8_t, 16> deviceUUID{};

		std::array<std::uint8_t, 16> driverUUID{};

		std::array<std::uint8_t, 8> deviceLUID{};

		bool deviceLUIDValid = false;

		std::uint64_t dedicatedVideoMemoryBytes = 0;

		std::uint64_t dedicatedSystemMemoryBytes = 0;

		std::uint64_t sharedSystemMemoryBytes = 0;

		bool unifiedMemoryArchitecture = false;
		bool linkedAdapter			   = false;

		const char * name = nullptr;

		DriverId driverId = DriverId::eUnknown;

		std::uint64_t driverVersionRaw = 0;

		const char * driverVersion = nullptr;

		const char * driverInfo = nullptr;
	};

	using AdapterUuidString = std::array<char, 37>;

	using AdapterLuidString = std::array<char, 17>;

	namespace detail
	{
		inline constexpr std::array<char, 16> kHexDigits{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	}

	[[nodiscard]] constexpr AdapterUuidString format_adapter_uuid(const std::array<std::uint8_t, 16> & uuid) noexcept
	{
		AdapterUuidString text{};
		std::size_t at = 0;

		// sixteen the digit table has. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		for (std::size_t byte = 0; byte < uuid.size(); ++byte)
		{
			if (byte == 4 || byte == 6 || byte == 8 || byte == 10)
			{
				text[at++] = '-';
			}
			text[at++] = detail::kHexDigits[(uuid[byte] >> 4u) & 0x0Fu];
			text[at++] = detail::kHexDigits[uuid[byte] & 0x0Fu];
		}
		text[at] = '\0';
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		return text;
	}

	[[nodiscard]] constexpr AdapterLuidString format_adapter_luid(const std::array<std::uint8_t, 8> & luid) noexcept
	{
		AdapterLuidString text{};
		std::size_t at = 0;

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		for (const std::uint8_t byte : luid)
		{
			text[at++] = detail::kHexDigits[(byte >> 4u) & 0x0Fu];
			text[at++] = detail::kHexDigits[byte & 0x0Fu];
		}
		text[at] = '\0';
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		return text;
	}

	enum class DestroyPolicy : std::uint8_t
	{
		eDeferUntilSafe,

		eRequireAlreadyIdle,
	};

	struct DestroyDesc final
	{
		DestroyPolicy policy = DestroyPolicy::eDeferUntilSafe;
		RetirePoint safeAfter{};
	};

	struct BackendInfo final
	{
		GraphicsApiId id{};
		std::string_view canonicalName;
		std::string_view displayName;
		std::uint32_t apiVersionMajor = 0;
		std::uint32_t apiVersionMinor = 0;

		bool supportsSurfaces = false;

		bool supportsDebugMarkers = false;

		bool supportsExternalNativeAccess = false;
	};

	struct BackendCreateInfo final
	{
		BackendInfo info{};
		void * (*createInstance)(const void * instanceDesc, Error * error) noexcept = nullptr;
	};

	struct InstanceApi;
	class BackendBlockSet;

	class AZO_RHI_API Instance final
	{
	public:
		Instance() = default;

		[[nodiscard]] GraphicsApiId get_graphics_api_id() const noexcept;

		[[nodiscard]] bool enumerate_adapters(std::span<AdapterInfo> adapters, std::uint32_t & out) const noexcept;
		[[nodiscard]] bool enumerate_adapters(std::span<AdapterInfo> adapters, std::uint32_t & out, Error & error) const noexcept;
		[[nodiscard]] Result<std::uint32_t> enumerate_adapters_with_result(std::span<AdapterInfo> adapters) const noexcept;

		[[nodiscard]] bool query_external_handle_support(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out) const noexcept;
		[[nodiscard]] bool query_external_handle_support(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out, Error & error) const noexcept;
		[[nodiscard]] Result<ExternalHandleSupport> query_external_handle_support_with_result(const ExternalHandleSupportDesc & desc) const noexcept;

	private:
		friend struct detail::FacadeBuilder;
		friend class UniqueInstance;

		Instance(void * impl, const InstanceApi * dispatch) noexcept : m_impl(impl), m_dispatch(dispatch) {}

		void * m_impl				   = nullptr;
		const InstanceApi * m_dispatch = nullptr;
	};

	class AZO_RHI_API UniqueInstance final
	{
	public:
		UniqueInstance() = default;

		UniqueInstance(const UniqueInstance &)			   = delete;
		UniqueInstance & operator=(const UniqueInstance &) = delete;

		UniqueInstance(UniqueInstance && other) noexcept : m_impl(other.m_impl), m_dispatch(other.m_dispatch)
		{
			other.m_impl	 = nullptr;
			other.m_dispatch = nullptr;
		}

		UniqueInstance & operator=(UniqueInstance && other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_impl			 = other.m_impl;
				m_dispatch		 = other.m_dispatch;
				other.m_impl	 = nullptr;
				other.m_dispatch = nullptr;
			}
			return *this;
		}

		~UniqueInstance()
		{
			Reset();
		}

		[[nodiscard]] Instance get() const noexcept
		{
			return Instance{ m_impl, m_dispatch };
		}

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr;
		}

	private:
		friend struct detail::FacadeBuilder;

		UniqueInstance(void * impl, const InstanceApi * dispatch) noexcept : m_impl(impl), m_dispatch(dispatch) {}

		void Reset() noexcept;

		void * m_impl				   = nullptr;
		const InstanceApi * m_dispatch = nullptr;
	};

	struct ValidationMessageCounts final
	{
		std::uint64_t errors   = 0;
		std::uint64_t warnings = 0;
	};

	class AZO_RHI_API Device final
	{
	public:
		Device() = default;

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		[[nodiscard]] GraphicsApiId get_graphics_api_id() const noexcept;
		[[nodiscard]] std::string_view get_graphics_api_name() const noexcept;

		[[nodiscard]] BufferHandle create_buffer(const BufferDesc & desc) noexcept;
		[[nodiscard]] BufferHandle create_buffer(const BufferDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> create_buffer_with_result(const BufferDesc & desc) noexcept;
		[[nodiscard]] TextureHandle create_texture(const TextureDesc & desc) noexcept;
		[[nodiscard]] TextureHandle create_texture(const TextureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> create_texture_with_result(const TextureDesc & desc) noexcept;
		[[nodiscard]] TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc & desc) noexcept;
		[[nodiscard]] TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureViewHandle> create_texture_view_with_result(TextureHandle texture, const TextureViewDesc & desc) noexcept;
		[[nodiscard]] SamplerHandle create_sampler(const SamplerDesc & desc) noexcept;
		[[nodiscard]] SamplerHandle create_sampler(const SamplerDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<SamplerHandle> create_sampler_with_result(const SamplerDesc & desc) noexcept;

		[[nodiscard]] HeapHandle create_heap(const HeapDesc & desc) noexcept;
		[[nodiscard]] HeapHandle create_heap(const HeapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<HeapHandle> create_heap_with_result(const HeapDesc & desc) noexcept;
		[[nodiscard]] BufferHandle create_placed_buffer(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] BufferHandle create_placed_buffer(const PlacedBufferDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> create_placed_buffer_with_result(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] TextureHandle create_placed_texture(const PlacedTextureDesc & desc) noexcept;
		[[nodiscard]] TextureHandle create_placed_texture(const PlacedTextureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> create_placed_texture_with_result(const PlacedTextureDesc & desc) noexcept;

		[[nodiscard]] bool get_texture_memory_info(const TextureDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool get_texture_memory_info(const TextureDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> get_texture_memory_info_with_result(const TextureDesc & desc) const noexcept;
		[[nodiscard]] bool get_buffer_memory_info(const BufferDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool get_buffer_memory_info(const BufferDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> get_buffer_memory_info_with_result(const BufferDesc & desc) const noexcept;

		[[nodiscard]] DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorSetLayoutHandle> create_descriptor_set_layout_with_result(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<PipelineLayoutHandle> create_pipeline_layout_with_result(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena create_descriptor_arena(const DescriptorArenaDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena create_descriptor_arena(const DescriptorArenaDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorArena> create_descriptor_arena_with_result(const DescriptorArenaDesc & desc) noexcept;

		[[nodiscard]] GraphicsPipelineHandle create_graphics_pipeline(const GraphicsPipelineDesc & desc) noexcept;
		[[nodiscard]] GraphicsPipelineHandle create_graphics_pipeline(const GraphicsPipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<GraphicsPipelineHandle> create_graphics_pipeline_with_result(const GraphicsPipelineDesc & desc) noexcept;
		[[nodiscard]] ComputePipelineHandle create_compute_pipeline(const ComputePipelineDesc & desc) noexcept;
		[[nodiscard]] ComputePipelineHandle create_compute_pipeline(const ComputePipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<ComputePipelineHandle> create_compute_pipeline_with_result(const ComputePipelineDesc & desc) noexcept;
		[[nodiscard]] RayTracingPipelineHandle create_ray_tracing_pipeline(const RayTracingPipelineDesc & desc) noexcept;
		[[nodiscard]] RayTracingPipelineHandle create_ray_tracing_pipeline(const RayTracingPipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<RayTracingPipelineHandle> create_ray_tracing_pipeline_with_result(const RayTracingPipelineDesc & desc) noexcept;
		[[nodiscard]] PipelineCacheHandle create_pipeline_cache(const PipelineCacheDesc & desc) noexcept;
		[[nodiscard]] PipelineCacheHandle create_pipeline_cache(const PipelineCacheDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<PipelineCacheHandle> create_pipeline_cache_with_result(const PipelineCacheDesc & desc) noexcept;

		[[nodiscard]] bool get_pipeline_cache_data(PipelineCacheHandle cache, PipelineCacheData & out) noexcept;
		[[nodiscard]] bool get_pipeline_cache_data(PipelineCacheHandle cache, PipelineCacheData & out, Error & error) noexcept;
		[[nodiscard]] Result<PipelineCacheData> get_pipeline_cache_data_with_result(PipelineCacheHandle cache) noexcept;

		[[nodiscard]] AccelerationStructureHandle create_acceleration_structure(const AccelerationStructureDesc & desc) noexcept;
		[[nodiscard]] AccelerationStructureHandle create_acceleration_structure(const AccelerationStructureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<AccelerationStructureHandle> create_acceleration_structure_with_result(const AccelerationStructureDesc & desc) noexcept;
		[[nodiscard]] QueryPoolHandle create_query_pool(const QueryPoolDesc & desc) noexcept;
		[[nodiscard]] QueryPoolHandle create_query_pool(const QueryPoolDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<QueryPoolHandle> create_query_pool_with_result(const QueryPoolDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle create_timeline(const TimelineDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle create_timeline(const TimelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TimelineHandle> create_timeline_with_result(const TimelineDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle create_binary_semaphore(const BinarySemaphoreDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle create_binary_semaphore(const BinarySemaphoreDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BinarySemaphoreHandle> create_binary_semaphore_with_result(const BinarySemaphoreDesc & desc) noexcept;
		[[nodiscard]] CommandPool create_command_pool(const CommandPoolDesc & desc) noexcept;
		[[nodiscard]] CommandPool create_command_pool(const CommandPoolDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<CommandPool> create_command_pool_with_result(const CommandPoolDesc & desc) noexcept;
		[[nodiscard]] Swapchain create_swapchain(const SwapchainDesc & desc) noexcept;
		[[nodiscard]] Swapchain create_swapchain(const SwapchainDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<Swapchain> create_swapchain_with_result(const SwapchainDesc & desc) noexcept;

		[[nodiscard]] Queue get_queue(QueueType type, std::uint32_t index = 0) noexcept;
		[[nodiscard]] Queue get_queue(QueueType type, std::uint32_t index, Error & error) noexcept;
		[[nodiscard]] Result<Queue> get_queue_with_result(QueueType type, std::uint32_t index = 0) noexcept;

		[[nodiscard]] std::uint32_t get_queue_count(QueueType type) const noexcept;

		[[nodiscard]] MappedMemory map(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] MappedMemory map(BufferHandle buffer, const MapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<MappedMemory> map_with_result(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] bool unmap(BufferHandle buffer) noexcept;
		[[nodiscard]] bool unmap(BufferHandle buffer, Error & error) noexcept;
		[[nodiscard]] bool flush_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool flush_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;
		[[nodiscard]] bool invalidate_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool invalidate_mapped_range(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;

		bool update_descriptors(std::span<const DescriptorWriteBuffer> writes) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteBuffer> writes, Error & error) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteTexture> writes) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteTexture> writes, Error & error) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteSampler> writes) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteSampler> writes, Error & error) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteAccelerationStructure> writes) noexcept;
		bool update_descriptors(std::span<const DescriptorWriteAccelerationStructure> writes, Error & error) noexcept;

		[[nodiscard]] bool query_memory_budget(HeapType heap, MemoryBudgetInfo & out) const noexcept;
		[[nodiscard]] bool query_memory_budget(HeapType heap, MemoryBudgetInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryBudgetInfo> query_memory_budget_with_result(HeapType heap) const noexcept;
		bool set_residency_priority(std::span<const ResidencyPriorityDesc> priorities) noexcept;
		bool set_residency_priority(std::span<const ResidencyPriorityDesc> priorities, Error & error) noexcept;

		[[nodiscard]] bool calibrate_timestamp(QueueType queueType, TimestampCalibration & out) const noexcept;
		[[nodiscard]] bool calibrate_timestamp(QueueType queueType, TimestampCalibration & out, Error & error) const noexcept;
		[[nodiscard]] Result<TimestampCalibration> calibrate_timestamp_with_result(QueueType queueType) const noexcept;

		[[nodiscard]] const DeviceCaps & get_caps() const noexcept;
		[[nodiscard]] FormatSupport get_format_support(Format format) const noexcept;
		[[nodiscard]] const AdapterInfo & get_adapter_info() const noexcept;

		[[nodiscard]] bool get_texture_info(TextureHandle texture, TextureInfo & out) const noexcept;
		[[nodiscard]] bool get_texture_info(TextureHandle texture, TextureInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<TextureInfo> get_texture_info_with_result(TextureHandle texture) const noexcept;

		[[nodiscard]] bool get_buffer_info(BufferHandle buffer, BufferInfo & out) const noexcept;
		[[nodiscard]] bool get_buffer_info(BufferHandle buffer, BufferInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<BufferInfo> get_buffer_info_with_result(BufferHandle buffer) const noexcept;

		[[nodiscard]] ValidationMessageCounts get_validation_message_counts() const noexcept;

		bool destroy(BufferHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(BufferHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(TextureHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(TextureHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(TextureViewHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(TextureViewHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(SamplerHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(SamplerHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(HeapHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(HeapHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(DescriptorSetHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(DescriptorSetHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(PipelineLayoutHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(PipelineLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(ComputePipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(ComputePipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(PipelineCacheHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(PipelineCacheHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(AccelerationStructureHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(AccelerationStructureHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(QueryPoolHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(QueryPoolHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(TimelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(TimelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc, Error & error) noexcept;

		bool collect_garbage() noexcept;
		bool collect_garbage(Error & error) noexcept;
		bool collect_garbage(TimelineHandle timeline, std::uint64_t completedValue) noexcept;
		bool collect_garbage(TimelineHandle timeline, std::uint64_t completedValue, Error & error) noexcept;

		template <GraphicsApiTag Api>
		[[nodiscard]] BufferHandle adopt_buffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BufferHandle adopt_buffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<BufferHandle> adopt_buffer_with_result(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureHandle adopt_texture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureHandle adopt_texture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TextureHandle> adopt_texture_with_result(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_buffer(BufferHandle buffer, NativeBuffer<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_buffer(BufferHandle buffer, NativeBuffer<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeBuffer<Api>> get_native_buffer_with_result(BufferHandle buffer) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_texture(TextureHandle texture, NativeTexture<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_texture(TextureHandle texture, NativeTexture<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTexture<Api>> get_native_texture_with_result(TextureHandle texture) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureViewHandle adopt_texture_view(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureViewHandle adopt_texture_view(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TextureViewHandle> adopt_texture_view_with_result(
			const NativeTextureView<Api> & native,
			const AdoptedTextureViewDesc & desc
		) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] SamplerHandle adopt_sampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] SamplerHandle adopt_sampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<SamplerHandle> adopt_sampler_with_result(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_texture_view(TextureViewHandle view, NativeTextureView<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_texture_view(TextureViewHandle view, NativeTextureView<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTextureView<Api>> get_native_texture_view_with_result(TextureViewHandle view) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_sampler(SamplerHandle sampler, NativeSampler<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_sampler(SamplerHandle sampler, NativeSampler<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeSampler<Api>> get_native_sampler_with_result(SamplerHandle sampler) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TimelineHandle adopt_timeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TimelineHandle adopt_timeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TimelineHandle> adopt_timeline_with_result(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BinarySemaphoreHandle adopt_binary_semaphore(const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BinarySemaphoreHandle adopt_binary_semaphore(
			const NativeBinarySemaphore<Api> & native,
			const AdoptedBinarySemaphoreDesc & desc,
			Error & error
		) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<BinarySemaphoreHandle> adopt_binary_semaphore_with_result(
			const NativeBinarySemaphore<Api> & native,
			const AdoptedBinarySemaphoreDesc & desc
		) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_timeline(TimelineHandle timeline, NativeTimeline<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_timeline(TimelineHandle timeline, NativeTimeline<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTimeline<Api>> get_native_timeline_with_result(TimelineHandle timeline) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_binary_semaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool get_native_binary_semaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeBinarySemaphore<Api>> get_native_binary_semaphore_with_result(BinarySemaphoreHandle semaphore) noexcept;

		[[nodiscard]] bool export_buffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool export_buffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> export_buffer_with_result(BufferHandle buffer, ExternalHandleType type) noexcept;
		[[nodiscard]] bool export_heap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool export_heap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> export_heap_with_result(HeapHandle heap, ExternalHandleType type) noexcept;
		[[nodiscard]] bool export_texture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool export_texture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> export_texture_with_result(TextureHandle texture, ExternalHandleType type) noexcept;
		[[nodiscard]] bool export_timeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool export_timeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> export_timeline_with_result(TimelineHandle timeline, ExternalHandleType type) noexcept;
		[[nodiscard]] bool export_binary_semaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool export_binary_semaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> export_binary_semaphore_with_result(BinarySemaphoreHandle semaphore, ExternalHandleType type) noexcept;

		[[nodiscard]] BufferHandle import_buffer(const ExternalBufferImportDesc & desc) noexcept;
		[[nodiscard]] BufferHandle import_buffer(const ExternalBufferImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> import_buffer_with_result(const ExternalBufferImportDesc & desc) noexcept;
		[[nodiscard]] HeapHandle import_heap(const ExternalHeapImportDesc & desc) noexcept;
		[[nodiscard]] HeapHandle import_heap(const ExternalHeapImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<HeapHandle> import_heap_with_result(const ExternalHeapImportDesc & desc) noexcept;
		[[nodiscard]] TextureHandle import_texture(const ExternalTextureImportDesc & desc) noexcept;
		[[nodiscard]] TextureHandle import_texture(const ExternalTextureImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> import_texture_with_result(const ExternalTextureImportDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle import_timeline(const ExternalTimelineImportDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle import_timeline(const ExternalTimelineImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TimelineHandle> import_timeline_with_result(const ExternalTimelineImportDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle import_binary_semaphore(const ExternalBinarySemaphoreImportDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle import_binary_semaphore(const ExternalBinarySemaphoreImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BinarySemaphoreHandle> import_binary_semaphore_with_result(const ExternalBinarySemaphoreImportDesc & desc) noexcept;

		bool close_exported_handle(const ExternalHandle & handle) noexcept;
		bool close_exported_handle(const ExternalHandle & handle, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;
		friend class UniqueDevice;

		Device(void * impl, BackendBlockSet * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		BufferHandle CreateBufferRouted(const BufferDesc & desc, Error * error) noexcept;
		TextureHandle CreateTextureRouted(const TextureDesc & desc, Error * error) noexcept;

		BufferHandle AdoptBufferRaw(GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept;
		TextureHandle AdoptTextureRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept;
		bool GetNativeBufferRaw(GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
		bool GetNativeTextureRaw(GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept;
		TextureViewHandle AdoptTextureViewRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
		SamplerHandle AdoptSamplerRaw(GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
		bool GetNativeTextureViewRaw(GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
		bool GetNativeSamplerRaw(GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
		TimelineHandle AdoptTimelineRaw(GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
		BinarySemaphoreHandle AdoptBinarySemaphoreRaw(
			GraphicsApiId api,
			const void * nativeImport,
			const AdoptedBinarySemaphoreDesc & desc,
			Error * error
		) noexcept;
		bool GetNativeTimelineRaw(GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
		bool GetNativeBinarySemaphoreRaw(GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;

		void * m_impl			   = nullptr;
		BackendBlockSet * m_blocks = nullptr;
	};

	template <GraphicsApiTag Api>
	BufferHandle Device::adopt_buffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept
	{
		return AdoptBufferRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	BufferHandle Device::adopt_buffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptBufferRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<BufferHandle> Device::adopt_buffer_with_result(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept
	{
		Error error{};
		const BufferHandle handle = AdoptBufferRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<BufferHandle>{ handle } : Result<BufferHandle>{ error };
	}

	template <GraphicsApiTag Api>
	TextureHandle Device::adopt_texture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept
	{
		return AdoptTextureRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TextureHandle Device::adopt_texture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTextureRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TextureHandle> Device::adopt_texture_with_result(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept
	{
		Error error{};
		const TextureHandle handle = AdoptTextureRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<TextureHandle>{ handle } : Result<TextureHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_buffer(BufferHandle buffer, NativeBuffer<Api> & out) noexcept
	{
		out = {};
		return GetNativeBufferRaw(Api::kId, buffer, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_buffer(BufferHandle buffer, NativeBuffer<Api> & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		return GetNativeBufferRaw(Api::kId, buffer, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeBuffer<Api>> Device::get_native_buffer_with_result(BufferHandle buffer) noexcept
	{
		Error error{};
		NativeBuffer<Api> out{};
		return GetNativeBufferRaw(Api::kId, buffer, &out, &error) ? Result<NativeBuffer<Api>>{ out } : Result<NativeBuffer<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_texture(TextureHandle texture, NativeTexture<Api> & out) noexcept
	{
		out = {};
		return GetNativeTextureRaw(Api::kId, texture, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_texture(TextureHandle texture, NativeTexture<Api> & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		return GetNativeTextureRaw(Api::kId, texture, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTexture<Api>> Device::get_native_texture_with_result(TextureHandle texture) noexcept
	{
		Error error{};
		NativeTexture<Api> out{};
		return GetNativeTextureRaw(Api::kId, texture, &out, &error) ? Result<NativeTexture<Api>>{ out } : Result<NativeTexture<Api>>{ error };
	}

	class AZO_RHI_API UniqueDevice final
	{
	public:
		UniqueDevice() = default;

		UniqueDevice(const UniqueDevice &)			   = delete;
		UniqueDevice & operator=(const UniqueDevice &) = delete;

		UniqueDevice(UniqueDevice && other) noexcept : m_impl(other.m_impl), m_blocks(other.m_blocks)
		{
			other.m_impl   = nullptr;
			other.m_blocks = nullptr;
		}

		UniqueDevice & operator=(UniqueDevice && other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_impl		   = other.m_impl;
				m_blocks	   = other.m_blocks;
				other.m_impl   = nullptr;
				other.m_blocks = nullptr;
			}
			return *this;
		}

		~UniqueDevice()
		{
			Reset();
		}

		[[nodiscard]] Device get() const noexcept
		{
			return Device{ m_impl, m_blocks };
		}

		[[nodiscard]] bool is_valid() const noexcept
		{
			return m_impl != nullptr;
		}

	private:
		friend struct detail::FacadeBuilder;

		UniqueDevice(void * impl, BackendBlockSet * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		void Reset() noexcept;

		void * m_impl			   = nullptr;
		BackendBlockSet * m_blocks = nullptr;
	};

	class GraphicsApiRegistry final
	{
	public:
		template <GraphicsApiTag Api>
		Result<void> Register(const BackendCreateInfo & createInfo)
		{
			if (is_registered(Api::kId))
			{
				return Error{
					.code	 = ErrorCode::eInvalidState,
					.message = "graphics API backend already registered",
				};
			}

			BackendCreateInfo entry = createInfo;
			entry.info.id			= Api::kId;

			if (!detail::try_push_back(m_entries, entry))
			{
				return Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "graphics API registry entry storage allocation failed",
				};
			}

			if (!detail::try_push_back(m_infos, entry.info))
			{
				m_entries.pop_back();
				return Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "graphics API registry info storage allocation failed",
				};
			}

			return {};
		}

		[[nodiscard]] const BackendInfo * find(GraphicsApiId id) const noexcept
		{
			const auto found = std::ranges::find(m_infos, id, &BackendInfo::id);
			return found != m_infos.end() ? &*found : nullptr;
		}

		[[nodiscard]] const BackendInfo * find(const std::string_view name) const noexcept
		{
			const auto found = std::ranges::find_if(
				m_infos,
				[name](const BackendInfo & info)
				{
					return name == info.canonicalName || name == short_api_name(info.canonicalName);
				}
			);
			return found != m_infos.end() ? &*found : nullptr;
		}

		[[nodiscard]] bool is_registered(GraphicsApiId id) const noexcept
		{
			return find(id) != nullptr;
		}

		[[nodiscard]] std::span<const BackendInfo> enumerate_backends() const noexcept
		{
			return m_infos;
		}

	private:
		friend struct detail::RegistryAccess;

		detail::HostVector<BackendCreateInfo> m_entries;
		detail::HostVector<BackendInfo> m_infos;
	};

	AZO_RHI_API Result<UniqueInstance> create_instance(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc);

	AZO_RHI_API Result<UniqueDevice> create_device(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const DeviceDesc & desc);

	template <GraphicsApiTag Api>
	Result<UniqueDevice> create_device(const DeviceDesc & desc);

	template <>
	AZO_RHI_API Result<UniqueDevice> create_device<VulkanApi>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> create_device<D3D12Api>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> create_device<MetalApi>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> create_device<Metal4Api>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> create_device<NullApi>(const DeviceDesc & desc);

	template <GraphicsApiTag Api>
	TextureViewHandle Device::adopt_texture_view(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept
	{
		return AdoptTextureViewRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TextureViewHandle Device::adopt_texture_view(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTextureViewRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TextureViewHandle> Device::adopt_texture_view_with_result(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept
	{
		Error error{};
		const TextureViewHandle handle = AdoptTextureViewRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<TextureViewHandle>{ handle } : Result<TextureViewHandle>{ error };
	}

	template <GraphicsApiTag Api>
	SamplerHandle Device::adopt_sampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept
	{
		return AdoptSamplerRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	SamplerHandle Device::adopt_sampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptSamplerRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<SamplerHandle> Device::adopt_sampler_with_result(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept
	{
		Error error{};
		const SamplerHandle handle = AdoptSamplerRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<SamplerHandle>{ handle } : Result<SamplerHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_texture_view(TextureViewHandle view, NativeTextureView<Api> & out) noexcept
	{
		return GetNativeTextureViewRaw(Api::kId, view, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_texture_view(TextureViewHandle view, NativeTextureView<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeTextureViewRaw(Api::kId, view, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTextureView<Api>> Device::get_native_texture_view_with_result(TextureViewHandle view) noexcept
	{
		Error error{};
		NativeTextureView<Api> out{};
		return GetNativeTextureViewRaw(Api::kId, view, &out, &error) ? Result<NativeTextureView<Api>>{ out } : Result<NativeTextureView<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_sampler(SamplerHandle sampler, NativeSampler<Api> & out) noexcept
	{
		return GetNativeSamplerRaw(Api::kId, sampler, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_sampler(SamplerHandle sampler, NativeSampler<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeSamplerRaw(Api::kId, sampler, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeSampler<Api>> Device::get_native_sampler_with_result(SamplerHandle sampler) noexcept
	{
		Error error{};
		NativeSampler<Api> out{};
		return GetNativeSamplerRaw(Api::kId, sampler, &out, &error) ? Result<NativeSampler<Api>>{ out } : Result<NativeSampler<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	TimelineHandle Device::adopt_timeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept
	{
		return AdoptTimelineRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TimelineHandle Device::adopt_timeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTimelineRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TimelineHandle> Device::adopt_timeline_with_result(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept
	{
		Error error{};
		const TimelineHandle handle = AdoptTimelineRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<TimelineHandle>{ handle } : Result<TimelineHandle>{ error };
	}

	template <GraphicsApiTag Api>
	BinarySemaphoreHandle Device::adopt_binary_semaphore(const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept
	{
		return AdoptBinarySemaphoreRaw(Api::kId, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	BinarySemaphoreHandle Device::adopt_binary_semaphore(
		const NativeBinarySemaphore<Api> & native,
		const AdoptedBinarySemaphoreDesc & desc,
		Error & error
	) noexcept
	{
		error = {};
		return AdoptBinarySemaphoreRaw(Api::kId, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<BinarySemaphoreHandle> Device::adopt_binary_semaphore_with_result(
		const NativeBinarySemaphore<Api> & native,
		const AdoptedBinarySemaphoreDesc & desc
	) noexcept
	{
		Error error{};
		const BinarySemaphoreHandle handle = AdoptBinarySemaphoreRaw(Api::kId, &native, desc, &error);
		return handle.is_valid() ? Result<BinarySemaphoreHandle>{ handle } : Result<BinarySemaphoreHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_timeline(TimelineHandle timeline, NativeTimeline<Api> & out) noexcept
	{
		return GetNativeTimelineRaw(Api::kId, timeline, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_timeline(TimelineHandle timeline, NativeTimeline<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeTimelineRaw(Api::kId, timeline, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTimeline<Api>> Device::get_native_timeline_with_result(TimelineHandle timeline) noexcept
	{
		Error error{};
		NativeTimeline<Api> out{};
		return GetNativeTimelineRaw(Api::kId, timeline, &out, &error) ? Result<NativeTimeline<Api>>{ out } : Result<NativeTimeline<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_binary_semaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out) noexcept
	{
		return GetNativeBinarySemaphoreRaw(Api::kId, semaphore, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::get_native_binary_semaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeBinarySemaphoreRaw(Api::kId, semaphore, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeBinarySemaphore<Api>> Device::get_native_binary_semaphore_with_result(BinarySemaphoreHandle semaphore) noexcept
	{
		Error error{};
		NativeBinarySemaphore<Api> out{};
		return GetNativeBinarySemaphoreRaw(Api::kId, semaphore, &out, &error) ? Result<NativeBinarySemaphore<Api>>{ out }
																			  : Result<NativeBinarySemaphore<Api>>{ error };
	}

}

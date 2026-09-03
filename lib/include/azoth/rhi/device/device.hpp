// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		ApiVersion apiVersion{};
		ValidationMode validation = ValidationMode::eReleaseLight;
		NativeValidationDesc nativeValidation{};
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

	struct DeviceDesc final
	{
		std::span<const QueueRequest> queues;

		ApiVersion apiVersion{};
		ValidationMode validation = ValidationMode::eReleaseLight;

		NativeValidationDesc nativeValidation{};

		bool enableDebugNames = true;

		bool enableDebugLabels = true;
		bool preferDiscreteGpu = true;

		bool requireSwapchain = true;

		bool allowDeviceLocalMapping = false;

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

		const char * debugName = nullptr;
	};

	[[nodiscard]] constexpr InstanceDesc InstanceDescForDevice(const DeviceDesc & desc) noexcept
	{
		InstanceDesc instance{};
		instance.apiVersion		  = desc.apiVersion;
		instance.validation		  = desc.validation;
		instance.nativeValidation = desc.nativeValidation;

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

		[[nodiscard]] bool Supports(const DeviceFeature feature) const noexcept
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

	[[nodiscard]] constexpr AdapterUuidString FormatAdapterUuid(const std::array<std::uint8_t, 16> & uuid) noexcept
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

	[[nodiscard]] constexpr AdapterLuidString FormatAdapterLuid(const std::array<std::uint8_t, 8> & luid) noexcept
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

		[[nodiscard]] GraphicsApiId GetGraphicsApiId() const noexcept;

		[[nodiscard]] bool EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out) const noexcept;
		[[nodiscard]] bool EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out, Error & error) const noexcept;
		[[nodiscard]] Result<std::uint32_t> EnumerateAdaptersWithResult(std::span<AdapterInfo> adapters) const noexcept;

		[[nodiscard]] bool QueryExternalHandleSupport(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out) const noexcept;
		[[nodiscard]] bool QueryExternalHandleSupport(const ExternalHandleSupportDesc & desc, ExternalHandleSupport & out, Error & error) const noexcept;
		[[nodiscard]] Result<ExternalHandleSupport> QueryExternalHandleSupportWithResult(const ExternalHandleSupportDesc & desc) const noexcept;

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

		[[nodiscard]] Instance Get() const noexcept
		{
			return Instance{ m_impl, m_dispatch };
		}

		[[nodiscard]] bool IsValid() const noexcept
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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_blocks != nullptr;
		}

		[[nodiscard]] GraphicsApiId GetGraphicsApiId() const noexcept;
		[[nodiscard]] std::string_view GetGraphicsApiName() const noexcept;

		[[nodiscard]] BufferHandle CreateBuffer(const BufferDesc & desc) noexcept;
		[[nodiscard]] BufferHandle CreateBuffer(const BufferDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> CreateBufferWithResult(const BufferDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreateTexture(const TextureDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreateTexture(const TextureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> CreateTextureWithResult(const TextureDesc & desc) noexcept;
		[[nodiscard]] TextureViewHandle CreateTextureView(TextureHandle texture, const TextureViewDesc & desc) noexcept;
		[[nodiscard]] TextureViewHandle CreateTextureView(TextureHandle texture, const TextureViewDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureViewHandle> CreateTextureViewWithResult(TextureHandle texture, const TextureViewDesc & desc) noexcept;
		[[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc & desc) noexcept;
		[[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<SamplerHandle> CreateSamplerWithResult(const SamplerDesc & desc) noexcept;

		[[nodiscard]] HeapHandle CreateHeap(const HeapDesc & desc) noexcept;
		[[nodiscard]] HeapHandle CreateHeap(const HeapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<HeapHandle> CreateHeapWithResult(const HeapDesc & desc) noexcept;
		[[nodiscard]] BufferHandle CreatePlacedBuffer(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] BufferHandle CreatePlacedBuffer(const PlacedBufferDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> CreatePlacedBufferWithResult(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreatePlacedTexture(const PlacedTextureDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreatePlacedTexture(const PlacedTextureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> CreatePlacedTextureWithResult(const PlacedTextureDesc & desc) noexcept;

		[[nodiscard]] bool GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> GetTextureMemoryInfoWithResult(const TextureDesc & desc) const noexcept;
		[[nodiscard]] bool GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> GetBufferMemoryInfoWithResult(const BufferDesc & desc) const noexcept;

		[[nodiscard]] DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorSetLayoutHandle> CreateDescriptorSetLayoutWithResult(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<PipelineLayoutHandle> CreatePipelineLayoutWithResult(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena CreateDescriptorArena(const DescriptorArenaDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena CreateDescriptorArena(const DescriptorArenaDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorArena> CreateDescriptorArenaWithResult(const DescriptorArenaDesc & desc) noexcept;

		[[nodiscard]] GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc & desc) noexcept;
		[[nodiscard]] GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<GraphicsPipelineHandle> CreateGraphicsPipelineWithResult(const GraphicsPipelineDesc & desc) noexcept;
		[[nodiscard]] ComputePipelineHandle CreateComputePipeline(const ComputePipelineDesc & desc) noexcept;
		[[nodiscard]] ComputePipelineHandle CreateComputePipeline(const ComputePipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<ComputePipelineHandle> CreateComputePipelineWithResult(const ComputePipelineDesc & desc) noexcept;
		[[nodiscard]] RayTracingPipelineHandle CreateRayTracingPipeline(const RayTracingPipelineDesc & desc) noexcept;
		[[nodiscard]] RayTracingPipelineHandle CreateRayTracingPipeline(const RayTracingPipelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<RayTracingPipelineHandle> CreateRayTracingPipelineWithResult(const RayTracingPipelineDesc & desc) noexcept;
		[[nodiscard]] PipelineCacheHandle CreatePipelineCache(const PipelineCacheDesc & desc) noexcept;
		[[nodiscard]] PipelineCacheHandle CreatePipelineCache(const PipelineCacheDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<PipelineCacheHandle> CreatePipelineCacheWithResult(const PipelineCacheDesc & desc) noexcept;

		[[nodiscard]] bool GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out) noexcept;
		[[nodiscard]] bool GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out, Error & error) noexcept;
		[[nodiscard]] Result<PipelineCacheData> GetPipelineCacheDataWithResult(PipelineCacheHandle cache) noexcept;

		[[nodiscard]] AccelerationStructureHandle CreateAccelerationStructure(const AccelerationStructureDesc & desc) noexcept;
		[[nodiscard]] AccelerationStructureHandle CreateAccelerationStructure(const AccelerationStructureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<AccelerationStructureHandle> CreateAccelerationStructureWithResult(const AccelerationStructureDesc & desc) noexcept;
		[[nodiscard]] QueryPoolHandle CreateQueryPool(const QueryPoolDesc & desc) noexcept;
		[[nodiscard]] QueryPoolHandle CreateQueryPool(const QueryPoolDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<QueryPoolHandle> CreateQueryPoolWithResult(const QueryPoolDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle CreateTimeline(const TimelineDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle CreateTimeline(const TimelineDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TimelineHandle> CreateTimelineWithResult(const TimelineDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle CreateBinarySemaphore(const BinarySemaphoreDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle CreateBinarySemaphore(const BinarySemaphoreDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BinarySemaphoreHandle> CreateBinarySemaphoreWithResult(const BinarySemaphoreDesc & desc) noexcept;
		[[nodiscard]] CommandPool CreateCommandPool(const CommandPoolDesc & desc) noexcept;
		[[nodiscard]] CommandPool CreateCommandPool(const CommandPoolDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<CommandPool> CreateCommandPoolWithResult(const CommandPoolDesc & desc) noexcept;
		[[nodiscard]] Swapchain CreateSwapchain(const SwapchainDesc & desc) noexcept;
		[[nodiscard]] Swapchain CreateSwapchain(const SwapchainDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<Swapchain> CreateSwapchainWithResult(const SwapchainDesc & desc) noexcept;

		[[nodiscard]] Queue GetQueue(QueueType type, std::uint32_t index = 0) noexcept;
		[[nodiscard]] Queue GetQueue(QueueType type, std::uint32_t index, Error & error) noexcept;
		[[nodiscard]] Result<Queue> GetQueueWithResult(QueueType type, std::uint32_t index = 0) noexcept;

		[[nodiscard]] std::uint32_t GetQueueCount(QueueType type) const noexcept;

		[[nodiscard]] MappedMemory Map(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] MappedMemory Map(BufferHandle buffer, const MapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<MappedMemory> MapWithResult(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] bool Unmap(BufferHandle buffer) noexcept;
		[[nodiscard]] bool Unmap(BufferHandle buffer, Error & error) noexcept;
		[[nodiscard]] bool FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;
		[[nodiscard]] bool InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;

		bool UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteTexture> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteTexture> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteSampler> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteSampler> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes, Error & error) noexcept;

		[[nodiscard]] bool QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out) const noexcept;
		[[nodiscard]] bool QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryBudgetInfo> QueryMemoryBudgetWithResult(HeapType heap) const noexcept;
		bool SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities) noexcept;
		bool SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities, Error & error) noexcept;

		[[nodiscard]] bool CalibrateTimestamp(QueueType queueType, TimestampCalibration & out) const noexcept;
		[[nodiscard]] bool CalibrateTimestamp(QueueType queueType, TimestampCalibration & out, Error & error) const noexcept;
		[[nodiscard]] Result<TimestampCalibration> CalibrateTimestampWithResult(QueueType queueType) const noexcept;

		[[nodiscard]] const DeviceCaps & GetCaps() const noexcept;
		[[nodiscard]] FormatSupport GetFormatSupport(Format format) const noexcept;
		[[nodiscard]] const AdapterInfo & GetAdapterInfo() const noexcept;

		[[nodiscard]] bool GetTextureInfo(TextureHandle texture, TextureInfo & out) const noexcept;
		[[nodiscard]] bool GetTextureInfo(TextureHandle texture, TextureInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<TextureInfo> GetTextureInfoWithResult(TextureHandle texture) const noexcept;

		[[nodiscard]] bool GetBufferInfo(BufferHandle buffer, BufferInfo & out) const noexcept;
		[[nodiscard]] bool GetBufferInfo(BufferHandle buffer, BufferInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<BufferInfo> GetBufferInfoWithResult(BufferHandle buffer) const noexcept;

		[[nodiscard]] ValidationMessageCounts GetValidationMessageCounts() const noexcept;

		bool Destroy(BufferHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(BufferHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(TextureHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(TextureHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(TextureViewHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(TextureViewHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(SamplerHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(SamplerHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(HeapHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(HeapHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(DescriptorSetLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(DescriptorSetHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(DescriptorSetHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(PipelineLayoutHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(PipelineLayoutHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(GraphicsPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(ComputePipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(ComputePipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(RayTracingPipelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(PipelineCacheHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(PipelineCacheHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(AccelerationStructureHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(AccelerationStructureHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(QueryPoolHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(QueryPoolHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(TimelineHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(TimelineHandle handle, const DestroyDesc & desc, Error & error) noexcept;
		bool Destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc = {}) noexcept;
		bool Destroy(BinarySemaphoreHandle handle, const DestroyDesc & desc, Error & error) noexcept;

		bool CollectGarbage() noexcept;
		bool CollectGarbage(Error & error) noexcept;
		bool CollectGarbage(TimelineHandle timeline, std::uint64_t completedValue) noexcept;
		bool CollectGarbage(TimelineHandle timeline, std::uint64_t completedValue, Error & error) noexcept;

		template <GraphicsApiTag Api>
		[[nodiscard]] BufferHandle AdoptBuffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BufferHandle AdoptBuffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<BufferHandle> AdoptBufferWithResult(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureHandle AdoptTexture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureHandle AdoptTexture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TextureHandle> AdoptTextureWithResult(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeBuffer(BufferHandle buffer, NativeBuffer<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeBuffer(BufferHandle buffer, NativeBuffer<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeBuffer<Api>> GetNativeBufferWithResult(BufferHandle buffer) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTexture(TextureHandle texture, NativeTexture<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTexture(TextureHandle texture, NativeTexture<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTexture<Api>> GetNativeTextureWithResult(TextureHandle texture) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureViewHandle AdoptTextureView(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TextureViewHandle AdoptTextureView(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TextureViewHandle> AdoptTextureViewWithResult(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] SamplerHandle AdoptSampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] SamplerHandle AdoptSampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<SamplerHandle> AdoptSamplerWithResult(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTextureView(TextureViewHandle view, NativeTextureView<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTextureView(TextureViewHandle view, NativeTextureView<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTextureView<Api>> GetNativeTextureViewWithResult(TextureViewHandle view) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeSampler(SamplerHandle sampler, NativeSampler<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeSampler(SamplerHandle sampler, NativeSampler<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeSampler<Api>> GetNativeSamplerWithResult(SamplerHandle sampler) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TimelineHandle AdoptTimeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] TimelineHandle AdoptTimeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<TimelineHandle> AdoptTimelineWithResult(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BinarySemaphoreHandle AdoptBinarySemaphore(const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] BinarySemaphoreHandle AdoptBinarySemaphore(
			const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<BinarySemaphoreHandle> AdoptBinarySemaphoreWithResult(
			const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTimeline(TimelineHandle timeline, NativeTimeline<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeTimeline(TimelineHandle timeline, NativeTimeline<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeTimeline<Api>> GetNativeTimelineWithResult(TimelineHandle timeline) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeBinarySemaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] bool GetNativeBinarySemaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out, Error & error) noexcept;
		template <GraphicsApiTag Api>
		[[nodiscard]] Result<NativeBinarySemaphore<Api>> GetNativeBinarySemaphoreWithResult(BinarySemaphoreHandle semaphore) noexcept;

		[[nodiscard]] bool ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportBufferWithResult(BufferHandle buffer, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportHeapWithResult(HeapHandle heap, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportTextureWithResult(TextureHandle texture, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportTimelineWithResult(TimelineHandle timeline, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportBinarySemaphoreWithResult(BinarySemaphoreHandle semaphore, ExternalHandleType type) noexcept;

		[[nodiscard]] BufferHandle ImportBuffer(const ExternalBufferImportDesc & desc) noexcept;
		[[nodiscard]] BufferHandle ImportBuffer(const ExternalBufferImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> ImportBufferWithResult(const ExternalBufferImportDesc & desc) noexcept;
		[[nodiscard]] HeapHandle ImportHeap(const ExternalHeapImportDesc & desc) noexcept;
		[[nodiscard]] HeapHandle ImportHeap(const ExternalHeapImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<HeapHandle> ImportHeapWithResult(const ExternalHeapImportDesc & desc) noexcept;
		[[nodiscard]] TextureHandle ImportTexture(const ExternalTextureImportDesc & desc) noexcept;
		[[nodiscard]] TextureHandle ImportTexture(const ExternalTextureImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> ImportTextureWithResult(const ExternalTextureImportDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle ImportTimeline(const ExternalTimelineImportDesc & desc) noexcept;
		[[nodiscard]] TimelineHandle ImportTimeline(const ExternalTimelineImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TimelineHandle> ImportTimelineWithResult(const ExternalTimelineImportDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle ImportBinarySemaphore(const ExternalBinarySemaphoreImportDesc & desc) noexcept;
		[[nodiscard]] BinarySemaphoreHandle ImportBinarySemaphore(const ExternalBinarySemaphoreImportDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BinarySemaphoreHandle> ImportBinarySemaphoreWithResult(const ExternalBinarySemaphoreImportDesc & desc) noexcept;

		bool CloseExportedHandle(const ExternalHandle & handle) noexcept;
		bool CloseExportedHandle(const ExternalHandle & handle, Error & error) noexcept;

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
			GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
		bool GetNativeTimelineRaw(GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
		bool GetNativeBinarySemaphoreRaw(GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;

		void * m_impl			   = nullptr;
		BackendBlockSet * m_blocks = nullptr;
	};

	template <GraphicsApiTag Api>
	BufferHandle Device::AdoptBuffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept
	{
		return AdoptBufferRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	BufferHandle Device::AdoptBuffer(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptBufferRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<BufferHandle> Device::AdoptBufferWithResult(const NativeBuffer<Api> & native, const AdoptedBufferDesc & desc) noexcept
	{
		Error error{};
		const BufferHandle handle = AdoptBufferRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<BufferHandle>{ handle } : Result<BufferHandle>{ error };
	}

	template <GraphicsApiTag Api>
	TextureHandle Device::AdoptTexture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept
	{
		return AdoptTextureRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TextureHandle Device::AdoptTexture(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTextureRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TextureHandle> Device::AdoptTextureWithResult(const NativeTexture<Api> & native, const AdoptedTextureDesc & desc) noexcept
	{
		Error error{};
		const TextureHandle handle = AdoptTextureRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<TextureHandle>{ handle } : Result<TextureHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeBuffer(BufferHandle buffer, NativeBuffer<Api> & out) noexcept
	{
		out = {};
		return GetNativeBufferRaw(Api::id, buffer, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeBuffer(BufferHandle buffer, NativeBuffer<Api> & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		return GetNativeBufferRaw(Api::id, buffer, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeBuffer<Api>> Device::GetNativeBufferWithResult(BufferHandle buffer) noexcept
	{
		Error error{};
		NativeBuffer<Api> out{};
		return GetNativeBufferRaw(Api::id, buffer, &out, &error) ? Result<NativeBuffer<Api>>{ out } : Result<NativeBuffer<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTexture(TextureHandle texture, NativeTexture<Api> & out) noexcept
	{
		out = {};
		return GetNativeTextureRaw(Api::id, texture, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTexture(TextureHandle texture, NativeTexture<Api> & out, Error & error) noexcept
	{
		out	  = {};
		error = {};
		return GetNativeTextureRaw(Api::id, texture, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTexture<Api>> Device::GetNativeTextureWithResult(TextureHandle texture) noexcept
	{
		Error error{};
		NativeTexture<Api> out{};
		return GetNativeTextureRaw(Api::id, texture, &out, &error) ? Result<NativeTexture<Api>>{ out } : Result<NativeTexture<Api>>{ error };
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

		[[nodiscard]] Device Get() const noexcept
		{
			return Device{ m_impl, m_blocks };
		}

		[[nodiscard]] bool IsValid() const noexcept
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
			if (IsRegistered(Api::id))
			{
				return Error{
					.code	 = ErrorCode::eInvalidState,
					.message = "graphics API backend already registered",
				};
			}

			BackendCreateInfo entry = createInfo;
			entry.info.id			= Api::id;

			if (!detail::TryPushBack(m_entries, entry))
			{
				return Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "graphics API registry entry storage allocation failed",
				};
			}

			if (!detail::TryPushBack(m_infos, entry.info))
			{
				m_entries.pop_back();
				return Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "graphics API registry info storage allocation failed",
				};
			}

			return {};
		}

		[[nodiscard]] const BackendInfo * Find(GraphicsApiId id) const noexcept
		{
			const auto found = std::ranges::find(m_infos, id, &BackendInfo::id);
			return found != m_infos.end() ? &*found : nullptr;
		}

		[[nodiscard]] const BackendInfo * Find(const std::string_view name) const noexcept
		{
			const auto found = std::ranges::find_if(m_infos,
				[name](const BackendInfo & info)
				{
					return name == info.canonicalName || name == ShortApiName(info.canonicalName);
				});
			return found != m_infos.end() ? &*found : nullptr;
		}

		[[nodiscard]] bool IsRegistered(GraphicsApiId id) const noexcept
		{
			return Find(id) != nullptr;
		}

		[[nodiscard]] std::span<const BackendInfo> EnumerateBackends() const noexcept
		{
			return m_infos;
		}

	private:
		friend struct detail::RegistryAccess;

		detail::HostVector<BackendCreateInfo> m_entries;
		detail::HostVector<BackendInfo> m_infos;
	};

	AZO_RHI_API Result<UniqueInstance> CreateInstance(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc);

	AZO_RHI_API Result<UniqueDevice> CreateDevice(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const DeviceDesc & desc);

	template <GraphicsApiTag Api>
	Result<UniqueDevice> CreateDevice(const DeviceDesc & desc);

	template <>
	AZO_RHI_API Result<UniqueDevice> CreateDevice<VulkanApi>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> CreateDevice<D3D12Api>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> CreateDevice<MetalApi>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> CreateDevice<Metal4Api>(const DeviceDesc & desc);
	template <>
	AZO_RHI_API Result<UniqueDevice> CreateDevice<NullApi>(const DeviceDesc & desc);

	template <GraphicsApiTag Api>
	TextureViewHandle Device::AdoptTextureView(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept
	{
		return AdoptTextureViewRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TextureViewHandle Device::AdoptTextureView(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTextureViewRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TextureViewHandle> Device::AdoptTextureViewWithResult(const NativeTextureView<Api> & native, const AdoptedTextureViewDesc & desc) noexcept
	{
		Error error{};
		const TextureViewHandle handle = AdoptTextureViewRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<TextureViewHandle>{ handle } : Result<TextureViewHandle>{ error };
	}

	template <GraphicsApiTag Api>
	SamplerHandle Device::AdoptSampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept
	{
		return AdoptSamplerRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	SamplerHandle Device::AdoptSampler(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptSamplerRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<SamplerHandle> Device::AdoptSamplerWithResult(const NativeSampler<Api> & native, const AdoptedSamplerDesc & desc) noexcept
	{
		Error error{};
		const SamplerHandle handle = AdoptSamplerRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<SamplerHandle>{ handle } : Result<SamplerHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTextureView(TextureViewHandle view, NativeTextureView<Api> & out) noexcept
	{
		return GetNativeTextureViewRaw(Api::id, view, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTextureView(TextureViewHandle view, NativeTextureView<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeTextureViewRaw(Api::id, view, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTextureView<Api>> Device::GetNativeTextureViewWithResult(TextureViewHandle view) noexcept
	{
		Error error{};
		NativeTextureView<Api> out{};
		return GetNativeTextureViewRaw(Api::id, view, &out, &error) ? Result<NativeTextureView<Api>>{ out } : Result<NativeTextureView<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeSampler(SamplerHandle sampler, NativeSampler<Api> & out) noexcept
	{
		return GetNativeSamplerRaw(Api::id, sampler, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeSampler(SamplerHandle sampler, NativeSampler<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeSamplerRaw(Api::id, sampler, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeSampler<Api>> Device::GetNativeSamplerWithResult(SamplerHandle sampler) noexcept
	{
		Error error{};
		NativeSampler<Api> out{};
		return GetNativeSamplerRaw(Api::id, sampler, &out, &error) ? Result<NativeSampler<Api>>{ out } : Result<NativeSampler<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	TimelineHandle Device::AdoptTimeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept
	{
		return AdoptTimelineRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	TimelineHandle Device::AdoptTimeline(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptTimelineRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<TimelineHandle> Device::AdoptTimelineWithResult(const NativeTimeline<Api> & native, const AdoptedTimelineDesc & desc) noexcept
	{
		Error error{};
		const TimelineHandle handle = AdoptTimelineRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<TimelineHandle>{ handle } : Result<TimelineHandle>{ error };
	}

	template <GraphicsApiTag Api>
	BinarySemaphoreHandle Device::AdoptBinarySemaphore(const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept
	{
		return AdoptBinarySemaphoreRaw(Api::id, &native, desc, nullptr);
	}

	template <GraphicsApiTag Api>
	BinarySemaphoreHandle Device::AdoptBinarySemaphore(
		const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc, Error & error) noexcept
	{
		error = {};
		return AdoptBinarySemaphoreRaw(Api::id, &native, desc, &error);
	}

	template <GraphicsApiTag Api>
	Result<BinarySemaphoreHandle> Device::AdoptBinarySemaphoreWithResult(
		const NativeBinarySemaphore<Api> & native, const AdoptedBinarySemaphoreDesc & desc) noexcept
	{
		Error error{};
		const BinarySemaphoreHandle handle = AdoptBinarySemaphoreRaw(Api::id, &native, desc, &error);
		return handle.IsValid() ? Result<BinarySemaphoreHandle>{ handle } : Result<BinarySemaphoreHandle>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTimeline(TimelineHandle timeline, NativeTimeline<Api> & out) noexcept
	{
		return GetNativeTimelineRaw(Api::id, timeline, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeTimeline(TimelineHandle timeline, NativeTimeline<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeTimelineRaw(Api::id, timeline, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeTimeline<Api>> Device::GetNativeTimelineWithResult(TimelineHandle timeline) noexcept
	{
		Error error{};
		NativeTimeline<Api> out{};
		return GetNativeTimelineRaw(Api::id, timeline, &out, &error) ? Result<NativeTimeline<Api>>{ out } : Result<NativeTimeline<Api>>{ error };
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeBinarySemaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out) noexcept
	{
		return GetNativeBinarySemaphoreRaw(Api::id, semaphore, &out, nullptr);
	}

	template <GraphicsApiTag Api>
	bool Device::GetNativeBinarySemaphore(BinarySemaphoreHandle semaphore, NativeBinarySemaphore<Api> & out, Error & error) noexcept
	{
		error = {};
		return GetNativeBinarySemaphoreRaw(Api::id, semaphore, &out, &error);
	}

	template <GraphicsApiTag Api>
	Result<NativeBinarySemaphore<Api>> Device::GetNativeBinarySemaphoreWithResult(BinarySemaphoreHandle semaphore) noexcept
	{
		Error error{};
		NativeBinarySemaphore<Api> out{};
		return GetNativeBinarySemaphoreRaw(Api::id, semaphore, &out, &error) ? Result<NativeBinarySemaphore<Api>>{ out }
																			 : Result<NativeBinarySemaphore<Api>>{ error };
	}

}

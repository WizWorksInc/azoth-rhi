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

/**
 * \file
 * \brief Public instance, device, capability, adapter, and backend registry API.
 */

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
#include "azoth/rhi/native/native_access.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include <algorithm>
#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace azo::rhi
{

	class DeviceMemoryAllocator;
	class Profiler;

	namespace detail
	{
		struct FacadeBuilder;
		struct RegistryAccess;
	} // namespace detail

	class UniqueDevice;
	class UniqueInstance;

	/**
	 * \brief RHI validation level.
	 *
	 * Native API validation is configured separately through NativeValidationDesc.
	 */
	enum class ValidationMode : std::uint8_t
	{
		eOff,

		/**
		 * \brief Low-overhead checks for release and profiling builds.
		 */
		eReleaseLight,

		/**
		 * \brief Tracks resource states, lifetimes, queue ownership, and descriptor compatibility.
		 */
		eDeveloper,

		/**
		 * \brief Preserves debug metadata for capture and debugger work.
		 */
		eCapture,
	};

	/**
	 * \brief Per-native-validation-feature policy.
	 */
	enum class NativeValidationPolicy : std::uint8_t
	{
		/**
		 * \brief Uses backend policy for the active ValidationMode.
		 */
		eFollowValidationMode,

		eDisabled,

		/**
		 * \brief Forces the native validation feature on when the backend supports it.
		 *
		 * \attention Affects performance, timing, memory use, and driver behavior.
		 */
		eEnabled,
	};

	/**
	 * \brief Native validation message severity.
	 */
	enum class ValidationMessageSeverity : std::uint8_t
	{
		eWarning,
		eError,
	};

	/**
	 * \brief Receives one native validation message.
	 *
	 * \attention Called on the thread that trips the check, from inside the driver call, and may be entered concurrently. Keep implementations thread-safe and
	 * do not re-enter the RHI from the callback.
	 */
	using ValidationMessageCallback = void (*)(ValidationMessageSeverity severity, const char * message, void * userData) noexcept;

	/**
	 * \brief Native validation requested during instance creation.
	 *
	 * \warning Support is backend-specific. Vulkan reads every field. Direct3D 12 reads apiValidation and gpuBasedValidation, but eDisabled cannot disable a
	 * debug layer already enabled by ValidationMode. Metal and Null currently ignore these fields.
	 */
	struct NativeValidationDesc final
	{
		NativeValidationPolicy apiValidation			 = NativeValidationPolicy::eFollowValidationMode;
		NativeValidationPolicy synchronizationValidation = NativeValidationPolicy::eFollowValidationMode;
		NativeValidationPolicy gpuBasedValidation		 = NativeValidationPolicy::eDisabled;
		NativeValidationPolicy bestPractices			 = NativeValidationPolicy::eFollowValidationMode;
		bool breakOnError								 = false;
		bool breakOnWarning								 = false;

		/**
		 * \brief Optional destination for native validation messages.
		 *
		 * Null leaves each backend at its default. Vulkan prints to stderr when no callback is installed. Direct3D 12 relies on the platform debug layer. Message
		 * counts are tracked whether or not this callback is installed when the backend has a validation channel.
		 */
		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;

		std::span<const char * const> extraNativeLayers;
		std::span<const char * const> extraNativeExtensions;
	};

	/**
	 * \brief Requested version of a backend's underlying graphics API.
	 */
	struct ApiVersion final
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
	};

	/**
	 * \brief Instance creation request used for explicit instance creation.
	 */
	struct InstanceDesc final
	{
		const char * applicationName		  = "Azoth Application";
		const char * engineName				  = "Azoth";
		std::uint32_t applicationVersionMajor = 0;
		std::uint32_t applicationVersionMinor = 0;
		std::uint32_t engineVersionMajor	  = 0;
		std::uint32_t engineVersionMinor	  = 0;

		/**
		 * \brief Requested underlying graphics-API version.
		 *
		 * {0, 0} selects the backend default.
		 */
		ApiVersion apiVersion{};
		ValidationMode validation = ValidationMode::eReleaseLight;
		NativeValidationDesc nativeValidation{};
	};

	/**
	 * \brief Queue capability request used during device creation.
	 */
	struct QueueRequest final
	{
		QueueType type			   = QueueType::eGraphics;
		std::uint32_t minCount	   = 1;
		bool requireDedicatedQueue = false;
	};

	/**
	 * \brief How much of the sparse-residency ladder a device provides.
	 *
	 * Ordinal, so a level implies every level below it. Support here varies by degree and is not simply present or absent, and a single tier is what keeps the
	 * levels from contradicting one another the way independent booleans could.
	 */
	enum class SparseTier : std::uint8_t
	{
		/**
		 * \brief No sparse binding. The queue publishes no SparseApi.
		 */
		eNone,

		/**
		 * \brief Sparse buffers, and textures may be bound tile by tile.
		 *
		 * Reads of an unmapped tile are undefined at this level. Direct3D 12 tiled resources tier 1, Vulkan sparseBinding with sparseResidencyBuffer.
		 */
		eBuffers,

		/**
		 * \brief Adds the 2D texture residency guarantee: an unmapped tile reads zero and discards writes.
		 *
		 * Direct3D 12 tiled resources tier 2, Vulkan sparseResidencyImage2D.
		 */
		eResidentTextures,

		/**
		 * \brief Adds the same guarantee for 3D textures.
		 *
		 * Direct3D 12 tiled resources tier 3, Vulkan sparseResidencyImage3D.
		 */
		eResidentVolumes,
	};

	/**
	 * \brief How much of the conservative-rasterization ladder a device provides.
	 *
	 * Ordinal, so a level implies every level below it. Direct3D 12 reads this as a three-level tier natively and reporting a boolean would discard the
	 * distinction between them.
	 */
	enum class ConservativeRasterTier : std::uint8_t
	{
		eNone,

		/**
		 * \brief Conservative raster with a half-pixel uncertainty region and no post-snap degenerate culling.
		 *
		 * Direct3D 12 conservative rasterization tier 1, Vulkan VK_EXT_conservative_rasterization overestimate.
		 */
		eBasic,

		/**
		 * \brief Tightens the uncertainty region to 1/256 of a pixel and culls post-snap degenerate triangles.
		 *
		 * Direct3D 12 conservative rasterization tier 2.
		 */
		eDegenerateCulling,

		/**
		 * \brief Adds inner input coverage, so a shader can tell a fully covered pixel from a partially covered one.
		 *
		 * Direct3D 12 conservative rasterization tier 3 and its SV_InnerCoverage, Vulkan fullyCoveredFragmentShaderInputVariable.
		 */
		eInnerCoverage,
	};

	/**
	 * \brief How far a device's descriptor binding model reaches.
	 *
	 * Ordinal, so a level implies every level below it. There is no absent level because every backend binds descriptors somehow. Direct3D 12 derives every level
	 * of this from one resource binding tier, so the levels cannot vary independently.
	 */
	enum class BindingTier : std::uint8_t
	{
		/**
		 * \brief Fixed-size descriptor tables, indexed only by a constant.
		 *
		 * Direct3D 12 resource binding tier 1.
		 */
		eBasic,

		/**
		 * \brief Shader-visible descriptor arrays may be indexed dynamically.
		 *
		 * Direct3D 12 resource binding tier 2, Vulkan descriptor indexing.
		 */
		eDynamicIndexing,

		/**
		 * \brief Unbounded descriptor arrays, which is what a bindless renderer needs.
		 *
		 * Direct3D 12 resource binding tier 3, Vulkan descriptor indexing with runtime-sized arrays and non-uniform indexing.
		 */
		eUnbounded,
	};

	/**
	 * \brief Optional device capability that adapter selection can require or prefer.
	 *
	 * Each value mirrors a DeviceCaps flag negotiated per adapter. Required features reject adapters that lack them. Preferred features bias selection but do not
	 * reject adapters.
	 */
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

	/**
	 * \brief Selects how a device records rendering scopes.
	 *
	 * Dynamic rendering begins a scope from its attachments alone. Otherwise the backend lowers the same BeginRenderingDesc onto cached render-pass and
	 * framebuffer objects. Only Vulkan changes behavior here. Metal and Direct3D 12 are always attachment-driven.
	 */
	enum class DynamicRenderingMode : std::uint8_t
	{
		/**
		 * \brief Use dynamic rendering when the adapter supports it, otherwise lower to render-pass objects.
		 */
		ePreferred,

		/**
		 * \brief Always lower rendering scopes to render-pass objects where the backend has that distinction.
		 */
		eDisabled,

		/**
		 * \brief Device creation fails when the adapter cannot provide dynamic rendering.
		 */
		eRequired,
	};

	/**
	 * \brief Device creation request and per-device policy.
	 *
	 * Spans and string pointers are borrowed for the duration of device creation.
	 */
	struct DeviceDesc final
	{
		std::span<const QueueRequest> queues;

		/**
		 * \brief Requested underlying graphics-API version.
		 *
		 * {0, 0} selects the backend default.
		 */
		ApiVersion apiVersion{};
		ValidationMode validation = ValidationMode::eReleaseLight;

		/**
		 * \brief Native validation for the instance created behind this device.
		 *
		 * Device creation also creates the backend instance, so instance-scoped validation settings are named here too.
		 */
		NativeValidationDesc nativeValidation{};

		/**
		 * \brief Names objects so captures and validation messages can identify resources.
		 */
		bool enableDebugNames = true;

		/**
		 * \brief Enables recorded debug labels through backend-native marker APIs.
		 *
		 * When false, label calls are no-op successes so callers can record them unconditionally.
		 */
		bool enableDebugLabels = true;
		bool preferDiscreteGpu = true;

		/**
		 * \brief Rejects adapters that cannot create swapchains.
		 */
		bool requireSwapchain = true;

		/**
		 * \brief Lets Map hand back a pointer into memory this device calls device local.
		 *
		 * Off by default so every backend answers the same way and a renderer written on a unified adapter does not map what a split one cannot. Turning it on
		 * only works where DeviceCaps::deviceLocalMemoryIsHostVisible is true, and it is a promise the caller is writing platform-specific code, since ordering
		 * that pointer against the GPU is theirs to arrange.
		 */
		bool allowDeviceLocalMapping = false;

		/**
		 * \brief Selects dynamic rendering or render-pass-object lowering for rendering scopes.
		 */
		DynamicRenderingMode dynamicRendering = DynamicRenderingMode::ePreferred;

		/**
		 * \brief Calling-side threading policy for device creates and destroys.
		 *
		 * Command-list recording is not guarded in any mode. A command list is host-thread owned for its recording lifetime.
		 */
		ThreadingMode threading = ThreadingMode::eThreads;

		/**
		 * \brief Cooperative host synchronization callbacks.
		 *
		 * Read only when threading is eCooperative and required then. Creation fails instead of falling back to thread locks when the set is incomplete.
		 */
		SyncOps sync{};

		/**
		 * \brief Optional allocator for this device's heap-backed resource memory.
		 *
		 * The device captures the pointer at creation. Backends without placed-resource support reject a non-null allocator during creation.
		 */
		DeviceMemoryAllocator * allocator = nullptr;

		/**
		 * \brief Optional profiler sink for this device's RHI-owned zones and plots.
		 *
		 * Null falls back to the process-wide profiler. Backend-internal zones may still use the process-wide sink when no device context is available.
		 */
		Profiler * profiler = nullptr;

		/**
		 * \brief Allows CPU or software adapters during device selection.
		 */
		bool allowSoftwareAdapter = false;

		/**
		 * \brief Allows linked multi-adapter devices during selection.
		 */
		bool allowLinkedAdapters = false;

		/**
		 * \brief Selects a backend-local adapter when not kInvalidIndex.
		 */
		std::uint32_t preferredAdapterIndex = kInvalidIndex;

		/**
		 * \brief Features an adapter must expose.
		 *
		 * Selection fails when no adapter satisfies all required features.
		 *
		 * \attention Together with preferredFeatures this is a declaration, not a filter. A device grants no optional feature that appears in neither
		 * list, even where the adapter has it, so DeviceCaps after creation reports what was granted, not what the adapter can do. A caller that declares nothing
		 * gets a device with no optional features at all.
		 */
		std::span<const DeviceFeature> requiredFeatures;

		/**
		 * \brief Features to use where the adapter has them, without refusing an adapter that does not.
		 *
		 * The right list for anything a caller can do without. It is also how to find out what an adapter offers: declare everything you could use, then read
		 * DeviceCaps back to see which of them you were given.
		 */
		std::span<const DeviceFeature> preferredFeatures;

		const char * debugName = nullptr;
	};

	/**
	 * \brief Builds the InstanceDesc used when device creation creates its own instance.
	 *
	 * Add new instance-scoped fields carried by DeviceDesc here too. AbiLayout.DescsThatFeedInstanceCreationHaveNotChangedShape is the tripwire for missed copies.
	 */
	[[nodiscard]] constexpr InstanceDesc InstanceDescForDevice(const DeviceDesc & desc) noexcept
	{
		InstanceDesc instance{};
		instance.apiVersion		  = desc.apiVersion;
		instance.validation		  = desc.validation;
		instance.nativeValidation = desc.nativeValidation;

		return instance;
	}

	/**
	 * \brief Format capabilities for one device.
	 */
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

		/**
		 * \brief This format can be the source of a scaled blit.
		 *
		 * Separate from copySrc because a scaled blit resamples and a copy does not, so a format can be copyable and not blittable. Meaningless when
		 * DeviceCaps::supportsScaledBlit is false, since then no format can take part in one.
		 */
		bool blitSrc = false;

		/**
		 * \brief This format can be the destination of a scaled blit.
		 */
		bool blitDst = false;
	};

	/**
	 * \brief Features, limits and backend facts for a created device.
	 *
	 * Query optional features before using them. Every field a DeviceFeature names reports the granted set: what the caller declared through DeviceDesc, narrowed
	 * to what the adapter has. A feature the caller never declared reads false whatever the hardware can do. A program that runs on one adapter therefore runs on
	 * any adapter meeting its declaration. Tiers, limits and queue counts are the adapter's own answers and are not narrowed.
	 */
	struct DeviceCaps final
	{
		GraphicsApiId apiId{};

		/**
		 * \brief Underlying graphics-API version the device was actually created with.
		 */
		ApiVersion apiVersion{};

		bool supportsTimelineSync = false;

		/**
		 * \brief Reflects the active DynamicRenderingMode.
		 *
		 * False means the backend lowers rendering scopes onto render-pass objects internally.
		 */
		bool supportsDynamicRendering = false;

		/**
		 * \brief How far this device's descriptor binding model reaches.
		 *
		 * eDynamicIndexing or better is what descriptor-indexed arrays need, eUnbounded is what a bindless renderer needs.
		 */
		BindingTier bindingTier = BindingTier::eBasic;

		/**
		 * \brief Descriptors may be updated after binding when the layout allows it.
		 */
		bool supportsUpdateAfterBind = false;

		/**
		 * \brief Descriptor arrays may leave unused entries unbound when the layout allows it.
		 */
		bool supportsPartiallyBoundDescriptors = false;

		/**
		 * \brief This created device can create swapchains.
		 *
		 * BackendInfo::supportsSurfaces is a backend-level claim before any device exists. This flag is the device-level answer.
		 */
		bool supportsSurfaces = false;

		bool supportsAnisotropy		  = false;
		bool supportsIndependentBlend = false;

		/**
		 * \brief Texture views may remap channels through TextureViewDesc::swizzle.
		 *
		 * Not universal. Vulkan portability reports this per adapter. False means a non-identity swizzle is refused, not silently dropped.
		 */
		bool supportsTextureViewSwizzle = false;

		/**
		 * \brief Multi-planar Y'CbCr formats can be created and viewed a plane at a time.
		 *
		 * Sampling a plane returns its raw texels: no backend converts Y'CbCr to RGB on this path, so the shader does. Metal reports false, having no way to create a
		 * multi-planar texture from a descriptor at all, and reaches these formats only through an imported IOSurface.
		 */
		bool supportsMultiPlanarFormats = false;

		/**
		 * \brief A sampler can perform the Y'CbCr conversion itself, through SamplerYcbcrConversionDesc.
		 *
		 * Vulkan alone offers it. Direct3D 12 and Metal have no equivalent object, so they report false and the conversion belongs in the shader over the per-plane
		 * views supportsMultiPlanarFormats provides.
		 */
		bool supportsSamplerYcbcrConversion = false;

		/**
		 * \brief How much of the conservative-rasterization ladder this device provides.
		 */
		ConservativeRasterTier conservativeRasterTier = ConservativeRasterTier::eNone;

		bool supportsDepthBounds			   = false;
		bool supportsTimestampQueries		   = false;
		bool supportsPipelineStatisticsQueries = false;
		bool supportsIndirectCount			   = false;

		/**
		 * \brief A timestamp may be written while a rendering or dispatch scope is open.
		 *
		 * False means the device fixes its counter sample points when a scope opens without accepting a sample at an arbitrary command, so
		 * CommandList::WriteTimestamp is refused between the begin and end of one and is available everywhere else. Time a rendering scope with
		 * BeginRenderingDesc::timestamps instead, which records on those fixed points and works either way.
		 *
		 * Only meaningful when supportsTimestampQueries is set.
		 */
		bool supportsTimestampWritesInScope = false;

		/**
		 * \brief One indirect draw call can issue several draws.
		 *
		 * When false, the backend emits one single-draw command per entry.
		 */
		bool supportsMultiDrawIndirect = false;

		/**
		 * \brief Indirect draws may name a non-zero firstInstance.
		 *
		 * No backend can emulate this because the value lives in GPU memory. Shaders using firstInstance need another addressing path when false.
		 */
		bool supportsDrawIndirectFirstInstance = false;

		/**
		 * \brief Shaders may read base vertex, base instance, and draw index.
		 *
		 * Slang lowering can require this for HLSL vertex-id semantics even when the shader does not name a draw parameter directly.
		 */
		bool supportsShaderDrawParameters = false;

		/**
		 * \brief Half-precision math is legal in shader bodies.
		 *
		 * This is separate from R16Float storage support. Keep an fp32 fallback because driver performance can regress even when fp16 is available.
		 */
		bool supportsShaderFloat16 = false;

		bool supportsEnhancedBarriers = false;

		/**
		 * \brief Explicit placed resources or aliasing heaps are available.
		 */
		bool supportsPlacedResources = false;

		/**
		 * \brief Memory this device calls device local can also be mapped, because the adapter shares one pool with the host.
		 *
		 * False everywhere memory is split, which is what the portable model assumes. True is not permission on its own: Map still refuses a device-local buffer
		 * unless DeviceDesc::allowDeviceLocalMapping asked for it, so code written against the portable rule keeps behaving the same on a unified adapter.
		 */
		bool deviceLocalMemoryIsHostVisible = false;

		/**
		 * \brief This device has a fixed-function scaled blit, so CommandList::Blit resamples without refusing.
		 *
		 * False means the backend has no such hardware operation and Blit reports eUnsupportedFeature. Resampling is still reachable through the utility target,
		 * which dispatches to this when it is true and to a compute shader when it is not.
		 *
		 * True is necessary and not sufficient: a given format pair also has to report FormatSupport::blitSrc and blitDst.
		 */
		bool supportsScaledBlit = false;

		/**
		 * \brief This device can take a native object made on it and hand back the native object a handle stands for.
		 *
		 * Separate from any external sharing answer on purpose. Adoption needs no extension and works on any device, while moving memory across a boundary is gated
		 * per adapter and handle type. What each backend adopts differs: Direct3D 12 has no view or sampler object and refuses those four by name.
		 */
		bool supportsResourceAdoption = false;

		bool supportsMemoryBudget = false;

		/**
		 * \brief How much of the sparse-residency ladder this device provides.
		 *
		 * Anything above eNone means the queue publishes SparseApi, since binding sparse memory is the only thing the levels are good for.
		 */
		SparseTier sparseTier = SparseTier::eNone;

		bool supportsDynamicBufferOffsets = false;

		/**
		 * \brief Buffer bindings may lower to root descriptors where legal.
		 */
		bool supportsRootDescriptors = false;
		bool supportsPipelineCache	 = false;

		/**
		 * \brief The compiled encoding this device's pipelines take.
		 *
		 * Read it without keeping a table of which backend wants what: a shader pipeline asks the device and compiles for the answer, which is what lets a backend
		 * the caller has never heard of work without a new entry somewhere.
		 */
		ShaderBinaryFormat shaderBinaryFormat = ShaderBinaryFormat::eBackendNative;

		/**
		 * \brief Whether this device also compiles shading language source at pipeline creation, which ShaderBinary::isSource asks for.
		 *
		 * False means a source binary is refused, not compiled, so a caller with no build step can tell before it tries.
		 */
		bool supportsShaderSource = false;

		/**
		 * \brief CPU and GPU timestamps can be calibrated to a common time domain.
		 */
		bool supportsTimestampCalibration = false;

		/**
		 * \brief Validation message totals can be queried from this device.
		 *
		 * False means zero counts are not evidence of a clean run because no validation channel was installed or readable.
		 */
		bool reportsValidationMessageCounts = false;

		/**
		 * \brief Ray tracing is designed and unimplemented as of 2026-07-31.
		 *
		 * The descriptions, shader stages, resource kind and both interface blocks exist. No backend talking to real hardware fills them in so this and the three
		 * flags below read false everywhere except Null. They are kept because the design would be rebuilt unchanged. Marking them matters because a flag that can
		 * never be true is worth saying so about.
		 */
		bool supportsRayTracing = false;

		bool supportsAccelerationStructureUpdate = false;

		bool supportsAccelerationStructureCompaction = false;

		bool supportsShaderBindingTable = false;

		bool supportsMultiAdapter = false;

		/**
		 * \brief Queues created per capability, clamped to what the adapter exposes.
		 *
		 * Zero means no queue of that capability was created.
		 */
		std::uint32_t graphicsQueueCount = 0;
		std::uint32_t computeQueueCount	 = 0;
		std::uint32_t copyQueueCount	 = 0;

		/**
		 * \brief True when the capability runs on a separate engine from graphics.
		 *
		 * Folding onto the graphics family reports false even if a distinct queue index from that family is returned.
		 */
		bool hasDedicatedComputeQueue  = false;
		bool hasDedicatedTransferQueue = false;

		std::uint32_t maxColorAttachments  = 0;
		std::uint32_t maxRenderTargets	   = 0;
		std::uint32_t maxDescriptorSets	   = 0;
		std::uint32_t maxDescriptorsPerSet = 0;
		/**
		 * \brief Largest descriptor-indexed array of each kind or zero when the device states no ceiling.
		 *
		 * Zero is not the same as no bindless. Read bindingTier for that: eUnbounded means descriptor-indexed arrays work and these say how large one may get where
		 * the backend can put a number on it. Direct3D 12 and Vulkan both can. Metal has no descriptor heap to bound so it reports zero and the tier carries the
		 * answer.
		 */
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

		/**
		 * \brief Meaningful low bits in a GPU timestamp.
		 *
		 * Mask to this width before differencing so wraparound is handled.
		 */
		std::uint32_t timestampValidBits = 0;

		/**
		 * \brief Returns whether the created device exposes the given optional feature.
		 */
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
			// The ladder read back as the thresholds the request tokens name. Sparse resources at all is the first level above none, and the texture residency guarantee
			// is the level above that.
			case DeviceFeature::eSparseResources:
			case DeviceFeature::eSparseBuffers:			 return sparseTier >= SparseTier::eBuffers;
			case DeviceFeature::eSparseTextures:		 return sparseTier >= SparseTier::eResidentTextures;
			case DeviceFeature::eSparseVolumes:			 return sparseTier >= SparseTier::eResidentVolumes;
			case DeviceFeature::eTextureViewSwizzle:	 return supportsTextureViewSwizzle;
			case DeviceFeature::eMultiPlanarFormats:	 return supportsMultiPlanarFormats;
			case DeviceFeature::eSamplerYcbcrConversion: return supportsSamplerYcbcrConversion;
			}

			return false;
		}
	};

	/**
	 * \brief Physical adapter category reported by the backend.
	 */
	enum class AdapterType : std::uint8_t
	{
		eUnknown,
		eIntegrated,
		eDiscrete,
		eVirtual,
		eCpu,
	};

	/**
	 * \brief Normalized graphics-driver identity.
	 */
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

	/**
	 * \brief Selectable graphics adapter information.
	 */
	struct AdapterInfo final
	{
		AdapterType type = AdapterType::eUnknown;

		/**
		 * \brief Graphics API that reported this adapter.
		 */
		GraphicsApiId apiId{};

		/**
		 * \brief Backend-local adapter index used for selection.
		 */
		std::uint32_t adapterIndex = 0;

		/**
		 * \brief PCI vendor identifier when available.
		 */
		std::uint32_t vendorId = 0;

		/**
		 * \brief PCI device identifier when available.
		 */
		std::uint32_t deviceId = 0;

		/**
		 * \brief Universally unique identifier for this physical device, zeroed where the backend has none.
		 *
		 * This is what identifies an adapter across an API or process boundary. vendorId and deviceId cannot. They are PCI identifiers so two identical adapters in
		 * one machine report the same pair. Vulkan reports it from VkPhysicalDeviceIDProperties and it is stable across driver versions. Direct3D 12 has no
		 * equivalent and identifies adapters through deviceLUID. Metal and Null zero it.
		 */
		std::array<std::uint8_t, 16> deviceUUID{};

		/**
		 * \brief Identifier for the driver build behind this device, zeroed where the backend has none.
		 *
		 * Distinct from deviceUUID and not a finer version of it. Two components can only share external memory when this matches, so it gates whether sharing is
		 * possible at all, where deviceUUID says which device to share with. Filled by Vulkan, zeroed elsewhere.
		 */
		std::array<std::uint8_t, 16> driverUUID{};

		/**
		 * \brief Locally unique identifier correlating this adapter with the same one seen through another Windows API.
		 *
		 * This is what matches a Vulkan physical device to its DXGI adapter. Meaningful only when deviceLUIDValid is set, and a zeroed LUID is not distinguishable
		 * from an absent one without it, which is why the flag exists and not a zero test.
		 */
		std::array<std::uint8_t, 8> deviceLUID{};

		/**
		 * \brief Whether deviceLUID holds a value the backend was given.
		 *
		 * Reported, not inferred from the platform. A LUID is a Windows concept and this is not simply true on Windows and false elsewhere: MoltenVK sets it on
		 * macOS, so the flag carries what the driver answered and a caller reads it without testing which platform it is on.
		 *
		 * False on any backend that fills no LUID, which is Metal and Null.
		 */
		bool deviceLUIDValid = false;

		/**
		 * \brief Dedicated video memory in bytes when available.
		 */
		std::uint64_t dedicatedVideoMemoryBytes = 0;

		/**
		 * \brief Dedicated system memory in bytes when available.
		 */
		std::uint64_t dedicatedSystemMemoryBytes = 0;

		/**
		 * \brief Shared system memory in bytes when available.
		 */
		std::uint64_t sharedSystemMemoryBytes = 0;

		bool unifiedMemoryArchitecture = false;
		bool linkedAdapter			   = false;

		/**
		 * \brief Human-readable adapter name when available.
		 */
		const char * name = nullptr;

		/**
		 * \brief Vulkan driver id when available.
		 *
		 * Direct3D 12 identifies the vendor through vendorId. Metal and Null leave this eUnknown.
		 */
		DriverId driverId = DriverId::eUnknown;

		/**
		 * \brief Raw driver version for programmatic comparison.
		 *
		 * Vulkan reports its 32-bit driverVersion widened. Direct3D 12 reports a 64-bit WDDM value. Metal and Null report zero.
		 */
		std::uint64_t driverVersionRaw = 0;

		/**
		 * \brief Human-readable driver or platform version string.
		 *
		 * Null when unavailable. Non-null storage is backend-owned.
		 */
		const char * driverVersion = nullptr;

		/**
		 * \brief Free-form driver identification string.
		 *
		 * Null when unavailable. Non-null storage is backend-owned.
		 */
		const char * driverInfo = nullptr;
	};

	/**
	 * \brief Canonical text of an adapter UUID, NUL terminated.
	 */
	using AdapterUuidString = std::array<char, 37>;

	/**
	 * \brief Canonical text of an adapter LUID, NUL terminated.
	 */
	using AdapterLuidString = std::array<char, 17>;

	namespace detail
	{
		inline constexpr std::array<char, 16> kHexDigits{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	} // namespace detail

	/**
	 * \brief Formats an adapter UUID as the usual 8-4-4-4-12 hexadecimal text, for logs and for a human comparing two adapters.
	 *
	 * Layout only. These sixteen bytes are what the driver reported and carry none of RFC 4122's variant or version structure, so nothing here reads a meaning out
	 * of them and the text is the bytes in order and not a decoded identifier.
	 */
	[[nodiscard]] constexpr AdapterUuidString FormatAdapterUuid(const std::array<std::uint8_t, 16> & uuid) noexcept
	{
		AdapterUuidString text{};
		std::size_t at = 0;

		// Thirty-two hex digits and four dashes fill the thirty-six the string holds, leaving the last for the terminator, and each hex index is masked to the
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

	/**
	 * \brief Formats an adapter LUID as plain hexadecimal, for correlating one adapter across two Windows APIs.
	 *
	 * Meaningful only where AdapterInfo::deviceLUIDValid is set. The bytes are emitted in the order the backend reported them and are not byte swapped.
	 */
	[[nodiscard]] constexpr AdapterLuidString FormatAdapterLuid(const std::array<std::uint8_t, 8> & luid) noexcept
	{
		AdapterLuidString text{};
		std::size_t at = 0;

		// Two hex digits per byte fill the sixteen the string holds, leaving the last for the terminator, and each index is masked to the digit table.
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

	/**
	 * \brief Selects when native resource destruction may occur.
	 */
	enum class DestroyPolicy : std::uint8_t
	{
		/**
		 * \brief Defers native release until the resource is no longer in use.
		 */
		eDeferUntilSafe,

		/**
		 * \brief Releases immediately and requires GPU use to already be impossible.
		 */
		eRequireAlreadyIdle,
	};

	/**
	 * \brief Destruction policy for an RHI object.
	 *
	 * The public handle is invalidated immediately. Native release follows policy and safeAfter.
	 */
	struct DestroyDesc final
	{
		DestroyPolicy policy = DestroyPolicy::eDeferUntilSafe;
		RetirePoint safeAfter{};
	};

	/**
	 * \brief Static information reported by a registered backend.
	 */
	struct BackendInfo final
	{
		GraphicsApiId id{};
		std::string_view canonicalName;
		std::string_view displayName;
		std::uint32_t apiVersionMajor = 0;
		std::uint32_t apiVersionMinor = 0;

		/**
		 * \brief Backend can create presentation surfaces and swapchains.
		 */
		bool supportsSurfaces = false;

		/**
		 * \brief Backend can emit native debug names, labels, or markers that tools can observe.
		 *
		 * Every backend accepts debug-label facade calls. This says whether those labels reach a native tool.
		 */
		bool supportsDebugMarkers = false;

		/**
		 * \brief Backend hands out native objects through the azoth/rhi/native headers.
		 */
		bool supportsExternalNativeAccess = false;
	};

	/**
	 * \brief Backend registration entry.
	 */
	struct BackendCreateInfo final
	{
		BackendInfo info{};
		void * (*createInstance)(const void * instanceDesc, Error * error) noexcept = nullptr;
	};

	struct InstanceApi;
	class BackendBlockSet;

	/**
	 * \brief Non-owning backend instance view used for adapter enumeration.
	 */
	class AZO_RHI_API Instance final
	{
	public:
		Instance() = default;

		[[nodiscard]] GraphicsApiId GetGraphicsApiId() const noexcept;

		// Pass an empty span to query only the count. Outputs reset on failure.
		[[nodiscard]] bool EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out) const noexcept;
		[[nodiscard]] bool EnumerateAdapters(std::span<AdapterInfo> adapters, std::uint32_t & out, Error & error) const noexcept;
		[[nodiscard]] Result<std::uint32_t> EnumerateAdaptersWithResult(std::span<AdapterInfo> adapters) const noexcept;

		/**
		 * \brief Asks what an adapter supports for one object kind and one external handle type.
		 *
		 * Answerable before a device exists, so an adapter that cannot share what a caller needs can be skipped, not created on and found out later.
		 */
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

	/**
	 * \brief Owns a created instance.
	 *
	 * Instance views are valid only while this owner is alive. Explicit release prevents a backend instance from outliving its loader or validation layers during
	 * process teardown.
	 */
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

		/**
		 * \brief Returns a non-owning instance view.
		 */
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

		// Defined out of line because the teardown entry lives in the internal instance dispatch table.
		void Reset() noexcept;

		void * m_impl				   = nullptr;
		const InstanceApi * m_dispatch = nullptr;
	};

	/**
	 * \brief Native validation messages recorded since device creation.
	 *
	 * Counts rise as the validation layer emits messages and cover the whole device lifetime. Check DeviceCaps::reportsValidationMessageCounts before treating
	 * zeros as evidence of a clean run.
	 */
	struct ValidationMessageCounts final
	{
		std::uint64_t errors   = 0;
		std::uint64_t warnings = 0;
	};

	/**
	 * \brief Non-owning dynamic device view.
	 *
	 * Calls go through the stored backend block set and backend object pointer.
	 */
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

		// Handle creation returns an invalid handle on failure.
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

		// Placed resources and aliasing.
		[[nodiscard]] HeapHandle CreateHeap(const HeapDesc & desc) noexcept;
		[[nodiscard]] HeapHandle CreateHeap(const HeapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<HeapHandle> CreateHeapWithResult(const HeapDesc & desc) noexcept;
		[[nodiscard]] BufferHandle CreatePlacedBuffer(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] BufferHandle CreatePlacedBuffer(const PlacedBufferDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<BufferHandle> CreatePlacedBufferWithResult(const PlacedBufferDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreatePlacedTexture(const PlacedTextureDesc & desc) noexcept;
		[[nodiscard]] TextureHandle CreatePlacedTexture(const PlacedTextureDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<TextureHandle> CreatePlacedTextureWithResult(const PlacedTextureDesc & desc) noexcept;

		// Heap footprint of a resource described by a desc, for choosing a placed offset. Outputs reset on failure.
		[[nodiscard]] bool GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool GetTextureMemoryInfo(const TextureDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> GetTextureMemoryInfoWithResult(const TextureDesc & desc) const noexcept;
		[[nodiscard]] bool GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out) const noexcept;
		[[nodiscard]] bool GetBufferMemoryInfo(const BufferDesc & desc, MemoryInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryInfo> GetBufferMemoryInfoWithResult(const BufferDesc & desc) const noexcept;

		// Descriptors and pipeline layouts.
		[[nodiscard]] DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorSetLayoutHandle> CreateDescriptorSetLayoutWithResult(const DescriptorSetLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<PipelineLayoutHandle> CreatePipelineLayoutWithResult(const PipelineLayoutDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena CreateDescriptorArena(const DescriptorArenaDesc & desc) noexcept;
		[[nodiscard]] DescriptorArena CreateDescriptorArena(const DescriptorArenaDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorArena> CreateDescriptorArenaWithResult(const DescriptorArenaDesc & desc) noexcept;

		// Pipelines and caches.
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

		// Non-handle output structs are reset to their default value on failure.
		[[nodiscard]] bool GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out) noexcept;
		[[nodiscard]] bool GetPipelineCacheData(PipelineCacheHandle cache, PipelineCacheData & out, Error & error) noexcept;
		[[nodiscard]] Result<PipelineCacheData> GetPipelineCacheDataWithResult(PipelineCacheHandle cache) noexcept;

		// Ray tracing, queries, synchronization objects, command pools and swapchains.
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

		// Queue access for queues created from DeviceDesc queue requests.
		[[nodiscard]] Queue GetQueue(QueueType type, std::uint32_t index = 0) noexcept;
		[[nodiscard]] Queue GetQueue(QueueType type, std::uint32_t index, Error & error) noexcept;
		[[nodiscard]] Result<Queue> GetQueueWithResult(QueueType type, std::uint32_t index = 0) noexcept;

		// Number of queues that exist for a capability, as reported by DeviceCaps.
		[[nodiscard]] std::uint32_t GetQueueCount(QueueType type) const noexcept;

		// Buffer mapping.
		[[nodiscard]] MappedMemory Map(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] MappedMemory Map(BufferHandle buffer, const MapDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<MappedMemory> MapWithResult(BufferHandle buffer, const MapDesc & desc) noexcept;
		[[nodiscard]] bool Unmap(BufferHandle buffer) noexcept;
		[[nodiscard]] bool Unmap(BufferHandle buffer, Error & error) noexcept;
		[[nodiscard]] bool FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool FlushMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;
		[[nodiscard]] bool InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) noexcept;
		[[nodiscard]] bool InvalidateMappedRange(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, Error & error) noexcept;

		// Descriptor writes, one overload per descriptor payload kind.
		bool UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteBuffer> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteTexture> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteTexture> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteSampler> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteSampler> writes, Error & error) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes) noexcept;
		bool UpdateDescriptors(std::span<const DescriptorWriteAccelerationStructure> writes, Error & error) noexcept;

		// Memory pressure and residency. Outputs reset on failure.
		[[nodiscard]] bool QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out) const noexcept;
		[[nodiscard]] bool QueryMemoryBudget(HeapType heap, MemoryBudgetInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<MemoryBudgetInfo> QueryMemoryBudgetWithResult(HeapType heap) const noexcept;
		bool SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities) noexcept;
		bool SetResidencyPriority(std::span<const ResidencyPriorityDesc> priorities, Error & error) noexcept;

		// Timestamp profiling.
		[[nodiscard]] bool CalibrateTimestamp(QueueType queueType, TimestampCalibration & out) const noexcept;
		[[nodiscard]] bool CalibrateTimestamp(QueueType queueType, TimestampCalibration & out, Error & error) const noexcept;
		[[nodiscard]] Result<TimestampCalibration> CalibrateTimestampWithResult(QueueType queueType) const noexcept;

		// Capability and adapter queries.
		[[nodiscard]] const DeviceCaps & GetCaps() const noexcept;
		[[nodiscard]] FormatSupport GetFormatSupport(Format format) const noexcept;
		[[nodiscard]] const AdapterInfo & GetAdapterInfo() const noexcept;

		/**
		 * \brief What a texture was created with, for code holding the handle and not the description.
		 *
		 * Anything layered over the RHI receives textures as handles, so without this the extent, format and mip count have to travel beside every handle or be
		 * passed again at each call. Output resets on failure.
		 *
		 * \attention TextureInfo::desc::debugName is always null, the name being borrowed for the creation call, not owned here.
		 */
		[[nodiscard]] bool GetTextureInfo(TextureHandle texture, TextureInfo & out) const noexcept;
		[[nodiscard]] bool GetTextureInfo(TextureHandle texture, TextureInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<TextureInfo> GetTextureInfoWithResult(TextureHandle texture) const noexcept;

		/**
		 * \brief What a buffer was created with, plus its allocated size and whether the host can reach its memory.
		 *
		 * The buffer half of the query above, and the same rule applies to BufferInfo::desc::debugName.
		 */
		[[nodiscard]] bool GetBufferInfo(BufferHandle buffer, BufferInfo & out) const noexcept;
		[[nodiscard]] bool GetBufferInfo(BufferHandle buffer, BufferInfo & out, Error & error) const noexcept;
		[[nodiscard]] Result<BufferInfo> GetBufferInfoWithResult(BufferHandle buffer) const noexcept;

		// Native validation messages recorded since device creation. Zero on backends without a readable validation channel.
		[[nodiscard]] ValidationMessageCounts GetValidationMessageCounts() const noexcept;

		// Destruction invalidates the public handle immediately and defers native release according to DestroyDesc.
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

		/**
		 * \name Native resource import and export
		 *
		 * Imported objects still participate in RHI validation and state tracking. \{
		 */

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

		/** \} */

		/**
		 * \name External sharing
		 *
		 * Moves memory and synchronization across an API, device or process boundary, unlike the adoption above.
		 *
		 * \attention Every entry declines with eUnsupportedFeature where the device cannot share. An object can only be exported to a handle type its
		 * description declared at creation. A resource crossing needs a queue family ownership transfer naming kExternalQueueFamily. \{
		 */

		[[nodiscard]] bool ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportBuffer(BufferHandle buffer, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportBufferWithResult(BufferHandle buffer, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportHeap(HeapHandle heap, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportHeapWithResult(HeapHandle heap, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportTexture(TextureHandle texture, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportTextureWithResult(TextureHandle texture, ExternalHandleType type) noexcept;
		/**
		 * \brief Produces a handle another component can wait on or signal.
		 *
		 * \attention This takes on the far side's ordering rules, which can be stricter than this API's. Vulkan permits waiting on a timeline value before the
		 * signal reaching it is submitted. CUDA forbids wait before signal on an imported semaphore. A program legal here can deadlock under a stricter importer.
		 * Nothing in this RHI can see the far side to check. \{
		 */
		[[nodiscard]] bool ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportTimeline(TimelineHandle timeline, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportTimelineWithResult(TimelineHandle timeline, ExternalHandleType type) noexcept;
		[[nodiscard]] bool ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out) noexcept;
		[[nodiscard]] bool ExportBinarySemaphore(BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle & out, Error & error) noexcept;
		[[nodiscard]] Result<ExternalHandle> ExportBinarySemaphoreWithResult(BinarySemaphoreHandle semaphore, ExternalHandleType type) noexcept;
		/** \} */

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

		/**
		 * \brief Releases a handle this device exported.
		 *
		 * Accepts every exported handle and every handle a caller obtained elsewhere and imported here. An import never consumes what it is given, taking a duplicate
		 * of a file descriptor and not the caller's own. What a caller holds stays theirs until this releases it. A global share handle is the exception, owned by
		 * nobody and left alone.
		 */
		bool CloseExportedHandle(const ExternalHandle & handle) noexcept;
		bool CloseExportedHandle(const ExternalHandle & handle, Error & error) noexcept;

		/** \} */

	private:
		friend struct detail::FacadeBuilder;
		friend class UniqueDevice;

		Device(void * impl, BackendBlockSet * blocks) noexcept : m_impl(impl), m_blocks(blocks) {}

		// Shared create path so installed device memory allocators apply to every public create overload.
		BufferHandle CreateBufferRouted(const BufferDesc & desc, Error * error) noexcept;
		TextureHandle CreateTextureRouted(const TextureDesc & desc, Error * error) noexcept;

		// Backend-neutral hooks for native import and export.
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
		BufferHandle out = ImportBuffer(native, desc, error);
		if (!out.IsValid())
		{
			return error;
		}
		return out;
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
		TextureHandle out = ImportTexture(native, desc, error);
		if (!out.IsValid())
		{
			return error;
		}
		return out;
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
		NativeBuffer<Api> out{};
		Error error{};
		if (!ExportBuffer(buffer, out, error))
		{
			return error;
		}
		return out;
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
		NativeTexture<Api> out{};
		Error error{};
		if (!ExportTexture(texture, out, error))
		{
			return error;
		}
		return out;
	}

	// A caller who knows the backend at compile time still uses the dynamic dispatch table today. A static device view would need each backend to provide the full
	// surface and should come with compile-time surface tests.

	/**
	 * \brief Owns a created device.
	 *
	 * Device views are valid only while this owner is alive.
	 */
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

		/**
		 * \brief Returns a non-owning dynamic device view.
		 */
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

		// Defined out of line because the teardown entry lives in a block this header only forward-declares.
		void Reset() noexcept;

		void * m_impl			   = nullptr;
		BackendBlockSet * m_blocks = nullptr;
	};

	/**
	 * \brief Stores backend registrations available during initialization.
	 *
	 * Command recording does not query the registry.
	 */
	class GraphicsApiRegistry final
	{
	public:
		/**
		 * \brief Registers one backend under the compile-time API tag id.
		 *
		 * \attention BackendInfo spans and string_views are copied by value. Their referenced storage must outlive the registry entry.
		 */
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
			m_entries.push_back(entry);
			m_infos.push_back(entry.info);
			return {};
		}

		/**
		 * \brief Returns backend info by id, or null when no backend registered under that id.
		 *
		 * \attention The returned pointer is valid only until the next Register, which may move registry storage.
		 */
		[[nodiscard]] const BackendInfo * Find(GraphicsApiId id) const noexcept
		{
			const auto found = std::ranges::find(m_infos, id, &BackendInfo::id);
			return found != m_infos.end() ? &*found : nullptr;
		}

		/**
		 * \brief Returns backend info by canonical or short name.
		 *
		 * Backends are searched in registration order, which settles collisions when two namespaces share the same short name.
		 */
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

		std::vector<BackendCreateInfo> m_entries;
		std::vector<BackendInfo> m_infos;
	};

	/**
	 * \brief Creates an instance using the first registered preferred API that succeeds.
	 */
	AZO_RHI_API Result<UniqueInstance> CreateInstance(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const InstanceDesc & desc);

	/**
	 * \brief Creates a device by selecting a registered backend at runtime.
	 */
	AZO_RHI_API Result<UniqueDevice> CreateDevice(GraphicsApiRegistry & registry, std::span<const GraphicsApiId> preferredApis, const DeviceDesc & desc);

	/**
	 * \brief Creates a device for a backend fixed at compile time.
	 */
	template <GraphicsApiTag Api>
	// ReSharper disable once CppFunctionIsNotImplemented
	Result<UniqueDevice> CreateDevice(const DeviceDesc & desc);

	// These specializations are declared for every API known to the public headers, not only APIs bundled into this build. Naming an unbundled backend through
	// this path becomes an unresolved symbol. Runtime selection can return eUnsupportedApi instead.
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

} // namespace azo::rhi

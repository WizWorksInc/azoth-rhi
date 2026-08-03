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

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/resource_tables.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/object_pool.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/metal_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include "backends/metal_common/conversions.hpp"
#include "backends/registration.hpp"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace azo::rhi::metal
{
	// How an RHI value becomes a Metal value, which is the same answer here and on the Metal 4 backend. NOLINTNEXTLINE(google-build-using-namespace): the
	// stateless leaf both Metal backends are written against.
	using namespace azo::rhi::metal_common;

	struct MetalDevice;
	struct MetalObject;

	struct MetalCmdList final
	{
		NS::SharedPtr<MTL::CommandBuffer> commandBuffer;
		std::uint8_t lifecycle = 0; // 0 allocated, 1 recording, 2 ended, 3 submitted

		// Transient staging buffers, held until the next Begin drops them. By then the caller has waited for this list's previous submission so the GPU is finished
		// with what they carried.
		detail::HostVector<NS::SharedPtr<MTL::Buffer>> keepAlive;

		// Active encoders and the state draws and dispatches need from the bound pipeline.
		NS::SharedPtr<MTL::RenderCommandEncoder> renderEncoder;
		NS::SharedPtr<MTL::ComputeCommandEncoder> computeEncoder;
		MTL::PrimitiveType boundPrimitive = MTL::PrimitiveTypeTriangle;
		MTL::Buffer * boundIndexBuffer	  = nullptr;
		std::uint64_t boundIndexOffset	  = 0;
		MTL::IndexType boundIndexType	  = MTL::IndexTypeUInt32;
		MTL::Size boundThreadGroup{ 1, 1, 1 };

		// Aliasing hazards. Heaps are created MTLHeapTypePlacement, which Metal does not hazard track so an alias barrier lowers to a fence the encoder after it
		// waits on. One fence per list is enough: each barrier updates it and the next encoder to open consumes the wait.
		NS::SharedPtr<MTL::Fence> aliasFence;
		bool aliasWaitPending = false;

		/*
		 * A rendering scope's end timestamp on an adapter that samples by command and not from the pass descriptor, left by the begin for the end to record while the
		 * encoder is still open. Null the rest of the time, which is every scope asking for no end timestamp and every adapter that samples at a stage boundary.
		 */
		NS::SharedPtr<MTL::CounterSampleBuffer> pendingEndTimestamp;
		std::uint32_t pendingEndQuery = 0;

		/*
		 * Carried by the encoder a standalone timestamp opens and there for no other reason. Metal drops an encoder that records nothing and takes its stage-boundary
		 * counter sample with it, measured on an Apple part as the second such sample in a command buffer resolving to zero while the first survived. The encoder
		 * needs one command in it and a fence update is the cheapest that touches no memory.
		 */
		NS::SharedPtr<MTL::Fence> timestampFence;

		/*
		 * What each open debug label was pushed on so the matching pop reaches the same object: the encoder that was live at the time or null for a label opened
		 * between scopes and pushed on the command buffer. The pointer and not a flag because an encoder that has since closed took its group with it and the encoder
		 * open now is a different one that never saw this label.
		 */
		detail::HostVector<MTL::CommandEncoder *> debugLabelScopes;

		// Name the pool was allocated with, reapplied to each Begin's command buffer.
		detail::HostString debugName;
	};

	/*
	 * A query pool wraps an MTLCounterSampleBuffer over the timestamp counter set. queryCount is kept because Metal cannot be asked a sample buffer's capacity
	 * back in a form a resolve can range check against, and a resolve past the end is undefined, not refused.
	 */
	struct MetalQueryPool final
	{
		NS::SharedPtr<MTL::CounterSampleBuffer> sampleBuffer;

		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	/*
	 * A timeline, which is an MTLSharedEvent whether or not it will be shared. The declaration is recorded, not acted on, this backend having lowered timelines
	 * onto MTLSharedEvent from the start. It is still enforced at export: a caller that did not ask for an exportable timeline should not get a handle to one by
	 * accident.
	 */
	struct MetalTimeline final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	// Binary semaphore modeled as a shared event plus a monotonic counter so each signal advances the event to a value a later wait can target.
	struct MetalBinarySemaphore final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		std::uint64_t value = 0;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	// A created graphics pipeline. Metal sets cull, winding, fill, depth bias and the depth-stencil state on the encoder and not the pipeline so they are kept
	// here for draw time.
	struct MetalGraphicsPipeline final
	{
		NS::SharedPtr<MTL::RenderPipelineState> state;
		NS::SharedPtr<MTL::DepthStencilState> depthStencil;
		MTL::PrimitiveType primitive = MTL::PrimitiveTypeTriangle;
		MTL::CullMode cull			 = MTL::CullModeBack;
		MTL::Winding winding		 = MTL::WindingCounterClockwise;
		MTL::TriangleFillMode fill	 = MTL::TriangleFillModeFill;
		bool depthBiasEnable		 = false;
		float depthBiasConstant		 = 0.0f;
		float depthBiasSlope		 = 0.0f;
		float depthBiasClamp		 = 0.0f;
	};

	struct MetalComputePipeline final
	{
		NS::SharedPtr<MTL::ComputePipelineState> state;
		MTL::Size threadsPerThreadgroup{ 1, 1, 1 };
	};

	// One descriptor written into a set, resolved to its native object. Bound to a Metal argument-table index equal to its binding number, matching how Slang
	// assigns (set 0, binding N) to index N.
	struct MetalDescriptor final
	{
		DescriptorType type			= DescriptorType::eUniformBuffer;
		MTL::Buffer * buffer		= nullptr;
		std::uint64_t offset		= 0;
		MTL::Texture * texture		= nullptr;
		MTL::SamplerState * sampler = nullptr;
	};

	struct MetalDescriptorSet final
	{
		// Keyed by binding and array index together, an array binding holding one descriptor per element. DescriptorKey packs the two.
		detail::HostMap<std::uint64_t, MetalDescriptor> bindings;

		// The arena this set came from and the epoch it was stamped with. A reset bumps the arena's epoch so a set that outlived one no longer matches and binding it
		// is refused.
		const MetalObject * arena = nullptr;
		std::uint64_t epoch		  = 0;

		// What this set was allocated against, kept because an argument buffer member's position comes from the layout and not from the binding number, and the
		// writes that fill it arrive one at a time with only a binding number on them.
		DescriptorSetLayoutHandle layout{};

		/*
		 * The set as a Metal argument buffer, on a device that has them.
		 *
		 * Eight bytes per member in the layout's declaration order, a resource id for a texture or a sampler and a GPU address for a buffer, which is the struct
		 * slangc lowers a ParameterBlock to. Null below the tier that supports them, where the discrete argument tables are used instead.
		 */
		NS::SharedPtr<MTL::Buffer> argumentBuffer;
	};

	/**
	 * \brief The lists one command pool has built, and how many of them are currently out.
	 *
	 * A command buffer here is transient and remade at every Begin, so it is not what the pool holds on to. The record around it is: a list costs a
	 * MetalCmdList and a backend object, and a caller allocating per frame adds both to the device every frame.
	 *
	 * So the pool keeps the records, and a reset only rewinds the count.
	 */
	struct MetalCmdPool final
	{
		detail::HostVector<MetalObject *> lists;
		std::size_t handedOut = 0;
	};

	// First word is the dispatch table pointer the facades read. The rest is per-object state shared by queues, command lists, command pools and descriptor
	// arenas.
	struct MetalObject final
	{
		const BackendObject * object = nullptr;
		MetalDevice * owner			 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		// The recording state behind a command list object, null on every other kind of object.
		MetalCmdList * list = nullptr;

		// What a command pool object hands out and takes back, null on every other kind of object.
		MetalCmdPool * pool = nullptr;

		// Reset counter of a descriptor arena object, stamped onto every set it hands out. Bumped by an arena reset, read by a bind. Atomic because the bind is a
		// recording entry and recording takes no lock.
		std::atomic<std::uint64_t> arenaEpoch{ 0 };
	};

	struct MetalInstance final
	{
		const BackendObject * object = nullptr;
	};

	/*
	 * A texture and the portability format it was created with, which buffer copy row and image stride math needs.
	 *
	 * Kept beside the texture and not in a second table keyed the same way so one resolve answers both and the two cannot fall out of step.
	 */
	struct MetalTextureSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;
		Format format = Format::eUndefined;

		// What a view over this texture is judged against when it narrows nothing of its own.
		Flags<TextureUsage> usage;

		// Created with PixelFormatView usage, so a view may name a format other than this one.
		bool mutableFormat = false;

		// True when the texture came from newSharedTexture, not newTexture, which is what MTLTexture needs to hand back a handle at all. Recorded , not
		// derived, an ordinary texture having no way to say afterwards that it wishes it were shared.
		bool shared = false;

		// Who owns the MTLTexture, in the same vocabulary the other backends use. Only the swapchain's back buffer is borrowed here, and marking it is what lets
		// getTextureInfo tell a texture that came from a description apart from one that never did.
		SlotLifetime lifetime = SlotLifetime::eOwned;

		// What the texture was created with, answered by getTextureInfo. debugName is null: the name is borrowed for the creation call.
		TextureDesc desc{};
	};

	// A buffer, in the one table that names it.
	struct MetalBufferSlot final
	{
		NS::SharedPtr<MTL::Buffer> buffer;

		// What the buffer was created with, answered by getBufferInfo. debugName is null: the name is borrowed for the creation call.
		BufferDesc desc{};
	};

	// Empty tag for the Metal device's handle registry, which tracks only generation and liveness for stale-handle validation (the native objects live in the side
	// tables keyed by handle index).
	struct MetalSlotTag final
	{
	};

	// What a descriptor set layout was declared with. immutableSamplers is cleared on the way in, its span being borrowed for the duration of creation.
	struct MetalDescriptorSetLayout final
	{
		detail::HostVector<DescriptorBinding> bindings;
	};

	// The set layouts a pipeline layout was built from, so a pipeline can rebuild the ShaderAbiLayout its shaders are checked against.
	struct MetalPipelineLayout final
	{
		detail::HostVector<DescriptorSetLayoutHandle> sets;

		// Whether anything will ever be bound at kMetalPushConstantIndex, which is what makes a shader using that buffer for something else detectable.
		bool hasPushConstants = false;
	};

	/*
	 * A presentation swapchain over a CAMetalLayer. The layer is created and owned by the host, which hands it over through SurfaceSource so this only borrows it.
	 * Each acquire pulls the next drawable and re-points one back-buffer texture handle at the drawable's texture. Present schedules that drawable once the render
	 * semaphore fires.
	 */
	struct MetalSwapchain final
	{
		const BackendObject * object = nullptr;
		MetalDevice * owner			 = nullptr;
		CA::MetalLayer * layer		 = nullptr;
		Format format				 = Format::eBGRA8Srgb;
		PresentMode presentMode		 = PresentMode::eFifo; // effective mode, only ever immediate or fifo
		std::uint32_t width			 = 1;
		std::uint32_t height		 = 1;
		std::uint32_t imageCount	 = 0;
		std::uint32_t frameCursor	 = 0; // rotates the reported image index across the present semaphores

		NS::SharedPtr<CA::MetalDrawable> currentDrawable; // held between acquire and present
		TextureHandle backBuffer{};						  // single handle re-pointed at the current drawable
		TextureViewHandle backBufferView{};
		BinarySemaphoreHandle imageAvailable{};						 // never signaled so the submit wait is a no-op
		detail::HostVector<BinarySemaphoreHandle> presentSemaphores; // one render-finished semaphore per image
	};

	struct MetalDevice final
	{
		const BackendObject * object = nullptr;
		NS::SharedPtr<MTL::Device> device;

		/*
		 * Metal has no queue families. Each QueueType gets its own independent command queues, honoring the DeviceDesc queue requests so compute and copy work can
		 * run concurrently with graphics. A command list records into a command buffer made from its pool type's first queue so different types run on different
		 * command queues while same-type queue indices share execution ordering.
		 */
		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> graphicsQueues;
		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> computeQueues;
		detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> copyQueues;

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & QueuesForType(QueueType type) const noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		// The command queue a type's command buffers commit to. Index 0 of the type or null when the device has no queue of that type.
		[[nodiscard]] MTL::CommandQueue * CommandQueueFor(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL::CommandQueue>> & pool = QueuesForType(type);
			return pool.empty() ? nullptr : pool.front().get();
		}

		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		// DeviceDesc.enableDebugLabels: bracket command spans as Metal debug groups for Xcode and Instruments.
		bool debugLabels = true;

		// The instance this device was made from, borrowed or null in the static CreateDevice form that makes no instance. Used only so teardown can retire the
		// instance alongside its last device.
		MetalInstance * instanceWrapper = nullptr;

		// Tags this device's handles so another device rejects them. Returned to the pool at teardown.
		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		// Backing storage AdapterInfo::name points at.
		detail::HostString adapterName;

		// Backing storage for the OS-derived driver strings (Metal has no driver version of its own).
		detail::HostString driverVersion;
		detail::HostString driverInfo;

		detail::TypedObjectPool<MetalObject> objects{ 64, 0, "rhi.metal.objects" };

		/*
		 * One table per kind, the way Vulkan and Direct3D 12 keep theirs. Slot maps, not hash maps because recording resolves handles and is guarded in no threading
		 * mode. A rehash moves the buckets a concurrent lookup is walking, where a slot map never moves its storage.
		 *
		 * Nothing guards them here. Writers are serialized by the guard the RHI takes for that kind.
		 */
		SlotMap<BufferTag, MetalBufferSlot> buffers;
		SlotMap<TextureTag, MetalTextureSlot> textures;
		SlotMap<TextureViewTag, NS::SharedPtr<MTL::Texture>> textureViews;
		SlotMap<SamplerTag, NS::SharedPtr<MTL::SamplerState>> samplers;
		SlotMap<HeapTag, NS::SharedPtr<MTL::Heap>> heaps;
		SlotMap<TimelineTag, MetalTimeline> timelines;
		SlotMap<BinarySemaphoreTag, MetalBinarySemaphore> binarySemaphores;
		SlotMap<GraphicsPipelineTag, MetalGraphicsPipeline> graphicsPipelines;
		SlotMap<ComputePipelineTag, MetalComputePipeline> computePipelines;
		SlotMap<DescriptorSetTag, MetalDescriptorSet> descriptorSets;
		SlotMap<QueryPoolTag, MetalQueryPool> queryPools;

		// The timestamp counter set this adapter exposes, or null where it exposes none. Resolved once because MTLDevice answers it as an array to walk.
		NS::SharedPtr<MTL::CounterSet> timestampCounterSet;

		/*
		 * Where this adapter will sample a counter, which decides how a timestamp write is recorded, not whether one can be.
		 *
		 * Apple parts answer stage boundary alone, where the sample points are fixed on the pass descriptor before the encoder opens. The discrete parts answer the
		 * encoder boundaries instead, where a sample is taken by a command mid-encoder. Both paths are built and a write reads these to pick one, since neither is
		 * available everywhere.
		 */
		bool samplesAtStageBoundary	   = false;
		bool samplesAtDrawBoundary	   = false;
		bool samplesAtDispatchBoundary = false;
		bool samplesAtBlitBoundary	   = false;

		/*
		 * Metal binds by argument-table index and builds no layout object, so these hold no native object either. They hold what the shader binding ABI is resolved
		 * against, which is the one thing a pipeline needs back from a layout it was built with.
		 */
		SlotMap<DescriptorSetLayoutTag, MetalDescriptorSetLayout> descriptorSetLayouts;
		SlotMap<PipelineLayoutTag, MetalPipelineLayout> pipelineLayouts;

		// The kinds this backend tracks for liveness alone, with no native object of their own.
		detail::ResourceTables<MetalSlotTag, std::monostate> tracked;

		detail::HostVector<HostUniquePtr<MetalCmdList>> cmdLists;
		detail::HostVector<HostUniquePtr<MetalCmdPool>> cmdPools;

		/**
		 * \brief Which residency set an allocation belongs in, one per resource kind that fills one.
		 *
		 * Four sets and not one, because the RHI takes a lock per ResourceType and a single set would be written by four of them at once, which Metal refuses.
		 * One set per kind is written only under that kind's guard, so this backend needs no lock of its own and the promise that a backend author writes none
		 * holds.
		 */
		enum class Residency : std::uint8_t
		{
			eBuffers,
			eTextures,
			eHeaps,
			eDescriptorSets,
			eCount,
		};

		/**
		 * \brief Everything this device made, kept resident for every queue it has.
		 *
		 * A bind calls useResource for each descriptor a set names, while the list is recording. A set written after that, which is what updateAfterBind is
		 * for, names resources no useResource ever mentioned.
		 *
		 * A residency set closes that: it is held by the queue, not by an encoder. Made at device creation, and left null where the operating system has none.
		 */
		std::array<NS::SharedPtr<MTL::ResidencySet>, static_cast<std::size_t>(Residency::eCount)> residencySets;

		/**
		 * \brief Adds one allocation to its kind's residency set.
		 *
		 * \param kind Which set, which is the resource kind whose RHI guard the caller is already holding.
		 */
		void NoteAllocation(Residency kind, const MTL::Allocation * allocation) noexcept;

		detail::HostVector<HostUniquePtr<MetalSwapchain>> swapchains;

		~MetalDevice()
		{
			objects.Reset();
		}

		// Held through a HostUniquePtr in the backend's device list and pointed at by every facade built off it so it is never copied or moved. The default
		// constructor is restored because declaring the four below is what would otherwise take it away and HostNew needs it.
		MetalDevice()								 = default;
		MetalDevice(const MetalDevice &)			 = delete;
		MetalDevice & operator=(const MetalDevice &) = delete;
		MetalDevice(MetalDevice &&)					 = delete;
		MetalDevice & operator=(MetalDevice &&)		 = delete;
	};

	/*
	 * Process lifetime owner for the instances and the devices.
	 *
	 * Unguarded. The RHI serializes createInstance, createDevice, destroyDevice and destroyInstance, which are the only entries that reach either list so a
	 * backend writes no synchronization of its own.
	 */
	struct MetalBackendOwner final
	{
		detail::HostVector<HostUniquePtr<MetalInstance>> instances;
		detail::HostVector<HostUniquePtr<MetalDevice>> devices;
	};

	[[nodiscard]] void * AllocObject(MetalDevice * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool MetalRefuseUnexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] MetalBackendOwner & Owner();
	GraphicsApiId MetalDeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view MetalDeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & MetalDeviceCaps(void * impl) noexcept;
	const AdapterInfo & MetalDeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts MetalDeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport MetalDeviceFormatSupport(void * impl, Format format) noexcept;
	bool MetalGetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool MetalGetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle MetalCreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalCreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle MetalCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle MetalCreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool MetalGetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool MetalGetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * ResolveHeap(MetalDevice * device, HeapHandle handle) noexcept;
	HeapHandle MetalCreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle MetalCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle MetalCreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * ResolveBuffer(MetalDevice * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTexture(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format ResolveTextureFormat(MetalDevice * device, TextureHandle handle) noexcept;
	[[nodiscard]] MTL::CommandBuffer * CmdBufferOf(MetalObject * object) noexcept;
	void EndActiveEncoders(MetalObject * object) noexcept;
	[[nodiscard]] MTL::BlitCommandEncoder * BeginBlit(MetalObject * object) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::RenderCommandEncoder * encoder) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::ComputeCommandEncoder * encoder) noexcept;
	void ConsumeAliasWait(MetalCmdList * rec, MTL::BlitCommandEncoder * encoder) noexcept;
	[[nodiscard]] MetalCmdList * NewCmdList(MetalDevice * device, QueueType queueType);
	BinarySemaphoreHandle MetalCreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;
	bool MetalCmdBegin(void * impl, Error * error) noexcept;
	bool MetalCmdEnd(void * impl, Error * error) noexcept;
	bool MetalCmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool MetalCmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool MetalCmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool MetalCmdEndDebugLabel(void * impl, Error * error) noexcept;
	bool MetalClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool MetalResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool MetalCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool MetalCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool MetalCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool MetalCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool MetalBlit(void * impl, [[maybe_unused]] TextureHandle dst, [[maybe_unused]] TextureHandle src, [[maybe_unused]] std::span<const TextureBlit> regions,
		[[maybe_unused]] Filter filter, Error * error) noexcept;
	bool MetalGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool MetalClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTextureView(MetalDevice * device, TextureViewHandle handle) noexcept;
	PipelineLayoutHandle MetalCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle MetalCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle MetalCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	bool MetalBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool MetalEndRendering(void * impl, Error * error) noexcept;
	bool MetalSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool MetalSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool MetalSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool MetalSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool MetalSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool MetalSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool MetalSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool MetalSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool MetalPushConstants(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size,
		const void * data, Error * error) noexcept;
	bool MetalDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool MetalDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool MetalDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool MetalDrawIndexedIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	void EnsureComputeEncoder(MetalObject * object);
	bool MetalSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool MetalBindDescriptorSet(void * impl, [[maybe_unused]] PipelineLayoutHandle layout, [[maybe_unused]] std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool MetalUpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool MetalUpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool MetalUpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool MetalDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool MetalDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	void * MetalCreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * MetalCreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * MetalGetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory MetalMap(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool MetalQueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] MetalQueryPool * ResolveQueryPool(MetalDevice * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle MetalCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool MetalCalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool MetalCmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool MetalCmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error * error) noexcept;
	bool MetalCmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool MetalCmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool MetalCmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	bool MetalDestroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool MetalCollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool MetalCollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle MetalAdoptBuffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle MetalAdoptTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool MetalGetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept;
	AccelerationStructureHandle MetalCreateAccelerationStructure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept;
	RayTracingPipelineHandle MetalCreateRayTracingPipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept;
	bool MetalBeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle MetalArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool MetalArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const RayTracingApi & RayTracingBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	TextureViewHandle MetalAdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle MetalAdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool MetalGetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle MetalAdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle MetalAdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool MetalGetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool MetalGetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const AliasingCommandApi & AliasingCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;
	AcquireResult MetalSwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult MetalSwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	TextureHandle MetalSwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle MetalSwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle MetalSwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format MetalSwapchainFormat(void * impl) noexcept;
	bool MetalSwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t MetalSwapchainImageCount(void * impl) noexcept;
	std::uint32_t MetalSwapchainWidth(void * impl) noexcept;
	std::uint32_t MetalSwapchainHeight(void * impl) noexcept;
	bool MetalSwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool MetalSwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	void * MetalCreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType MetalQueueTypeOf(void * impl) noexcept;
	std::uint32_t MetalQueueFamilyIndex(void * impl) noexcept;
	bool MetalQueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool MetalQueueWaitIdle(void * impl, Error * error) noexcept;
	bool MetalQueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool MetalQueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool MetalQueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool MetalQueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool MetalQueueEndDebugLabel(void * impl, Error * error) noexcept;
	void * MetalCommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool MetalCommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId MetalInstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool MetalEnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool MetalQueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool MetalExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool MetalExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle MetalImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle MetalImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle MetalImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle MetalImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle MetalImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool MetalCloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;
	void PopulateCaps(MetalDevice * device);
	/**
	 * \brief Builds a device on the generation DeviceDesc::apiVersion asked for.
	 *
	 * \param refusedReason Set to why a pinned generation could not be provided, and left alone for every other failure, so a caller can tell a refused pin
	 * from having no adapter at all.
	 */
	[[nodiscard]] MetalDevice * MakeOwnedDevice(MetalInstance * instance, const DeviceDesc & desc, const char *& refusedReason);
	[[nodiscard]] MetalInstance * MakeOwnedInstance();
	void MetalDestroyDevice(void * impl) noexcept;
	void MetalDestroyInstance(void * impl) noexcept;
	void * MetalInstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * MetalCreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	/*
	 * A handle for a kind this backend tracks for liveness alone, with no native object behind it.
	 *
	 * Every kind is minted from a table, here or from the one holding its native object so one lookup answers both what the handle names and whether it is still
	 * live. A handle fabricated from a bare counter instead would resolve nowhere and the retire that Destroy runs would fail against a table that had never heard
	 * of it.
	 */
	template <typename HandleT>
	[[nodiscard]] HandleT MintHandle(MetalDevice * device)
	{
		return device->tracked.Store<HandleT>(std::monostate{});
	}

	/*
	 * Whether a handle names something this device issued and still holds.
	 *
	 * One overload per kind and not one lookup in a shared table because the table that answers is the table that holds the object. Reads take nothing: a slot
	 * map's storage never moves so these are safe from a recording thread while another is creating.
	 */
	[[nodiscard]] inline bool Resolves(MetalDevice * device, BufferHandle handle) noexcept
	{
		return device->buffers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TextureHandle handle) noexcept
	{
		return device->textures.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, SamplerHandle handle) noexcept
	{
		return device->samplers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, HeapHandle handle) noexcept
	{
		return device->heaps.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, TimelineHandle handle) noexcept
	{
		return device->timelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(MetalDevice * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	// The kinds with nothing native behind them: query pools, pipeline caches and the ray tracing objects this backend does not build.
	template <typename HandleT>
	[[nodiscard]] bool Resolves(MetalDevice * device, HandleT handle) noexcept
	{
		return device->tracked.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool MetalUnimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	// The creation-entry form of MetalUnimplemented, for a kind this backend has nothing to put behind a handle. An invalid handle is what a failed create
	// returns, so a caller that ignores the error still cannot pass the result anywhere.
	template <typename HandleT, typename... Args>
	HandleT MetalUnimplementedHandle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<HandleT>(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 3 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT MetalCreateHandle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.create");

		Error * error		 = LastError(args...);
		const HandleT handle = MintHandle<HandleT>(static_cast<MetalDevice *>(impl));
		if (!handle.IsValid())
		{
			return FailValue<HandleT>(error, ErrorCode::eOutOfHostMemory, "Metal backend handle allocation failed");
		}

		return ReturnValue(handle, error);
	}

	/*
	 * Metal binds by argument-table index, not through a layout object, so a layout is a handle and nothing else. The one field it cannot quietly ignore is an
	 * immutable sampler: dropping one leaves the binding with no sampler at all, which is a blank read and not an error.
	 */
	inline DescriptorSetLayoutHandle MetalCreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return FailValue<DescriptorSetLayoutHandle>(
					error, ErrorCode::eUnsupportedFeature, "Metal does not bake samplers into a descriptor set layout, so write the sampler into the set");
			}
		}

		// Refused above, so nothing here carries the borrowed sampler span that would otherwise dangle in a slot outliving this call.
		MetalDescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<MetalDevice *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal

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

namespace azo::rhi::metal4
{
	// How an RHI value becomes a Metal value, which is the same answer here and on the Metal 4 backend. NOLINTNEXTLINE(google-build-using-namespace): the
	// stateless leaf both Metal backends are written against.
	using namespace azo::rhi::metal_common;

	struct Metal4Device;
	struct Metal4Object;

	/**
	 * \brief What one Metal 4 command list is recording into.
	 *
	 * Three objects where Metal 3 had one. A command buffer is transient and takes its memory from an allocator, which is reset, not freed, and the bindings a
	 * shader sees come from an argument table, not from calls on the encoder.
	 */
	struct CmdList final
	{
		NS::SharedPtr<MTL4::CommandBuffer> commandBuffer;

		/**
		 * \brief Backing memory for the command buffer, reset at Begin, not reallocated.
		 *
		 * One per list and not one per device, because resetting an allocator invalidates every command buffer built from it and a list has no way to know what
		 * its siblings are still holding.
		 */
		NS::SharedPtr<MTL4::CommandAllocator> allocator;

		/**
		 * \brief Where bindings go, replacing the per-encoder setBuffer and setTexture calls Metal 3 makes.
		 *
		 * Made once when the list is allocated and reused, since its contents are overwritten per bind and its size comes from the device's argument table
		 * limits and not from anything a caller does.
		 */
		NS::SharedPtr<MTL4::ArgumentTable> argumentTable;

		std::uint8_t lifecycle = 0; // 0 allocated, 1 recording, 2 ended, 3 submitted

		NS::SharedPtr<MTL4::RenderCommandEncoder> renderEncoder;

		/**
		 * \brief The compute encoder, which on this generation also carries every copy, fill and mip generate.
		 *
		 * Metal 4 has no blit encoder. Copies moved onto the compute encoder, which means a copy between two dispatches no longer closes the scope they are in,
		 * and is why a timestamp can sit between them here and cannot on Metal 3.
		 */
		NS::SharedPtr<MTL4::ComputeCommandEncoder> computeEncoder;

		MTL::PrimitiveType boundPrimitive = MTL::PrimitiveTypeTriangle;
		MTL::Size boundThreadGroup{ 1, 1, 1 };

		/**
		 * \brief The index buffer as a draw takes it here, an address and a length, not a buffer object.
		 */
		MTL::GPUAddress boundIndexBuffer = 0;
		std::uint64_t boundIndexLength	 = 0;
		MTL::IndexType boundIndexType	 = MTL::IndexTypeUInt32;

		/**
		 * \brief Push constant storage, because Metal 4 has no inline setBytes.
		 *
		 * Metal 3 hands a push constant range straight to the encoder. An argument table binds addresses only, so the bytes need to live in a buffer that
		 * outlives the submission. Written at a bump offset and grown by adding another block, so a list pushing constants per draw does not allocate per draw.
		 */
		detail::HostVector<NS::SharedPtr<MTL::Buffer>> pushConstantBlocks;
		std::uint64_t pushConstantOffset = 0;

		// Transient staging buffers, held until the next Begin drops them, as on the other generation.
		detail::HostVector<NS::SharedPtr<MTL::Buffer>> keepAlive;

		/**
		 * \brief The scope's end timestamp, left by BeginRendering for EndRendering to write.
		 *
		 * The end has to be recorded while the encoder is still open, and EndRendering is the last point that is true. Null whenever the scope asked for no end
		 * timestamp.
		 */
		NS::SharedPtr<MTL4::CounterHeap> pendingEndHeap;
		std::uint32_t pendingEndQuery = 0;

		/**
		 * \brief Whether the open rendering scope has drawn anything.
		 *
		 * Which stage the scope's end timestamp is taken after. A timestamp names one stage here, not a mask, and a fragment-stage sample in a scope with no
		 * draw in it never lands: the stage is never reached, and the slot reads back as the sentinel the reset wrote. Vertex is the boundary such a scope does
		 * reach.
		 */
		bool scopeDrew = false;

		/**
		 * \brief Orders a counter resolve against the encoder writes it reads.
		 *
		 * A resolve runs on the blit stage and is not ordered against an encoder's timestamp writes by sitting after them in command order, which is what the
		 * wait and update fence pair on resolveCounterHeap is for. Made on first use, since a list that never resolves a query never needs one.
		 */
		NS::SharedPtr<MTL::Fence> timestampFence;

		/**
		 * \brief Whether any encoder in this recording wrote a timestamp.
		 *
		 * The fence has to be updated where each encoder closes and not only where the resolve is recorded, because by then the encoder that took the
		 * sample is long gone. This says whether there is anything to order, so a list that never times anything pays for no fences.
		 */
		bool wroteEncoderTimestamps = false;

		/**
		 * \brief A barrier recorded while no encoder was open, held for the next one to carry.
		 *
		 * A caller records a barrier between the work that produces and the scope that consumes, so at that moment there is often no encoder at all. Opening
		 * one purely to host it spends an encoder on a command that orders nothing.
		 *
		 * Held instead, and recorded on the next encoder that opens. Stages accumulate, so several barriers before one scope collapse into one.
		 */
		MTL::Stages pendingProducer = static_cast<MTL::Stages>(0);
		MTL::Stages pendingConsumer = static_cast<MTL::Stages>(0);

		/**
		 * \brief The list's own residency set, for the transients it makes while recording.
		 *
		 * Push constant blocks and staging buffers are made mid-recording and read by the GPU, so they have to be resident. They cannot go in the device's
		 * sets: those are written under the RHI guard for a resource kind, and recording takes no lock. A set per list is written only by the thread recording
		 * it.
		 *
		 * Made under the object guard, and added to every queue there.
		 */
		NS::SharedPtr<MTL::ResidencySet> residency;

		// What each open debug label was pushed on, so the matching pop reaches the same object.
		detail::HostVector<MTL4::CommandEncoder *> debugLabelScopes;

		// Name the pool was allocated with, reapplied to each Begin's command buffer.
		detail::HostString debugName;
	};

	/**
	 * \brief The lists one command pool has built, and how many of them are currently out.
	 *
	 * Metal 3 makes a command buffer per Allocate and lets it go, the object being transient by design. The four objects behind a list here are not: building a
	 * set of them per frame and never letting go reaches the abort inside Metal's own resource table in a few minutes.
	 *
	 * So the pool keeps them, and a reset only rewinds the count.
	 */
	struct CmdPool final
	{
		detail::HostVector<Metal4Object *> lists;
		std::size_t handedOut = 0;
	};

	/*
	 * A query pool wraps an MTL4CounterHeap over the timestamp counters. queryCount is kept because a heap cannot be asked its capacity back in a form a resolve
	 * can range check against, and a resolve past the end is undefined, not refused.
	 */
	struct Metal4QueryPool final
	{
		NS::SharedPtr<MTL4::CounterHeap> heap;

		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	/*
	 * A timeline, which is an MTLSharedEvent whether or not it will be shared. The declaration is recorded, not acted on, this backend having lowered timelines
	 * onto MTLSharedEvent from the start. It is still enforced at export: a caller that did not ask for an exportable timeline should not get a handle to one by
	 * accident.
	 */
	struct Metal4Timeline final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	// Binary semaphore modeled as a shared event plus a monotonic counter so each signal advances the event to a value a later wait can target.
	struct Metal4BinarySemaphore final
	{
		NS::SharedPtr<MTL::SharedEvent> event;
		std::uint64_t value = 0;
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	// A created graphics pipeline. Metal sets cull, winding, fill, depth bias and the depth-stencil state on the encoder and not the pipeline so they are kept
	// here for draw time.
	struct Metal4GraphicsPipeline final
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

	struct Metal4ComputePipeline final
	{
		NS::SharedPtr<MTL::ComputePipelineState> state;
		MTL::Size threadsPerThreadgroup{ 1, 1, 1 };
	};

	// One descriptor written into a set, resolved to its native object. Bound to a Metal argument-table index equal to its binding number, matching how Slang
	// assigns (set 0, binding N) to index N.
	struct Metal4Descriptor final
	{
		DescriptorType type			= DescriptorType::eUniformBuffer;
		MTL::Buffer * buffer		= nullptr;
		std::uint64_t offset		= 0;
		MTL::Texture * texture		= nullptr;
		MTL::SamplerState * sampler = nullptr;
	};

	struct Metal4DescriptorSet final
	{
		// Keyed by binding and array index together, an array binding holding one descriptor per element. DescriptorKey packs the two.
		detail::HostMap<std::uint64_t, Metal4Descriptor> bindings;

		// The arena this set came from and the epoch it was stamped with. A reset bumps the arena's epoch so a set that outlived one no longer matches and binding it
		// is refused.
		const Metal4Object * arena = nullptr;
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

	// First word is the dispatch table pointer the facades read. The rest is per-object state shared by queues, command lists, command pools and descriptor
	// arenas.
	struct Metal4Object final
	{
		const BackendObject * object = nullptr;
		Metal4Device * owner			 = nullptr;
		QueueType queueType			 = QueueType::eGraphics;

		// The recording state behind a command list object, null on every other kind of object.
		CmdList * list = nullptr;

		// What a command pool object hands out and takes back, null on every other kind of object.
		CmdPool * pool = nullptr;

		// Reset counter of a descriptor arena object, stamped onto every set it hands out. Bumped by an arena reset, read by a bind. Atomic because the bind is a
		// recording entry and recording takes no lock.
		std::atomic<std::uint64_t> arenaEpoch{ 0 };
	};

	struct Metal4Instance final
	{
		const BackendObject * object = nullptr;
	};

	/*
	 * A texture and the portability format it was created with, which buffer copy row and image stride math needs.
	 *
	 * Kept beside the texture and not in a second table keyed the same way so one resolve answers both and the two cannot fall out of step.
	 */
	struct Metal4TextureSlot final
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

	/*
	 * A view over a texture, which on Metal is another MTLTexture.
	 *
	 * It carries the same ownership word the texture slot does, so a destroy can tell the swapchain's back buffer view from one the caller made. Without it the
	 * view the swapchain hands out every frame could be destroyed by the caller, and acquire would go on writing the drawable into whatever took the slot.
	 */
	struct Metal4TextureViewSlot final
	{
		NS::SharedPtr<MTL::Texture> texture;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	// A buffer, in the one table that names it.
	struct Metal4BufferSlot final
	{
		NS::SharedPtr<MTL::Buffer> buffer;

		// What the buffer was created with, answered by getBufferInfo. debugName is null: the name is borrowed for the creation call.
		BufferDesc desc{};
	};

	// Empty tag for the Metal device's handle registry, which tracks only generation and liveness for stale-handle validation (the native objects live in the side
	// tables keyed by handle index).
	struct Metal4SlotTag final
	{
	};

	// What a descriptor set layout was declared with. immutableSamplers is cleared on the way in, its span being borrowed for the duration of creation.
	struct Metal4DescriptorSetLayout final
	{
		detail::HostVector<DescriptorBinding> bindings;
	};

	// The set layouts a pipeline layout was built from, so a pipeline can rebuild the ShaderAbiLayout its shaders are checked against.
	struct Metal4PipelineLayout final
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
	struct Metal4Swapchain final
	{
		const BackendObject * object = nullptr;
		Metal4Device * owner			 = nullptr;
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

	struct Metal4Device final
	{
		const BackendObject * object = nullptr;
		NS::SharedPtr<MTL::Device> device;

		/*
		 * Metal has no queue families. Each QueueType gets its own independent command queues, honoring the DeviceDesc queue requests so compute and copy work can
		 * run concurrently with graphics. A command list records into a command buffer committed to its pool type's first queue so different types run on different
		 * command queues while same-type queue indices share execution ordering.
		 */
		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> graphicsQueues;
		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> computeQueues;
		detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> copyQueues;

		[[nodiscard]] const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & QueuesForType(QueueType type) const noexcept
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
		[[nodiscard]] MTL4::CommandQueue * CommandQueueFor(QueueType type) const noexcept
		{
			const detail::HostVector<NS::SharedPtr<MTL4::CommandQueue>> & pool = QueuesForType(type);
			return pool.empty() ? nullptr : pool.front().get();
		}

		std::atomic<std::uint32_t> nextHandleIndex{ 0 };
		std::atomic<std::uint64_t> pendingRetire{ 0 };
		ValidationMode validation = ValidationMode::eReleaseLight;

		// DeviceDesc.enableDebugLabels: bracket command spans as Metal debug groups for Xcode and Instruments.
		bool debugLabels = true;

		// The instance this device was made from, borrowed or null in the static CreateDevice form that makes no instance. Used only so teardown can retire the
		// instance alongside its last device.
		Metal4Instance * instanceWrapper = nullptr;

		// Tags this device's handles so another device rejects them. Returned to the pool at teardown.
		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapter{};

		// Backing storage AdapterInfo::name points at.
		detail::HostString adapterName;

		// Backing storage for the OS-derived driver strings (Metal has no driver version of its own).
		detail::HostString driverVersion;
		detail::HostString driverInfo;

		detail::TypedObjectPool<Metal4Object> objects{ 64, 0, "rhi.metal4.objects" };

		/*
		 * One table per kind, the way Vulkan and Direct3D 12 keep theirs. Slot maps, not hash maps because recording resolves handles and is guarded in no threading
		 * mode. A rehash moves the buckets a concurrent lookup is walking, where a slot map never moves its storage.
		 *
		 * Nothing guards them here. Writers are serialized by the guard the RHI takes for that kind.
		 */
		SlotMap<BufferTag, Metal4BufferSlot> buffers;
		SlotMap<TextureTag, Metal4TextureSlot> textures;
		SlotMap<TextureViewTag, Metal4TextureViewSlot> textureViews;
		SlotMap<SamplerTag, NS::SharedPtr<MTL::SamplerState>> samplers;
		SlotMap<HeapTag, NS::SharedPtr<MTL::Heap>> heaps;
		SlotMap<TimelineTag, Metal4Timeline> timelines;
		SlotMap<BinarySemaphoreTag, Metal4BinarySemaphore> binarySemaphores;
		SlotMap<GraphicsPipelineTag, Metal4GraphicsPipeline> graphicsPipelines;
		SlotMap<ComputePipelineTag, Metal4ComputePipeline> computePipelines;
		SlotMap<DescriptorSetTag, Metal4DescriptorSet> descriptorSets;
		SlotMap<QueryPoolTag, Metal4QueryPool> queryPools;

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
		SlotMap<DescriptorSetLayoutTag, Metal4DescriptorSetLayout> descriptorSetLayouts;
		SlotMap<PipelineLayoutTag, Metal4PipelineLayout> pipelineLayouts;

		// The kinds this backend tracks for liveness alone, with no native object of their own.
		detail::ResourceTables<Metal4SlotTag, std::monostate> tracked;

		detail::HostVector<HostUniquePtr<CmdList>> cmdLists;
		detail::HostVector<HostUniquePtr<CmdPool>> cmdPools;

		/*
		 * The residency set every queue on this device holds, and the compiler every pipeline is built through.
		 *
		 * Both are made on first use, not at device creation: a device that creates no resource and compiles no pipeline pays for neither, and both are
		 * reached from entries that already report failure.
		 */
		/**
		 * \brief The compiler every pipeline on this device is built through, made once at device creation.
		 *
		 * Compiling is the expensive part and a compiler holds what makes that cheaper across calls. Made here and not on first use because the graphics and
		 * the compute pipeline entries are guarded separately, so a lazily made one would be two threads racing to make the same object.
		 */
		NS::SharedPtr<MTL4::Compiler> compiler;

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
		 * \brief Everything this device made, kept reachable by the GPU.
		 *
		 * Metal 4 has no per-encoder useResource. A resource an argument buffer points at is reachable only if a residency set the queue holds contains it, and
		 * one that is not faults instead of reading zeros.
		 *
		 * Coarser than tracking what each list touches, and deliberately so: the alternative rebuilds a set per submission, trading a one-time residency cost
		 * for per-frame bookkeeping.
		 */
		std::array<NS::SharedPtr<MTL::ResidencySet>, static_cast<std::size_t>(Residency::eCount)> residencySets;

		/**
		 * \brief Adds one allocation to its kind's residency set.
		 *
		 * \param kind Which set, which is the resource kind whose RHI guard the caller is already holding.
		 */
		void NoteAllocation(Residency kind, const MTL::Allocation * allocation) noexcept;

		/*
		 * What waitIdle blocks on, there being no command buffer to wait for on this generation.
		 *
		 * Metal 3 drains by committing an empty command buffer and waiting on it, which a Metal 4 queue has no equivalent of without building one from an
		 * allocator. Signalling a monotonic event past every value it has held and waiting for that on the host is the same barrier through the primitive this
		 * generation does have.
		 */
		NS::SharedPtr<MTL::SharedEvent> drainEvent;
		std::atomic<std::uint64_t> drainValue{ 0 };

		detail::HostVector<HostUniquePtr<Metal4Swapchain>> swapchains;

		~Metal4Device()
		{
			objects.Reset();
		}

		// Held through a HostUniquePtr in the backend's device list and pointed at by every facade built off it so it is never copied or moved. The default
		// constructor is restored because declaring the four below is what would otherwise take it away and HostNew needs it.
		Metal4Device()								 = default;
		Metal4Device(const Metal4Device &)			 = delete;
		Metal4Device & operator=(const Metal4Device &) = delete;
		Metal4Device(Metal4Device &&)					 = delete;
		Metal4Device & operator=(Metal4Device &&)		 = delete;
	};

	/*
	 * Process lifetime owner for the instances and the devices.
	 *
	 * Unguarded. The RHI serializes createInstance, createDevice, destroyDevice and destroyInstance, which are the only entries that reach either list so a
	 * backend writes no synchronization of its own.
	 */
	struct Metal4BackendOwner final
	{
		detail::HostVector<HostUniquePtr<Metal4Instance>> instances;
		detail::HostVector<HostUniquePtr<Metal4Device>> devices;
	};

	[[nodiscard]] void * AllocObject(Metal4Device * device, const BackendObject * published, QueueType queueType = QueueType::eGraphics);

	[[nodiscard]] bool RefuseUnexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] Metal4BackendOwner & Owner();
	GraphicsApiId DeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view DeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & DeviceCapsOf(void * impl) noexcept;
	const AdapterInfo & DeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts DeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport DeviceFormatSupport(void * impl, Format format) noexcept;
	bool GetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool GetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	BufferHandle CreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	TextureHandle CreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	TextureViewHandle CreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle CreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool GetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool GetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	[[nodiscard]] MTL::Heap * ResolveHeap(Metal4Device * device, HeapHandle handle) noexcept;
	HeapHandle CreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle CreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle CreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	TimelineHandle CreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	[[nodiscard]] MTL::Buffer * ResolveBuffer(Metal4Device * device, BufferHandle handle) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTexture(Metal4Device * device, TextureHandle handle) noexcept;
	[[nodiscard]] Format ResolveTextureFormat(Metal4Device * device, TextureHandle handle) noexcept;
	/**
	 * \brief The recording state behind a command list object.
	 *
	 * Null means the object is not a command list at all, which every caller tests for because a queue and a command pool are the same object type with the
	 * field left alone.
	 */
	[[nodiscard]] inline CmdList * ListOf(Metal4Object * object) noexcept
	{
		return object != nullptr ? object->list : nullptr;
	}

	/// Stages, which this generation names on a barrier where the other one had nothing to name.

	[[nodiscard]] MTL::Stages StagesFor(Flags<PipelineStage> stages) noexcept;

	/// Encoder lifetime. There is no blit encoder here, so copies open the compute one.

	void EndActiveEncoders(CmdList * list) noexcept;
	[[nodiscard]] MTL4::ComputeCommandEncoder * BeginCompute(Metal4Object * object) noexcept;

	/**
	 * \brief Makes one of a list's own transients resident.
	 *
	 * Separate from the device's sets because recording takes no RHI lock, so only the list's own set can be written from here without one of this backend's
	 * own.
	 */
	void NoteListAllocation(CmdList * list, const MTL::Allocation * allocation) noexcept;

	/**
	 * \brief Records a barrier held from before this encoder existed, if there is one.
	 *
	 * Called by whatever opens an encoder, since that encoder is the one the held barrier was meant to order.
	 */
	void FlushPendingBarrier(CmdList * list, MTL4::RenderCommandEncoder * encoder) noexcept;
	void FlushPendingBarrier(CmdList * list, MTL4::ComputeCommandEncoder * encoder) noexcept;

	/**
	 * \brief Writes push constant bytes somewhere an argument table can point at, and answers where.
	 *
	 * Metal 3 hands the range straight to the encoder through setBytes. An argument table binds addresses only, so the bytes have to live in a buffer that
	 * outlives the submission.
	 */
	[[nodiscard]] MTL::GPUAddress WritePushConstants(Metal4Device * device, CmdList * list, const void * data, std::uint32_t size) noexcept;

	BinarySemaphoreHandle CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept;

	/// Command list entries.

	bool CmdBegin(void * impl, Error * error) noexcept;
	bool CmdEnd(void * impl, Error * error) noexcept;
	bool CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool CmdBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool CmdEndDebugLabel(void * impl, Error * error) noexcept;

	/// Compute and copies, which share one encoder on this generation.

	bool CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool CmdDispatch(void * impl, std::uint32_t x, std::uint32_t y, std::uint32_t z, Error * error) noexcept;
	bool CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	bool CmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	bool CmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool CmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool CmdBlit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	[[nodiscard]] MTL::Texture * ResolveTextureView(Metal4Device * device, TextureViewHandle handle) noexcept;

	/**
	 * \brief Whether every shader's binding map agrees with the layout it will be bound through.
	 */
	[[nodiscard]] bool BindingMapsAgree(
		Metal4Device * device, PipelineLayoutHandle layout, std::span<const ShaderBinary> shaders, Error * error) noexcept;

	/**
	 * \brief Whether every buffer a compiled function asks for is one the layout actually binds.
	 *
	 * Read off the pipeline's own reflection, so a shader numbering its sets elsewhere is refused at creation, not dispatched to read zeros.
	 */
	[[nodiscard]] bool FunctionBuffersAreBound(
		Metal4Device * device, PipelineLayoutHandle layout, const NS::Array * bindings, Error * error) noexcept;

	PipelineLayoutHandle CreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	GraphicsPipelineHandle CreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle CreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	/// Rendering.

	bool CmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool CmdEndRendering(void * impl, Error * error) noexcept;
	bool CmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool CmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool CmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool CmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool CmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool CmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool CmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool CmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	bool CmdDraw(void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance,
		Error * error) noexcept;
	bool CmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool CmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool CmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;

	/// Binding, which goes through an argument table, not through calls on the encoder.

	bool CmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	bool CmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data,
		Error * error) noexcept;

	/// Writing a set, which is unchanged: a set is the same argument buffer on both generations.

	bool UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	void * CreateDescriptorArena(void * impl, [[maybe_unused]] const DescriptorArenaDesc & desc, Error * error) noexcept;
	void * CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	MappedMemory Map(void * impl, BufferHandle buffer, const MapDesc & desc, Error * error) noexcept;
	bool QueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] Metal4QueryPool * ResolveQueryPool(Metal4Device * device, QueryPoolHandle handle) noexcept;
	QueryPoolHandle CreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool CalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool CmdResetQueryPool(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, Error * error) noexcept;
	bool CmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, Flags<PipelineStage> stage, Error * error) noexcept;
	bool CmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool CmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool CmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	bool Destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	bool CollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool CollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle AdoptBuffer(
		void * impl, GraphicsApiId api, const void * nativeImport, [[maybe_unused]] const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle AdoptTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] const void * nativeImport,
		[[maybe_unused]] const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool GetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool GetNativeTexture([[maybe_unused]] void * impl, [[maybe_unused]] GraphicsApiId api, [[maybe_unused]] TextureHandle texture,
		[[maybe_unused]] void * outNativeImport, Error * error) noexcept;
	AccelerationStructureHandle CreateAccelerationStructure(
		[[maybe_unused]] void * impl, [[maybe_unused]] const AccelerationStructureDesc & desc, Error * error) noexcept;
	RayTracingPipelineHandle CreateRayTracingPipeline(
		[[maybe_unused]] void * impl, [[maybe_unused]] const RayTracingPipelineDesc & desc, Error * error) noexcept;
	bool BeginNativeMutation([[maybe_unused]] void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	DescriptorSetHandle ArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool ArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const RayTracingApi & RayTracingBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	TextureViewHandle AdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle AdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool GetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool GetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle AdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle AdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool GetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool GetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;
	AcquireResult SwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult SwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	TextureHandle SwapchainBackBuffer(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	TextureViewHandle SwapchainBackBufferView(void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle SwapchainPresentSemaphore(void * impl, std::uint32_t imageIndex) noexcept;
	Format SwapchainFormat(void * impl) noexcept;
	bool SwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept;
	std::uint32_t SwapchainImageCount(void * impl) noexcept;
	std::uint32_t SwapchainWidth(void * impl) noexcept;
	std::uint32_t SwapchainHeight(void * impl) noexcept;
	bool SwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool SwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	void * CreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	QueueType QueueTypeOf(void * impl) noexcept;
	std::uint32_t QueueFamilyIndex(void * impl) noexcept;
	bool QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool QueueWaitIdle(void * impl, Error * error) noexcept;
	bool QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	bool QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool QueueBeginDebugLabel(void * impl, CString name, std::uint32_t color, Error * error) noexcept;
	bool QueueEndDebugLabel(void * impl, Error * error) noexcept;
	void * CommandPoolAllocate(void * impl, [[maybe_unused]] CString debugName, Error * error) noexcept;
	bool CommandPoolReset(void * impl, RetirePoint safeAfter, Error * error) noexcept;
	GraphicsApiId InstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool EnumerateAdapters([[maybe_unused]] void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool QueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;

	bool ExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool ExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool ExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool ExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool ExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle ImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle ImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle ImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle ImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle ImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool CloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;
	void PopulateCaps(Metal4Device * device);

	/**
	 * \brief Whether this adapter reports the Metal 4 family.
	 *
	 * One call, supportsFamily against GPUFamilyMetal4, which is the documented predicate. It needs no operating system version test around it: supportsFamily
	 * long predates Metal 4, so an older OS asked about a family it never heard of answers no instead of trapping.
	 *
	 * Verified on an Apple M1 Max, macOS 26.5.2. Metal 4 is not gated on recent hardware.
	 */
	[[nodiscard]] bool AdapterHasMetal4(MTL::Device * device) noexcept;

	/**
	 * \brief Builds a Metal 4 device, or refuses one this adapter or this version pin cannot have.
	 *
	 * \param refusedReason Set to why Metal 4 could not be provided, and left alone for every other failure, so a caller can tell a refusal from having no
	 * adapter at all.
	 */
	[[nodiscard]] Metal4Device * MakeOwnedDevice(Metal4Instance * instance, const DeviceDesc & desc, const char *& refusedReason);
	[[nodiscard]] Metal4Instance * MakeOwnedInstance();
	void DestroyDevice(void * impl) noexcept;
	void DestroyInstance(void * impl) noexcept;
	void * InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * CreateInstance([[maybe_unused]] const void * instanceDesc, Error * error) noexcept;

	/*
	 * A handle for a kind this backend tracks for liveness alone, with no native object behind it.
	 *
	 * Every kind is minted from a table, here or from the one holding its native object so one lookup answers both what the handle names and whether it is still
	 * live. A handle fabricated from a bare counter instead would resolve nowhere and the retire that Destroy runs would fail against a table that had never heard
	 * of it.
	 */
	template <typename HandleT>
	[[nodiscard]] HandleT MintHandle(Metal4Device * device)
	{
		return device->tracked.Store<HandleT>(std::monostate{});
	}

	/*
	 * Whether a handle names something this device issued and still holds.
	 *
	 * One overload per kind and not one lookup in a shared table because the table that answers is the table that holds the object. Reads take nothing: a slot
	 * map's storage never moves so these are safe from a recording thread while another is creating.
	 */
	[[nodiscard]] inline bool Resolves(Metal4Device * device, BufferHandle handle) noexcept
	{
		return device->buffers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TextureHandle handle) noexcept
	{
		return device->textures.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TextureViewHandle handle) noexcept
	{
		return device->textureViews.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, SamplerHandle handle) noexcept
	{
		return device->samplers.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, HeapHandle handle) noexcept
	{
		return device->heaps.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, TimelineHandle handle) noexcept
	{
		return device->timelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, BinarySemaphoreHandle handle) noexcept
	{
		return device->binarySemaphores.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelines.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSets.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	[[nodiscard]] inline bool Resolves(Metal4Device * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayouts.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	// The kinds with nothing native behind them: query pools, pipeline caches and the ray tracing objects this backend does not build.
	template <typename HandleT>
	[[nodiscard]] bool Resolves(Metal4Device * device, HandleT handle) noexcept
	{
		return device->tracked.Resolve(handle, kHandleAlreadyChecked) != nullptr;
	}

	template <typename... Args>
	bool Unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	// The creation-entry form of Unimplemented, for a kind this backend has nothing to put behind a handle. An invalid handle is what a failed create
	// returns, so a caller that ignores the error still cannot pass the result anywhere.
	template <typename HandleT, typename... Args>
	HandleT UnimplementedHandle([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<HandleT>(LastError(args...), ErrorCode::eUnsupportedFeature, "Metal 4 RHI backend: operation not implemented yet");
	}

	template <typename HandleT, typename... Args>
	HandleT CreateHandle(void * impl, Args... args) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.create");

		Error * error		 = LastError(args...);
		const HandleT handle = MintHandle<HandleT>(static_cast<Metal4Device *>(impl));
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
	inline DescriptorSetLayoutHandle CreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createDescriptorSetLayout");

		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return FailValue<DescriptorSetLayoutHandle>(
					error, ErrorCode::eUnsupportedFeature, "Metal does not bake samplers into a descriptor set layout, so write the sampler into the set");
			}
		}

		// Refused above, so nothing here carries the borrowed sampler span that would otherwise dangle in a slot outliving this call.
		Metal4DescriptorSetLayout slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());

		auto * device						   = static_cast<Metal4Device *>(impl);
		const DescriptorSetLayoutHandle handle = device->descriptorSetLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<DescriptorSetLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal descriptor set layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal4

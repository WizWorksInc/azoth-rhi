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
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/d3d12_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include "backends/registration.hpp"
#include "support/driver_version.hpp"

#ifndef NOMINMAX
	#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
// Declares ID3D12Debug and the info queue. Named, not left to reach us through d3d12.h, which is not guaranteed to pull it in.
#include <D3D12MemAlloc.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#if defined(AZOTH_RHI_ENABLE_PIX)
	// d3d12.h above defines __d3d12_h__, which unlocks pix3.h's command-list and queue event overloads.
	#include <pix3.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace azo::rhi::d3d12
{
	using Microsoft::WRL::ComPtr;

	struct D3D12Instance final
	{
		const BackendObject * object = nullptr;
		ComPtr<IDXGIFactory6> factory;
		bool debugLayer = false;

		// NativeValidationDesc onMessage and messageUserData. Kept here because the desc carrying them belongs to the instance, and read by each device this instance
		// creates, since the info queue they reach is per device.
		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;
		detail::HostVector<detail::HostString> adapterNames;   // stable storage backing AdapterInfo::name
		detail::HostVector<detail::HostString> driverVersions; // stable storage backing AdapterInfo::driverVersion
	};

	struct D3D12Device;

	// Stored per buffer: the ID3D12Resource, its D3D12MA allocation, the size and whether the heap is CPU-mappable so Map rejects a device-local buffer.
	struct BufferSlot final
	{
		// Declared allocation-before-resource so the resource (declared later) destructs first. D3D12MA requires that the resource is released before the allocation
		// that owns its memory.
		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		std::uint64_t size = 0;
		bool hostVisible   = false;

		// Who frees the ID3D12Resource. An adopted one is the caller's and destroy retires the slot without touching it.
		SlotLifetime lifetime = SlotLifetime::eOwned;

		// True for a sparse (reserved) buffer created with CreateReservedResource: it owns no D3D12MA allocation and has no backing store until tiles are bound
		// through bindSparse so it is never host-visible and destroy releases only the virtual resource.
		bool reserved = false;

		// What BufferDesc declared. CreateSharedHandle works only on a resource created with the shared heap flag, so export reads this without trying and taking the
		// failure.
		Flags<ExternalHandleType> exportableHandleTypes;

		// What the buffer was created with, answered by getBufferInfo. Last so designated initializers at every fill site stay in order. debugName is null: the name
		// is borrowed for the creation call.
		BufferDesc desc{};
	};

	// One registry slot per texture. Keeps the format, geometry and usage view creation needs to build RTV, DSV, SRV and UAV descriptors. The allocation member is
	// declared before resource for the same destruction order as BufferSlot.
	struct TextureSlot final
	{
		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		DXGI_FORMAT format		  = DXGI_FORMAT_UNKNOWN;
		Format rhiFormat		  = Format::eUndefined; // the portability format, which a plane view needs to resolve one plane's single-plane format
		TextureType type		  = TextureType::eTex2D;
		std::uint32_t mipLevels	  = 1;
		std::uint32_t arrayLayers = 1;
		Flags<TextureUsage> usage;

		// True when the resource was created typeless for allowFormatViews, so a view may name a format other than this one. format stays the typed format the caller
		// asked for, which is what a view inherits and what the blit and clear paths need.
		bool mutableFormat = false;

		/*
		 * Who frees the ID3D12Resource, which was two bools until the third combination turned out to mean nothing.
		 *
		 * A back buffer and an adopted resource both leave the native object alone and differ in what destroy does with the slot: the first refuses, because
		 * destroying a back buffer is a caller mistake, and the second retires it, that being how a caller hands the slot back. Retiring is what keeps repeated adopt
		 * and destroy from leaking slots.
		 */
		SlotLifetime lifetime = SlotLifetime::eOwned;

		// True for a sparse texture created with CreateReservedResource. It owns no D3D12MA allocation and has no backing store until tiles are bound through
		// bindSparse.
		bool reserved = false;

		// What TextureDesc declared, read by export for the reason the buffer slot's copy is.
		Flags<ExternalHandleType> exportableHandleTypes;

		// What the texture was created with, answered by getTextureInfo. The fields above are the same values in the shapes the record paths want them in, so both
		// are written from this one at the same Store call. debugName is null here: the name is borrowed for the creation call.
		TextureDesc desc{};
	};

	/*
	 * A texture view resolves to typed D3D12 descriptors. Render-target and depth views are created eagerly into the device's CPU-only RTV and DSV heaps (their
	 * indices live here, kInvalidIndex when absent). Shader-resource and unordered-access views are built into shader-visible heaps at bind time from the stored
	 * format and range, which the descriptor and pipeline slices add.
	 */
	struct TextureViewSlot final
	{
		TextureHandle texture{};
		DXGI_FORMAT format	 = DXGI_FORMAT_UNKNOWN;
		TextureViewType type = TextureViewType::eTex2D;
		TextureSubresourceRange range{};
		std::uint32_t rtvIndex = kInvalidIndex;
		std::uint32_t dsvIndex = kInvalidIndex;

		// The plane a plane view selects, zero for an ordinary view. Written into the shader resource view's PlaneSlice.
		UINT planeSlice = 0;

		// Encoded TextureViewDesc::swizzle, applied when the shader resource view is written. Direct3D 12 carries the mapping on the SRV and not on the view object,
		// and the SRV is not built until a descriptor set names this view, so the encoding is kept here until then.
		UINT shaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// True for swapchain back-buffer views, freed by the swapchain on resize and teardown.
		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	// A CPU-only descriptor heap with a free list, used for render-target and depth-stencil views. The shader-visible heaps used for binding are managed
	// separately by the descriptor arena.
	struct CpuDescriptorHeap final
	{
		ComPtr<ID3D12DescriptorHeap> heap;
		D3D12_CPU_DESCRIPTOR_HANDLE base{};
		std::uint32_t increment = 0;
		std::uint32_t capacity	= 0;
		std::uint32_t next		= 0; // next fresh index, used when the free list is empty
		detail::HostVector<std::uint32_t> freeList;

		[[nodiscard]] bool Init(ID3D12Device * device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t count)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type			= type;
			desc.NumDescriptors = count;
			desc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return false;
			}
			base	  = heap->GetCPUDescriptorHandleForHeapStart();
			increment = device->GetDescriptorHandleIncrementSize(type);
			capacity  = count;
			return true;
		}

		// Returns kInvalidIndex when the heap is full.
		[[nodiscard]] std::uint32_t Allocate()
		{
			if (!freeList.empty())
			{
				const std::uint32_t index = freeList.back();
				freeList.pop_back();
				return index;
			}
			return next < capacity ? next++ : kInvalidIndex;
		}

		void Free(std::uint32_t index)
		{
			if (index != kInvalidIndex)
			{
				freeList.push_back(index);
			}
		}

		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Handle(std::uint32_t index) const noexcept
		{
			return { base.ptr + static_cast<SIZE_T>(index) * increment };
		}
	};

	// A sampler holds only state. The shader visible sampler descriptor is created at bind time so the slot just keeps the translated D3D12 description.
	struct SamplerSlot final
	{
		D3D12_SAMPLER_DESC desc{};
	};

	// A heap backs placed resources. Its type fixes the mappability of any buffer placed into it.
	struct HeapSlot final
	{
		ComPtr<ID3D12Heap> heap;
		D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT;
		std::uint64_t size	 = 0;

		// What HeapDesc declared. A shared heap takes placed resources, so this is the granularity a caller controlling its own memory shares at.
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	// A descriptor set layout records its bindings (copied, since the desc span need not outlive the call) and the precomputed table sizes. CBV, SRV and UAV
	// descriptors share one shader-visible heap. Samplers use another so their counts are tracked separately for the pipeline layout and the arena.
	struct DescriptorSetLayoutSlot final
	{
		detail::HostVector<DescriptorBinding> bindings;
		std::uint32_t cbvSrvUavCount = 0;
		std::uint32_t samplerCount	 = 0;
	};

	// A pipeline layout owns the root signature and records which root-parameter index binds each set's resource table and sampler table (kInvalidIndex when
	// absent), plus the first root-constants parameter so BindDescriptorSet and PushConstants address the right slots.
	struct PipelineLayoutSlot final
	{
		struct SetParams final
		{
			std::uint32_t resourceParam = kInvalidIndex;
			std::uint32_t samplerParam	= kInvalidIndex;
		};

		// One root 32-bit-constants parameter per PushConstantRange, tagged with the range's byte offset and size so pushConstants can route a write to the parameter
		// whose range contains it. A multi-range layout addresses each range independently instead of funneling every push into the first.
		struct PushConstantParam final
		{
			std::uint32_t rootParam = kInvalidIndex;
			std::uint32_t offset	= 0;
			std::uint32_t size		= 0;
		};

		ComPtr<ID3D12RootSignature> rootSignature;
		detail::HostVector<SetParams> setParams;
		detail::HostVector<PushConstantParam> pushConstantParams;

		// The set layouts this was built from, so pipeline creation can rebuild the ShaderAbiLayout its root signature was derived from.
		detail::HostVector<DescriptorSetLayoutHandle> sets;
	};

	/*
	 * A graphics pipeline keeps its PSO plus the root signature and primitive topology a draw needs: D3D12 bakes neither the root signature nor the concrete
	 * topology into the PSO so setGraphicsPipeline binds the root signature and IASetPrimitiveTopology separately. The root signature is held by ComPtr so it
	 * outlives the pipeline layout that produced it.
	 */
	struct GraphicsPipelineSlot final
	{
		ComPtr<ID3D12PipelineState> pipeline;
		ComPtr<ID3D12RootSignature> rootSignature;
		D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		// Per-binding vertex stride, indexed by input slot. D3D12 carries the stride in the vertex buffer view (not the PSO) so setVertexBuffer reads it back from
		// the bound pipeline.
		std::array<std::uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};

		/*
		 * D3D12 has no PSO slot for blend factor, stencil reference or depth bounds. They are command-list state. setGraphicsPipeline applies these baked values at
		 * bind time. Blend factor and stencil ref are baked only when not declared dynamic (a later dynamic setter then overrides them). Depth bounds is never
		 * dynamic in this RHI so it always applies and resets to the full range when disabled.
		 */
		bool bakeBlendConstants = false;
		std::array<float, 4> blendConstants{};
		bool bakeStencilReference	   = false;
		std::uint32_t stencilReference = 0;
		bool depthBoundsTestEnable	   = false;
		float minDepthBounds		   = 0.0f;
		float maxDepthBounds		   = 1.0f;
	};

	struct ComputePipelineSlot final
	{
		ComPtr<ID3D12PipelineState> pipeline;
		ComPtr<ID3D12RootSignature> rootSignature;
	};

	// A pipeline cache wraps an ID3D12PipelineLibrary. getPipelineCacheData serializes it into data, which backs the returned view until the next query on this
	// cache.
	struct PipelineCacheSlot final
	{
		ComPtr<ID3D12PipelineLibrary> library;
		detail::HostVector<std::uint8_t> data;
	};

	/*
	 * A query pool wraps an ID3D12QueryHeap. The query type is kept so the recording commands select the matching D3D12_QUERY_TYPE for begin/end/resolve, and the
	 * count so they can refuse an index past the end. Direct3D 12 cannot be asked a heap's capacity back and a query past it is undefined, not reported, so this
	 * is the only place the bound exists.
	 */
	struct QueryPoolSlot final
	{
		ComPtr<ID3D12QueryHeap> heap;
		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	struct D3D12DescriptorArena;

	/*
	 * A descriptor set is a sub-range of an arena's shader-visible heaps. The base offsets locate its CBV/SRV/UAV and sampler descriptors. The layout bindings map
	 * a (binding, arrayIndex) to an offset within those ranges. A set outliving the reset that gave its range away is refused above this.
	 */
	struct DescriptorSetSlot final
	{
		D3D12DescriptorArena * arena = nullptr;
		std::uint32_t resourceBase	 = 0;
		std::uint32_t resourceCount	 = 0;
		std::uint32_t samplerBase	 = 0;
		std::uint32_t samplerCount	 = 0;
		detail::HostVector<DescriptorBinding> bindings;
	};

	/*
	 * A timeline is an ID3D12Fence, whose monotonic counter is the timeline value. A binary semaphore is also an ID3D12Fence, emulating binary acquire/present
	 * edges on D3D12's single monotonic fence primitive. The Nth signal produces fence value N and the Nth wait targets value N, kept on separate monotonic
	 * counters so a wait enqueued before its signal still blocks until that signal (VkSemaphore wait-before-signal ordering).
	 */
	struct TimelineSlot final
	{
		ComPtr<ID3D12Fence> fence;

		// What TimelineDesc declared. A fence has to be created with D3D12_FENCE_FLAG_SHARED to be shareable and cannot gain it later.
		Flags<ExternalHandleType> exportableHandleTypes;

		// Who frees the fence. The ComPtr holds a reference either way, so an adopted one is released without the caller's own reference going with it.
		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct BinarySemaphoreSlot final
	{
		ComPtr<ID3D12Fence> fence;
		std::uint64_t signalValue = 0; // value the next signal produces (pre-incremented)
		std::uint64_t waitValue	  = 0; // value the next wait targets (pre-incremented), pairs with the Nth signal

		/*
		 * What BinarySemaphoreDesc declared.
		 *
		 * Sharing one of these works because the two counters above start at zero on both sides and each advances once per operation, so an exporter that only
		 * signals and an importer that only waits stay in step. That is the binary semaphore contract and not a coincidence, and it is also the one rule a stricter
		 * importer names explicitly: exactly one wait per signal.
		 */
		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct D3D12Queue final
	{
		const BackendObject * object = nullptr;
		ComPtr<ID3D12CommandQueue> queue;
		QueueType type		= QueueType::eGraphics;
		D3D12Device * owner = nullptr;

		// Fence backing WaitIdle: signal an incrementing value on the queue, then block the host on it. D3D12 has no native queue-wait-idle.
		ComPtr<ID3D12Fence> idleFence;
		std::uint64_t idleValue = 0;
	};

	// A command signature for ExecuteIndirect, keyed by argument type and byte stride.
	struct CommandSignatureEntry final
	{
		D3D12_INDIRECT_ARGUMENT_TYPE type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
		std::uint32_t stride			  = 0;
		ComPtr<ID3D12CommandSignature> signature;
	};

	struct D3D12CommandList;

	// A command pool is an ID3D12CommandAllocator on the family selected by desc.queueType. Reset reclaims the allocator's memory once the RHI has established the
	// GPU is done.
	struct D3D12CommandPool final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		ComPtr<ID3D12CommandAllocator> allocator;
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueType queueType			 = QueueType::eGraphics;

		/*
		 * Command signatures built by an indirect draw or dispatch, cached here and not on the device. ExecuteIndirect looks one up during recording, which is
		 * guarded in no threading mode, where a device-wide cache would mean a lock. A pool is owned by one host thread so a cache here needs nothing.
		 *
		 * Two pools may each build a signature for the same key, which is harmless since a signature is an immutable argument layout. Freed with the pool.
		 */
		detail::HostVector<CommandSignatureEntry> commandSignatures;

		/*
		 * Every list this pool has built, and how many of them are currently out.
		 *
		 * Resetting the allocator is what frees the recordings, and the lists over it are reset onto it again at Begin, so the ones already built are the ones
		 * the next frame records into. Creating one per call would grow both this and the device's list storage by one a frame. The cursor rewinds at Reset.
		 */
		detail::HostVector<D3D12CommandList *> lists;
		std::size_t handedOut = 0;
	};

	// A command list wraps an ID3D12GraphicsCommandList. It borrows its pool's allocator (the pool owns it). Allocate leaves the list closed. Begin resets it into
	// the recording state and End closes it.
	struct D3D12CommandList final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		ComPtr<ID3D12GraphicsCommandList> list;
		ID3D12CommandAllocator * allocator = nullptr; // borrowed from the owning pool
		D3D12CommandPool * pool			   = nullptr; // the pool that allocated it, which owns its command signature cache
		D3D12_COMMAND_LIST_TYPE type	   = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueType queueType				   = QueueType::eGraphics;

		// Strides of the currently bound graphics pipeline, copied at setGraphicsPipeline so setVertexBuffer can fill the vertex buffer view's stride.
		std::array<std::uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};

		// A rendering scope's end timestamp, left by beginRendering for endRendering to record. Null outside a scope, and outside one that asked for no end
		// timestamp. Borrowed from the query pool slot, which the caller keeps alive across the scope.
		ID3D12QueryHeap * pendingEndTimestampHeap = nullptr;
		std::uint32_t pendingEndTimestampQuery	  = 0;

		// Whether the last bound pipeline was compute so bindDescriptorSet targets the matching root and the shader-visible heaps last bound via SetDescriptorHeaps,
		// to skip redundant rebinds.
		bool computePipelineBound				 = false;
		ID3D12DescriptorHeap * boundResourceHeap = nullptr;
		ID3D12DescriptorHeap * boundSamplerHeap	 = nullptr;

		/*
		 * A descriptor set bound via bindDescriptorSet but not yet written to the root signature. D3D12 picks the compute or graphics root from the bound pipeline so
		 * a set bound before its pipeline cannot be applied at bind time. The Vulkan backend accepts either order.
		 *
		 * Each is recorded per set index and flushed at the next draw or dispatch, once the root is known. A slot clears when applied and the root table persists
		 * until the root signature changes.
		 */
		struct PendingDescriptorSet final
		{
			D3D12_GPU_DESCRIPTOR_HANDLE resourceHandle{};
			D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle{};
			std::uint32_t resourceParam = 0;
			std::uint32_t samplerParam	= 0;
			bool hasResource			= false;
			bool hasSampler				= false;
			bool valid					= false;
		};

		static constexpr std::size_t kMaxBoundDescriptorSets = 8;
		std::array<PendingDescriptorSet, kMaxBoundDescriptorSets> pendingSets{};

		// Transient RTV/DSV descriptors allocated by clearTexture, returned to the device heaps at the next Begin. The pool is reset only after the GPU finished the
		// prior recording.
		detail::HostVector<std::uint32_t> transientRtvs;
		detail::HostVector<std::uint32_t> transientDsvs;

		// Lazily created shader-visible + staging heaps for clearBuffer's ClearUnorderedAccessViewUint, with a bump cursor reset at Begin. The shader-visible heap
		// binding clobbers the app's bound descriptor heaps so clearBuffer forces the next bindDescriptorSet to rebind.
		ComPtr<ID3D12DescriptorHeap> clearGpuHeap;
		ComPtr<ID3D12DescriptorHeap> clearStagingHeap;
		std::uint32_t clearHeapIncrement = 0;
		std::uint32_t clearHeapCapacity	 = 0;
		std::uint32_t clearHeapNext		 = 0;
		// clearBuffer heaps filled earlier in this recording, held until the next Begin (after the GPU has consumed their clears). When the current heap fills it is
		// retired here and a fresh one allocated so a recording is never capped at a fixed number of buffer clears.
		detail::HostVector<ComPtr<ID3D12DescriptorHeap>> retiredClearHeaps;

		// Attachment resources with StoreOp::eDontCare for the active rendering scope. endRendering issues a DiscardResource for each so the driver may drop their
		// contents, matching the Vulkan store ops.
		detail::HostVector<ID3D12Resource *> pendingDiscards;

		// Scratch buffers allocated to repack a buffer<->texture copy whose tightly-packed row pitch is not 256-aligned (D3D12 requires an aligned pitch). Held until
		// the next Begin, after the GPU consumes them.
		detail::HostVector<ComPtr<ID3D12Resource>> retiredCopyScratch;
		detail::HostVector<ComPtr<D3D12MA::Allocation>> retiredCopyAllocs;
	};

	// A descriptor arena owns shader-visible CBV/SRV/UAV and sampler heaps that allocated sets sub-range, plus CPU-only staging heaps the updates write into
	// before copying to the shader-visible side. A bump cursor per heap hands out ranges and a reset rewinds it.
	struct D3D12DescriptorArena final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		ComPtr<ID3D12DescriptorHeap> resourceHeap;
		ComPtr<ID3D12DescriptorHeap> samplerHeap;
		ComPtr<ID3D12DescriptorHeap> resourceStaging;
		ComPtr<ID3D12DescriptorHeap> samplerStaging;
		std::uint32_t resourceIncrement = 0;
		std::uint32_t samplerIncrement	= 0;
		std::uint32_t resourceCapacity	= 0;
		std::uint32_t samplerCapacity	= 0;
		std::uint32_t resourceNext		= 0;
		std::uint32_t samplerNext		= 0;
	};

	/*
	 * A flip-model swapchain over an HWND. Flip-model forbids an sRGB swapchain format so the swapchain holds the UNORM base format and the back-buffer RTVs carry
	 * the requested (possibly sRGB) view format. Back buffers register as borrowed texture and view slots so barriers and beginRendering can resolve them.
	 */
	struct D3D12Swapchain final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		HWND hwnd					 = nullptr;
		ComPtr<IDXGISwapChain3> swapchain;
		Format format				= Format::eBGRA8Srgb;
		DXGI_FORMAT viewFormat		= DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; // RTV format, may be sRGB
		DXGI_FORMAT swapchainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;	   // flip-model storage format, never sRGB
		std::uint32_t width			= 0;
		std::uint32_t height		= 0;
		std::uint32_t imageCount	= 0;
		UINT presentSyncInterval	= 1;
		UINT presentFlags			= 0;
		bool allowTearing			= false;
		detail::HostVector<TextureHandle> backBuffers;
		detail::HostVector<TextureViewHandle> backBufferViews;
	};

	struct D3D12Device final
	{
		const BackendObject * object = nullptr;
		ComPtr<IDXGIFactory6> factory; // shared with the owning instance, used for swapchain creation
		ComPtr<IDXGIAdapter4> adapter;
		ComPtr<ID3D12Device> device;
		ComPtr<D3D12MA::Allocator> allocator;

		// Queues created for each capability, honoring the DeviceDesc queue requests. D3D12 engine types are always independent so compute and copy queues are always
		// dedicated. Filled once at creation and never grown so the pointers GetQueue hands out stay valid.
		detail::HostVector<D3D12Queue> graphicsQueues;
		detail::HostVector<D3D12Queue> computeQueues;
		detail::HostVector<D3D12Queue> copyQueues;

		[[nodiscard]] detail::HostVector<D3D12Queue> & QueuesForType(QueueType type) noexcept
		{
			switch (type)
			{
			case QueueType::eCompute:  return computeQueues;
			case QueueType::eCopy:	   return copyQueues;
			case QueueType::eGraphics: break;
			}

			return graphicsQueues;
		}

		// Destroys since the last collectGarbage, reported as the pendingRetire counter. Nothing here waits on it: this backend releases on Destroy so the number is
		// what a profiler graphs, not what the collector reads.
		std::atomic<std::uint64_t> pendingRetire{ 0 };

		ValidationMode validation = ValidationMode::eReleaseLight;
		bool debugNames			  = true; // DeviceDesc.enableDebugNames: name resources for PIX and the debug layer.
		bool debugLabels		  = true; // DeviceDesc.enableDebugLabels: bracket command spans as PIX events.
		// Tags this device's handles so another device rejects them. Returned to the pool at teardown.
		std::uint32_t deviceTag = 0;

		DeviceCaps caps{};
		AdapterInfo adapterInfo{};
		detail::HostString adapterName;	  // stable storage backing adapterInfo.name
		detail::HostString driverVersion; // stable storage backing adapterInfo.driverVersion

		// In the static CreateDevice<D3D12Api> form the device owns its factory and releases it from this struct's destructor. Null on the dynamic-registry form,
		// where the factory lives in the owner.
		HostUniquePtr<D3D12Instance> ownedInstance;

		// The instance this device was made from, borrowed or null in the static form that owns its own above. Used only so teardown can retire the instance
		// alongside its last device, since on the dynamic form nothing else ever would.
		D3D12Instance * instanceWrapper = nullptr;

		// Resource registries. Declared below the allocator and device so they destruct first, releasing every resource while both are still alive.
		SlotMap<BufferTag, BufferSlot> bufferSlots;

		SlotMap<TextureTag, TextureSlot> textureSlots;

		SlotMap<TextureViewTag, TextureViewSlot> textureViewSlots;

		SlotMap<SamplerTag, SamplerSlot> samplerSlots;

		SlotMap<HeapTag, HeapSlot> heapSlots;

		SlotMap<DescriptorSetLayoutTag, DescriptorSetLayoutSlot> descriptorSetLayoutSlots;

		SlotMap<PipelineLayoutTag, PipelineLayoutSlot> pipelineLayoutSlots;

		SlotMap<GraphicsPipelineTag, GraphicsPipelineSlot> graphicsPipelineSlots;

		SlotMap<ComputePipelineTag, ComputePipelineSlot> computePipelineSlots;

		SlotMap<PipelineCacheTag, PipelineCacheSlot> pipelineCacheSlots;

		SlotMap<QueryPoolTag, QueryPoolSlot> queryPoolSlots;

		detail::HostVector<HostUniquePtr<D3D12DescriptorArena>> descriptorArenas;

		/*
		 * One shader-visible CBV/SRV/UAV heap and one shader-visible sampler heap shared by every descriptor arena, each with a CPU staging heap. D3D12 binds only
		 * one heap of each type at a time so a draw that binds descriptor sets from two arenas needs them in the same heap. Every arena is a bump range in these
		 * globals (see D3D12DescriptorArenaAllocate) so a set's base is a global offset the arena's shared heap pointer indexes directly.
		 */
		ComPtr<ID3D12DescriptorHeap> globalResourceHeap;
		ComPtr<ID3D12DescriptorHeap> globalSamplerHeap;
		ComPtr<ID3D12DescriptorHeap> globalResourceStaging;
		ComPtr<ID3D12DescriptorHeap> globalSamplerStaging;
		std::uint32_t globalResourceIncrement = 0;
		std::uint32_t globalSamplerIncrement  = 0;
		std::uint32_t globalResourceCapacity  = 0;
		std::uint32_t globalSamplerCapacity	  = 0;
		std::uint32_t globalResourceNext	  = 0;
		std::uint32_t globalSamplerNext		  = 0;

		SlotMap<DescriptorSetTag, DescriptorSetSlot> descriptorSetSlots;

		detail::HostVector<HostUniquePtr<D3D12Swapchain>> swapchains;

		SlotMap<TimelineTag, TimelineSlot> timelineSlots;

		SlotMap<BinarySemaphoreTag, BinarySemaphoreSlot> binarySemaphoreSlots;

		// Command pools and lists are owned, not slot-indexed: their facades hold the raw pointer directly. Declared before the lists so the lists (which borrow each
		// pool's allocator) destruct first.
		detail::HostVector<HostUniquePtr<D3D12CommandPool>> commandPools;
		detail::HostVector<HostUniquePtr<D3D12CommandList>> commandLists;

		// CPU-only descriptor heaps for render-target and depth-stencil views. The RHI serializes the create that touches these, per ResourceType.
		CpuDescriptorHeap rtvHeap;
		CpuDescriptorHeap dsvHeap;

		/*
		 * Internal resample-blit pipeline. D3D12 has no fixed-function image blit (unlike Vulkan's vkCmdBlitImage) so blit and generateMips lower to this compute
		 * shader, which reads the source mip through an SRV plus sampler and writes each destination texel through a UAV. Created once in MakeOwnedDevice while
		 * device setup is single-threaded so no lock guards it (a prior lazy attempt was stopped for reaching for a pipeline-cache mutex. Eager creation removes the
		 * need for one).
		 */

		/*
		 * Tallied by the debug layer's message callback, the same shape the Vulkan messenger keeps. Monotonic so a reader sees everything the run produced , not
		 * whatever is still sitting in the info queue's storage, which discards on its own once full.
		 *
		 * The callback runs on whichever thread trips a check so both counters are atomic.
		 */
		std::atomic<std::uint64_t> validationErrors{ 0 };
		std::atomic<std::uint64_t> validationWarnings{ 0 };

		// Copied from the instance at creation, not reached through instanceWrapper, so the callback registered below reads a field that was already set when it was
		// registered.
		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;

#ifdef __ID3D12InfoQueue1_INTERFACE_DEFINED__
		ComPtr<ID3D12InfoQueue1> infoQueue;
		DWORD infoQueueCookie = 0;
#endif

		~D3D12Device()
		{
#ifdef __ID3D12InfoQueue1_INTERFACE_DEFINED__
			// The callback holds a pointer to this device so it has to stop firing before the device goes.
			if (infoQueue && infoQueueCookie != 0)
			{
				infoQueue->UnregisterMessageCallback(infoQueueCookie);
			}
#endif
		}
	};

	// Types and the slice-crossing declarations the backend/d3d12/*.cpp files share.

	/*
	 * Process lifetime owner for the instances and the devices.
	 *
	 * Unguarded. The RHI serializes createInstance, createDevice, destroyDevice and destroyInstance, which are the only entries that reach either list so a
	 * backend writes no synchronization of its own.
	 */
	struct D3D12BackendOwner final
	{
		detail::HostVector<HostUniquePtr<D3D12Instance>> instances;
		detail::HostVector<HostUniquePtr<D3D12Device>> devices;
	};

	// Reads the user-mode driver version an adapter reports. The value is the uniform WDDM four-by-16-bit packing that every vendor shares. Returns an empty
	// string and a zero raw when the adapter has no queryable driver (for example the software Basic Render Driver).
	struct D3D12DriverVersion final
	{
		detail::HostString text;
		std::uint64_t raw = 0;
	};

	/*
	 * Declared here and not with the rest of the slice-crossing block below. The templates that follow pass an Error * straight through so those calls depend on
	 * nothing the template parameter decides and are looked up where they are written, not where they are instantiated.
	 */
	bool Succeed(Error * error) noexcept;
	bool Fail(Error * error, ErrorCode code, const char * message) noexcept;

	template <typename T>
	[[nodiscard]] T ReturnValue(T value, Error * error) noexcept
	{
		Succeed(error);
		return value;
	}

	template <typename T>
	[[nodiscard]] T FailValue(Error * error, ErrorCode code, const char * message) noexcept
	{
		Fail(error, code, message);
		return {};
	}

	template <typename... Args>
	[[nodiscard]] Error * LastError(Args &&... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		return std::get<sizeof...(Args) - 1>(tuple);
	}

	// Every not-yet-implemented bool slot routes here. The last argument is the Error out-pointer and the one before it is reset if writable.
	template <typename... Args>
	bool D3D12Unimplemented([[maybe_unused]] void * impl, Args... args) noexcept
	{
		static_assert(sizeof...(Args) > 0);
		auto tuple = std::forward_as_tuple(args...);
		if constexpr (sizeof...(Args) > 1)
		{
			auto && value = std::get<sizeof...(Args) - 2>(tuple);
			using Value	  = std::remove_reference_t<decltype(value)>;
			if constexpr (std::is_pointer_v<Value> && !std::is_void_v<std::remove_pointer_t<Value>> && !std::is_const_v<std::remove_pointer_t<Value>>)
			{
				if (value != nullptr)
				{
					*value = {};
				}
			}
		}
		return Fail(LastError(args...), ErrorCode::eUnsupportedFeature, "D3D12 RHI backend: operation not implemented yet");
	}

	template <typename T, typename... Args>
	T D3D12UnimplementedValue([[maybe_unused]] void * impl, Args... args) noexcept
	{
		return FailValue<T>(LastError(args...), ErrorCode::eUnsupportedFeature, "D3D12 RHI backend: operation not implemented yet");
	}

	// Descriptor-handle math, defined with the descriptor slice but used earlier by clearBuffer.
	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;

	// The feature levels this backend probes, highest first. The 12_0 floor is this backend's own choice, not the API's: D3D12 itself creates devices down to
	// feature level 11_0.
	constexpr std::array kProbeLevels{ D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };

	/*
	 * The D3D12 standard tile size is a fixed 64 KiB for buffers and for every non-packed texture tile (D3D12_TILED_RESOURCES_TILE_SIZE_IN_BYTES). Sparse binds
	 * translate byte offsets and sizes into tile counts against this granularity. Shared because the device slice reports it through DeviceCaps and the queue
	 * slice divides by it.
	 */
	constexpr std::uint64_t kD3D12TileSizeBytes = 65536;

	// How many descriptors the shared shader-visible resource heap holds. Named here, not left at the create site because maxDescriptorsPerSet reports it, and a
	// set addressing more than the heap holds is a set that cannot be bound.
	constexpr std::uint32_t kD3D12GlobalResourceCapacity = 65536;

	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const PipelineCacheApi & PipelineCacheBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const SparseApi & SparseBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const AliasingCommandApi & AliasingCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const IndirectCountApi & IndirectCountBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;

	[[nodiscard]] detail::HostString NarrowAdapterName(const wchar_t * wide);
	void NameD3D12Object(ID3D12Object * object, CString name, bool enabled) noexcept;
	[[nodiscard]] ApiVersion FeatureLevelToApiVersion(D3D_FEATURE_LEVEL level) noexcept;
	[[nodiscard]] SparseTier SparseTierFromTiledResourcesTier(D3D12_TILED_RESOURCES_TIER tier) noexcept;
	[[nodiscard]] ConservativeRasterTier ConservativeRasterTierFrom(D3D12_CONSERVATIVE_RASTERIZATION_TIER tier) noexcept;
	[[nodiscard]] BindingTier BindingTierFromResourceBindingTier(D3D12_RESOURCE_BINDING_TIER tier) noexcept;
	[[nodiscard]] D3D_FEATURE_LEVEL ApiVersionToFloor(ApiVersion requested) noexcept;
	[[nodiscard]] D3D12BackendOwner & Owner();
	[[nodiscard]] BufferSlot * ResolveBuffer(D3D12Device * device, BufferHandle handle) noexcept;
	[[nodiscard]] D3D12_HEAP_TYPE MapHeapType(MemoryUsage memory, bool & hostVisible) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES InitialBufferState(D3D12_HEAP_TYPE heap) noexcept;
	[[nodiscard]] D3D12_RESOURCE_FLAGS MapBufferResourceFlags(Flags<BufferUsage> usage) noexcept;
	[[nodiscard]] bool BoundBufferRange(std::uint64_t bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept;
	BufferHandle D3D12CreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept;
	MappedMemory D3D12Map(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept;
	bool D3D12Unmap(void * impl, BufferHandle handle, Error * error) noexcept;
	bool D3D12FlushMappedRange(
		void * impl, BufferHandle handle, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] std::uint64_t size, Error * error) noexcept;
	bool D3D12InvalidateMappedRange(
		void * impl, BufferHandle handle, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] std::uint64_t size, Error * error) noexcept;
	bool D3D12GetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool D3D12DestroyBuffer(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] DXGI_FORMAT MapFormat(Format format) noexcept;
	[[nodiscard]] UINT MapSampleCount(SampleCount samples) noexcept;
	[[nodiscard]] D3D12_RESOURCE_DIMENSION MapResourceDimension(TextureType type) noexcept;
	[[nodiscard]] D3D12_RESOURCE_FLAGS MapTextureResourceFlags(Flags<TextureUsage> usage) noexcept;
	[[nodiscard]] DXGI_FORMAT DepthTypelessResourceFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] DXGI_FORMAT TypelessResourceFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] D3D12_RESOURCE_DESC MakeTextureResourceDesc(const TextureDesc & desc) noexcept;
	TextureHandle D3D12CreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept;
	bool D3D12GetTextureInfo(void * impl, TextureHandle texture, TextureInfo * out, Error * error) noexcept;
	bool D3D12GetBufferInfo(void * impl, BufferHandle buffer, BufferInfo * out, Error * error) noexcept;
	bool D3D12GetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept;
	bool D3D12DestroyTexture(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] TextureSlot * ResolveTexture(D3D12Device * device, TextureHandle handle) noexcept;
	void FillRtvDesc(D3D12_RENDER_TARGET_VIEW_DESC & rtv, DXGI_FORMAT format, const TextureViewDesc & desc) noexcept;
	void FillDsvDesc(D3D12_DEPTH_STENCIL_VIEW_DESC & dsv, DXGI_FORMAT format, const TextureViewDesc & desc) noexcept;
	TextureViewHandle D3D12CreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept;
	bool D3D12DestroyTextureView(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE MapAddressMode(AddressMode mode) noexcept;
	[[nodiscard]] D3D12_COMPARISON_FUNC MapCompareOp(CompareOp op) noexcept;
	void FillBorderColor(float (&out)[4], BorderColor color) noexcept;
	[[nodiscard]] D3D12_FILTER MapFilter(const SamplerDesc & desc) noexcept;
	SamplerHandle D3D12CreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept;
	bool D3D12DestroySampler(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] D3D12_HEAP_TYPE MapHeapClass(HeapType type) noexcept;
	[[nodiscard]] HeapSlot * ResolveHeap(D3D12Device * device, HeapHandle handle) noexcept;
	HeapHandle D3D12CreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept;
	BufferHandle D3D12CreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept;
	TextureHandle D3D12CreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept;
	bool D3D12DestroyHeap(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] D3D12_DESCRIPTOR_RANGE_TYPE MapRangeType(DescriptorType type) noexcept;
	[[nodiscard]] bool IsSamplerDescriptor(DescriptorType type) noexcept;
	[[nodiscard]] bool UsesSamplerHeap(DescriptorType type) noexcept;
	[[nodiscard]] bool UsesResourceHeap(DescriptorType type) noexcept;
	[[nodiscard]] D3D12_SHADER_VISIBILITY MapShaderVisibility(Flags<ShaderStage> stages) noexcept;
	DescriptorSetLayoutHandle D3D12CreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept;
	[[nodiscard]] DescriptorSetLayoutSlot * ResolveDescriptorSetLayout(D3D12Device * device, DescriptorSetLayoutHandle handle) noexcept;
	PipelineLayoutHandle D3D12CreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept;
	bool D3D12DestroyDescriptorSetLayout(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyPipelineLayout(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] bool DestroyByType(D3D12Device * device, ResourceType type, RawHandle handle, Error * error) noexcept;
	bool D3D12Destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept;
	[[nodiscard]] AdapterType ClassifyAdapter(const DXGI_ADAPTER_DESC3 & desc, bool unifiedMemory) noexcept;
	void FillAdapterInfo(AdapterInfo & info, const DXGI_ADAPTER_DESC3 & desc, std::uint32_t index, bool unifiedMemory) noexcept;
	[[nodiscard]] D3D12DriverVersion QueryDriverVersion(IDXGIAdapter * adapter) noexcept;
	void NegotiateCaps(D3D12Device & dev, D3D_FEATURE_LEVEL level) noexcept;
	void DestroyDeviceObject(D3D12Device * device) noexcept;
	GraphicsApiId D3D12DeviceApiId([[maybe_unused]] void * impl) noexcept;
	std::string_view D3D12DeviceApiName([[maybe_unused]] void * impl) noexcept;
	const DeviceCaps & D3D12DeviceCaps(void * impl) noexcept;
	const AdapterInfo & D3D12DeviceAdapterInfo(void * impl) noexcept;
	ValidationMessageCounts D3D12DeviceValidationMessageCounts(void * impl) noexcept;
	FormatSupport D3D12DeviceFormatSupport(void * impl, Format format) noexcept;
	void * D3D12GetQueue(void * impl, QueueType type, std::uint32_t index, Error * error) noexcept;
	void D3D12DestroyDevice(void * impl) noexcept;
	QueueType D3D12QueueType(void * impl) noexcept;
	std::uint32_t D3D12QueueFamilyIndex([[maybe_unused]] void * impl) noexcept;
	GraphicsApiId D3D12InstanceApiId([[maybe_unused]] void * impl) noexcept;
	bool D3D12EnumerateAdapters(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept;
	bool D3D12QueryExternalHandleSupport(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept;
	bool D3D12ExportBuffer(void * impl, BufferHandle buffer, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool D3D12ExportHeap(void * impl, HeapHandle heap, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool D3D12ExportTexture(void * impl, TextureHandle texture, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool D3D12ExportTimeline(void * impl, TimelineHandle timeline, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	bool D3D12ExportBinarySemaphore(void * impl, BinarySemaphoreHandle semaphore, ExternalHandleType type, ExternalHandle * out, Error * error) noexcept;
	BufferHandle D3D12ImportBuffer(void * impl, const ExternalBufferImportDesc & desc, Error * error) noexcept;
	HeapHandle D3D12ImportHeap(void * impl, const ExternalHeapImportDesc & desc, Error * error) noexcept;
	TextureHandle D3D12ImportTexture(void * impl, const ExternalTextureImportDesc & desc, Error * error) noexcept;
	TimelineHandle D3D12ImportTimeline(void * impl, const ExternalTimelineImportDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle D3D12ImportBinarySemaphore(void * impl, const ExternalBinarySemaphoreImportDesc & desc, Error * error) noexcept;
	bool D3D12CloseExportedHandle(void * impl, const ExternalHandle & handle, Error * error) noexcept;
	const ExternalSharingApi & ExternalSharingBlock() noexcept;

	[[nodiscard]] bool D3D12RefuseUnexportable(
		Flags<ExternalHandleType> declared, Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept;
	[[nodiscard]] HostUniquePtr<D3D12Instance> BuildInstance(const InstanceDesc & desc, Error * error);
	[[nodiscard]] ComPtr<ID3D12CommandQueue> CreateQueue(ID3D12Device * device, D3D12_COMMAND_LIST_TYPE type);
	[[nodiscard]] D3D12Device * MakeOwnedDevice(D3D12Instance * instance, HostUniquePtr<D3D12Instance> ownedInstance, const DeviceDesc & desc, Error * error);
	void * D3D12InstanceCreateDevice(void * impl, const DeviceDesc & desc, Error * error) noexcept;
	void * D3D12CreateInstance(const void * instanceDesc, Error * error) noexcept;
	void D3D12DestroyInstance(void * impl) noexcept;
	[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE MapPrimitiveTopologyType(PrimitiveTopology topology) noexcept;
	[[nodiscard]] D3D_PRIMITIVE_TOPOLOGY MapPrimitiveTopology(PrimitiveTopology topology) noexcept;
	[[nodiscard]] D3D12_FILL_MODE MapFillMode(FillMode mode) noexcept;
	[[nodiscard]] D3D12_CULL_MODE MapCullMode(CullMode mode) noexcept;
	[[nodiscard]] D3D12_STENCIL_OP MapStencilOp(StencilOp op) noexcept;
	[[nodiscard]] D3D12_DEPTH_STENCILOP_DESC MapStencilFace(const StencilFaceDesc & face) noexcept;
	[[nodiscard]] D3D12_BLEND MapBlendFactor(BlendFactor factor) noexcept;
	[[nodiscard]] D3D12_BLEND ToAlphaBlendFactor(D3D12_BLEND factor) noexcept;
	[[nodiscard]] D3D12_BLEND_OP MapBlendOp(BlendOp op) noexcept;
	[[nodiscard]] UINT8 MapColorWriteMask(Flags<ColorWrite> mask) noexcept;
	[[nodiscard]] PipelineLayoutSlot * ResolvePipelineLayout(D3D12Device * device, PipelineLayoutHandle handle) noexcept;
	[[nodiscard]] GraphicsPipelineSlot * ResolveGraphicsPipeline(D3D12Device * device, GraphicsPipelineHandle handle) noexcept;
	[[nodiscard]] ComputePipelineSlot * ResolveComputePipeline(D3D12Device * device, ComputePipelineHandle handle) noexcept;
	[[nodiscard]] ID3D12PipelineLibrary * ResolvePipelineLibrary(D3D12Device * device, PipelineCacheHandle handle) noexcept;
	void BuildInputElements(const VertexInputDesc & vertexInput, const char * semanticName, detail::HostVector<D3D12_INPUT_ELEMENT_DESC> & out);
	GraphicsPipelineHandle D3D12CreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept;
	ComputePipelineHandle D3D12CreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept;
	PipelineCacheHandle D3D12CreatePipelineCache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept;
	bool D3D12GetPipelineCacheData(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept;
	bool D3D12DestroyGraphicsPipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyComputePipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyPipelineCache(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] D3D12_COMMAND_LIST_TYPE MapCommandListType(QueueType type) noexcept;
	[[nodiscard]] DWORD WaitFenceHost(ID3D12Fence * fence, std::uint64_t value, std::uint64_t timeoutNanoseconds) noexcept;
	[[nodiscard]] TimelineSlot * ResolveTimeline(D3D12Device * device, TimelineHandle handle) noexcept;
	[[nodiscard]] BinarySemaphoreSlot * ResolveBinarySemaphore(D3D12Device * device, BinarySemaphoreHandle handle) noexcept;
	TimelineHandle D3D12CreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept;
	bool D3D12DestroyTimeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	BinarySemaphoreHandle D3D12CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & /*desc*/, Error * error) noexcept;
	bool D3D12DestroyBinarySemaphore(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	void * D3D12CreateCommandPool(void * impl, const CommandPoolDesc & desc, Error * error) noexcept;
	void * D3D12CommandPoolAllocate(void * impl, CString debugName, Error * error) noexcept;
	bool D3D12CommandPoolReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	bool D3D12CommandListBegin(void * impl, Error * error) noexcept;
	bool D3D12CommandListEnd(void * impl, Error * error) noexcept;
	bool D3D12CmdCopyBuffer(
		void * impl, BufferHandle dst, std::uint64_t dstOffset, BufferHandle src, std::uint64_t srcOffset, std::uint64_t size, Error * error) noexcept;
	bool D3D12QueueSubmit(void * impl, const SubmitDesc & desc, Error * error) noexcept;
	bool BindSparseBuffer(D3D12Device * device, D3D12Queue * queue, const SparseBufferBind & bind, bool validate, Error * error) noexcept;
	bool BindSparseTexture(D3D12Device * device, D3D12Queue * queue, const SparseTextureBind & bind, bool validate, Error * error) noexcept;
	bool D3D12QueueBindSparse(void * impl, const SparseBindDesc & desc, Error * error) noexcept;
	bool D3D12QueueWaitIdle(void * impl, Error * error) noexcept;
	bool D3D12QueueGetCompletedValue(void * impl, TimelineHandle timeline, std::uint64_t * out, Error * error) noexcept;
	bool D3D12QueueWait(void * impl, TimelineHandle timeline, std::uint64_t value, std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	bool D3D12QueueSignal(void * impl, TimelineHandle timeline, std::uint64_t value, Error * error) noexcept;
	[[nodiscard]] TextureViewSlot * ResolveTextureView(D3D12Device * device, TextureViewHandle handle) noexcept;
	[[nodiscard]] QueryPoolSlot * ResolveQueryPool(D3D12Device * device, QueryPoolHandle handle) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES MapTextureStates(TextureLayout layout) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES MapBufferStates(Flags<Access> access) noexcept;
	[[nodiscard]] D3D12_QUERY_TYPE MapQueryType(QueryType type) noexcept;
	[[nodiscard]] D3D12_QUERY_HEAP_TYPE MapQueryHeapType(QueryType type) noexcept;
	[[nodiscard]] UINT SubresourceIndex(const TextureSubresource & sub, std::uint32_t mipLevels) noexcept;
	[[nodiscard]] ID3D12CommandSignature * GetCommandSignature(D3D12CommandList * list, D3D12_INDIRECT_ARGUMENT_TYPE type, std::uint32_t stride) noexcept;
	QueryPoolHandle D3D12CreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept;
	bool D3D12DestroyQueryPool(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12CmdBarriers(void * impl, const BarrierBatch & barriers, Error * error) noexcept;
	bool D3D12CmdAliasBarriers(void * impl, std::span<const AliasBarrier> barriers, Error * error) noexcept;
	bool D3D12CmdBeginRendering(void * impl, const BeginRenderingDesc & desc, Error * error) noexcept;
	bool D3D12CmdEndRendering(void * impl, Error * error) noexcept;
	bool D3D12CmdSetGraphicsPipeline(void * impl, GraphicsPipelineHandle pipeline, Error * error) noexcept;
	bool D3D12CmdSetComputePipeline(void * impl, ComputePipelineHandle pipeline, Error * error) noexcept;
	bool D3D12CmdPushConstants(void * impl, PipelineLayoutHandle layout, Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size, const void * data,
		Error * error) noexcept;
	bool D3D12CmdSetViewport(void * impl, const Viewport & viewport, Error * error) noexcept;
	bool D3D12CmdSetScissor(void * impl, const Rect2D & scissor, Error * error) noexcept;
	bool D3D12CmdSetBlendConstants(void * impl, float r, float g, float b, float a, Error * error) noexcept;
	bool D3D12CmdSetStencilReference(void * impl, std::uint32_t reference, Error * error) noexcept;
	bool D3D12CmdSetDepthBias(void * impl, float constantFactor, float clamp, float slopeFactor, Error * error) noexcept;
	bool D3D12CmdSetVertexBuffer(void * impl, std::uint32_t slot, BufferHandle buffer, std::uint64_t offset, Error * error) noexcept;
	bool D3D12CmdSetIndexBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, bool index32, Error * error) noexcept;
	void FlushPendingDescriptorSets(D3D12CommandList * list) noexcept;
	bool D3D12CmdDraw(
		void * impl, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance, Error * error) noexcept;
	bool D3D12CmdDrawIndexed(void * impl, std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
		std::uint32_t firstInstance, Error * error) noexcept;
	bool D3D12ExecuteIndirect(D3D12CommandList * list, D3D12_INDIRECT_ARGUMENT_TYPE type, std::uint32_t stride, BufferHandle args, std::uint64_t argsOffset,
		std::uint32_t maxCount, BufferHandle count, std::uint64_t countOffset, bool hasCount, Error * error) noexcept;
	bool D3D12CmdDrawIndirect(void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool D3D12CmdDrawIndexedIndirect(
		void * impl, BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride, Error * error) noexcept;
	bool D3D12CmdDrawIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	bool D3D12CmdDrawIndexedIndirectCount(void * impl, BufferHandle args, std::uint64_t argsOffset, BufferHandle count, std::uint64_t countOffset,
		std::uint32_t maxDrawCount, std::uint32_t stride, Error * error) noexcept;
	bool D3D12CmdDispatch(void * impl, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ, Error * error) noexcept;
	bool D3D12CmdDispatchIndirect(void * impl, BufferHandle args, std::uint64_t offset, Error * error) noexcept;
	[[nodiscard]] ID3D12Resource * AllocateCopyScratch(D3D12Device * device, D3D12CommandList * list, std::uint64_t bytes) noexcept;
	[[nodiscard]] D3D12_RESOURCE_DESC RegionFootprintDesc(const D3D12_RESOURCE_DESC & texDesc, const Extent3D & extent) noexcept;
	[[nodiscard]] D3D12_BOX RegionSourceBox(const Offset3D & offset, const Extent3D & extent) noexcept;
	[[nodiscard]] bool RegionIsEmpty(const Extent3D & extent) noexcept;
	bool D3D12CmdCopyBufferToTexture(void * impl, TextureHandle dst, BufferHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool D3D12CmdCopyTextureToBuffer(void * impl, BufferHandle dst, TextureHandle src, std::span<const BufferTextureCopy> regions, Error * error) noexcept;
	bool D3D12CmdCopyTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureCopy> regions, Error * error) noexcept;
	[[nodiscard]] bool IsDepthDxgiFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] bool IsBlockCompressedDxgiFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] bool IsIntegerDxgiFormat(DXGI_FORMAT format) noexcept;
	bool D3D12CmdClearBuffer(void * impl, BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value, Error * error) noexcept;
	bool D3D12CmdClearTexture(
		void * impl, TextureHandle texture, const ClearColor & color, std::span<const TextureSubresourceRange> ranges, Error * error) noexcept;
	bool D3D12CmdResolveTexture(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureResolve> regions, Error * error) noexcept;
	bool D3D12CmdBlit(void * impl, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error * error) noexcept;
	bool D3D12CmdGenerateMips(void * impl, TextureHandle texture, Error * error) noexcept;
	bool D3D12CmdResetQueryPool(
		void * impl, QueryPoolHandle pool, [[maybe_unused]] std::uint32_t firstQuery, [[maybe_unused]] std::uint32_t queryCount, Error * error) noexcept;
	bool D3D12CmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, [[maybe_unused]] Flags<PipelineStage> stage, Error * error) noexcept;
	bool D3D12CmdBeginQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool D3D12CmdEndQuery(void * impl, QueryPoolHandle pool, std::uint32_t query, Error * error) noexcept;
	bool D3D12CmdResolveQueryData(void * impl, QueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, BufferHandle dst,
		std::uint64_t dstOffset, Error * error) noexcept;
	[[nodiscard]] UINT PixColor(std::uint32_t rgba) noexcept;
	bool D3D12CmdBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept;
	bool D3D12CmdEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept;
	bool D3D12QueueBeginDebugLabel([[maybe_unused]] void * impl, [[maybe_unused]] CString name, [[maybe_unused]] std::uint32_t color, Error * error) noexcept;
	bool D3D12QueueEndDebugLabel([[maybe_unused]] void * impl, Error * error) noexcept;
	bool D3D12CmdBeginNativeMutation(void * impl, GraphicsApiId api, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	bool D3D12CmdEndNativeMutation(void * impl, [[maybe_unused]] const NativeMutationDesc & desc, Error * error) noexcept;
	[[nodiscard]] SamplerSlot * ResolveSampler(D3D12Device * device, SamplerHandle handle) noexcept;
	[[nodiscard]] DescriptorSetSlot * ResolveDescriptorSet(D3D12Device * device, DescriptorSetHandle handle) noexcept;
	[[nodiscard]] std::uint32_t BindingOffsetInClass(const detail::HostVector<DescriptorBinding> & bindings, std::uint32_t binding, bool wantSampler) noexcept;
	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;
	[[nodiscard]] D3D12_SRV_DIMENSION MapSrvDimension(TextureViewType type) noexcept;
	[[nodiscard]] D3D12_UAV_DIMENSION MapUavDimension(TextureViewType type) noexcept;
	void * D3D12CreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept;
	DescriptorSetHandle D3D12DescriptorArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept;
	bool D3D12DescriptorArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept;
	bool D3D12DestroyDescriptorSet(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	[[nodiscard]] DescriptorType LayoutBufferType(
		const detail::HostVector<DescriptorBinding> & bindings, std::uint32_t binding, DescriptorType fallback) noexcept;
	bool D3D12UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept;
	bool D3D12UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept;
	bool D3D12UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept;
	bool D3D12CmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept;
	[[nodiscard]] DXGI_FORMAT StripSrgbFormat(DXGI_FORMAT format) noexcept;
	[[nodiscard]] SwapchainStatus MapPresentStatus(HRESULT hr) noexcept;
	bool BuildSwapchainBackBuffers(D3D12Swapchain * sc, Error * error) noexcept;
	void * D3D12CreateSwapchain(void * impl, const SwapchainDesc & desc, Error * error) noexcept;
	AcquireResult D3D12SwapchainAcquire(void * impl, [[maybe_unused]] std::uint64_t timeoutNanoseconds, Error * error) noexcept;
	PresentResult D3D12SwapchainPresent(void * impl, [[maybe_unused]] std::uint32_t imageIndex, [[maybe_unused]] BinarySemaphoreHandle renderFinished,
		[[maybe_unused]] void * queueImpl, Error * error) noexcept;
	bool D3D12SwapchainSupportsReadback([[maybe_unused]] void * impl) noexcept;
	bool D3D12SwapchainResize(void * impl, std::uint32_t width, std::uint32_t height, Error * error) noexcept;
	bool D3D12SwapchainSetPresentMode(void * impl, PresentMode mode, Error * error) noexcept;
	TextureHandle D3D12SwapchainGetBackBuffer(void * impl, std::uint32_t imageIndex) noexcept;
	TextureViewHandle D3D12SwapchainGetBackBufferView(void * impl, std::uint32_t imageIndex) noexcept;
	BinarySemaphoreHandle D3D12SwapchainGetPresentSemaphore([[maybe_unused]] void * impl, [[maybe_unused]] std::uint32_t imageIndex) noexcept;
	Format D3D12SwapchainGetFormat(void * impl) noexcept;
	PresentMode D3D12SwapchainGetPresentMode(void * impl) noexcept;
	std::uint32_t D3D12SwapchainGetImageCount(void * impl) noexcept;
	std::uint32_t D3D12SwapchainGetWidth(void * impl) noexcept;
	std::uint32_t D3D12SwapchainGetHeight(void * impl) noexcept;
	bool D3D12CalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept;
	bool D3D12QueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept;
	[[nodiscard]] D3D12_RESIDENCY_PRIORITY MapResidencyPriority(ResidencyPriority priority) noexcept;
	bool D3D12SetResidencyPriority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept;
	bool D3D12CollectGarbage(void * impl, ResourceType type, Error * error) noexcept;
	bool D3D12CollectGarbageTimeline(
		void * impl, ResourceType type, [[maybe_unused]] TimelineHandle timeline, [[maybe_unused]] std::uint64_t completedValue, Error * error) noexcept;
	BufferHandle D3D12AdoptBuffer(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBufferDesc & desc, Error * error) noexcept;
	TextureHandle D3D12AdoptTexture(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureDesc & desc, Error * error) noexcept;
	bool D3D12GetNativeBuffer(void * impl, GraphicsApiId api, BufferHandle buffer, void * outNativeImport, Error * error) noexcept;
	bool D3D12GetNativeTexture(void * impl, GraphicsApiId api, TextureHandle texture, void * outNativeImport, Error * error) noexcept;
	TextureViewHandle D3D12AdoptTextureView(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTextureViewDesc & desc, Error * error) noexcept;
	SamplerHandle D3D12AdoptSampler(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedSamplerDesc & desc, Error * error) noexcept;
	bool D3D12GetNativeTextureView(void * impl, GraphicsApiId api, TextureViewHandle view, void * outNativeImport, Error * error) noexcept;
	bool D3D12GetNativeSampler(void * impl, GraphicsApiId api, SamplerHandle sampler, void * outNativeImport, Error * error) noexcept;
	TimelineHandle D3D12AdoptTimeline(void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedTimelineDesc & desc, Error * error) noexcept;
	BinarySemaphoreHandle D3D12AdoptBinarySemaphore(
		void * impl, GraphicsApiId api, const void * nativeImport, const AdoptedBinarySemaphoreDesc & desc, Error * error) noexcept;
	bool D3D12GetNativeTimeline(void * impl, GraphicsApiId api, TimelineHandle timeline, void * outNativeImport, Error * error) noexcept;
	bool D3D12GetNativeBinarySemaphore(void * impl, GraphicsApiId api, BinarySemaphoreHandle semaphore, void * outNativeImport, Error * error) noexcept;
	const CoreDeviceApi & CoreDeviceBlock() noexcept;
	const PresentApi & PresentBlock() noexcept;
	const PlacedMemoryApi & PlacedMemoryBlock() noexcept;
	const ResourceIntrospectionApi & ResourceIntrospectionBlock() noexcept;
	const QueryApi & QueryBlock() noexcept;
	const PipelineCacheApi & PipelineCacheBlock() noexcept;
	const ResidencyApi & ResidencyBlock() noexcept;
	const AdoptionApi & AdoptionBlock() noexcept;
	const InstanceApi & InstanceBlock() noexcept;
	const ExternalCapabilityApi & ExternalCapabilityBlock() noexcept;
	const QueueApi & QueueBlock() noexcept;
	const SparseApi & SparseBlock() noexcept;
	const CommandPoolApi & CommandPoolBlock() noexcept;
	const RenderCommandApi & RenderCommandBlock() noexcept;
	const AliasingCommandApi & AliasingCommandBlock() noexcept;
	const QueryCommandApi & QueryCommandBlock() noexcept;
	const IndirectApi & IndirectBlock() noexcept;
	const IndirectCountApi & IndirectCountBlock() noexcept;
	const NativeEscapeApi & NativeEscapeBlock() noexcept;
	const SwapchainApi & SwapchainBlock() noexcept;
	const DescriptorArenaApi & DescriptorArenaBlock() noexcept;

} // namespace azo::rhi::d3d12

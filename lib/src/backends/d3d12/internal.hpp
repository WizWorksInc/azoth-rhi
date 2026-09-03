// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/format_info.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/scope_guard.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/d3d12_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include "backends/d3d12/barrier_tables.hpp"
#include "backends/registration.hpp"
#include "support/driver_version.hpp"

#ifndef NOMINMAX
	#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <D3D12MemAlloc.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#if defined(AZOTH_RHI_ENABLE_PIX)
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

		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;
		detail::HostVector<detail::HostString> adapterNames;
		detail::HostVector<detail::HostString> driverVersions;
	};

	struct D3D12Device;

	struct BufferSlot final
	{
		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		std::uint64_t size = 0;
		bool hostVisible   = false;

		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		bool reserved = false;

		Flags<ExternalHandleType> exportableHandleTypes;

		BufferDesc desc{};
	};

	struct TextureSlot final
	{
		ComPtr<D3D12MA::Allocation> allocation;
		ComPtr<ID3D12Resource> resource;
		DXGI_FORMAT format		  = DXGI_FORMAT_UNKNOWN;
		Format rhiFormat		  = Format::eUndefined;
		TextureType type		  = TextureType::eTex2D;
		std::uint32_t mipLevels	  = 1;
		std::uint32_t arrayLayers = 1;
		Flags<TextureUsage> usage;

		bool mutableFormat = false;

		SlotLifetime lifetime = SlotLifetime::eOwned;

		bool reserved = false;

		Flags<ExternalHandleType> exportableHandleTypes;

		TextureDesc desc{};
	};

	struct TextureViewSlot final
	{
		TextureHandle texture{};
		DXGI_FORMAT format	 = DXGI_FORMAT_UNKNOWN;
		TextureViewType type = TextureViewType::eTex2D;
		TextureSubresourceRange range{};
		std::uint32_t rtvIndex = kInvalidIndex;
		std::uint32_t dsvIndex = kInvalidIndex;

		UINT planeSlice = 0;

		UINT shaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct CpuDescriptorHeap final
	{
		ComPtr<ID3D12DescriptorHeap> heap;
		D3D12_CPU_DESCRIPTOR_HANDLE base{};
		std::uint32_t increment = 0;
		std::uint32_t capacity	= 0;
		std::uint32_t next		= 0;
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

	struct SamplerSlot final
	{
		D3D12_SAMPLER_DESC desc{};
	};

	struct HeapSlot final
	{
		ComPtr<ID3D12Heap> heap;
		D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT;
		std::uint64_t size	 = 0;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct DescriptorSetLayoutSlot final
	{
		detail::HostVector<DescriptorBinding> bindings;
		std::uint32_t cbvSrvUavCount = 0;
		std::uint32_t samplerCount	 = 0;
	};

	struct PipelineLayoutSlot final
	{
		struct SetParams final
		{
			std::uint32_t resourceParam = kInvalidIndex;
			std::uint32_t samplerParam	= kInvalidIndex;
		};

		struct PushConstantParam final
		{
			std::uint32_t rootParam = kInvalidIndex;
			std::uint32_t offset	= 0;
			std::uint32_t size		= 0;
		};

		ComPtr<ID3D12RootSignature> rootSignature;
		detail::HostVector<SetParams> setParams;
		detail::HostVector<PushConstantParam> pushConstantParams;

		detail::HostVector<DescriptorSetLayoutHandle> sets;
	};

	struct GraphicsPipelineSlot final
	{
		ComPtr<ID3D12PipelineState> pipeline;
		ComPtr<ID3D12RootSignature> rootSignature;
		D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		std::array<std::uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};

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

	struct PipelineCacheSlot final
	{
		ComPtr<ID3D12PipelineLibrary> library;
		detail::HostVector<std::uint8_t> data;
	};

	struct QueryPoolSlot final
	{
		ComPtr<ID3D12QueryHeap> heap;

		// A copy command list cannot emit into a regular timestamp heap, and the portable pool is not told which queue will use it, so a device that supports
		// copy-queue timestamps gets the second heap alongside the first and the recording picks by list type.
		ComPtr<ID3D12QueryHeap> copyHeap;

		QueryType type			 = QueryType::eTimestamp;
		std::uint32_t queryCount = 0;
	};

	struct D3D12DescriptorArena;

	struct DescriptorSetSlot final
	{
		D3D12DescriptorArena * arena = nullptr;
		std::uint32_t resourceBase	 = 0;
		std::uint32_t resourceCount	 = 0;
		std::uint32_t samplerBase	 = 0;
		std::uint32_t samplerCount	 = 0;
		detail::HostVector<DescriptorBinding> bindings;
	};

	struct TimelineSlot final
	{
		ComPtr<ID3D12Fence> fence;

		Flags<ExternalHandleType> exportableHandleTypes;

		SlotLifetime lifetime = SlotLifetime::eOwned;
	};

	struct BinarySemaphoreSlot final
	{
		ComPtr<ID3D12Fence> fence;
		std::uint64_t signalValue = 0;
		std::uint64_t waitValue	  = 0;

		Flags<ExternalHandleType> exportableHandleTypes;
	};

	struct D3D12Queue final
	{
		const BackendObject * object = nullptr;
		ComPtr<ID3D12CommandQueue> queue;
		QueueType type		= QueueType::eGraphics;
		D3D12Device * owner = nullptr;

		ComPtr<ID3D12Fence> idleFence;
		std::uint64_t idleValue = 0;
	};

	struct CommandSignatureEntry final
	{
		D3D12_INDIRECT_ARGUMENT_TYPE type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
		std::uint32_t stride			  = 0;
		ComPtr<ID3D12CommandSignature> signature;
	};

	struct D3D12CommandList;

	struct D3D12CommandPool final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		ComPtr<ID3D12CommandAllocator> allocator;
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueType queueType			 = QueueType::eGraphics;

		detail::HostVector<CommandSignatureEntry> commandSignatures;

		detail::HostVector<D3D12CommandList *> lists;
		std::size_t handedOut = 0;
	};

	struct D3D12CommandList final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12GraphicsCommandList7> list7;
		ID3D12CommandAllocator * allocator = nullptr;
		D3D12CommandPool * pool			   = nullptr;
		D3D12_COMMAND_LIST_TYPE type	   = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueType queueType				   = QueueType::eGraphics;

		std::array<std::uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};

		ID3D12QueryHeap * pendingEndTimestampHeap = nullptr;
		std::uint32_t pendingEndTimestampQuery	  = 0;

		bool computePipelineBound				 = false;
		ID3D12DescriptorHeap * boundResourceHeap = nullptr;
		ID3D12DescriptorHeap * boundSamplerHeap	 = nullptr;

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

		detail::HostVector<std::uint32_t> transientRtvs;
		detail::HostVector<std::uint32_t> transientDsvs;

		ComPtr<ID3D12DescriptorHeap> clearGpuHeap;
		ComPtr<ID3D12DescriptorHeap> clearStagingHeap;
		std::uint32_t clearHeapIncrement = 0;
		std::uint32_t clearHeapCapacity	 = 0;
		std::uint32_t clearHeapNext		 = 0;
		detail::HostVector<ComPtr<ID3D12DescriptorHeap>> retiredClearHeaps;

		detail::HostVector<ID3D12Resource *> pendingDiscards;

		detail::HostVector<ComPtr<ID3D12Resource>> retiredCopyScratch;
		detail::HostVector<ComPtr<D3D12MA::Allocation>> retiredCopyAllocs;
	};

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

	struct D3D12Swapchain final
	{
		const BackendObject * object = nullptr;
		D3D12Device * owner			 = nullptr;
		HWND hwnd					 = nullptr;
		ComPtr<IDXGISwapChain3> swapchain;
		Format format				= Format::eBGRA8Srgb;
		DXGI_FORMAT viewFormat		= DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		DXGI_FORMAT swapchainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
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
		ComPtr<IDXGIFactory6> factory;
		ComPtr<IDXGIAdapter4> adapter;
		ComPtr<ID3D12Device> device;
		ComPtr<D3D12MA::Allocator> allocator;

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

		std::atomic<std::uint64_t> pendingRetire{ 0 };

		ValidationMode validation = ValidationMode::eReleaseLight;
		bool debugNames			  = true;
		bool debugLabels		  = true;
		bool copyQueueTimestamps  = false;
		std::uint32_t deviceTag	  = 0;

		DeviceCaps caps{};
		AdapterInfo adapterInfo{};
		detail::HostString adapterName;
		detail::HostString driverVersion;

		HostUniquePtr<D3D12Instance> ownedInstance;

		D3D12Instance * instanceWrapper = nullptr;

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

		detail::HostVector<HostUniquePtr<D3D12CommandPool>> commandPools;
		detail::HostVector<HostUniquePtr<D3D12CommandList>> commandLists;

		CpuDescriptorHeap rtvHeap;
		CpuDescriptorHeap dsvHeap;

		std::atomic<std::uint64_t> validationErrors{ 0 };
		std::atomic<std::uint64_t> validationWarnings{ 0 };

		ValidationMessageCallback onMessage = nullptr;
		void * messageUserData				= nullptr;

#ifdef __ID3D12InfoQueue1_INTERFACE_DEFINED__
		ComPtr<ID3D12InfoQueue1> infoQueue;
		DWORD infoQueueCookie = 0;
#endif

		~D3D12Device()
		{
#ifdef __ID3D12InfoQueue1_INTERFACE_DEFINED__
			if (infoQueue && infoQueueCookie != 0)
			{
				infoQueue->UnregisterMessageCallback(infoQueueCookie);
			}
#endif
		}
	};

	struct D3D12BackendOwner final
	{
		detail::HostVector<HostUniquePtr<D3D12Instance>> instances;
		detail::HostVector<HostUniquePtr<D3D12Device>> devices;
	};

	struct D3D12DriverVersion final
	{
		detail::HostString text;
		std::uint64_t raw = 0;
	};

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

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept;

	constexpr std::array kProbeLevels{ D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };

	constexpr std::uint64_t kD3D12TileSizeBytes = 65536;

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
	[[nodiscard]] D3D12_RESOURCE_STATES InitialBufferState(D3D12_HEAP_TYPE heap, Flags<BufferUsage> usage) noexcept;
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
	BinarySemaphoreHandle D3D12CreateBinarySemaphore(void * impl, const BinarySemaphoreDesc &, Error * error) noexcept;
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
	void BarrierScratchToSource(D3D12CommandList * list, ID3D12Resource * scratch) noexcept;
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
	bool D3D12CmdWriteTimestamp(void * impl, QueryPoolHandle pool, std::uint32_t query, [[maybe_unused]] Flags<Stage> stage, Error * error) noexcept;
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

}

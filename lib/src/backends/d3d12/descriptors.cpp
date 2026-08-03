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
	[[nodiscard]] SamplerSlot * ResolveSampler(D3D12Device * device, SamplerHandle handle) noexcept
	{
		return device->samplerSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] DescriptorSetSlot * ResolveDescriptorSet(D3D12Device * device, DescriptorSetHandle handle) noexcept
	{
		return device->descriptorSetSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	// A binding's offset within its heap class, summing earlier bindings of that class. kInvalidIndex when absent or of the other class.
	[[nodiscard]] std::uint32_t BindingOffsetInClass(const detail::HostVector<DescriptorBinding> & bindings, std::uint32_t binding, bool wantSampler) noexcept
	{
		std::uint32_t offset = 0;
		for (const DescriptorBinding & b : bindings)
		{
			// A combined binding occupies both heaps, so it counts toward whichever class is being asked about and not one of the two.
			if (!(wantSampler ? UsesSamplerHeap(b.type) : UsesResourceHeap(b.type)))
			{
				continue;
			}
			if (b.binding == binding)
			{
				return offset;
			}
			offset += b.count;
		}
		return kInvalidIndex;
	}

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept
	{
		return { heap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(index) * increment };
	}

	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(ID3D12DescriptorHeap * heap, std::uint32_t increment, std::uint32_t index) noexcept
	{
		return { heap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(index) * increment };
	}

	[[nodiscard]] D3D12_SRV_DIMENSION MapSrvDimension(TextureViewType type) noexcept
	{
		switch (type)
		{
		case TextureViewType::eTex1D:		 return D3D12_SRV_DIMENSION_TEXTURE1D;
		case TextureViewType::eTex3D:		 return D3D12_SRV_DIMENSION_TEXTURE3D;
		case TextureViewType::eTexCube:		 return D3D12_SRV_DIMENSION_TEXTURECUBE;
		case TextureViewType::eTex2DArray:	 return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		case TextureViewType::eTexCubeArray: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		case TextureViewType::eTex1DArray:	 return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
		case TextureViewType::eTex2D:		 return D3D12_SRV_DIMENSION_TEXTURE2D;
		}
		return D3D12_SRV_DIMENSION_TEXTURE2D;
	}

	// Cube UAVs have no dedicated dimension in D3D12. They are addressed as 2D arrays.
	[[nodiscard]] D3D12_UAV_DIMENSION MapUavDimension(TextureViewType type) noexcept
	{
		switch (type)
		{
		case TextureViewType::eTex1D:		 return D3D12_UAV_DIMENSION_TEXTURE1D;
		case TextureViewType::eTex1DArray:	 return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
		case TextureViewType::eTex3D:		 return D3D12_UAV_DIMENSION_TEXTURE3D;
		case TextureViewType::eTex2DArray:
		case TextureViewType::eTexCube:
		case TextureViewType::eTexCubeArray: return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		case TextureViewType::eTex2D:		 return D3D12_UAV_DIMENSION_TEXTURE2D;
		}
		return D3D12_UAV_DIMENSION_TEXTURE2D;
	}

	void * D3D12CreateDescriptorArena(void * impl, const DescriptorArenaDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createDescriptorArena");

		auto * device = static_cast<D3D12Device *>(impl);

		auto arena	  = HostNew<D3D12DescriptorArena>();
		arena->object = PublishingObject<Published<DescriptorArenaApi, &DescriptorArenaBlock>>();
		arena->owner  = device;

		// Share the device's global heaps, not own per-arena ones so a set's base is a global offset and one draw can bind sets from several arenas out of a single
		// bound heap. The maxDescriptors hint does not size anything.
		arena->resourceHeap		 = device->globalResourceHeap;
		arena->samplerHeap		 = device->globalSamplerHeap;
		arena->resourceStaging	 = device->globalResourceStaging;
		arena->samplerStaging	 = device->globalSamplerStaging;
		arena->resourceIncrement = device->globalResourceIncrement;
		arena->samplerIncrement	 = device->globalSamplerIncrement;
		arena->resourceCapacity	 = device->globalResourceCapacity;
		arena->samplerCapacity	 = device->globalSamplerCapacity;

		D3D12DescriptorArena * raw = arena.get();
		device->descriptorArenas.push_back(std::move(arena));
		Succeed(error);
		return raw;
	}

	DescriptorSetHandle D3D12DescriptorArenaAllocate(void * impl, const DescriptorSetAllocDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.descriptorArena.allocate");

		auto * arena		 = static_cast<D3D12DescriptorArena *>(impl);
		D3D12Device * device = arena->owner;

		DescriptorSetLayoutSlot * layout = ResolveDescriptorSetLayout(device, desc.layout);
		if (layout == nullptr)
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eInvalidHandle, "descriptor set allocation references an invalid layout");
		}

		// Apply the variable count to the trailing binding so the offsets and totals reflect the real allocation size.
		detail::HostVector<DescriptorBinding> bindings = layout->bindings;
		for (DescriptorBinding & binding : bindings)
		{
			if (binding.flags.Contains(DescriptorBindingFlag::eVariableDescriptorCount))
			{
				binding.count = desc.variableDescriptorCount;
			}
		}

		std::uint32_t resourceCount = 0;
		std::uint32_t samplerCount	= 0;
		for (const DescriptorBinding & binding : bindings)
		{
			if (UsesResourceHeap(binding.type))
			{
				resourceCount += binding.count;
			}
			if (UsesSamplerHeap(binding.type))
			{
				samplerCount += binding.count;
			}
		}

		// Carve the set's slots from the shared heaps so its base is a global offset the arena indexes directly. Arenas are persistent, not reset per frame so a
		// monotonic bump matches the real lifetime.
		if (device->globalResourceNext + resourceCount > device->globalResourceCapacity ||
			device->globalSamplerNext + samplerCount > device->globalSamplerCapacity)
		{
			return FailValue<DescriptorSetHandle>(error, ErrorCode::eOutOfMemory, "the shared D3D12 descriptor heap is out of space");
		}

		DescriptorSetSlot slot;
		slot.arena		   = arena;
		slot.resourceBase  = device->globalResourceNext;
		slot.resourceCount = resourceCount;
		slot.samplerBase   = device->globalSamplerNext;
		slot.samplerCount  = samplerCount;
		slot.bindings	   = std::move(bindings);
		device->globalResourceNext += resourceCount;
		device->globalSamplerNext += samplerCount;

		return ReturnValue(device->descriptorSetSlots.Store(std::move(slot)), error);
	}

	bool D3D12DescriptorArenaReset(void * impl, [[maybe_unused]] RetirePoint safeAfter, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.descriptorArena.reset");

		auto * arena		= static_cast<D3D12DescriptorArena *>(impl);
		arena->resourceNext = 0;
		arena->samplerNext	= 0;
		return Succeed(error);
	}

	bool D3D12DestroyDescriptorSet(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const DescriptorSetHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		DescriptorSetSlot * slot = device->descriptorSetSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid descriptor set handle");
		}
		slot->bindings.clear();
		slot->arena = nullptr;
		static_cast<void>(device->descriptorSetSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	/*
	 * The descriptor kind follows the set LAYOUT's declared type and not the write's, since the root-signature range type comes from the layout and the descriptor
	 * has to match it. Reflection separates a read-only StructuredBuffer from a read-write one, while a write may still pass the neutral eStorageBuffer that
	 * Vulkan and Metal take for both.
	 *
	 * Binding a read-only buffer as a UAV is a D3D12-only error. The layout's type is resolved here.
	 */
	[[nodiscard]] DescriptorType LayoutBufferType(
		const detail::HostVector<DescriptorBinding> & bindings, std::uint32_t binding, DescriptorType fallback) noexcept
	{
		for (const DescriptorBinding & b : bindings)
		{
			if (b.binding == binding)
			{
				return b.type;
			}
		}
		return fallback;
	}

	// Resolves a buffer write to its slot, creates the matching view in staging, then copies it into the shader-visible heap.
	bool D3D12UpdateDescriptorsBuffer(void * impl, std::span<const DescriptorWriteBuffer> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.updateDescriptorsBuffer");

		auto * device = static_cast<D3D12Device *>(impl);
		for (const DescriptorWriteBuffer & write : writes)
		{
			DescriptorSetSlot * set = ResolveDescriptorSet(device, write.set);
			BufferSlot * buffer		= ResolveBuffer(device, write.buffer);
			if (set == nullptr || buffer == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsBuffer with an invalid handle");
			}
			const std::uint32_t inClass = BindingOffsetInClass(set->bindings, write.binding, false);
			if (inClass == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "updateDescriptorsBuffer binding is not in the set");
			}
			D3D12DescriptorArena * arena		  = set->arena;
			const std::uint32_t index			  = set->resourceBase + inClass + write.arrayIndex;
			const D3D12_CPU_DESCRIPTOR_HANDLE cpu = CpuHandleAt(arena->resourceStaging.Get(), arena->resourceIncrement, index);
			const std::uint64_t range			  = write.range == std::numeric_limits<std::uint64_t>::max() ? buffer->size - write.offset : write.range;

			switch (LayoutBufferType(set->bindings, write.binding, write.type))
			{
			case DescriptorType::eUniformBuffer:
			case DescriptorType::eDynamicUniformBuffer:
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
				cbv.BufferLocation = buffer->resource->GetGPUVirtualAddress() + write.offset;
				cbv.SizeInBytes	   = static_cast<UINT>((range + 255) & ~static_cast<std::uint64_t>(255));
				device->device->CreateConstantBufferView(&cbv, cpu);
				break;
			}
			case DescriptorType::eBufferSRV:
			case DescriptorType::eTexelBufferSRV:
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format					= DXGI_FORMAT_R32_TYPELESS;
				srv.ViewDimension			= D3D12_SRV_DIMENSION_BUFFER;
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.Buffer.FirstElement		= write.offset / 4;
				srv.Buffer.NumElements		= static_cast<UINT>(range / 4);
				srv.Buffer.Flags			= D3D12_BUFFER_SRV_FLAG_RAW;
				device->device->CreateShaderResourceView(buffer->resource.Get(), &srv, cpu);
				break;
			}
			default:
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format				= DXGI_FORMAT_R32_TYPELESS;
				uav.ViewDimension		= D3D12_UAV_DIMENSION_BUFFER;
				uav.Buffer.FirstElement = write.offset / 4;
				uav.Buffer.NumElements	= static_cast<UINT>(range / 4);
				uav.Buffer.Flags		= D3D12_BUFFER_UAV_FLAG_RAW;
				device->device->CreateUnorderedAccessView(buffer->resource.Get(), nullptr, &uav, cpu);
				break;
			}
			}

			device->device->CopyDescriptorsSimple(
				1, CpuHandleAt(arena->resourceHeap.Get(), arena->resourceIncrement, index), cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		return Succeed(error);
	}

	bool D3D12UpdateDescriptorsTexture(void * impl, std::span<const DescriptorWriteTexture> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.updateDescriptorsTexture");

		auto * device = static_cast<D3D12Device *>(impl);
		for (const DescriptorWriteTexture & write : writes)
		{
			DescriptorSetSlot * set = ResolveDescriptorSet(device, write.set);
			TextureViewSlot * view	= ResolveTextureView(device, write.view);
			if (set == nullptr || view == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsTexture with an invalid handle");
			}
			TextureSlot * texture = ResolveTexture(device, view->texture);
			if (texture == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsTexture references a destroyed texture");
			}
			const std::uint32_t inClass = BindingOffsetInClass(set->bindings, write.binding, false);
			if (inClass == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "updateDescriptorsTexture binding is not in the set");
			}
			D3D12DescriptorArena * arena		  = set->arena;
			const std::uint32_t index			  = set->resourceBase + inClass + write.arrayIndex;
			const D3D12_CPU_DESCRIPTOR_HANDLE cpu = CpuHandleAt(arena->resourceStaging.Get(), arena->resourceIncrement, index);

			if (write.type == DescriptorType::eTextureUAV)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format		  = view->format;
				uav.ViewDimension = MapUavDimension(view->type);
				switch (uav.ViewDimension)
				{
				case D3D12_UAV_DIMENSION_TEXTURE1D: uav.Texture1D.MipSlice = view->range.baseMip; break;
				case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
					uav.Texture1DArray.MipSlice		   = view->range.baseMip;
					uav.Texture1DArray.FirstArraySlice = view->range.baseLayer;
					uav.Texture1DArray.ArraySize	   = view->range.layerCount;
					break;
				case D3D12_UAV_DIMENSION_TEXTURE3D:
					uav.Texture3D.MipSlice	  = view->range.baseMip;
					uav.Texture3D.FirstWSlice = view->range.baseLayer;
					uav.Texture3D.WSize		  = view->range.layerCount;
					break;
				case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
					uav.Texture2DArray.MipSlice		   = view->range.baseMip;
					uav.Texture2DArray.FirstArraySlice = view->range.baseLayer;
					uav.Texture2DArray.ArraySize	   = view->range.layerCount;
					break;
				default: uav.Texture2D.MipSlice = view->range.baseMip; break;
				}
				device->device->CreateUnorderedAccessView(texture->resource.Get(), nullptr, &uav, cpu);
			}
			else
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				// A sampled depth texture is typeless so its SRV aliases the depth bits as color, D3D12 rejecting a depth format here.
				srv.Format					= IsDepthDxgiFormat(view->format) ? DepthSrvFormat(view->format) : view->format;
				srv.ViewDimension			= MapSrvDimension(view->type);
				srv.Shader4ComponentMapping = view->shaderComponentMapping;
				switch (srv.ViewDimension)
				{
				case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
					srv.Texture1DArray.MostDetailedMip = view->range.baseMip;
					srv.Texture1DArray.MipLevels	   = view->range.mipCount;
					srv.Texture1DArray.FirstArraySlice = view->range.baseLayer;
					srv.Texture1DArray.ArraySize	   = view->range.layerCount;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
					srv.Texture2DArray.MostDetailedMip = view->range.baseMip;
					srv.Texture2DArray.MipLevels	   = view->range.mipCount;
					srv.Texture2DArray.FirstArraySlice = view->range.baseLayer;
					srv.Texture2DArray.ArraySize	   = view->range.layerCount;
					srv.Texture2DArray.PlaneSlice	   = view->planeSlice;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
					srv.TextureCubeArray.MostDetailedMip  = view->range.baseMip;
					srv.TextureCubeArray.MipLevels		  = view->range.mipCount;
					srv.TextureCubeArray.First2DArrayFace = view->range.baseLayer;
					srv.TextureCubeArray.NumCubes		  = view->range.layerCount / 6;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE3D:
					srv.Texture3D.MostDetailedMip = view->range.baseMip;
					srv.Texture3D.MipLevels		  = view->range.mipCount;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBE:
					srv.TextureCube.MostDetailedMip = view->range.baseMip;
					srv.TextureCube.MipLevels		= view->range.mipCount;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE1D:
					srv.Texture1D.MostDetailedMip = view->range.baseMip;
					srv.Texture1D.MipLevels		  = view->range.mipCount;
					break;
				default:
					srv.Texture2D.MostDetailedMip = view->range.baseMip;
					srv.Texture2D.MipLevels		  = view->range.mipCount;
					// Zero for an ordinary view. A plane view of NV12 or P010 selects its plane here, which is the only place Direct3D 12 names one.
					srv.Texture2D.PlaneSlice = view->planeSlice;
					break;
				}
				device->device->CreateShaderResourceView(texture->resource.Get(), &srv, cpu);
			}

			device->device->CopyDescriptorsSimple(
				1, CpuHandleAt(arena->resourceHeap.Get(), arena->resourceIncrement, index), cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			/*
			 * A combined write carries the sampler alongside the view, and its binding owns a slot in the sampler heap as well. Written here, not left to a separate
			 * sampler write, since one binding number addressing two heaps is what makes the pair combined at all.
			 */
			if (write.type == DescriptorType::eCombinedImageSampler)
			{
				SamplerSlot * sampler = ResolveSampler(device, write.sampler);
				if (sampler == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "a combined image sampler write needs a valid sampler alongside its view");
				}

				const std::uint32_t samplerInClass = BindingOffsetInClass(set->bindings, write.binding, true);
				if (samplerInClass == kInvalidIndex)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "updateDescriptorsTexture binding is not in the set's sampler class");
				}

				const std::uint32_t samplerIndex			 = set->samplerBase + samplerInClass + write.arrayIndex;
				const D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu = CpuHandleAt(arena->samplerStaging.Get(), arena->samplerIncrement, samplerIndex);
				device->device->CreateSampler(&sampler->desc, samplerCpu);
				device->device->CopyDescriptorsSimple(
					1, CpuHandleAt(arena->samplerHeap.Get(), arena->samplerIncrement, samplerIndex), samplerCpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			}
		}
		return Succeed(error);
	}

	bool D3D12UpdateDescriptorsSampler(void * impl, std::span<const DescriptorWriteSampler> writes, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.updateDescriptorsSampler");

		auto * device = static_cast<D3D12Device *>(impl);
		for (const DescriptorWriteSampler & write : writes)
		{
			DescriptorSetSlot * set = ResolveDescriptorSet(device, write.set);
			SamplerSlot * sampler	= ResolveSampler(device, write.sampler);
			if (set == nullptr || sampler == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "updateDescriptorsSampler with an invalid handle");
			}
			const std::uint32_t inClass = BindingOffsetInClass(set->bindings, write.binding, true);
			if (inClass == kInvalidIndex)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "updateDescriptorsSampler binding is not in the set");
			}
			D3D12DescriptorArena * arena		  = set->arena;
			const std::uint32_t index			  = set->samplerBase + inClass + write.arrayIndex;
			const D3D12_CPU_DESCRIPTOR_HANDLE cpu = CpuHandleAt(arena->samplerStaging.Get(), arena->samplerIncrement, index);
			device->device->CreateSampler(&sampler->desc, cpu);
			device->device->CopyDescriptorsSimple(
				1, CpuHandleAt(arena->samplerHeap.Get(), arena->samplerIncrement, index), cpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}
		return Succeed(error);
	}

	bool D3D12CmdBindDescriptorSet(void * impl, PipelineLayoutHandle layout, std::uint32_t setIndex, DescriptorSetHandle set,
		std::span<const DynamicDescriptorOffset> dynamicOffsets, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.bindDescriptorSet");

		auto * list						= static_cast<D3D12CommandList *>(impl);
		D3D12Device * device			= list->owner;
		DescriptorSetSlot * setSlot		= ResolveDescriptorSet(device, set);
		PipelineLayoutSlot * layoutSlot = ResolvePipelineLayout(device, layout);
		if (setSlot == nullptr || layoutSlot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "bindDescriptorSet with an invalid handle");
		}
		if (setIndex >= layoutSlot->setParams.size())
		{
			return Fail(error, ErrorCode::eInvalidArgument, "bindDescriptorSet set index is outside the pipeline layout");
		}
		if (!dynamicOffsets.empty())
		{
			// Every buffer binding lowers to a descriptor table, which carries no bind-time offset so a dynamic offset would need a root descriptor.
			// supportsDynamicBufferOffsets is false so reject a non-empty list, not silently read the wrong data.
			return Fail(error, ErrorCode::eUnsupportedFeature, "dynamic descriptor offsets are not supported by the D3D12 backend");
		}

		D3D12DescriptorArena * arena = setSlot->arena;
		if (list->boundResourceHeap != arena->resourceHeap.Get() || list->boundSamplerHeap != arena->samplerHeap.Get())
		{
			const std::array<ID3D12DescriptorHeap *, 2> heaps{ arena->resourceHeap.Get(), arena->samplerHeap.Get() };
			list->list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
			list->boundResourceHeap = arena->resourceHeap.Get();
			list->boundSamplerHeap	= arena->samplerHeap.Get();
		}

		if (setIndex >= list->pendingSets.size())
		{
			return Fail(error, ErrorCode::eUnsupportedFeature, "bindDescriptorSet set index exceeds the D3D12 backend's bound-set limit");
		}

		// Record the tables against the set index, not write them now: a root table targets the bound root signature so a set bound before its pipeline (an order
		// Vulkan allows) waits for the draw or dispatch, where FlushPendingDescriptorSets applies it.
		const PipelineLayoutSlot::SetParams & params	 = layoutSlot->setParams[setIndex];
		D3D12CommandList::PendingDescriptorSet & pending = list->pendingSets[setIndex];
		pending											 = D3D12CommandList::PendingDescriptorSet{};
		pending.valid									 = true;
		if (params.resourceParam != kInvalidIndex && setSlot->resourceCount > 0)
		{
			pending.resourceHandle = GpuHandleAt(arena->resourceHeap.Get(), arena->resourceIncrement, setSlot->resourceBase);
			pending.resourceParam  = params.resourceParam;
			pending.hasResource	   = true;
		}
		if (params.samplerParam != kInvalidIndex && setSlot->samplerCount > 0)
		{
			pending.samplerHandle = GpuHandleAt(arena->samplerHeap.Get(), arena->samplerIncrement, setSlot->samplerBase);
			pending.samplerParam  = params.samplerParam;
			pending.hasSampler	  = true;
		}
		return Succeed(error);
	}

	// Flip-model swapchains cannot store an sRGB format so map an sRGB request to its UNORM storage base.

} // namespace azo::rhi::d3d12

#endif // _WIN32

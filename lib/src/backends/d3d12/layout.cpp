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
	[[nodiscard]] D3D12_DESCRIPTOR_RANGE_TYPE MapRangeType(DescriptorType type) noexcept
	{
		switch (type)
		{
		case DescriptorType::eSampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		// The resource half of a combined binding. Its sampler half is emitted separately, this returning only the class the texture lives in.
		case DescriptorType::eCombinedImageSampler:	 return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case DescriptorType::eUniformBuffer:
		case DescriptorType::eDynamicUniformBuffer:	 return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case DescriptorType::eTextureSRV:
		case DescriptorType::eBufferSRV:
		case DescriptorType::eTexelBufferSRV:
		case DescriptorType::eAccelerationStructure: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case DescriptorType::eTextureUAV:
		case DescriptorType::eBufferUAV:
		case DescriptorType::eStorageBuffer:
		case DescriptorType::eDynamicStorageBuffer:
		case DescriptorType::eTexelBufferUAV:		 return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		}
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}

	/*
	 * Which of the two shader-visible heaps a binding occupies. Direct3D 12 keeps samplers apart from everything else, and a combined binding is the one type that
	 * sits in both: it takes a descriptor in each and a range in each table, the pair addressed by one binding number.
	 */
	[[nodiscard]] bool UsesSamplerHeap(DescriptorType type) noexcept
	{
		return type == DescriptorType::eSampler || type == DescriptorType::eCombinedImageSampler;
	}

	[[nodiscard]] bool UsesResourceHeap(DescriptorType type) noexcept
	{
		return type != DescriptorType::eSampler;
	}

	[[nodiscard]] bool IsSamplerDescriptor(DescriptorType type) noexcept
	{
		return type == DescriptorType::eSampler;
	}

	// Maps the RHI stage mask onto a D3D12 shader visibility. A single graphics stage maps to its specific visibility. Compute, multiple stages or eAll fall back
	// to ALL.
	[[nodiscard]] D3D12_SHADER_VISIBILITY MapShaderVisibility(Flags<ShaderStage> stages) noexcept
	{
		if (stages == Flags<ShaderStage>(ShaderStage::eVertex))
		{
			return D3D12_SHADER_VISIBILITY_VERTEX;
		}
		if (stages == Flags<ShaderStage>(ShaderStage::eFragment))
		{
			return D3D12_SHADER_VISIBILITY_PIXEL;
		}
		if (stages == Flags<ShaderStage>(ShaderStage::eGeometry))
		{
			return D3D12_SHADER_VISIBILITY_GEOMETRY;
		}
		if (stages == Flags<ShaderStage>(ShaderStage::eTessellationControl))
		{
			return D3D12_SHADER_VISIBILITY_HULL;
		}
		if (stages == Flags<ShaderStage>(ShaderStage::eTessellationEvaluation))
		{
			return D3D12_SHADER_VISIBILITY_DOMAIN;
		}
		return D3D12_SHADER_VISIBILITY_ALL;
	}

	DescriptorSetLayoutHandle D3D12CreateDescriptorSetLayout(void * impl, const DescriptorSetLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createDescriptorSetLayout");

		auto * device = static_cast<D3D12Device *>(impl);
		for (const DescriptorBinding & binding : desc.bindings)
		{
			if (!binding.immutableSamplers.empty())
			{
				return FailValue<DescriptorSetLayoutHandle>(error,
					ErrorCode::eUnsupportedFeature,
					"Direct3D 12 does not bake samplers into a descriptor set layout here, so write the sampler into the set instead");
			}
		}

		DescriptorSetLayoutSlot slot;
		slot.bindings.assign(desc.bindings.begin(), desc.bindings.end());
		for (const DescriptorBinding & binding : slot.bindings)
		{
			if (UsesResourceHeap(binding.type))
			{
				slot.cbvSrvUavCount += binding.count;
			}
			if (UsesSamplerHeap(binding.type))
			{
				slot.samplerCount += binding.count;
			}
		}

		return ReturnValue(device->descriptorSetLayoutSlots.Store(std::move(slot)), error);
	}

	[[nodiscard]] DescriptorSetLayoutSlot * ResolveDescriptorSetLayout(D3D12Device * device, DescriptorSetLayoutHandle handle) noexcept
	{
		return device->descriptorSetLayoutSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	/*
	 * Builds an ID3D12RootSignature from the pipeline layout's descriptor sets and push-constant ranges. Each set becomes up to two descriptor tables (one for
	 * CBV/SRV/UAV, one for samplers, which D3D12 requires in a separate heap). Registers are assigned to match what slangc emits, not from the (set, binding)
	 * pair, which the block inside explains.
	 */
	PipelineLayoutHandle D3D12CreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createPipelineLayout");

		auto * device = static_cast<D3D12Device *>(impl);

		detail::HostVector<DescriptorSetLayoutSlot *> setLayouts;
		setLayouts.reserve(desc.sets.size());
		std::size_t totalResourceRanges = 0;
		std::size_t totalSamplerRanges	= 0;
		for (const DescriptorSetLayoutHandle setHandle : desc.sets)
		{
			DescriptorSetLayoutSlot * setLayout = ResolveDescriptorSetLayout(device, setHandle);
			if (setLayout == nullptr)
			{
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout references an invalid descriptor set layout");
			}
			for (const DescriptorBinding & binding : setLayout->bindings)
			{
				totalResourceRanges += UsesResourceHeap(binding.type) ? 1 : 0;
				totalSamplerRanges += UsesSamplerHeap(binding.type) ? 1 : 0;
			}
			setLayouts.push_back(setLayout);
		}

		// Reserved up front so the root parameters can hold stable pointers into these vectors.
		detail::HostVector<D3D12_DESCRIPTOR_RANGE1> resourceRanges;
		detail::HostVector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;
		resourceRanges.reserve(totalResourceRanges);
		samplerRanges.reserve(totalSamplerRanges);

		detail::HostVector<D3D12_ROOT_PARAMETER1> params;
		PipelineLayoutSlot slot;
		slot.setParams.resize(setLayouts.size());

		/*
		 * Registers come from NativeBindingFor without being counted here.
		 *
		 * They were counted here once, and the published ABI mirrored the counting, which left two implementations of one mapping and nothing stopping them drifting.
		 * Calling the ABI makes the root signature agree with what a shader author was told by construction instead of by review, which is the only version of that
		 * guarantee worth having.
		 */
		detail::HostVector<DescriptorSetLayoutDesc> abiSets;
		abiSets.reserve(setLayouts.size());
		for (const DescriptorSetLayoutSlot * setLayout : setLayouts)
		{
			abiSets.push_back(DescriptorSetLayoutDesc{ .bindings = setLayout->bindings });
		}

		const ShaderAbiLayout abiLayout{ .sets = abiSets, .pushConstants = desc.pushConstants };

		// Kept so a pipeline built against this layout can rebuild the same ABI layout and check what its shaders claim about themselves.
		slot.sets.assign(desc.sets.begin(), desc.sets.end());

		for (std::size_t i = 0; i < setLayouts.size(); ++i)
		{
			const std::size_t resourceStart = resourceRanges.size();
			const std::size_t samplerStart	= samplerRanges.size();
			std::size_t resourceCount		= 0;
			std::size_t samplerCount		= 0;
			for (const DescriptorBinding & binding : setLayouts[i]->bindings)
			{
				const D3D12_DESCRIPTOR_RANGE_TYPE rangeType = MapRangeType(binding.type);
				const NativeBinding abiSlot =
					NativeBindingFor(D3D12Api::id, device->caps.bindingTier, abiLayout, static_cast<std::uint32_t>(i), binding.binding);

				D3D12_DESCRIPTOR_RANGE1 range{};
				range.RangeType							= rangeType;
				range.NumDescriptors					= binding.count;
				range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
				range.BaseShaderRegister				= abiSlot.resource.index;
				range.RegisterSpace						= abiSlot.resource.space;

				if (UsesResourceHeap(binding.type))
				{
					range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
					resourceRanges.push_back(range);
					++resourceCount;
				}
				if (UsesSamplerHeap(binding.type))
				{
					/*
					 * A combined binding pushed its SRV range above and needs a second range of its own in the sampler table. The ABI hands back both slots from one query for
					 * exactly this reason, so the s register comes from there and not from a counter kept beside it.
					 */
					D3D12_DESCRIPTOR_RANGE1 samplerRange = range;
					samplerRange.RangeType				 = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
					samplerRange.Flags					 = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
					if (abiSlot.hasSampler)
					{
						samplerRange.BaseShaderRegister = abiSlot.sampler.index;
						samplerRange.RegisterSpace		= abiSlot.sampler.space;
					}
					samplerRanges.push_back(samplerRange);
					++samplerCount;
				}
			}

			if (resourceCount > 0)
			{
				D3D12_ROOT_PARAMETER1 param{};
				param.ParameterType						  = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				param.ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;
				param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(resourceCount);
				param.DescriptorTable.pDescriptorRanges	  = resourceRanges.data() + resourceStart;
				slot.setParams[i].resourceParam			  = static_cast<std::uint32_t>(params.size());
				params.push_back(param);
			}
			if (samplerCount > 0)
			{
				D3D12_ROOT_PARAMETER1 param{};
				param.ParameterType						  = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				param.ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;
				param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(samplerCount);
				param.DescriptorTable.pDescriptorRanges	  = samplerRanges.data() + samplerStart;
				slot.setParams[i].samplerParam			  = static_cast<std::uint32_t>(params.size());
				params.push_back(param);
			}
		}

		for (std::size_t i = 0; i < desc.pushConstants.size(); ++i)
		{
			D3D12_ROOT_PARAMETER1 param{};
			param.ParameterType	   = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			param.ShaderVisibility = MapShaderVisibility(desc.pushConstants[i].stages);
			// A push constant lowers to a cbuffer in kPushConstantRegisterSpace, which no descriptor set uses, so its register cannot move because a set was added or a
			// binding changed. Descriptor sets start one space above it.
			param.Constants.ShaderRegister = static_cast<UINT>(i);
			param.Constants.RegisterSpace  = kPushConstantRegisterSpace;
			param.Constants.Num32BitValues = (desc.pushConstants[i].size + 3u) / 4u;
			slot.pushConstantParams.push_back(PipelineLayoutSlot::PushConstantParam{
				.rootParam = static_cast<std::uint32_t>(params.size()), .offset = desc.pushConstants[i].offset, .size = desc.pushConstants[i].size });
			params.push_back(param);
		}

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
		versioned.Version				 = D3D_ROOT_SIGNATURE_VERSION_1_1;
		versioned.Desc_1_1.NumParameters = static_cast<UINT>(params.size());
		versioned.Desc_1_1.pParameters	 = params.empty() ? nullptr : params.data();
		versioned.Desc_1_1.Flags		 = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> serializeError;
		if (FAILED(D3D12SerializeVersionedRootSignature(&versioned, blob.GetAddressOf(), serializeError.GetAddressOf())))
		{
			return FailValue<PipelineLayoutHandle>(error, ErrorCode::eNativeApiError, "root signature serialization failed");
		}
		if (FAILED(device->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(slot.rootSignature.GetAddressOf()))))
		{
			return FailValue<PipelineLayoutHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateRootSignature failed");
		}

		return ReturnValue(device->pipelineLayoutSlots.Store(std::move(slot)), error);
	}

	bool D3D12DestroyDescriptorSetLayout(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const DescriptorSetLayoutHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		DescriptorSetLayoutSlot * slot = device->descriptorSetLayoutSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid descriptor set layout handle");
		}
		slot->bindings.clear();
		static_cast<void>(device->descriptorSetLayoutSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	bool D3D12DestroyPipelineLayout(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const PipelineLayoutHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		PipelineLayoutSlot * slot = device->pipelineLayoutSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid pipeline layout handle");
		}
		slot->rootSignature.Reset();
		slot->setParams.clear();
		slot->pushConstantParams.clear();
		static_cast<void>(device->pipelineLayoutSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	// Defined in other files of this backend. Declared here because the switch below is the one place a destroy is dispatched from.
	bool D3D12DestroyGraphicsPipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyComputePipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyPipelineCache(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyQueryPool(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyDescriptorSet(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyTimeline(D3D12Device * device, RawHandle handle, Error * error) noexcept;
	bool D3D12DestroyBinarySemaphore(D3D12Device * device, RawHandle handle, Error * error) noexcept;

	[[nodiscard]] bool DestroyByType(D3D12Device * device, ResourceType type, RawHandle handle, Error * error) noexcept
	{
		switch (type)
		{
		case ResourceType::eBuffer:				 return D3D12DestroyBuffer(device, handle, error);
		case ResourceType::eTexture:			 return D3D12DestroyTexture(device, handle, error);
		case ResourceType::eTextureView:		 return D3D12DestroyTextureView(device, handle, error);
		case ResourceType::eSampler:			 return D3D12DestroySampler(device, handle, error);
		case ResourceType::eHeap:				 return D3D12DestroyHeap(device, handle, error);
		case ResourceType::eDescriptorSetLayout: return D3D12DestroyDescriptorSetLayout(device, handle, error);
		case ResourceType::eDescriptorSet:		 return D3D12DestroyDescriptorSet(device, handle, error);
		case ResourceType::ePipelineLayout:		 return D3D12DestroyPipelineLayout(device, handle, error);
		case ResourceType::eGraphicsPipeline:	 return D3D12DestroyGraphicsPipeline(device, handle, error);
		case ResourceType::eComputePipeline:	 return D3D12DestroyComputePipeline(device, handle, error);
		case ResourceType::ePipelineCache:		 return D3D12DestroyPipelineCache(device, handle, error);
		case ResourceType::eQueryPool:			 return D3D12DestroyQueryPool(device, handle, error);
		case ResourceType::eTimeline:			 return D3D12DestroyTimeline(device, handle, error);
		case ResourceType::eBinarySemaphore:	 return D3D12DestroyBinarySemaphore(device, handle, error);
		default:								 return Fail(error, ErrorCode::eUnsupportedFeature, "D3D12 RHI backend: destroy not implemented for this resource type yet");
		}
	}

	bool D3D12Destroy(void * impl, ResourceType type, RawHandle handle, [[maybe_unused]] const DestroyDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.destroy");

		auto * device = static_cast<D3D12Device *>(impl);
		if (!DestroyByType(device, type, handle, error))
		{
			return false;
		}

		[[maybe_unused]] const std::uint64_t pending = device->pendingRetire.fetch_add(1, std::memory_order_relaxed) + 1;
		AZO_RHI_PROFILE_PLOT("rhi.d3d12.pendingRetire", static_cast<std::int64_t>(pending));
		return true;
	}

	// Classifies an adapter. D3D12 has no direct integrated/discrete flag so a software adapter maps to eCpu and a unified-memory adapter to eIntegrated,
	// otherwise eDiscrete.

} // namespace azo::rhi::d3d12

#endif // _WIN32

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
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace azo::rhi
{

	struct ShaderAbiLayout final
	{
		std::span<const DescriptorSetLayoutDesc> sets;
		std::span<const PushConstantRange> pushConstants;
	};

	namespace detail
	{
		[[nodiscard]] constexpr bool abi_uses_resource_heap(const DescriptorType type) noexcept
		{
			return type != DescriptorType::eSampler;
		}

		[[nodiscard]] constexpr bool abi_uses_sampler_heap(const DescriptorType type) noexcept
		{
			return type == DescriptorType::eSampler || type == DescriptorType::eCombinedImageSampler;
		}

		[[nodiscard]] constexpr NativeSlotClass abi_d3_d12_class(const DescriptorType type) noexcept
		{
			switch (type)
			{
			case DescriptorType::eSampler:				 return NativeSlotClass::eSampler;
			case DescriptorType::eUniformBuffer:
			case DescriptorType::eDynamicUniformBuffer:	 return NativeSlotClass::eConstantBuffer;
			case DescriptorType::eTextureUAV:
			case DescriptorType::eBufferUAV:
			case DescriptorType::eStorageBuffer:
			case DescriptorType::eDynamicStorageBuffer:
			case DescriptorType::eTexelBufferUAV:		 return NativeSlotClass::eUnorderedAccess;
			case DescriptorType::eCombinedImageSampler:
			case DescriptorType::eTextureSRV:
			case DescriptorType::eBufferSRV:
			case DescriptorType::eTexelBufferSRV:
			case DescriptorType::eAccelerationStructure: return NativeSlotClass::eShaderResource;
			}

			return NativeSlotClass::eShaderResource;
		}

		[[nodiscard]] constexpr NativeSlotClass abi_metal_class(const DescriptorType type) noexcept
		{
			switch (type)
			{
			case DescriptorType::eSampler:				 return NativeSlotClass::eSampler;
			case DescriptorType::eUniformBuffer:
			case DescriptorType::eDynamicUniformBuffer:
			case DescriptorType::eStorageBuffer:
			case DescriptorType::eDynamicStorageBuffer:
			case DescriptorType::eBufferSRV:
			case DescriptorType::eBufferUAV:			 return NativeSlotClass::eBuffer;
			case DescriptorType::eCombinedImageSampler:
			case DescriptorType::eTextureSRV:
			case DescriptorType::eTextureUAV:
			case DescriptorType::eTexelBufferSRV:
			case DescriptorType::eTexelBufferUAV:
			case DescriptorType::eAccelerationStructure: return NativeSlotClass::eTexture;
			}

			return NativeSlotClass::eTexture;
		}
	}

	[[nodiscard]] inline NativeBinding native_binding_for(
		const GraphicsApiId api,
		const BindingTier tier,
		const ShaderAbiLayout & layout,
		const std::uint32_t set,
		const std::uint32_t binding
	) noexcept
	{
		if (set >= layout.sets.size())
		{
			return {};
		}

		if (api == VulkanApi::kId)
		{
			for (const DescriptorBinding & entry : azo::rhi::detail::at(layout.sets, set).bindings)
			{
				if (entry.binding == binding)
				{
					return NativeBinding{
						.resource = NativeSlot{ .space = set, .index = binding, .klass = NativeSlotClass::eBinding },
						.exists	  = true,
					};
				}
			}

			return {};
		}

		if (is_metal_family(api) && tier >= BindingTier::eUnbounded)
		{
			std::uint32_t member = 0;
			for (const DescriptorBinding & entry : azo::rhi::detail::at(layout.sets, set).bindings)
			{
				if (entry.binding == binding)
				{
					NativeBinding result{
						.resource =
							NativeSlot{ .space = metal_argument_buffer_index_for_set(set), .index = member, .klass = NativeSlotClass::eArgumentBufferMember },
						.exists = true,
					};

					if (entry.type == DescriptorType::eCombinedImageSampler)
					{
						result.sampler = NativeSlot{
							.space = metal_argument_buffer_index_for_set(set),
							.index = member + 1,
							.klass = NativeSlotClass::eArgumentBufferMember,
						};
						result.hasSampler = true;
					}

					return result;
				}

				member += (entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u) * std::max(entry.count, 1u);
			}

			return {};
		}

		if (is_metal_family(api))
		{
			if (set != 0)
			{
				return {};
			}

			for (const DescriptorBinding & entry : azo::rhi::detail::at(layout.sets, set).bindings)
			{
				if (entry.binding == binding)
				{
					const NativeSlotClass klass = detail::abi_metal_class(entry.type);
					NativeBinding result{
						.resource = NativeSlot{ .space = 0, .index = binding, .klass = klass },
						.exists	  = true,
					};

					if (entry.type == DescriptorType::eCombinedImageSampler)
					{
						result.sampler	  = NativeSlot{ .space = 0, .index = binding, .klass = NativeSlotClass::eSampler };
						result.hasSampler = true;
					}

					return result;
				}
			}

			return {};
		}

		if (api != D3D12Api::kId)
		{
			return {};
		}

		std::uint32_t bindlessSpace = static_cast<std::uint32_t>(layout.sets.size()) + 1;

		for (std::size_t setIndex = 0; setIndex < layout.sets.size(); ++setIndex)
		{
			std::uint32_t constantBufferRegister  = 0;
			std::uint32_t shaderResourceRegister  = 0;
			std::uint32_t unorderedAccessRegister = 0;
			std::uint32_t samplerRegister		  = 0;

			for (const DescriptorBinding & entry : azo::rhi::detail::at(layout.sets, setIndex).bindings)
			{
				const bool wanted			= setIndex == set && entry.binding == binding;
				const NativeSlotClass klass = detail::abi_d3_d12_class(entry.type);

				if (entry.flags.contains(DescriptorBindingFlag::eBindless))
				{
					const std::uint32_t space = bindlessSpace;
					++bindlessSpace;
					if (wanted)
					{
						return NativeBinding{
							.resource = NativeSlot{ .space = space, .index = 0, .klass = klass },
							.exists	  = true,
						};
					}

					continue;
				}

				std::uint32_t * counter = &shaderResourceRegister;
				switch (klass)
				{
				case NativeSlotClass::eConstantBuffer:	counter = &constantBufferRegister; break;
				case NativeSlotClass::eUnorderedAccess: counter = &unorderedAccessRegister; break;
				case NativeSlotClass::eSampler:			counter = &samplerRegister; break;
				default:								break;
				}

				const std::uint32_t taken = *counter;
				*counter += entry.count;

				std::uint32_t samplerTaken = 0;
				bool tookSampler		   = false;
				if (entry.type == DescriptorType::eCombinedImageSampler)
				{
					samplerTaken = samplerRegister;
					samplerRegister += entry.count;
					tookSampler = true;
				}

				if (wanted)
				{
					const std::uint32_t space = d3_d12_register_space_for_set(static_cast<std::uint32_t>(setIndex));
					NativeBinding result{
						.resource = NativeSlot{ .space = space, .index = taken, .klass = klass },
						.exists	  = true,
					};
					if (tookSampler)
					{
						result.sampler	  = NativeSlot{ .space = space, .index = samplerTaken, .klass = NativeSlotClass::eSampler };
						result.hasSampler = true;
					}

					return result;
				}
			}
		}

		return {};
	}

	struct ShaderBindingDisagreement final
	{
		bool found = false;

		std::uint32_t set	  = 0;
		std::uint32_t binding = 0;

		NativeSlot declared{};

		NativeSlot expected{};

		bool unknownToLayout = false;

		bool wrongAbiVersion = false;
	};

	[[nodiscard]] inline ShaderBindingDisagreement check_shader_binding_map(
		const GraphicsApiId api,
		const BindingTier tier,
		const ShaderAbiLayout & layout,
		const ShaderBindingMap & map
	) noexcept
	{
		if (map.bindings.empty())
		{
			return map.abi == kShaderAbiVersion ? ShaderBindingDisagreement{} : ShaderBindingDisagreement{ .found = true, .wrongAbiVersion = true };
		}

		for (const ShaderBindingEntry & entry : map.bindings)
		{
			const NativeBinding expected = native_binding_for(api, tier, layout, entry.set, entry.binding);
			if (!expected.exists)
			{
				return ShaderBindingDisagreement{
					.found			 = true,
					.set			 = entry.set,
					.binding		 = entry.binding,
					.declared		 = entry.resource,
					.unknownToLayout = true,
				};
			}

			const bool samplerAgrees = entry.hasSampler == expected.hasSampler && (!entry.hasSampler || entry.sampler == expected.sampler);
			if (entry.resource != expected.resource || !samplerAgrees)
			{
				return ShaderBindingDisagreement{
					.found	  = true,
					.set	  = entry.set,
					.binding  = entry.binding,
					.declared = entry.resource,
					.expected = expected.resource,
				};
			}
		}

		return {};
	}

}

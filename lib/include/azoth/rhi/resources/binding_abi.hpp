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
 * \brief The shader binding ABI: where an RHI-space (set, binding) lands in each backend's native slots.
 *
 * A shader either conforms to this or carries its own binding map. With Slang, conforming means declaring each set as a ParameterBlock, not globals carrying
 * vk::binding. Flat vk::binding globals share one counter per register class across every set in space zero, which binds wrong on Direct3D 12.
 *
 * Vertex attributes are stated by kVertexSemanticName in native_slot.hpp. Only Direct3D 12 needs them.
 */

#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <algorithm>
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief The shape a binding is resolved against.
	 *
	 * The descriptions and not the handles, because a caller computing slots offline has no device to have created layouts on. This is what PipelineLayoutDesc
	 * says, with the set layouts spelled out instead of referenced.
	 */
	struct ShaderAbiLayout final
	{
		std::span<const DescriptorSetLayoutDesc> sets;
		std::span<const PushConstantRange> pushConstants;
	};

	namespace detail
	{
		[[nodiscard]] constexpr bool AbiUsesResourceHeap(const DescriptorType type) noexcept
		{
			return type != DescriptorType::eSampler;
		}

		[[nodiscard]] constexpr bool AbiUsesSamplerHeap(const DescriptorType type) noexcept
		{
			return type == DescriptorType::eSampler || type == DescriptorType::eCombinedImageSampler;
		}

		/*
		 * Which Direct3D 12 register class a descriptor type is numbered in.
		 *
		 * Mirrors MapRangeType in the backend one case at a time. A combined binding answers with the class its texture half lives in, its sampler half being carried
		 * separately, which is the same split the backend makes. The agreement test is what keeps the two from drifting.
		 */
		[[nodiscard]] constexpr NativeSlotClass AbiD3D12Class(const DescriptorType type) noexcept
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

		// Which Metal argument table a descriptor type is numbered in. Buffers and textures are separate tables there, so the split is by storage kind and not by
		// read or write access the way Direct3D 12 splits t from u.
		[[nodiscard]] constexpr NativeSlotClass AbiMetalClass(const DescriptorType type) noexcept
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
	} // namespace detail

	/**
	 * \brief Resolves an RHI (set, binding) to the native slots a backend binds it at, for a ShaderBinary carrying no map.
	 *
	 * \param tier Changes the Metal answer: argument buffers address a set as a struct, discrete binding numbers an argument table.
	 * \param layout The whole pipeline layout, since Direct3D 12 numbers register classes across every set.
	 * \return Where the binding lands, exists false when the layout does not declare it.
	 * \attention Revision kShaderAbiVersion. A different revision may bind elsewhere.
	 */
	[[nodiscard]] inline NativeBinding NativeBindingFor(
		const GraphicsApiId api, const BindingTier tier, const ShaderAbiLayout & layout, const std::uint32_t set, const std::uint32_t binding) noexcept
	{
		if (set >= layout.sets.size())
		{
			return {};
		}

		/*
		 * Vulkan addresses a binding by the pair it was declared with, so nothing has to be counted and the answer does not depend on any other set. This is the
		 * mapping the other two are measured against, and the reason the RHI's own model is set and binding in the first place.
		 */
		if (api == VulkanApi::id)
		{
			for (const DescriptorBinding & entry : layout.sets[set].bindings)
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

		/*
		 * Metal addresses a set two ways, chosen by tier. With argument buffers a set is a struct: slangc lowers each ParameterBlock to one, binds set N at buffer N
		 * + 1 and lays the members out in declaration order. Below that tier descriptors go into three argument tables addressed by index and only set 0 is
		 * reachable.
		 *
		 * Measured against slangc 1.4.341.1 and pinned by ShaderAbiAgreement. Reserving buffer 0 for the push constant makes that the rule and not the example.
		 */
		if (IsMetalFamily(api) && tier >= BindingTier::eUnbounded)
		{
			/*
			 * The member ordinal, which is this binding's position in the set's own binding list.
			 *
			 * Declaration order, not binding number, because that is what slangc lays a struct out by: a set declaring bindings 0, 5 and 9 has members 0, 1 and
			 * 2. The list is taken as the caller wrote it.
			 *
			 * This answers where a binding starts. An array binding occupies count members from there, which is what a caller adds its array index to.
			 */
			std::uint32_t member = 0;
			for (const DescriptorBinding & entry : layout.sets[set].bindings)
			{
				if (entry.binding == binding)
				{
					NativeBinding result{
						.resource = NativeSlot{ .space = MetalArgumentBufferIndexForSet(set), .index = member, .klass = NativeSlotClass::eArgumentBufferMember },
						.exists	  = true,
					};

					// A combined binding is two members in the struct, the texture then the sampler, because Metal has no combined type to declare.
					if (entry.type == DescriptorType::eCombinedImageSampler)
					{
						result.sampler	  = NativeSlot{ .space = set, .index = member + 1, .klass = NativeSlotClass::eArgumentBufferMember };
						result.hasSampler = true;
					}

					return result;
				}

				/*
				 * An array binding is count members and not one. slangc lowers an array inside a ParameterBlock to that many consecutive words, so a binding
				 * declared after one starts that far along, and counting it as a single member puts every later binding at the wrong offset.
				 */
				member += (entry.type == DescriptorType::eCombinedImageSampler ? 2u : 1u) * std::max(entry.count, 1u);
			}

			return {};
		}

		if (IsMetalFamily(api))
		{
			if (set != 0)
			{
				return {};
			}

			for (const DescriptorBinding & entry : layout.sets[set].bindings)
			{
				if (entry.binding == binding)
				{
					const NativeSlotClass klass = detail::AbiMetalClass(entry.type);
					NativeBinding result{
						.resource = NativeSlot{ .space = 0, .index = binding, .klass = klass },
						.exists	  = true,
					};

					// A combined binding occupies a texture index and a sampler index, both numbered by the same binding number.
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

		if (api != D3D12Api::id)
		{
			// Null, or a backend of somebody's own. Neither has native slots this can speak for.
			return {};
		}

		/*
		 * Direct3D 12 gives each descriptor set one register space. Shader Model 5.1 spaces are the descriptor-set analog so a set maps onto one, with each register
		 * class counting from zero inside it. A binding's register depends on its set and nothing else. A bindless binding takes a space above every set, since a
		 * runtime-sized array cannot share one.
		 *
		 * One HLSL constraint no arithmetic can hide: an array of N resources consumes N contiguous registers where Vulkan treats it as one.
		 */
		std::uint32_t bindlessSpace = static_cast<std::uint32_t>(layout.sets.size()) + 1;

		for (std::size_t setIndex = 0; setIndex < layout.sets.size(); ++setIndex)
		{
			std::uint32_t constantBufferRegister  = 0;
			std::uint32_t shaderResourceRegister  = 0;
			std::uint32_t unorderedAccessRegister = 0;
			std::uint32_t samplerRegister		  = 0;

			for (const DescriptorBinding & entry : layout.sets[setIndex].bindings)
			{
				const bool wanted			= setIndex == set && entry.binding == binding;
				const NativeSlotClass klass = detail::AbiD3D12Class(entry.type);

				if (entry.flags.Contains(DescriptorBindingFlag::eBindless))
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

				// A combined binding took its t register above and takes an s register beside it, which is how slangc splits a type Direct3D 12 lacks.
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
					const std::uint32_t space = D3D12RegisterSpaceForSet(static_cast<std::uint32_t>(setIndex));
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

	/**
	 * \brief One place a binary's map and this API's ABI say different things.
	 */
	struct ShaderBindingDisagreement final
	{
		/**
		 * \brief False when the map and the ABI agree, in which case nothing else here is set.
		 */
		bool found = false;

		std::uint32_t set	  = 0;
		std::uint32_t binding = 0;

		/**
		 * \brief Where the binary said it put the binding.
		 */
		NativeSlot declared{};

		/**
		 * \brief Where the ABI puts it, and therefore where this backend will bind it.
		 */
		NativeSlot expected{};

		/**
		 * \brief True when the map names a set and binding the pipeline layout does not declare at all.
		 *
		 * A different failure from the two landing in different places: nothing will ever be bound there, so expected is empty and the two slots are not worth
		 * comparing.
		 */
		bool unknownToLayout = false;

		/**
		 * \brief True when the binary claims a revision of the ABI this build does not implement.
		 *
		 * Set and binding are meaningless in that case. It is checked here and not at the call site so that a binary built against a revision that has since moved is
		 * refused by the same path.
		 */
		bool wrongAbiVersion = false;
	};

	/**
	 * \brief Checks a binary's account of itself against where this API's ABI binds things.
	 *
	 * The RHI binds from the pipeline layout and never from the map. This only decides whether a pipeline is refused. Without the check a binary built against
	 * different slots reads the wrong descriptors.
	 *
	 * Only the entries the map lists are checked, since a tool reflecting a binary emits the bindings the shader uses, not every binding the layout declares.
	 */
	[[nodiscard]] inline ShaderBindingDisagreement CheckShaderBindingMap(
		const GraphicsApiId api, const BindingTier tier, const ShaderAbiLayout & layout, const ShaderBindingMap & map) noexcept
	{
		// Listing nothing is a claim of conformance, so the revision claimed is the whole of what there is to check.
		if (map.bindings.empty())
		{
			return map.abi == kShaderAbiVersion ? ShaderBindingDisagreement{} : ShaderBindingDisagreement{ .found = true, .wrongAbiVersion = true };
		}

		for (const ShaderBindingEntry & entry : map.bindings)
		{
			const NativeBinding expected = NativeBindingFor(api, tier, layout, entry.set, entry.binding);
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

			// The sampler half only has to match where both sides have one, since a backend that does not split a combined binding leaves it empty.
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

} // namespace azo::rhi

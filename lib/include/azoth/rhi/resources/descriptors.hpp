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
 * \brief Descriptor set layouts, pipeline layouts, descriptor writes, and descriptor arenas.
 */

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{

	struct DescriptorArenaApi;
	class BackendBlockSet;

	/**
	 * \brief Shader stage visibility mask.
	 */
	enum class ShaderStage : std::uint32_t
	{
		eNone					= 0,
		eVertex					= 1u << 0u,
		eTessellationControl	= 1u << 1u,
		eTessellationEvaluation = 1u << 2u,
		eGeometry				= 1u << 3u,
		eFragment				= 1u << 4u,
		eCompute				= 1u << 5u,
		eRayGeneration			= 1u << 6u,

		/**
		 * \brief Ray tracing shader for candidate hit filtering.
		 */
		eAnyHit = 1u << 7u,

		/**
		 * \brief Ray tracing shader for the nearest accepted hit.
		 */
		eClosestHit = 1u << 8u,

		eMiss = 1u << 9u,

		/**
		 * \brief Ray tracing shader for procedural geometry intersection.
		 */
		eIntersection = 1u << 10u,

		/**
		 * \brief Ray tracing shader callable from other ray tracing shaders.
		 */
		eCallable = 1u << 11u,

		/**
		 * \brief Broad mask for all graphics shader stages.
		 */
		eAllGraphics = 1u << 28u,

		/**
		 * \brief Broad mask for all ray tracing shader stages.
		 */
		eAllRayTracing = 1u << 29u,

		/**
		 * \brief Full shader stage mask.
		 */
		eAll = 0xffffffffu,
	};

	/**
	 * \brief Descriptor resource binding kind.
	 */
	enum class DescriptorType : std::uint8_t
	{
		eSampler,

		/**
		 * \brief One descriptor pairing a texture with the sampler that reads it.
		 *
		 * The only binding a sampler carrying a Y'CbCr conversion can occupy, and then only through DescriptorBinding::immutableSamplers, which is why it
		 * exists. Vulkan pairs the two natively. Direct3D 12 keeps samplers in a separate heap and lowers one binding onto a descriptor in each, written
		 * together by a single DescriptorWriteTexture carrying both view and sampler.
		 */
		eCombinedImageSampler,

		/**
		 * \brief Read-only texture descriptor.
		 */
		eTextureSRV,

		/**
		 * \brief Read-write texture descriptor.
		 */
		eTextureUAV,

		/**
		 * \brief Read-only buffer descriptor.
		 */
		eBufferSRV,

		/**
		 * \brief Read-write buffer descriptor.
		 */
		eBufferUAV,

		eUniformBuffer,

		eStorageBuffer,

		/**
		 * \brief Uniform buffer with a bind-time byte offset.
		 */
		eDynamicUniformBuffer,

		/**
		 * \brief Storage buffer with a bind-time byte offset.
		 */
		eDynamicStorageBuffer,

		/**
		 * \brief Read-only formatted buffer view.
		 */
		eTexelBufferSRV,

		/**
		 * \brief Read-write formatted buffer view.
		 */
		eTexelBufferUAV,

		eAccelerationStructure,
	};

	/**
	 * \brief Per-binding descriptor layout flags.
	 */
	enum class DescriptorBindingFlag : std::uint32_t // NOLINT(performance-enum-size)
	{
		/**
		 * \brief Allows updates after the descriptor set is bound.
		 */
		eUpdateAfterBind = 1u << 0u,

		/**
		 * \brief Allows unused array entries to remain unpopulated.
		 */
		ePartiallyBound = 1u << 1u,

		/**
		 * \brief Allows the descriptor count to be chosen during set allocation.
		 */
		eVariableDescriptorCount = 1u << 2u,

		/**
		 * \brief Marks the binding as a large descriptor-indexed array.
		 */
		eBindless = 1u << 3u,

		/**
		 * \brief Hints that the binding may lower to a Direct3D 12 root descriptor.
		 */
		eRootDescriptorCandidate = 1u << 4u,
	};

	/**
	 * \brief One descriptor binding inside a descriptor set layout.
	 */
	struct DescriptorBinding final
	{
		std::uint32_t binding	  = 0;
		DescriptorType type		  = DescriptorType::eUniformBuffer;
		std::uint32_t count		  = 1;
		Flags<ShaderStage> stages = ShaderStage::eAll;
		Flags<DescriptorBindingFlag> flags;

		/**
		 * \brief Samplers baked into the layout instead of written into a set, borrowed for the duration of creation.
		 *
		 * Empty for an ordinary binding. When present the span must hold exactly count entries, and a set never writes a sampler over one. A sampler carrying a
		 * Y'CbCr conversion can only be bound this way, the conversion being compiled into the pipeline, not chosen per draw.
		 */
		std::span<const SamplerHandle> immutableSamplers;
	};

	/**
	 * \brief Descriptor set layout creation request.
	 *
	 * bindings and debugName are borrowed for the duration of creation.
	 */
	struct DescriptorSetLayoutDesc final
	{
		std::span<const DescriptorBinding> bindings;
		const char * debugName = nullptr;
	};

	/**
	 * \brief Push constant byte range and visible stages.
	 */
	struct PushConstantRange final
	{
		Flags<ShaderStage> stages;
		std::uint32_t offset = 0;
		std::uint32_t size	 = 0;
	};

	/**
	 * \brief Descriptor sets and push constants used by a pipeline.
	 *
	 * sets, pushConstants, and debugName are borrowed for the duration of creation.
	 */
	struct PipelineLayoutDesc final
	{
		std::span<const DescriptorSetLayoutHandle> sets;
		std::span<const PushConstantRange> pushConstants;
		const char * debugName = nullptr;
	};

	/**
	 * \brief Descriptor arena lifetime model.
	 */
	enum class DescriptorArenaType : std::uint8_t
	{
		/**
		 * \brief Frame-local arena reset as a whole.
		 */
		eFrameTransient,

		/**
		 * \brief Long-lived arena for persistent descriptor sets.
		 */
		ePersistent
	};

	/**
	 * \brief Descriptor arena creation request.
	 */
	struct DescriptorArenaDesc final
	{
		DescriptorArenaType type	 = DescriptorArenaType::eFrameTransient;
		std::uint32_t maxSets		 = 0;
		std::uint32_t maxDescriptors = 0;
		bool shaderVisible			 = true;
		const char * debugName		 = nullptr;
	};

	/**
	 * \brief Descriptor set allocation request.
	 *
	 * variableDescriptorCount sizes a trailing binding marked eVariableDescriptorCount.
	 */
	struct DescriptorSetAllocDesc final
	{
		DescriptorSetLayoutHandle layout{};
		std::uint32_t variableDescriptorCount = 0;
		const char * debugName				  = nullptr;
	};

	/**
	 * \brief Descriptor write for a buffer-backed descriptor.
	 *
	 * range is in bytes. UINT64_MAX means the rest of the buffer from offset.
	 */
	struct DescriptorWriteBuffer final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		DescriptorType type		 = DescriptorType::eUniformBuffer;
		BufferHandle buffer{};
		std::uint64_t offset = 0;
		std::uint64_t range	 = std::numeric_limits<std::uint64_t>::max();
	};

	/**
	 * \brief Descriptor write for a texture view and optional sampler.
	 *
	 * expectedLayout is the layout the texture is expected to be in when the descriptor is used.
	 */
	struct DescriptorWriteTexture final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		DescriptorType type		 = DescriptorType::eTextureSRV;
		TextureViewHandle view{};
		SamplerHandle sampler{};
		TextureLayout expectedLayout = TextureLayout::eShaderReadOnly;
	};

	/**
	 * \brief Descriptor write for a sampler descriptor.
	 */
	struct DescriptorWriteSampler final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		SamplerHandle sampler{};
	};

	/**
	 * \brief Descriptor write for an acceleration structure descriptor.
	 */
	struct DescriptorWriteAccelerationStructure final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		AccelerationStructureHandle accelerationStructure{};
	};

	/**
	 * \brief Bind-time byte offset for a dynamic buffer descriptor.
	 */
	struct DynamicDescriptorOffset final
	{
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		std::uint64_t offset	 = 0;
	};

	/**
	 * \brief Allocates descriptor sets from one arena.
	 *
	 * Reset is valid only once safeAfter has passed and all GPU work that can reference the old sets has completed.
	 */
	class AZO_RHI_API DescriptorArena final
	{
	public:
		DescriptorArena() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_impl != nullptr && m_dispatch != nullptr;
		}

		[[nodiscard]] DescriptorSetHandle Allocate(const DescriptorSetAllocDesc & desc) noexcept;
		[[nodiscard]] DescriptorSetHandle Allocate(const DescriptorSetAllocDesc & desc, Error & error) noexcept;
		[[nodiscard]] Result<DescriptorSetHandle> AllocateWithResult(const DescriptorSetAllocDesc & desc) noexcept;
		[[nodiscard]] bool Reset(RetirePoint safeAfter) noexcept;
		[[nodiscard]] bool Reset(RetirePoint safeAfter, Error & error) noexcept;

	private:
		friend struct detail::FacadeBuilder;

		DescriptorArena(void * impl, const DescriptorArenaApi * dispatch, BackendBlockSet * blocks) noexcept
			: m_impl(impl),
			  m_dispatch(dispatch),
			  m_blocks(blocks)
		{
		}

		void * m_impl						  = nullptr;
		const DescriptorArenaApi * m_dispatch = nullptr;

		// Held for descriptor-set table guards, not for dispatch.
		BackendBlockSet * m_blocks = nullptr;
	};

} // namespace azo::rhi

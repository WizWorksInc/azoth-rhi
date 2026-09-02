// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace azo::rhi
{

	struct DescriptorArenaApi;
	class BackendBlockSet;

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

		eAnyHit = 1u << 7u,

		eClosestHit = 1u << 8u,

		eMiss = 1u << 9u,

		eIntersection = 1u << 10u,

		eCallable = 1u << 11u,

		eAllGraphics = 1u << 28u,

		eAllRayTracing = 1u << 29u,

		eAll = 0xffffffffu,
	};

	enum class DescriptorType : std::uint8_t
	{
		eSampler,

		eCombinedImageSampler,

		eTextureSRV,

		eTextureUAV,

		eBufferSRV,

		eBufferUAV,

		eUniformBuffer,

		eStorageBuffer,

		eDynamicUniformBuffer,

		eDynamicStorageBuffer,

		eTexelBufferSRV,

		eTexelBufferUAV,

		eAccelerationStructure,
	};

	enum class DescriptorBindingFlag : std::uint32_t // NOLINT(performance-enum-size)
	{
		eUpdateAfterBind = 1u << 0u,

		ePartiallyBound = 1u << 1u,

		eVariableDescriptorCount = 1u << 2u,

		eBindless = 1u << 3u,

		eRootDescriptorCandidate = 1u << 4u,
	};

	struct DescriptorBinding final
	{
		std::uint32_t binding	  = 0;
		DescriptorType type		  = DescriptorType::eUniformBuffer;
		std::uint32_t count		  = 1;
		Flags<ShaderStage> stages = ShaderStage::eAll;
		Flags<DescriptorBindingFlag> flags;

		std::span<const SamplerHandle> immutableSamplers;
	};

	struct DescriptorSetLayoutDesc final
	{
		std::span<const DescriptorBinding> bindings;
		const char * debugName = nullptr;
	};

	struct PushConstantRange final
	{
		Flags<ShaderStage> stages;
		std::uint32_t offset = 0;
		std::uint32_t size	 = 0;
	};

	struct PipelineLayoutDesc final
	{
		std::span<const DescriptorSetLayoutHandle> sets;
		std::span<const PushConstantRange> pushConstants;
		const char * debugName = nullptr;
	};

	enum class DescriptorArenaType : std::uint8_t
	{
		eFrameTransient,

		ePersistent
	};

	struct DescriptorArenaDesc final
	{
		DescriptorArenaType type	 = DescriptorArenaType::eFrameTransient;
		std::uint32_t maxSets		 = 0;
		std::uint32_t maxDescriptors = 0;
		bool shaderVisible			 = true;
		const char * debugName		 = nullptr;
	};

	struct DescriptorSetAllocDesc final
	{
		DescriptorSetLayoutHandle layout{};
		std::uint32_t variableDescriptorCount = 0;
		const char * debugName				  = nullptr;
	};

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

	struct DescriptorWriteTexture final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		DescriptorType type		 = DescriptorType::eTextureSRV;
		TextureViewHandle view{};
		SamplerHandle sampler{};
		Flags<ResourceUse> expectedUse = ResourceUse::eSampledRead;
	};

	struct DescriptorWriteSampler final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		SamplerHandle sampler{};
	};

	struct DescriptorWriteAccelerationStructure final
	{
		DescriptorSetHandle set{};
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		AccelerationStructureHandle accelerationStructure{};
	};

	struct DynamicDescriptorOffset final
	{
		std::uint32_t binding	 = 0;
		std::uint32_t arrayIndex = 0;
		std::uint64_t offset	 = 0;
	};

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

		BackendBlockSet * m_blocks = nullptr;
	};

}

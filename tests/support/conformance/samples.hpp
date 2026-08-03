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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include <array>
#include <cstdint>

namespace azo::rhi::test::samples
{

	inline constexpr std::uint64_t kBufferSize = 256;
	inline constexpr std::uint32_t kTextureDim = 64;

	[[nodiscard]] inline BufferDesc StorageBuffer(const std::uint64_t size = kBufferSize) noexcept
	{
		BufferDesc desc{};
		desc.size	   = size;
		desc.usage	   = Flags<BufferUsage>(BufferUsage::eStorage) | BufferUsage::eCopyDst | BufferUsage::eCopySrc;
		desc.memory	   = MemoryUsage::eGpuOnly;
		desc.debugName = "azoth.rhi.test.storageBuffer";
		return desc;
	}

	[[nodiscard]] inline BufferDesc UploadBuffer(const std::uint64_t size = kBufferSize) noexcept
	{
		BufferDesc desc{};
		desc.size	   = size;
		desc.usage	   = BufferUsage::eCopySrc;
		desc.memory	   = MemoryUsage::eCpuUpload;
		desc.debugName = "azoth.rhi.test.uploadBuffer";
		return desc;
	}

	[[nodiscard]] inline BufferDesc ReadbackBuffer(const std::uint64_t size = kBufferSize) noexcept
	{
		BufferDesc desc{};
		desc.size	   = size;
		desc.usage	   = BufferUsage::eCopyDst;
		desc.memory	   = MemoryUsage::eCpuReadback;
		desc.debugName = "azoth.rhi.test.readbackBuffer";
		return desc;
	}

	[[nodiscard]] inline TextureDesc SampledTexture2D(const std::uint32_t dimension = kTextureDim, const Format format = Format::eRGBA8UNorm) noexcept
	{
		TextureDesc desc{};
		desc.type	   = TextureType::eTex2D;
		desc.format	   = format;
		desc.width	   = dimension;
		desc.height	   = dimension;
		desc.usage	   = Flags<TextureUsage>(TextureUsage::eSampled) | TextureUsage::eCopyDst;
		desc.debugName = "azoth.rhi.test.sampledTexture";
		return desc;
	}

	[[nodiscard]] inline TextureDesc ColorTarget2D(const std::uint32_t dimension = kTextureDim, const Format format = Format::eRGBA8UNorm) noexcept
	{
		TextureDesc desc{};
		desc.type	   = TextureType::eTex2D;
		desc.format	   = format;
		desc.width	   = dimension;
		desc.height	   = dimension;
		desc.usage	   = Flags<TextureUsage>(TextureUsage::eColorAttachment) | TextureUsage::eSampled | TextureUsage::eCopySrc;
		desc.debugName = "azoth.rhi.test.colorTarget";
		return desc;
	}

	[[nodiscard]] inline TextureDesc DepthTarget2D(const std::uint32_t dimension = kTextureDim) noexcept
	{
		TextureDesc desc{};
		desc.type	   = TextureType::eTex2D;
		desc.format	   = Format::eD32Float;
		desc.width	   = dimension;
		desc.height	   = dimension;
		desc.usage	   = TextureUsage::eDepthStencilAttachment;
		desc.debugName = "azoth.rhi.test.depthTarget";
		return desc;
	}

	[[nodiscard]] inline TextureDesc MippedTexture2D(const std::uint32_t dimension = kTextureDim, const std::uint32_t mips = 4) noexcept
	{
		TextureDesc desc = SampledTexture2D(dimension);
		desc.mipLevels	 = mips;
		desc.usage		 = Flags<TextureUsage>(TextureUsage::eSampled) | TextureUsage::eCopyDst | TextureUsage::eCopySrc;
		desc.debugName	 = "azoth.rhi.test.mippedTexture";
		return desc;
	}

	[[nodiscard]] inline TextureViewDesc FullTextureView(const TextureViewType type = TextureViewType::eTex2D) noexcept
	{
		TextureViewDesc desc{};
		desc.type	   = type;
		desc.debugName = "azoth.rhi.test.textureView";
		return desc;
	}

	[[nodiscard]] inline SamplerDesc LinearSampler() noexcept
	{
		SamplerDesc desc{};
		desc.debugName = "azoth.rhi.test.sampler";
		return desc;
	}

	[[nodiscard]] inline HeapDesc GpuHeap(const std::uint64_t size = 1u << 20u) noexcept
	{
		HeapDesc desc{};
		desc.type	   = HeapType::eGpuLocal;
		desc.size	   = size;
		desc.debugName = "azoth.rhi.test.heap";
		return desc;
	}

	[[nodiscard]] inline TimelineDesc Timeline(const std::uint64_t initialValue = 0) noexcept
	{
		TimelineDesc desc{};
		desc.initialValue = initialValue;
		desc.debugName	  = "azoth.rhi.test.timeline";
		return desc;
	}

	[[nodiscard]] inline CommandPoolDesc CommandPool(const QueueType queueType = QueueType::eGraphics) noexcept
	{
		CommandPoolDesc desc{};
		desc.queueType = queueType;
		desc.debugName = "azoth.rhi.test.commandPool";
		return desc;
	}

	[[nodiscard]] inline DescriptorArenaDesc DescriptorArena(const std::uint32_t maxSets = 16, const std::uint32_t maxDescriptors = 64) noexcept
	{
		DescriptorArenaDesc desc{};
		desc.maxSets		= maxSets;
		desc.maxDescriptors = maxDescriptors;
		desc.debugName		= "azoth.rhi.test.descriptorArena";
		return desc;
	}

	[[nodiscard]] inline QueryPoolDesc TimestampPool(const std::uint32_t queryCount = 8) noexcept
	{
		QueryPoolDesc desc{};
		desc.type		= QueryType::eTimestamp;
		desc.queryCount = queryCount;
		desc.debugName	= "azoth.rhi.test.queryPool";
		return desc;
	}

	struct UniformLayout final
	{
		std::array<DescriptorBinding, 1> bindings{ DescriptorBinding{
			.binding = 0,
			.type	 = DescriptorType::eUniformBuffer,
			.count	 = 1,
		} };

		[[nodiscard]] DescriptorSetLayoutDesc Desc() const noexcept
		{
			return DescriptorSetLayoutDesc{
				.bindings  = bindings,
				.debugName = "azoth.rhi.test.setLayout",
			};
		}
	};

	struct SimplePipelineLayout final
	{
		std::array<DescriptorSetLayoutHandle, 1> sets{};
		std::array<PushConstantRange, 1> pushConstants{ PushConstantRange{
			.stages = ShaderStage::eAll,
			.offset = 0,
			.size	= 16,
		} };

		explicit SimplePipelineLayout(const DescriptorSetLayoutHandle layout) noexcept : sets{ layout } {}

		[[nodiscard]] PipelineLayoutDesc Desc() const noexcept
		{
			return PipelineLayoutDesc{
				.sets		   = sets,
				.pushConstants = pushConstants,
				.debugName	   = "azoth.rhi.test.pipelineLayout",
			};
		}
	};

	[[nodiscard]] inline TextureSubresourceRange WholeColorRange() noexcept
	{
		return TextureSubresourceRange{
			.aspects	= TextureAspect::eColor,
			.baseMip	= 0,
			.mipCount	= kAllMips,
			.baseLayer	= 0,
			.layerCount = kAllLayers,
		};
	}

} // namespace azo::rhi::test::samples

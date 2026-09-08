// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/resources/descriptors.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace azo::rhi
{
	class DescriptorBindingBuilder final
	{
	public:
		DescriptorBindingBuilder & Binding(std::uint32_t binding) noexcept
		{
			m_desc.binding = binding;
			return *this;
		}

		DescriptorBindingBuilder & Type(DescriptorType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		DescriptorBindingBuilder & Count(std::uint32_t count) noexcept
		{
			m_desc.count = count;
			return *this;
		}

		DescriptorBindingBuilder & Stages(Flags<ShaderStage> stages) noexcept
		{
			m_desc.stages = stages;
			return *this;
		}

		DescriptorBindingBuilder & Flags(Flags<DescriptorBindingFlag> flags) noexcept
		{
			m_desc.flags = flags;
			return *this;
		}

		DescriptorBindingBuilder & AddFlag(DescriptorBindingFlag flag) noexcept
		{
			m_desc.flags = m_desc.flags | flag;
			return *this;
		}

		[[nodiscard]] constexpr DescriptorBinding Build() const noexcept
		{
			return m_desc;
		}

	private:
		DescriptorBinding m_desc{};
	};

	class DescriptorSetLayoutBuilder final
	{
	public:
		DescriptorSetLayoutBuilder & Binding(DescriptorBinding binding)
		{
			m_bindings.push_back(binding);
			return *this;
		}

		DescriptorSetLayoutBuilder & Bindings(std::span<const DescriptorBinding> bindings)
		{
			m_bindings.assign(bindings.begin(), bindings.end());
			return *this;
		}

		DescriptorSetLayoutBuilder & ClearBindings() noexcept
		{
			m_bindings.clear();
			return *this;
		}

		DescriptorSetLayoutBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] DescriptorSetLayoutDesc Build() const noexcept
		{
			return DescriptorSetLayoutDesc{ .bindings = std::span<const DescriptorBinding>{ m_bindings.data(), m_bindings.size() },
				.debugName							  = m_debugName.empty() ? nullptr : m_debugName.c_str() };
		}

	private:
		std::vector<DescriptorBinding> m_bindings;
		std::string m_debugName;
	};

	class PipelineLayoutBuilder final
	{
	public:
		PipelineLayoutBuilder & Set(DescriptorSetLayoutHandle set)
		{
			m_sets.push_back(set);
			return *this;
		}

		PipelineLayoutBuilder & Sets(std::span<const DescriptorSetLayoutHandle> sets)
		{
			m_sets.assign(sets.begin(), sets.end());
			return *this;
		}

		PipelineLayoutBuilder & PushConstant(PushConstantRange range)
		{
			m_pushConstants.push_back(range);
			return *this;
		}

		PipelineLayoutBuilder & PushConstant(Flags<ShaderStage> stages, std::uint32_t offset, std::uint32_t size)
		{
			m_pushConstants.push_back(PushConstantRange{
				.stages = stages,
				.offset = offset,
				.size	= size,
			});
			return *this;
		}

		PipelineLayoutBuilder & PushConstants(std::span<const PushConstantRange> ranges)
		{
			m_pushConstants.assign(ranges.begin(), ranges.end());
			return *this;
		}

		PipelineLayoutBuilder & ClearSets() noexcept
		{
			m_sets.clear();
			return *this;
		}

		PipelineLayoutBuilder & ClearPushConstants() noexcept
		{
			m_pushConstants.clear();
			return *this;
		}

		PipelineLayoutBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] PipelineLayoutDesc Build() const noexcept
		{
			return PipelineLayoutDesc{ .sets = std::span<const DescriptorSetLayoutHandle>{ m_sets.data(), m_sets.size() },
				.pushConstants				 = std::span<const PushConstantRange>{ m_pushConstants.data(), m_pushConstants.size() },
				.debugName					 = m_debugName.empty() ? nullptr : m_debugName.c_str() };
		}

	private:
		std::vector<DescriptorSetLayoutHandle> m_sets;
		std::vector<PushConstantRange> m_pushConstants;
		std::string m_debugName;
	};

	class DescriptorArenaBuilder final
	{
	public:
		DescriptorArenaBuilder & Type(DescriptorArenaType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		DescriptorArenaBuilder & FrameTransient() noexcept
		{
			return Type(DescriptorArenaType::eFrameTransient);
		}

		DescriptorArenaBuilder & Persistent() noexcept
		{
			return Type(DescriptorArenaType::ePersistent);
		}

		DescriptorArenaBuilder & MaxSets(std::uint32_t maxSets) noexcept
		{
			m_desc.maxSets = maxSets;
			return *this;
		}

		DescriptorArenaBuilder & MaxDescriptors(std::uint32_t maxDescriptors) noexcept
		{
			m_desc.maxDescriptors = maxDescriptors;
			return *this;
		}

		DescriptorArenaBuilder & ShaderVisible(bool enabled = true) noexcept
		{
			m_desc.shaderVisible = enabled;
			return *this;
		}

		DescriptorArenaBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] DescriptorArenaDesc Build() const noexcept
		{
			DescriptorArenaDesc desc = m_desc;
			desc.debugName			 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		DescriptorArenaDesc m_desc{};
		std::string m_debugName;
	};

	class DescriptorSetAllocBuilder final
	{
	public:
		DescriptorSetAllocBuilder & Layout(DescriptorSetLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		DescriptorSetAllocBuilder & VariableDescriptorCount(std::uint32_t count) noexcept
		{
			m_desc.variableDescriptorCount = count;
			return *this;
		}

		DescriptorSetAllocBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] DescriptorSetAllocDesc Build() const noexcept
		{
			DescriptorSetAllocDesc desc = m_desc;
			desc.debugName				= m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

	private:
		DescriptorSetAllocDesc m_desc{};
		std::string m_debugName;
	};

	class DescriptorWriteBufferBuilder final
	{
	public:
		DescriptorWriteBufferBuilder & Set(DescriptorSetHandle set) noexcept
		{
			m_desc.set = set;
			return *this;
		}

		DescriptorWriteBufferBuilder & Binding(std::uint32_t binding, std::uint32_t arrayIndex = 0) noexcept
		{
			m_desc.binding	  = binding;
			m_desc.arrayIndex = arrayIndex;
			return *this;
		}

		DescriptorWriteBufferBuilder & Type(DescriptorType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		DescriptorWriteBufferBuilder & Buffer(BufferHandle buffer) noexcept
		{
			m_desc.buffer = buffer;
			return *this;
		}

		DescriptorWriteBufferBuilder & Range(std::uint64_t offset, std::uint64_t range) noexcept
		{
			m_desc.offset = offset;
			m_desc.range  = range;
			return *this;
		}

		[[nodiscard]] constexpr DescriptorWriteBuffer Build() const noexcept
		{
			return m_desc;
		}

	private:
		DescriptorWriteBuffer m_desc{};
	};

	class DescriptorWriteTextureBuilder final
	{
	public:
		DescriptorWriteTextureBuilder & Set(DescriptorSetHandle set) noexcept
		{
			m_desc.set = set;
			return *this;
		}

		DescriptorWriteTextureBuilder & Binding(std::uint32_t binding, std::uint32_t arrayIndex = 0) noexcept
		{
			m_desc.binding	  = binding;
			m_desc.arrayIndex = arrayIndex;
			return *this;
		}

		DescriptorWriteTextureBuilder & Type(DescriptorType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		DescriptorWriteTextureBuilder & View(TextureViewHandle view) noexcept
		{
			m_desc.view = view;
			return *this;
		}

		DescriptorWriteTextureBuilder & Sampler(SamplerHandle sampler) noexcept
		{
			m_desc.sampler = sampler;
			return *this;
		}

		DescriptorWriteTextureBuilder & ExpectedUse(Flags<ResourceUse> use) noexcept
		{
			m_desc.expectedUse = use;
			return *this;
		}

		[[nodiscard]] constexpr DescriptorWriteTexture Build() const noexcept
		{
			return m_desc;
		}

	private:
		DescriptorWriteTexture m_desc{};
	};

	class DescriptorWriteSamplerBuilder final
	{
	public:
		DescriptorWriteSamplerBuilder & Set(DescriptorSetHandle set) noexcept
		{
			m_desc.set = set;
			return *this;
		}

		DescriptorWriteSamplerBuilder & Binding(std::uint32_t binding, std::uint32_t arrayIndex = 0) noexcept
		{
			m_desc.binding	  = binding;
			m_desc.arrayIndex = arrayIndex;
			return *this;
		}

		DescriptorWriteSamplerBuilder & Sampler(SamplerHandle sampler) noexcept
		{
			m_desc.sampler = sampler;
			return *this;
		}

		[[nodiscard]] constexpr DescriptorWriteSampler Build() const noexcept
		{
			return m_desc;
		}

	private:
		DescriptorWriteSampler m_desc{};
	};

	class DescriptorWriteAccelerationStructureBuilder final
	{
	public:
		DescriptorWriteAccelerationStructureBuilder & Set(DescriptorSetHandle set) noexcept
		{
			m_desc.set = set;
			return *this;
		}

		DescriptorWriteAccelerationStructureBuilder & Binding(std::uint32_t binding, std::uint32_t arrayIndex = 0) noexcept
		{
			m_desc.binding	  = binding;
			m_desc.arrayIndex = arrayIndex;
			return *this;
		}

		DescriptorWriteAccelerationStructureBuilder & AccelerationStructure(AccelerationStructureHandle accelerationStructure) noexcept
		{
			m_desc.accelerationStructure = accelerationStructure;
			return *this;
		}

		[[nodiscard]] constexpr DescriptorWriteAccelerationStructure Build() const noexcept
		{
			return m_desc;
		}

	private:
		DescriptorWriteAccelerationStructure m_desc{};
	};
}

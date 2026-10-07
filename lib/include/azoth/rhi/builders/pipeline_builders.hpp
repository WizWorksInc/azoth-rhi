// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace azo::rhi
{
	template <class BuilderT>
	class Built final
	{
	public:
		using DescType = decltype(std::declval<const BuilderT &>().BorrowedDesc());

		[[nodiscard]] DescType desc() const & noexcept
		{
			return m_storage.BorrowedDesc();
		}

		[[nodiscard]] DescType desc() const && = delete;

		// NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,hicpp-explicit-conversions,misc-explicit-constructor)
		[[nodiscard]] operator DescType() const & noexcept
		{
			return m_storage.BorrowedDesc();
		}

		[[nodiscard]] operator DescType() const && = delete;

	private:
		friend BuilderT;

		explicit Built(BuilderT storage) noexcept : m_storage(std::move(storage)) {}

		BuilderT m_storage;
	};

	class PipelineCacheBuilder final
	{
	public:
		PipelineCacheBuilder & initial_data(const void * data, std::size_t size) noexcept
		{
			m_desc.initialData = data;
			m_desc.initialSize = size;
			return *this;
		}

		PipelineCacheBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<PipelineCacheBuilder> build() &&
		{
			return Built<PipelineCacheBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<PipelineCacheBuilder> build() const &
		{
			return Built<PipelineCacheBuilder>{ *this };
		}

	private:
		friend class Built<PipelineCacheBuilder>;

		[[nodiscard]] PipelineCacheDesc BorrowedDesc() const noexcept
		{
			PipelineCacheDesc desc = m_desc;
			desc.debugName		   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		PipelineCacheDesc m_desc{};
		std::string m_debugName;
	};

	class ShaderBinaryBuilder final
	{
	public:
		ShaderBinaryBuilder & stage(ShaderStage stage) noexcept
		{
			m_desc.stage = stage;
			return *this;
		}

		ShaderBinaryBuilder & format(ShaderBinaryFormat format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		ShaderBinaryBuilder & spir_v() noexcept
		{
			return format(ShaderBinaryFormat::eSpirV);
		}

		ShaderBinaryBuilder & dxil() noexcept
		{
			return format(ShaderBinaryFormat::eDxil);
		}

		ShaderBinaryBuilder & data(const void * data, std::size_t size) noexcept
		{
			m_desc.data = data;
			m_desc.size = size;
			return *this;
		}

		ShaderBinaryBuilder & entry_point(const char * entryPoint) noexcept
		{
			m_desc.entryPoint = entryPoint;
			return *this;
		}

		ShaderBinaryBuilder & binding_map(const ShaderBindingMap * map) noexcept
		{
			m_desc.bindingMap = map;
			return *this;
		}

		ShaderBinaryBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<ShaderBinaryBuilder> build() &&
		{
			return Built<ShaderBinaryBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<ShaderBinaryBuilder> build() const &
		{
			return Built<ShaderBinaryBuilder>{ *this };
		}

	private:
		friend class Built<ShaderBinaryBuilder>;

		[[nodiscard]] ShaderBinary BorrowedDesc() const noexcept
		{
			ShaderBinary desc = m_desc;
			desc.debugName	  = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		ShaderBinary m_desc{};
		std::string m_debugName;
	};

	class VertexBindingBuilder final
	{
	public:
		VertexBindingBuilder & binding(std::uint32_t binding) noexcept
		{
			m_desc.binding = binding;
			return *this;
		}

		VertexBindingBuilder & stride(std::uint32_t stride) noexcept
		{
			m_desc.stride = stride;
			return *this;
		}

		VertexBindingBuilder & per_instance(bool enabled = true) noexcept
		{
			m_desc.perInstance = enabled;
			return *this;
		}

		[[nodiscard]] constexpr VertexBindingDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		VertexBindingDesc m_desc{};
	};

	class VertexAttributeBuilder final
	{
	public:
		VertexAttributeBuilder & location(std::uint32_t location) noexcept
		{
			m_desc.location = location;
			return *this;
		}

		VertexAttributeBuilder & binding(std::uint32_t binding) noexcept
		{
			m_desc.binding = binding;
			return *this;
		}

		VertexAttributeBuilder & format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		VertexAttributeBuilder & offset(std::uint32_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr VertexAttributeDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		VertexAttributeDesc m_desc{};
	};

	class RasterStateBuilder final
	{
	public:
		RasterStateBuilder & fill(FillMode fillMode) noexcept
		{
			m_desc.fillMode = fillMode;
			return *this;
		}

		RasterStateBuilder & cull(CullMode cullMode) noexcept
		{
			m_desc.cullMode = cullMode;
			return *this;
		}

		RasterStateBuilder & front_face(FrontFace frontFace) noexcept
		{
			m_desc.frontFace = frontFace;
			return *this;
		}

		RasterStateBuilder & depth_clamp(bool enabled = true) noexcept
		{
			m_desc.depthClampEnable = enabled;
			return *this;
		}

		RasterStateBuilder & rasterizer_discard(bool enabled = true) noexcept
		{
			m_desc.rasterizerDiscardEnable = enabled;
			return *this;
		}

		RasterStateBuilder & depth_bias(float constantFactor, float slopeFactor, float clamp = 0.0f) noexcept
		{
			m_desc.depthBiasEnable		   = true;
			m_desc.depthBiasConstantFactor = constantFactor;
			m_desc.depthBiasSlopeFactor	   = slopeFactor;
			m_desc.depthBiasClamp		   = clamp;
			return *this;
		}

		RasterStateBuilder & disable_depth_bias() noexcept
		{
			m_desc.depthBiasEnable = false;
			return *this;
		}

		[[nodiscard]] constexpr RasterStateDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		RasterStateDesc m_desc{};
	};

	class DepthStencilStateBuilder final
	{
	public:
		DepthStencilStateBuilder & depth_test(bool enabled = true) noexcept
		{
			m_desc.depthTestEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & depth_write(bool enabled = true) noexcept
		{
			m_desc.depthWriteEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & depth_compare(CompareOp op) noexcept
		{
			m_desc.depthCompareOp = op;
			return *this;
		}

		DepthStencilStateBuilder & depth_bounds(float minDepth, float maxDepth, bool enabled = true) noexcept
		{
			m_desc.depthBoundsTestEnable = enabled;
			m_desc.minDepthBounds		 = minDepth;
			m_desc.maxDepthBounds		 = maxDepth;
			return *this;
		}

		DepthStencilStateBuilder & stencil_test(bool enabled = true) noexcept
		{
			m_desc.stencilTestEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & front(StencilFaceDesc front) noexcept
		{
			m_desc.front = front;
			return *this;
		}

		DepthStencilStateBuilder & back(StencilFaceDesc back) noexcept
		{
			m_desc.back = back;
			return *this;
		}

		[[nodiscard]] constexpr DepthStencilStateDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		DepthStencilStateDesc m_desc{};
	};

	class ColorBlendAttachmentBuilder final
	{
	public:
		ColorBlendAttachmentBuilder & enable(bool enabled = true) noexcept
		{
			m_desc.blendEnable = enabled;
			return *this;
		}

		ColorBlendAttachmentBuilder & color(BlendFactor src, BlendFactor dst, BlendOp op = BlendOp::eAdd) noexcept
		{
			m_desc.srcColorBlendFactor = src;
			m_desc.dstColorBlendFactor = dst;
			m_desc.colorBlendOp		   = op;
			return *this;
		}

		ColorBlendAttachmentBuilder & alpha(BlendFactor src, BlendFactor dst, BlendOp op = BlendOp::eAdd) noexcept
		{
			m_desc.srcAlphaBlendFactor = src;
			m_desc.dstAlphaBlendFactor = dst;
			m_desc.alphaBlendOp		   = op;
			return *this;
		}

		ColorBlendAttachmentBuilder & write_mask(Flags<ColorWrite> mask) noexcept
		{
			m_desc.colorWriteMask = mask;
			return *this;
		}

		[[nodiscard]] constexpr ColorBlendAttachmentDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		ColorBlendAttachmentDesc m_desc{};
	};

	class BlendStateBuilder final
	{
	public:
		BlendStateBuilder & logic_op(bool enabled = true) noexcept
		{
			m_desc.logicOpEnable = enabled;
			return *this;
		}

		BlendStateBuilder & constants(float r, float g, float b, float a) noexcept
		{
			m_desc.blendConstants = { r, g, b, a };
			return *this;
		}

		BlendStateBuilder & attachment(ColorBlendAttachmentDesc attachment)
		{
			if (m_desc.attachmentCount < m_desc.attachments.size())
			{
				azo::rhi::detail::at(m_desc.attachments, m_desc.attachmentCount) = attachment;
				++m_desc.attachmentCount;
			}

			return *this;
		}

		BlendStateBuilder & attachment(std::uint32_t index, ColorBlendAttachmentDesc attachment) noexcept
		{
			if (index < m_desc.attachments.size())
			{
				azo::rhi::detail::at(m_desc.attachments, index) = attachment;

				if (m_desc.attachmentCount <= index)
				{
					m_desc.attachmentCount = index + 1;
				}
			}

			return *this;
		}

		BlendStateBuilder & attachment_count(std::uint32_t count) noexcept
		{
			m_desc.attachmentCount = count;
			return *this;
		}

		[[nodiscard]] constexpr BlendStateDesc build() const noexcept
		{
			return m_desc;
		}

	private:
		BlendStateDesc m_desc{};
	};

	class GraphicsPipelineBuilder final
	{
	public:
		GraphicsPipelineBuilder & layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		GraphicsPipelineBuilder & shader(ShaderBinary shader)
		{
			m_shaders.push_back(shader);
			return *this;
		}

		GraphicsPipelineBuilder & shaders(std::span<const ShaderBinary> shaders)
		{
			m_shaders.assign(shaders.begin(), shaders.end());
			return *this;
		}

		GraphicsPipelineBuilder & vertex_binding(VertexBindingDesc binding)
		{
			m_vertexBindings.push_back(binding);
			return *this;
		}

		GraphicsPipelineBuilder & vertex_bindings(std::span<const VertexBindingDesc> bindings)
		{
			m_vertexBindings.assign(bindings.begin(), bindings.end());
			return *this;
		}

		GraphicsPipelineBuilder & vertex_attribute(VertexAttributeDesc attribute)
		{
			m_vertexAttributes.push_back(attribute);
			return *this;
		}

		GraphicsPipelineBuilder & vertex_attributes(std::span<const VertexAttributeDesc> attributes)
		{
			m_vertexAttributes.assign(attributes.begin(), attributes.end());
			return *this;
		}

		GraphicsPipelineBuilder & topology(PrimitiveTopology topology) noexcept
		{
			m_vertexInput.topology = topology;
			return *this;
		}

		GraphicsPipelineBuilder & primitive_restart(bool enabled = true) noexcept
		{
			m_vertexInput.primitiveRestartEnable = enabled;
			return *this;
		}

		GraphicsPipelineBuilder & no_vertex_input() noexcept
		{
			m_sourcesVertices = false;
			return *this;
		}

		GraphicsPipelineBuilder & raster(RasterStateDesc raster) noexcept
		{
			m_desc.raster = raster;
			return *this;
		}

		GraphicsPipelineBuilder & depth_stencil(DepthStencilStateDesc depthStencil) noexcept
		{
			m_desc.depthStencil = depthStencil;
			return *this;
		}

		GraphicsPipelineBuilder & blend(BlendStateDesc blend) noexcept
		{
			m_desc.blend = blend;
			return *this;
		}

		GraphicsPipelineBuilder & color_format(Format format)
		{
			if (m_renderTarget.colorFormatCount < m_renderTarget.colorFormats.size())
			{
				azo::rhi::detail::at(m_renderTarget.colorFormats, m_renderTarget.colorFormatCount) = format;
				++m_renderTarget.colorFormatCount;
			}

			return *this;
		}

		GraphicsPipelineBuilder & color_format(std::uint32_t index, Format format) noexcept
		{
			if (index < m_renderTarget.colorFormats.size())
			{
				azo::rhi::detail::at(m_renderTarget.colorFormats, index) = format;

				if (m_renderTarget.colorFormatCount <= index)
				{
					m_renderTarget.colorFormatCount = index + 1;
				}
			}

			return *this;
		}

		GraphicsPipelineBuilder & depth_stencil_format(Format format) noexcept
		{
			m_renderTarget.depthStencilFormat = format;
			return *this;
		}

		GraphicsPipelineBuilder & samples(SampleCount samples) noexcept
		{
			m_renderTarget.samples = samples;
			return *this;
		}

		GraphicsPipelineBuilder & sample_mask(std::uint32_t sampleMask) noexcept
		{
			m_renderTarget.sampleMask = sampleMask;
			return *this;
		}

		GraphicsPipelineBuilder & alpha_to_coverage(bool enabled = true) noexcept
		{
			m_renderTarget.alphaToCoverageEnable = enabled;
			return *this;
		}

		GraphicsPipelineBuilder & pipeline_cache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		GraphicsPipelineBuilder & dynamic_states(Flags<DynamicState> dynamicStates) noexcept
		{
			m_desc.dynamicStates = dynamicStates;
			return *this;
		}

		GraphicsPipelineBuilder & add_dynamic_state(DynamicState state) noexcept
		{
			m_desc.dynamicStates = m_desc.dynamicStates | state;
			return *this;
		}

		GraphicsPipelineBuilder & dynamic_viewport_scissor() noexcept
		{
			return add_dynamic_state(DynamicState::eViewport).add_dynamic_state(DynamicState::eScissor);
		}

		GraphicsPipelineBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<GraphicsPipelineBuilder> build() &&
		{
			return Built<GraphicsPipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<GraphicsPipelineBuilder> build() const &
		{
			return Built<GraphicsPipelineBuilder>{ *this };
		}

	private:
		friend class Built<GraphicsPipelineBuilder>;

		[[nodiscard]] GraphicsPipelineDesc BorrowedDesc() const noexcept
		{
			m_vertexInput.bindings	 = std::span<const VertexBindingDesc>{ m_vertexBindings.data(), m_vertexBindings.size() };
			m_vertexInput.attributes = std::span<const VertexAttributeDesc>{ m_vertexAttributes.data(), m_vertexAttributes.size() };

			GraphicsPipelineDesc desc = m_desc;
			desc.shaders			  = std::span<const ShaderBinary>{ m_shaders.data(), m_shaders.size() };
			desc.vertexInput		  = m_sourcesVertices ? &m_vertexInput : nullptr;
			desc.renderTarget		  = m_renderTarget;
			desc.debugName			  = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		GraphicsPipelineDesc m_desc{};

		mutable VertexInputDesc m_vertexInput{};
		RenderTargetDesc m_renderTarget{};
		bool m_sourcesVertices = true;

		std::vector<ShaderBinary> m_shaders;
		std::vector<VertexBindingDesc> m_vertexBindings;
		std::vector<VertexAttributeDesc> m_vertexAttributes;
		std::string m_debugName;
	};

	class ComputePipelineBuilder final
	{
	public:
		ComputePipelineBuilder & layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		ComputePipelineBuilder & shader(ShaderBinary shader) noexcept
		{
			m_desc.shader = shader;
			return *this;
		}

		ComputePipelineBuilder & pipeline_cache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		ComputePipelineBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<ComputePipelineBuilder> build() &&
		{
			return Built<ComputePipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<ComputePipelineBuilder> build() const &
		{
			return Built<ComputePipelineBuilder>{ *this };
		}

	private:
		friend class Built<ComputePipelineBuilder>;

		[[nodiscard]] ComputePipelineDesc BorrowedDesc() const noexcept
		{
			ComputePipelineDesc desc = m_desc;
			desc.debugName			 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		ComputePipelineDesc m_desc{};
		std::string m_debugName;
	};

	class AccelerationStructureBuilder final
	{
	public:
		AccelerationStructureBuilder & type(AccelerationStructureType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		AccelerationStructureBuilder & bottom_level() noexcept
		{
			return type(AccelerationStructureType::eBottomLevel);
		}

		AccelerationStructureBuilder & top_level() noexcept
		{
			return type(AccelerationStructureType::eTopLevel);
		}

		AccelerationStructureBuilder & storage(BufferHandle storage, std::uint64_t offset, std::uint64_t size) noexcept
		{
			m_desc.storage		 = storage;
			m_desc.storageOffset = offset;
			m_desc.size			 = size;
			return *this;
		}

		AccelerationStructureBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<AccelerationStructureBuilder> build() &&
		{
			return Built<AccelerationStructureBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<AccelerationStructureBuilder> build() const &
		{
			return Built<AccelerationStructureBuilder>{ *this };
		}

	private:
		friend class Built<AccelerationStructureBuilder>;

		[[nodiscard]] AccelerationStructureDesc BorrowedDesc() const noexcept
		{
			AccelerationStructureDesc desc = m_desc;
			desc.debugName				   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		AccelerationStructureDesc m_desc{};
		std::string m_debugName;
	};

	class AccelerationStructureBuildBuilder final
	{
	public:
		AccelerationStructureBuildBuilder & dst(AccelerationStructureHandle dst) noexcept
		{
			m_desc.dst = dst;
			return *this;
		}

		AccelerationStructureBuildBuilder & src(AccelerationStructureHandle src) noexcept
		{
			m_desc.src = src;
			return *this;
		}

		AccelerationStructureBuildBuilder & mode(AccelerationStructureBuildMode mode) noexcept
		{
			m_desc.mode = mode;
			return *this;
		}

		AccelerationStructureBuildBuilder & flags(Flags<AccelerationStructureBuildFlag> flags) noexcept
		{
			m_desc.flags = flags;
			return *this;
		}

		AccelerationStructureBuildBuilder & add_flag(AccelerationStructureBuildFlag flag) noexcept
		{
			m_desc.flags = m_desc.flags | flag;
			return *this;
		}

		AccelerationStructureBuildBuilder & geometry(AccelerationStructureGeometryDesc geometry)
		{
			m_geometries.push_back(geometry);
			return *this;
		}

		AccelerationStructureBuildBuilder & geometries(std::span<const AccelerationStructureGeometryDesc> geometries)
		{
			m_geometries.assign(geometries.begin(), geometries.end());
			return *this;
		}

		AccelerationStructureBuildBuilder & instances(BufferHandle instanceBuffer, std::uint64_t offset, std::uint32_t count) noexcept
		{
			m_desc.instanceBuffer = instanceBuffer;
			m_desc.instanceOffset = offset;
			m_desc.instanceCount  = count;
			return *this;
		}

		AccelerationStructureBuildBuilder & scratch(BufferHandle scratchBuffer, std::uint64_t offset) noexcept
		{
			m_desc.scratchBuffer = scratchBuffer;
			m_desc.scratchOffset = offset;
			return *this;
		}

		[[nodiscard]] Built<AccelerationStructureBuildBuilder> build() &&
		{
			return Built<AccelerationStructureBuildBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<AccelerationStructureBuildBuilder> build() const &
		{
			return Built<AccelerationStructureBuildBuilder>{ *this };
		}

	private:
		friend class Built<AccelerationStructureBuildBuilder>;

		[[nodiscard]] AccelerationStructureBuildDesc BorrowedDesc() const noexcept
		{
			AccelerationStructureBuildDesc desc = m_desc;
			desc.geometries						= std::span<const AccelerationStructureGeometryDesc>{ m_geometries.data(), m_geometries.size() };
			return desc;
		}

		AccelerationStructureBuildDesc m_desc{};
		std::vector<AccelerationStructureGeometryDesc> m_geometries;
	};

	class RayTracingPipelineBuilder final
	{
	public:
		RayTracingPipelineBuilder & layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		RayTracingPipelineBuilder & shader(ShaderBinary shader)
		{
			m_shaders.push_back(shader);
			return *this;
		}

		RayTracingPipelineBuilder & shaders(std::span<const ShaderBinary> shaders)
		{
			m_shaders.assign(shaders.begin(), shaders.end());
			return *this;
		}

		RayTracingPipelineBuilder & group(RayTracingShaderGroupDesc group)
		{
			m_groups.push_back(group);
			return *this;
		}

		RayTracingPipelineBuilder & groups(std::span<const RayTracingShaderGroupDesc> groups)
		{
			m_groups.assign(groups.begin(), groups.end());
			return *this;
		}

		RayTracingPipelineBuilder & max_ray_recursion_depth(std::uint32_t depth) noexcept
		{
			m_desc.maxRayRecursionDepth = depth;
			return *this;
		}

		RayTracingPipelineBuilder & max_payload_bytes(std::uint32_t bytes) noexcept
		{
			m_desc.maxPayloadBytes = bytes;
			return *this;
		}

		RayTracingPipelineBuilder & max_attribute_bytes(std::uint32_t bytes) noexcept
		{
			m_desc.maxAttributeBytes = bytes;
			return *this;
		}

		RayTracingPipelineBuilder & pipeline_cache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		RayTracingPipelineBuilder & debug_name(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		[[nodiscard]] Built<RayTracingPipelineBuilder> build() &&
		{
			return Built<RayTracingPipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<RayTracingPipelineBuilder> build() const &
		{
			return Built<RayTracingPipelineBuilder>{ *this };
		}

	private:
		friend class Built<RayTracingPipelineBuilder>;

		[[nodiscard]] RayTracingPipelineDesc BorrowedDesc() const noexcept
		{
			RayTracingPipelineDesc desc = m_desc;
			desc.shaders				= std::span<const ShaderBinary>{ m_shaders.data(), m_shaders.size() };
			desc.groups					= std::span<const RayTracingShaderGroupDesc>{ m_groups.data(), m_groups.size() };
			desc.debugName				= m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		RayTracingPipelineDesc m_desc{};
		std::vector<ShaderBinary> m_shaders;
		std::vector<RayTracingShaderGroupDesc> m_groups;
		std::string m_debugName;
	};
}

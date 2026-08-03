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
 * \brief Builders for pipeline, shader, and ray tracing descriptions.
 */

#include "azoth/rhi/resources/pipeline.hpp"

#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace azo::rhi
{
	/**
	 * \brief A finished description plus the storage its spans and names point at.
	 *
	 * Descriptions carry spans and const char * across the ABI boundary so something must own what they point at. Build hands that storage here, not leaving it in
	 * the builder, which is what lets a caller build from a temporary.
	 *
	 * \attention The description borrows from this object so it has to outlive the creation call. Build is [[nodiscard]] because dropping the result destroys
	 * the description.
	 */
	template <class BuilderT>
	class Built final
	{
	public:
		using DescType = decltype(std::declval<const BuilderT &>().BorrowedDesc());

		/**
		 * \brief Returns the description, borrowing this object's storage.
		 */
		[[nodiscard]] DescType Desc() const & noexcept
		{
			return m_storage.BorrowedDesc();
		}

		/**
		 * \brief Deleted, so the built result has to be stored before its description is taken.
		 *
		 * builder.Build().Desc() would hand back a description pointing into a temporary that dies at the end of the full expression. Build returns an owning object
		 * so the result can be stored first. Deleting this overload turns skipping that step into a compile error and not a use-after-free.
		 */
		[[nodiscard]] DescType Desc() const && = delete;

		// Implicit so a stored result passes straight to a creation call without naming the description type. NOLINTNEXTLINE(hicpp-explicit-conversions)
		[[nodiscard]] operator DescType() const & noexcept
		{
			return m_storage.BorrowedDesc();
		}

		// Deleted for the same reason as Desc, so passing an unstored result straight into a creation call does not compile either.
		[[nodiscard]] operator DescType() const && = delete;

	private:
		friend BuilderT;

		explicit Built(BuilderT storage) noexcept : m_storage(std::move(storage)) {}

		BuilderT m_storage;
	};

	/**
	 * \brief Builds pipeline-cache descriptions backed by owned debug-name storage.
	 *
	 * \attention Initial cache data is referenced, not copied.
	 */
	class PipelineCacheBuilder final
	{
	public:
		/**
		 * \brief Sets initial pipeline-cache data.
		 *
		 * \param data Cache blob storage referenced by the description.
		 * \param size Byte size of data.
		 * \attention data must remain valid through pipeline-cache creation.
		 */
		PipelineCacheBuilder & InitialData(const void * data, std::size_t size) noexcept
		{
			m_desc.initialData = data;
			m_desc.initialSize = size;
			return *this;
		}

		PipelineCacheBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<PipelineCacheBuilder> Build() &&
		{
			return Built<PipelineCacheBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<PipelineCacheBuilder> Build() const &
		{
			return Built<PipelineCacheBuilder>{ *this };
		}

	private:
		friend class Built<PipelineCacheBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
		[[nodiscard]] PipelineCacheDesc BorrowedDesc() const noexcept
		{
			PipelineCacheDesc desc = m_desc;
			desc.debugName		   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		PipelineCacheDesc m_desc{};
		std::string m_debugName;
	};

	/**
	 * \brief Builds shader binary descriptions backed by owned debug-name storage.
	 *
	 * \attention Shader data, entry-point text, and shader interface storage are referenced, not copied.
	 */
	class ShaderBinaryBuilder final
	{
	public:
		ShaderBinaryBuilder & Stage(ShaderStage stage) noexcept
		{
			m_desc.stage = stage;
			return *this;
		}

		ShaderBinaryBuilder & Format(ShaderBinaryFormat format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		ShaderBinaryBuilder & SpirV() noexcept
		{
			return Format(ShaderBinaryFormat::eSpirV);
		}

		ShaderBinaryBuilder & Dxil() noexcept
		{
			return Format(ShaderBinaryFormat::eDxil);
		}

		/**
		 * \brief Sets shader bytecode storage.
		 *
		 * \param data Shader bytecode storage referenced by the description.
		 * \param size Byte size of data.
		 * \attention data must remain valid through pipeline creation.
		 */
		ShaderBinaryBuilder & Data(const void * data, std::size_t size) noexcept
		{
			m_desc.data = data;
			m_desc.size = size;
			return *this;
		}

		/**
		 * \brief Sets the shader entry-point string.
		 *
		 * \attention entryPoint is referenced, not copied, and must remain valid through pipeline creation.
		 */
		ShaderBinaryBuilder & EntryPoint(const char * entryPoint) noexcept
		{
			m_desc.entryPoint = entryPoint;
			return *this;
		}

		/**
		 * \brief States where this binary put its bindings, for one that does not follow the published binding ABI.
		 *
		 * \attention map is referenced, not copied, and must remain valid through pipeline creation.
		 */
		ShaderBinaryBuilder & BindingMap(const ShaderBindingMap * map) noexcept
		{
			m_desc.bindingMap = map;
			return *this;
		}

		ShaderBinaryBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<ShaderBinaryBuilder> Build() &&
		{
			return Built<ShaderBinaryBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<ShaderBinaryBuilder> Build() const &
		{
			return Built<ShaderBinaryBuilder>{ *this };
		}

	private:
		friend class Built<ShaderBinaryBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
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
		VertexBindingBuilder & Binding(std::uint32_t binding) noexcept
		{
			m_desc.binding = binding;
			return *this;
		}

		/**
		 * \brief Sets the byte stride between consecutive vertex records for this binding.
		 */
		VertexBindingBuilder & Stride(std::uint32_t stride) noexcept
		{
			m_desc.stride = stride;
			return *this;
		}

		VertexBindingBuilder & PerInstance(bool enabled = true) noexcept
		{
			m_desc.perInstance = enabled;
			return *this;
		}

		[[nodiscard]] constexpr VertexBindingDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		VertexBindingDesc m_desc{};
	};

	class VertexAttributeBuilder final
	{
	public:
		VertexAttributeBuilder & Location(std::uint32_t location) noexcept
		{
			m_desc.location = location;
			return *this;
		}

		VertexAttributeBuilder & Binding(std::uint32_t binding) noexcept
		{
			m_desc.binding = binding;
			return *this;
		}

		VertexAttributeBuilder & Format(Format format) noexcept
		{
			m_desc.format = format;
			return *this;
		}

		/**
		 * \brief Sets the byte offset of this attribute within one vertex record.
		 */
		VertexAttributeBuilder & Offset(std::uint32_t offset) noexcept
		{
			m_desc.offset = offset;
			return *this;
		}

		[[nodiscard]] constexpr VertexAttributeDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		VertexAttributeDesc m_desc{};
	};

	class RasterStateBuilder final
	{
	public:
		RasterStateBuilder & Fill(FillMode fillMode) noexcept
		{
			m_desc.fillMode = fillMode;
			return *this;
		}

		RasterStateBuilder & Cull(CullMode cullMode) noexcept
		{
			m_desc.cullMode = cullMode;
			return *this;
		}

		RasterStateBuilder & FrontFace(FrontFace frontFace) noexcept
		{
			m_desc.frontFace = frontFace;
			return *this;
		}

		RasterStateBuilder & DepthClamp(bool enabled = true) noexcept
		{
			m_desc.depthClampEnable = enabled;
			return *this;
		}

		RasterStateBuilder & RasterizerDiscard(bool enabled = true) noexcept
		{
			m_desc.rasterizerDiscardEnable = enabled;
			return *this;
		}

		/**
		 * \brief Enables depth bias and sets the bias factors.
		 *
		 * \param constantFactor Constant depth-bias term.
		 * \param slopeFactor Slope-scaled depth-bias term.
		 * \param clamp Maximum absolute depth bias after scaling. Zero means no clamp.
		 */
		RasterStateBuilder & DepthBias(float constantFactor, float slopeFactor, float clamp = 0.0f) noexcept
		{
			m_desc.depthBiasEnable		   = true;
			m_desc.depthBiasConstantFactor = constantFactor;
			m_desc.depthBiasSlopeFactor	   = slopeFactor;
			m_desc.depthBiasClamp		   = clamp;
			return *this;
		}

		RasterStateBuilder & DisableDepthBias() noexcept
		{
			m_desc.depthBiasEnable = false;
			return *this;
		}

		[[nodiscard]] constexpr RasterStateDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		RasterStateDesc m_desc{};
	};

	class DepthStencilStateBuilder final
	{
	public:
		DepthStencilStateBuilder & DepthTest(bool enabled = true) noexcept
		{
			m_desc.depthTestEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & DepthWrite(bool enabled = true) noexcept
		{
			m_desc.depthWriteEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & DepthCompare(CompareOp op) noexcept
		{
			m_desc.depthCompareOp = op;
			return *this;
		}

		/**
		 * \brief Sets the accepted normalized depth range for depth-bounds testing.
		 *
		 * \param minDepth Lower inclusive depth bound.
		 * \param maxDepth Upper inclusive depth bound.
		 */
		DepthStencilStateBuilder & DepthBounds(float minDepth, float maxDepth, bool enabled = true) noexcept
		{
			m_desc.depthBoundsTestEnable = enabled;
			m_desc.minDepthBounds		 = minDepth;
			m_desc.maxDepthBounds		 = maxDepth;
			return *this;
		}

		DepthStencilStateBuilder & StencilTest(bool enabled = true) noexcept
		{
			m_desc.stencilTestEnable = enabled;
			return *this;
		}

		DepthStencilStateBuilder & Front(StencilFaceDesc front) noexcept
		{
			m_desc.front = front;
			return *this;
		}

		DepthStencilStateBuilder & Back(StencilFaceDesc back) noexcept
		{
			m_desc.back = back;
			return *this;
		}

		[[nodiscard]] constexpr DepthStencilStateDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		DepthStencilStateDesc m_desc{};
	};

	class ColorBlendAttachmentBuilder final
	{
	public:
		ColorBlendAttachmentBuilder & Enable(bool enabled = true) noexcept
		{
			m_desc.blendEnable = enabled;
			return *this;
		}

		ColorBlendAttachmentBuilder & Color(BlendFactor src, BlendFactor dst, BlendOp op = BlendOp::eAdd) noexcept
		{
			m_desc.srcColorBlendFactor = src;
			m_desc.dstColorBlendFactor = dst;
			m_desc.colorBlendOp		   = op;
			return *this;
		}

		ColorBlendAttachmentBuilder & Alpha(BlendFactor src, BlendFactor dst, BlendOp op = BlendOp::eAdd) noexcept
		{
			m_desc.srcAlphaBlendFactor = src;
			m_desc.dstAlphaBlendFactor = dst;
			m_desc.alphaBlendOp		   = op;
			return *this;
		}

		ColorBlendAttachmentBuilder & WriteMask(Flags<ColorWrite> mask) noexcept
		{
			m_desc.colorWriteMask = mask;
			return *this;
		}

		[[nodiscard]] constexpr ColorBlendAttachmentDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		ColorBlendAttachmentDesc m_desc{};
	};

	class BlendStateBuilder final
	{
	public:
		BlendStateBuilder & LogicOp(bool enabled = true) noexcept
		{
			m_desc.logicOpEnable = enabled;
			return *this;
		}

		BlendStateBuilder & Constants(float r, float g, float b, float a) noexcept
		{
			m_desc.blendConstants = { r, g, b, a };
			return *this;
		}

		/**
		 * \brief Appends a color-blend attachment when fixed attachment storage is not full.
		 *
		 * \note Extra attachments past the fixed storage capacity are ignored.
		 */
		BlendStateBuilder & Attachment(ColorBlendAttachmentDesc attachment)
		{
			if (m_desc.attachmentCount < m_desc.attachments.size())
			{
				m_desc.attachments[m_desc.attachmentCount] = attachment;
				++m_desc.attachmentCount;
			}

			return *this;
		}

		/**
		 * \brief Writes a color-blend attachment at index and grows attachmentCount to include it.
		 *
		 * \note Out-of-range indices are ignored.
		 */
		BlendStateBuilder & Attachment(std::uint32_t index, ColorBlendAttachmentDesc attachment) noexcept
		{
			if (index < m_desc.attachments.size())
			{
				m_desc.attachments[index] = attachment;

				if (m_desc.attachmentCount <= index)
				{
					m_desc.attachmentCount = index + 1;
				}
			}

			return *this;
		}

		BlendStateBuilder & AttachmentCount(std::uint32_t count) noexcept
		{
			m_desc.attachmentCount = count;
			return *this;
		}

		[[nodiscard]] constexpr BlendStateDesc Build() const noexcept
		{
			return m_desc;
		}

	private:
		BlendStateDesc m_desc{};
	};

	/**
	 * \brief Builds graphics-pipeline descriptions backed by owned shader, vertex-input, and debug-name storage.
	 */
	class GraphicsPipelineBuilder final
	{
	public:
		GraphicsPipelineBuilder & Layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		GraphicsPipelineBuilder & Shader(ShaderBinary shader)
		{
			m_shaders.push_back(shader);
			return *this;
		}

		GraphicsPipelineBuilder & Shaders(std::span<const ShaderBinary> shaders)
		{
			m_shaders.assign(shaders.begin(), shaders.end());
			return *this;
		}

		GraphicsPipelineBuilder & VertexBinding(VertexBindingDesc binding)
		{
			m_vertexBindings.push_back(binding);
			return *this;
		}

		GraphicsPipelineBuilder & VertexBindings(std::span<const VertexBindingDesc> bindings)
		{
			m_vertexBindings.assign(bindings.begin(), bindings.end());
			return *this;
		}

		GraphicsPipelineBuilder & VertexAttribute(VertexAttributeDesc attribute)
		{
			m_vertexAttributes.push_back(attribute);
			return *this;
		}

		GraphicsPipelineBuilder & VertexAttributes(std::span<const VertexAttributeDesc> attributes)
		{
			m_vertexAttributes.assign(attributes.begin(), attributes.end());
			return *this;
		}

		GraphicsPipelineBuilder & Topology(PrimitiveTopology topology) noexcept
		{
			m_vertexInput.topology = topology;
			return *this;
		}

		GraphicsPipelineBuilder & PrimitiveRestart(bool enabled = true) noexcept
		{
			m_vertexInput.primitiveRestartEnable = enabled;
			return *this;
		}

		/**
		 * \brief Declares that primitives do not come from vertex buffers, leaving vertexInput null.
		 *
		 * Every backend here refuses such a pipeline today. It exists so the shape a mesh pipeline needs is reachable, not added later.
		 */
		GraphicsPipelineBuilder & NoVertexInput() noexcept
		{
			m_sourcesVertices = false;
			return *this;
		}

		GraphicsPipelineBuilder & Raster(RasterStateDesc raster) noexcept
		{
			m_desc.raster = raster;
			return *this;
		}

		GraphicsPipelineBuilder & DepthStencil(DepthStencilStateDesc depthStencil) noexcept
		{
			m_desc.depthStencil = depthStencil;
			return *this;
		}

		GraphicsPipelineBuilder & Blend(BlendStateDesc blend) noexcept
		{
			m_desc.blend = blend;
			return *this;
		}

		/**
		 * \brief Appends a color attachment format when fixed color-format storage is not full.
		 *
		 * \note Extra formats past the fixed storage capacity are ignored.
		 */
		GraphicsPipelineBuilder & ColorFormat(Format format)
		{
			if (m_renderTarget.colorFormatCount < m_renderTarget.colorFormats.size())
			{
				m_renderTarget.colorFormats[m_renderTarget.colorFormatCount] = format;
				++m_renderTarget.colorFormatCount;
			}

			return *this;
		}

		/**
		 * \brief Writes a color attachment format at index and grows colorFormatCount to include it.
		 *
		 * \note Out-of-range indices are ignored.
		 */
		GraphicsPipelineBuilder & ColorFormat(std::uint32_t index, Format format) noexcept
		{
			if (index < m_renderTarget.colorFormats.size())
			{
				m_renderTarget.colorFormats[index] = format;

				if (m_renderTarget.colorFormatCount <= index)
				{
					m_renderTarget.colorFormatCount = index + 1;
				}
			}

			return *this;
		}

		GraphicsPipelineBuilder & DepthStencilFormat(Format format) noexcept
		{
			m_renderTarget.depthStencilFormat = format;
			return *this;
		}

		GraphicsPipelineBuilder & Samples(SampleCount samples) noexcept
		{
			m_renderTarget.samples = samples;
			return *this;
		}

		GraphicsPipelineBuilder & SampleMask(std::uint32_t sampleMask) noexcept
		{
			m_renderTarget.sampleMask = sampleMask;
			return *this;
		}

		GraphicsPipelineBuilder & AlphaToCoverage(bool enabled = true) noexcept
		{
			m_renderTarget.alphaToCoverageEnable = enabled;
			return *this;
		}

		GraphicsPipelineBuilder & PipelineCache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		GraphicsPipelineBuilder & DynamicStates(Flags<DynamicState> dynamicStates) noexcept
		{
			m_desc.dynamicStates = dynamicStates;
			return *this;
		}

		GraphicsPipelineBuilder & AddDynamicState(DynamicState state) noexcept
		{
			m_desc.dynamicStates = m_desc.dynamicStates | state;
			return *this;
		}

		GraphicsPipelineBuilder & DynamicViewportScissor() noexcept
		{
			return AddDynamicState(DynamicState::eViewport).AddDynamicState(DynamicState::eScissor);
		}

		GraphicsPipelineBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<GraphicsPipelineBuilder> Build() &&
		{
			return Built<GraphicsPipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<GraphicsPipelineBuilder> Build() const &
		{
			return Built<GraphicsPipelineBuilder>{ *this };
		}

	private:
		friend class Built<GraphicsPipelineBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
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

		// Held here and not in m_desc because the desc points at it, so it has to live somewhere the desc outlives it in.
		mutable VertexInputDesc m_vertexInput{};
		RenderTargetDesc m_renderTarget{};
		bool m_sourcesVertices = true;

		std::vector<ShaderBinary> m_shaders;
		std::vector<VertexBindingDesc> m_vertexBindings;
		std::vector<VertexAttributeDesc> m_vertexAttributes;
		std::string m_debugName;
	};

	/**
	 * \brief Builds compute-pipeline descriptions backed by owned debug-name storage.
	 */
	class ComputePipelineBuilder final
	{
	public:
		ComputePipelineBuilder & Layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		ComputePipelineBuilder & Shader(ShaderBinary shader) noexcept
		{
			m_desc.shader = shader;
			return *this;
		}

		ComputePipelineBuilder & PipelineCache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		ComputePipelineBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<ComputePipelineBuilder> Build() &&
		{
			return Built<ComputePipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<ComputePipelineBuilder> Build() const &
		{
			return Built<ComputePipelineBuilder>{ *this };
		}

	private:
		friend class Built<ComputePipelineBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
		[[nodiscard]] ComputePipelineDesc BorrowedDesc() const noexcept
		{
			ComputePipelineDesc desc = m_desc;
			desc.debugName			 = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		ComputePipelineDesc m_desc{};
		std::string m_debugName;
	};

	/**
	 * \brief Builds acceleration-structure descriptions backed by owned debug-name storage.
	 */
	class AccelerationStructureBuilder final
	{
	public:
		AccelerationStructureBuilder & Type(AccelerationStructureType type) noexcept
		{
			m_desc.type = type;
			return *this;
		}

		AccelerationStructureBuilder & BottomLevel() noexcept
		{
			return Type(AccelerationStructureType::eBottomLevel);
		}

		AccelerationStructureBuilder & TopLevel() noexcept
		{
			return Type(AccelerationStructureType::eTopLevel);
		}

		/**
		 * \brief Sets backing storage for the acceleration structure.
		 *
		 * \param offset Byte offset into storage.
		 * \param size Byte size reserved for the acceleration structure.
		 */
		AccelerationStructureBuilder & Storage(BufferHandle storage, std::uint64_t offset, std::uint64_t size) noexcept
		{
			m_desc.storage		 = storage;
			m_desc.storageOffset = offset;
			m_desc.size			 = size;
			return *this;
		}

		AccelerationStructureBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<AccelerationStructureBuilder> Build() &&
		{
			return Built<AccelerationStructureBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<AccelerationStructureBuilder> Build() const &
		{
			return Built<AccelerationStructureBuilder>{ *this };
		}

	private:
		friend class Built<AccelerationStructureBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
		[[nodiscard]] AccelerationStructureDesc BorrowedDesc() const noexcept
		{
			AccelerationStructureDesc desc = m_desc;
			desc.debugName				   = m_debugName.empty() ? nullptr : m_debugName.c_str();
			return desc;
		}

		AccelerationStructureDesc m_desc{};
		std::string m_debugName;
	};

	/**
	 * \brief Builds acceleration-structure-build descriptions backed by owned geometry storage.
	 */
	class AccelerationStructureBuildBuilder final
	{
	public:
		AccelerationStructureBuildBuilder & Dst(AccelerationStructureHandle dst) noexcept
		{
			m_desc.dst = dst;
			return *this;
		}

		AccelerationStructureBuildBuilder & Src(AccelerationStructureHandle src) noexcept
		{
			m_desc.src = src;
			return *this;
		}

		AccelerationStructureBuildBuilder & Mode(AccelerationStructureBuildMode mode) noexcept
		{
			m_desc.mode = mode;
			return *this;
		}

		AccelerationStructureBuildBuilder & Flags(Flags<AccelerationStructureBuildFlag> flags) noexcept
		{
			m_desc.flags = flags;
			return *this;
		}

		AccelerationStructureBuildBuilder & AddFlag(AccelerationStructureBuildFlag flag) noexcept
		{
			m_desc.flags = m_desc.flags | flag;
			return *this;
		}

		AccelerationStructureBuildBuilder & Geometry(AccelerationStructureGeometryDesc geometry)
		{
			m_geometries.push_back(geometry);
			return *this;
		}

		AccelerationStructureBuildBuilder & Geometries(std::span<const AccelerationStructureGeometryDesc> geometries)
		{
			m_geometries.assign(geometries.begin(), geometries.end());
			return *this;
		}

		/**
		 * \brief Sets instance-buffer input for a top-level acceleration-structure build.
		 *
		 * \param offset Byte offset into instanceBuffer.
		 * \param count Number of instance records.
		 */
		AccelerationStructureBuildBuilder & Instances(BufferHandle instanceBuffer, std::uint64_t offset, std::uint32_t count) noexcept
		{
			m_desc.instanceBuffer = instanceBuffer;
			m_desc.instanceOffset = offset;
			m_desc.instanceCount  = count;
			return *this;
		}

		/**
		 * \brief Sets scratch storage used only while the acceleration-structure build executes.
		 *
		 * \param offset Byte offset into scratchBuffer.
		 */
		AccelerationStructureBuildBuilder & Scratch(BufferHandle scratchBuffer, std::uint64_t offset) noexcept
		{
			m_desc.scratchBuffer = scratchBuffer;
			m_desc.scratchOffset = offset;
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<AccelerationStructureBuildBuilder> Build() &&
		{
			return Built<AccelerationStructureBuildBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<AccelerationStructureBuildBuilder> Build() const &
		{
			return Built<AccelerationStructureBuildBuilder>{ *this };
		}

	private:
		friend class Built<AccelerationStructureBuildBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
		[[nodiscard]] AccelerationStructureBuildDesc BorrowedDesc() const noexcept
		{
			AccelerationStructureBuildDesc desc = m_desc;
			desc.geometries						= std::span<const AccelerationStructureGeometryDesc>{ m_geometries.data(), m_geometries.size() };
			return desc;
		}

		AccelerationStructureBuildDesc m_desc{};
		std::vector<AccelerationStructureGeometryDesc> m_geometries;
	};

	/**
	 * \brief Builds ray-tracing-pipeline descriptions backed by owned shader, group, and debug-name storage.
	 */
	class RayTracingPipelineBuilder final
	{
	public:
		RayTracingPipelineBuilder & Layout(PipelineLayoutHandle layout) noexcept
		{
			m_desc.layout = layout;
			return *this;
		}

		RayTracingPipelineBuilder & Shader(ShaderBinary shader)
		{
			m_shaders.push_back(shader);
			return *this;
		}

		RayTracingPipelineBuilder & Shaders(std::span<const ShaderBinary> shaders)
		{
			m_shaders.assign(shaders.begin(), shaders.end());
			return *this;
		}

		RayTracingPipelineBuilder & Group(RayTracingShaderGroupDesc group)
		{
			m_groups.push_back(group);
			return *this;
		}

		RayTracingPipelineBuilder & Groups(std::span<const RayTracingShaderGroupDesc> groups)
		{
			m_groups.assign(groups.begin(), groups.end());
			return *this;
		}

		RayTracingPipelineBuilder & MaxRayRecursionDepth(std::uint32_t depth) noexcept
		{
			m_desc.maxRayRecursionDepth = depth;
			return *this;
		}

		RayTracingPipelineBuilder & MaxPayloadBytes(std::uint32_t bytes) noexcept
		{
			m_desc.maxPayloadBytes = bytes;
			return *this;
		}

		RayTracingPipelineBuilder & MaxAttributeBytes(std::uint32_t bytes) noexcept
		{
			m_desc.maxAttributeBytes = bytes;
			return *this;
		}

		RayTracingPipelineBuilder & PipelineCache(PipelineCacheHandle pipelineCache) noexcept
		{
			m_desc.pipelineCache = pipelineCache;
			return *this;
		}

		RayTracingPipelineBuilder & DebugName(std::string_view name)
		{
			m_debugName.assign(name.data(), name.size());
			return *this;
		}

		/**
		 * \brief Hands the finished description and the storage it points at to the caller.
		 *
		 * The rvalue form moves this builder's storage into the result and leaves the builder empty, so building from a temporary does not dangle. The lvalue form
		 * copies instead, leaving the builder usable for another description.
		 */
		[[nodiscard]] Built<RayTracingPipelineBuilder> Build() &&
		{
			return Built<RayTracingPipelineBuilder>{ std::move(*this) };
		}

		[[nodiscard]] Built<RayTracingPipelineBuilder> Build() const &
		{
			return Built<RayTracingPipelineBuilder>{ *this };
		}

	private:
		friend class Built<RayTracingPipelineBuilder>;

		// What Built hands back. Private because the result borrows this builder, which is exactly what Built exists to stop a caller doing.
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
} // namespace azo::rhi

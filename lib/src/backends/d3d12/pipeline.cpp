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
	[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE MapPrimitiveTopologyType(PrimitiveTopology topology) noexcept
	{
		switch (topology)
		{
		case PrimitiveTopology::ePointList:		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case PrimitiveTopology::eLineList:
		case PrimitiveTopology::eLineStrip:		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PrimitiveTopology::eTriangleList:
		case PrimitiveTopology::eTriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PrimitiveTopology::ePatchList:		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		}
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}

	// The concrete topology a draw binds via IASetPrimitiveTopology (the PSO carries only the broad type).
	[[nodiscard]] D3D_PRIMITIVE_TOPOLOGY MapPrimitiveTopology(PrimitiveTopology topology) noexcept
	{
		switch (topology)
		{
		case PrimitiveTopology::ePointList:		return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case PrimitiveTopology::eLineList:		return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::eLineStrip:		return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case PrimitiveTopology::eTriangleList:	return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::eTriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		}
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	[[nodiscard]] D3D12_FILL_MODE MapFillMode(FillMode mode) noexcept
	{
		return mode == FillMode::eWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
	}

	[[nodiscard]] D3D12_CULL_MODE MapCullMode(CullMode mode) noexcept
	{
		switch (mode)
		{
		case CullMode::eFront: return D3D12_CULL_MODE_FRONT;
		case CullMode::eBack:  return D3D12_CULL_MODE_BACK;
		case CullMode::eNone:  return D3D12_CULL_MODE_NONE;
		}
		return D3D12_CULL_MODE_NONE;
	}

	[[nodiscard]] D3D12_STENCIL_OP MapStencilOp(StencilOp op) noexcept
	{
		switch (op)
		{
		case StencilOp::eKeep:			 return D3D12_STENCIL_OP_KEEP;
		case StencilOp::eZero:			 return D3D12_STENCIL_OP_ZERO;
		case StencilOp::eReplace:		 return D3D12_STENCIL_OP_REPLACE;
		case StencilOp::eIncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
		case StencilOp::eDecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
		case StencilOp::eInvert:		 return D3D12_STENCIL_OP_INVERT;
		case StencilOp::eIncrementWrap:	 return D3D12_STENCIL_OP_INCR;
		case StencilOp::eDecrementWrap:	 return D3D12_STENCIL_OP_DECR;
		}
		return D3D12_STENCIL_OP_KEEP;
	}

	[[nodiscard]] D3D12_DEPTH_STENCILOP_DESC MapStencilFace(const StencilFaceDesc & face) noexcept
	{
		D3D12_DEPTH_STENCILOP_DESC out{};
		out.StencilFailOp	   = MapStencilOp(face.failOp);
		out.StencilDepthFailOp = MapStencilOp(face.depthFailOp);
		out.StencilPassOp	   = MapStencilOp(face.passOp);
		out.StencilFunc		   = MapCompareOp(face.compareOp);
		return out;
	}

	[[nodiscard]] D3D12_BLEND MapBlendFactor(BlendFactor factor) noexcept
	{
		switch (factor)
		{
		case BlendFactor::eZero:				  return D3D12_BLEND_ZERO;
		case BlendFactor::eOne:					  return D3D12_BLEND_ONE;
		case BlendFactor::eSrcColor:			  return D3D12_BLEND_SRC_COLOR;
		case BlendFactor::eOneMinusSrcColor:	  return D3D12_BLEND_INV_SRC_COLOR;
		case BlendFactor::eDstColor:			  return D3D12_BLEND_DEST_COLOR;
		case BlendFactor::eOneMinusDstColor:	  return D3D12_BLEND_INV_DEST_COLOR;
		case BlendFactor::eSrcAlpha:			  return D3D12_BLEND_SRC_ALPHA;
		case BlendFactor::eOneMinusSrcAlpha:	  return D3D12_BLEND_INV_SRC_ALPHA;
		case BlendFactor::eDstAlpha:			  return D3D12_BLEND_DEST_ALPHA;
		case BlendFactor::eOneMinusDstAlpha:	  return D3D12_BLEND_INV_DEST_ALPHA;
		case BlendFactor::eConstantColor:		  return D3D12_BLEND_BLEND_FACTOR;
		case BlendFactor::eOneMinusConstantColor: return D3D12_BLEND_INV_BLEND_FACTOR;
		case BlendFactor::eConstantAlpha:		  return D3D12_BLEND_BLEND_FACTOR;
		case BlendFactor::eOneMinusConstantAlpha: return D3D12_BLEND_INV_BLEND_FACTOR;
		}
		return D3D12_BLEND_ZERO;
	}

	// D3D12 rejects a color factor in the alpha blend so each maps to its alpha equivalent. Vulkan applies one factor to both and this keeps the two backends
	// blending identically.
	[[nodiscard]] D3D12_BLEND ToAlphaBlendFactor(D3D12_BLEND factor) noexcept
	{
		switch (factor)
		{
		case D3D12_BLEND_SRC_COLOR:		 return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR:	 return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR:	 return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
		case D3D12_BLEND_SRC1_COLOR:	 return D3D12_BLEND_SRC1_ALPHA;
		case D3D12_BLEND_INV_SRC1_COLOR: return D3D12_BLEND_INV_SRC1_ALPHA;
		default:						 return factor;
		}
	}

	[[nodiscard]] D3D12_BLEND_OP MapBlendOp(BlendOp op) noexcept
	{
		switch (op)
		{
		case BlendOp::eAdd:				return D3D12_BLEND_OP_ADD;
		case BlendOp::eSubtract:		return D3D12_BLEND_OP_SUBTRACT;
		case BlendOp::eReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
		case BlendOp::eMin:				return D3D12_BLEND_OP_MIN;
		case BlendOp::eMax:				return D3D12_BLEND_OP_MAX;
		}
		return D3D12_BLEND_OP_ADD;
	}

	[[nodiscard]] UINT8 MapColorWriteMask(Flags<ColorWrite> mask) noexcept
	{
		UINT8 out = 0;
		if (mask.Contains(ColorWrite::eR))
		{
			out |= D3D12_COLOR_WRITE_ENABLE_RED;
		}
		if (mask.Contains(ColorWrite::eG))
		{
			out |= D3D12_COLOR_WRITE_ENABLE_GREEN;
		}
		if (mask.Contains(ColorWrite::eB))
		{
			out |= D3D12_COLOR_WRITE_ENABLE_BLUE;
		}
		if (mask.Contains(ColorWrite::eA))
		{
			out |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
		}
		return out;
	}

	[[nodiscard]] PipelineLayoutSlot * ResolvePipelineLayout(D3D12Device * device, PipelineLayoutHandle handle) noexcept
	{
		return device->pipelineLayoutSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] GraphicsPipelineSlot * ResolveGraphicsPipeline(D3D12Device * device, GraphicsPipelineHandle handle) noexcept
	{
		return device->graphicsPipelineSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	[[nodiscard]] ComputePipelineSlot * ResolveComputePipeline(D3D12Device * device, ComputePipelineHandle handle) noexcept
	{
		return device->computePipelineSlots.Resolve(handle, kHandleAlreadyChecked);
	}

	// Resolves the ID3D12PipelineLibrary a pipeline create should consult or null when no cache was given.
	[[nodiscard]] ID3D12PipelineLibrary * ResolvePipelineLibrary(D3D12Device * device, PipelineCacheHandle handle) noexcept
	{
		const PipelineCacheSlot * const slot = device->pipelineCacheSlots.Resolve(handle, true);
		return slot != nullptr ? slot->library.Get() : nullptr;
	}

	/*
	 * Maps location-indexed vertex attributes onto D3D12 input elements.
	 *
	 * Direct3D 12 matches an input element to a shader input by semantic name and index, and it fails pipeline creation on a mismatch without ignoring it. The
	 * name comes from the ABI and the index is the location the caller declared, so nothing here has to look at the binary. A binary whose compiler picked a
	 * different name passes it in semanticName.
	 */
	void BuildInputElements(const VertexInputDesc & vertexInput, const char * semanticName, detail::HostVector<D3D12_INPUT_ELEMENT_DESC> & out)
	{
		out.reserve(vertexInput.attributes.size());
		for (const VertexAttributeDesc & attr : vertexInput.attributes)
		{
			bool perInstance = false;
			for (const VertexBindingDesc & binding : vertexInput.bindings)
			{
				if (binding.binding == attr.binding)
				{
					perInstance = binding.perInstance;
					break;
				}
			}

			D3D12_INPUT_ELEMENT_DESC element{};
			element.SemanticName		 = semanticName;
			element.SemanticIndex		 = attr.location;
			element.Format				 = MapFormat(attr.format);
			element.InputSlot			 = attr.binding;
			element.AlignedByteOffset	 = attr.offset;
			element.InputSlotClass		 = perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			element.InstanceDataStepRate = perInstance ? 1u : 0u;
			out.push_back(element);
		}
	}

	// What this backend can consume, checked, not assumed. A D3D12_SHADER_BYTECODE takes a pointer and a length with no container check of its own, so a SPIR-V
	// blob reaches the driver and fails somewhere further in than the call that was wrong.
	[[nodiscard]] bool ShaderBytesUsable(const ShaderBinary & shader, Error * error) noexcept
	{
		if (shader.format != ShaderBinaryFormat::eDxil)
		{
			return Fail(error, ErrorCode::eUnsupportedFormat, "the Direct3D 12 backend takes DXIL shader binaries");
		}

		// Nothing here compiles a shader, which is what supportsShaderSource says. Reading source as DXIL would hand the driver text.
		if (shader.isSource)
		{
			return Fail(error, ErrorCode::eUnsupportedFormat, "the Direct3D 12 backend has no shader compiler, so it takes compiled DXIL only");
		}

		if (shader.data == nullptr || shader.size == 0)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "shader binary has no bytes");
		}

		return true;
	}

	/*
	 * Refuses a pipeline whose shaders claim their bindings landed somewhere other than where this root signature puts them. The root signature was built from the
	 * pipeline layout through NativeBindingFor. A binary carrying a map states where its own compiler put the same bindings.
	 *
	 * Nothing here changes what gets bound. It changes the failure: a binary compiled against different registers otherwise reads whatever descriptors sit at the
	 * registers it uses, which looks like a shading bug.
	 */
	[[nodiscard]] bool BindingMapsAgree(
		D3D12Device * device, const PipelineLayoutSlot & layout, const std::span<const ShaderBinary> shaders, Error * error) noexcept
	{
		const bool anyMapped = std::ranges::any_of(shaders,
			[](const ShaderBinary & shader) noexcept
			{
				return shader.bindingMap != nullptr;
			});
		if (!anyMapped)
		{
			return true;
		}

		detail::HostVector<DescriptorSetLayoutDesc> abiSets;
		abiSets.reserve(layout.sets.size());
		for (const DescriptorSetLayoutHandle setHandle : layout.sets)
		{
			const DescriptorSetLayoutSlot * const setLayout = ResolveDescriptorSetLayout(device, setHandle);
			if (setLayout == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "a descriptor set layout this pipeline layout was built from has been destroyed");
			}

			abiSets.push_back(DescriptorSetLayoutDesc{ .bindings = setLayout->bindings });
		}

		const ShaderAbiLayout abiLayout{ .sets = abiSets };

		for (const ShaderBinary & shader : shaders)
		{
			if (shader.bindingMap == nullptr)
			{
				continue;
			}

			const ShaderBindingDisagreement bad = CheckShaderBindingMap(D3D12Api::id, device->caps.bindingTier, abiLayout, *shader.bindingMap);
			if (!bad.found)
			{
				continue;
			}

			if (bad.wrongAbiVersion)
			{
				return Fail(
					error, ErrorCode::eUnsupportedFormat, "a shader binary was built against a revision of the binding ABI this build does not implement");
			}

			if (bad.unknownToLayout)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding the pipeline layout does not declare");
			}

			return Fail(error, ErrorCode::eInvalidArgument, "a shader binary put a binding at a different register than this pipeline layout binds it at");
		}

		return true;
	}

	GraphicsPipelineHandle D3D12CreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createGraphicsPipeline");
		/*
		 * A null vertexInput means primitives come from somewhere other than vertex buffers, which is what a mesh pipeline is. No backend here builds one, so it is
		 * refused by name, not lowered as an empty vertex layout that would draw nothing and report success.
		 */
		if (desc.vertexInput == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
		}

		const VertexInputDesc & vertexInput = *desc.vertexInput;
		/*
		 * Both of these change what the rasterizer actually covers, so a backend that cannot do them refuses without lowering the pipeline without them. Dropping
		 * either one silently produces a pipeline that creates, draws, and covers the wrong pixels.
		 */
		if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
		}

		auto * device = static_cast<D3D12Device *>(impl);
		if (desc.raster.conservativeRasterEnable && device->caps.conservativeRasterTier == ConservativeRasterTier::eNone)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "conservative rasterization was requested on a device that reports none");
		}

		// Both counts index arrays of a fixed size. This path used to clamp its loops instead, which honored the first eight attachments and dropped the rest without
		// a word, so a malformed desc now earns the same refusal here as it does on the other two backends.
		if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
		}

		PipelineLayoutSlot * layout = ResolvePipelineLayout(device, desc.layout);
		if (layout == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidHandle, "graphics pipeline references an invalid pipeline layout");
		}

		if (!BindingMapsAgree(device, *layout, desc.shaders, error))
		{
			return {};
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = layout->rootSignature.Get();

		// Taken off the vertex stage alone, since the input layout is the only thing that reads it and no other stage has vertex attributes.
		const char * vertexSemanticName = kVertexSemanticName;

		for (const ShaderBinary & shader : desc.shaders)
		{
			if (!ShaderBytesUsable(shader, error))
			{
				return {};
			}

			const D3D12_SHADER_BYTECODE bytecode{ shader.data, shader.size };
			switch (shader.stage)
			{
			case ShaderStage::eVertex:
				psoDesc.VS = bytecode;
				if (shader.bindingMap != nullptr && shader.bindingMap->vertexSemanticName != nullptr)
				{
					vertexSemanticName = shader.bindingMap->vertexSemanticName;
				}
				break;
			case ShaderStage::eFragment:			   psoDesc.PS = bytecode; break;
			case ShaderStage::eGeometry:			   psoDesc.GS = bytecode; break;
			case ShaderStage::eTessellationControl:	   psoDesc.HS = bytecode; break;
			case ShaderStage::eTessellationEvaluation: psoDesc.DS = bytecode; break;
			default:								   break;
			}
		}

		detail::HostVector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		BuildInputElements(vertexInput, vertexSemanticName, inputElements);
		psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };

		psoDesc.RasterizerState.FillMode			  = MapFillMode(desc.raster.fillMode);
		psoDesc.RasterizerState.CullMode			  = MapCullMode(desc.raster.cullMode);
		psoDesc.RasterizerState.FrontCounterClockwise = desc.raster.frontFace == FrontFace::eCounterClockwise ? TRUE : FALSE;
		psoDesc.RasterizerState.DepthBias			  = desc.raster.depthBiasEnable ? static_cast<INT>(desc.raster.depthBiasConstantFactor) : 0;
		psoDesc.RasterizerState.DepthBiasClamp		  = desc.raster.depthBiasEnable ? desc.raster.depthBiasClamp : 0.0f;
		psoDesc.RasterizerState.SlopeScaledDepthBias  = desc.raster.depthBiasEnable ? desc.raster.depthBiasSlopeFactor : 0.0f;
		psoDesc.RasterizerState.DepthClipEnable		  = desc.raster.depthClampEnable ? FALSE : TRUE;
		psoDesc.RasterizerState.ForcedSampleCount	  = 0;
		psoDesc.RasterizerState.ConservativeRaster =
			desc.raster.conservativeRasterEnable ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		psoDesc.DepthStencilState.DepthEnable	   = desc.depthStencil.depthTestEnable ? TRUE : FALSE;
		psoDesc.DepthStencilState.DepthWriteMask   = desc.depthStencil.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.DepthFunc		   = MapCompareOp(desc.depthStencil.depthCompareOp);
		psoDesc.DepthStencilState.StencilEnable	   = desc.depthStencil.stencilTestEnable ? TRUE : FALSE;
		psoDesc.DepthStencilState.StencilReadMask  = static_cast<UINT8>(desc.depthStencil.front.compareMask);
		psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(desc.depthStencil.front.writeMask);
		psoDesc.DepthStencilState.FrontFace		   = MapStencilFace(desc.depthStencil.front);
		psoDesc.DepthStencilState.BackFace		   = MapStencilFace(desc.depthStencil.back);

		psoDesc.BlendState.AlphaToCoverageEnable  = desc.renderTarget.alphaToCoverageEnable ? TRUE : FALSE;
		psoDesc.BlendState.IndependentBlendEnable = TRUE;
		// Creation refuses a count past these arrays, and both loops stop at eight besides.
		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		for (std::uint32_t i = 0; i < desc.blend.attachmentCount && i < 8; ++i)
		{
			const ColorBlendAttachmentDesc & a	= desc.blend.attachments[i];
			D3D12_RENDER_TARGET_BLEND_DESC & rt = psoDesc.BlendState.RenderTarget[i]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
			rt.BlendEnable						= a.blendEnable ? TRUE : FALSE;
			rt.LogicOpEnable					= (i == 0 && desc.blend.logicOpEnable) ? TRUE : FALSE;
			rt.SrcBlend							= MapBlendFactor(a.srcColorBlendFactor);
			rt.DestBlend						= MapBlendFactor(a.dstColorBlendFactor);
			rt.BlendOp							= MapBlendOp(a.colorBlendOp);
			rt.SrcBlendAlpha					= ToAlphaBlendFactor(MapBlendFactor(a.srcAlphaBlendFactor));
			rt.DestBlendAlpha					= ToAlphaBlendFactor(MapBlendFactor(a.dstAlphaBlendFactor));
			rt.BlendOpAlpha						= MapBlendOp(a.alphaBlendOp);
			rt.LogicOp							= D3D12_LOGIC_OP_COPY;
			rt.RenderTargetWriteMask			= MapColorWriteMask(a.colorWriteMask);
		}

		psoDesc.SampleMask			  = desc.renderTarget.sampleMask;
		psoDesc.PrimitiveTopologyType = MapPrimitiveTopologyType(vertexInput.topology);
		psoDesc.IBStripCutValue =
			vertexInput.primitiveRestartEnable ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		psoDesc.NumRenderTargets = desc.renderTarget.colorFormatCount;
		for (std::uint32_t i = 0; i < desc.renderTarget.colorFormatCount && i < 8; ++i)
		{
			psoDesc.RTVFormats[i] = MapFormat(desc.renderTarget.colorFormats[i]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) A depth format with no DXGI equivalent maps to UNKNOWN and would silently disable
		// depth output so reject it. eUndefined means no depth target and legitimately stays UNKNOWN.
		const DXGI_FORMAT dsvFormat = MapFormat(desc.renderTarget.depthStencilFormat);
		if (desc.renderTarget.depthStencilFormat != Format::eUndefined && dsvFormat == DXGI_FORMAT_UNKNOWN)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eUnsupportedFormat, "the depth-stencil format is not supported by the D3D12 backend");
		}
		psoDesc.DSVFormat		 = dsvFormat;
		psoDesc.SampleDesc.Count = MapSampleCount(desc.renderTarget.samples);

		ComPtr<ID3D12PipelineState> pipeline;
		ID3D12PipelineLibrary * library = ResolvePipelineLibrary(device, desc.pipelineCache);
		HRESULT hr						= S_OK;
		if (library != nullptr)
		{
			// Derive the cache name from the pipeline's defining state, since a runtime pointer would never match a deserialized library. Bytecode dominates the
			// identity and the fixed-function blocks are pointer-free POD so hashing their bytes is stable.
			std::uint64_t h		= 0xcbf29ce484222325ULL; // FNV-1a 64 offset basis
			const auto mixBytes = [&h](const void * data, std::size_t size) noexcept
			{
				const auto * bytes = static_cast<const unsigned char *>(data);
				for (std::size_t i = 0; i < size; ++i)
				{
					h = (h ^ bytes[i]) * 0x100000001b3ULL;
				}
			};
			for (const ShaderBinary & shader : desc.shaders)
			{
				mixBytes(&shader.stage, sizeof(shader.stage));
				mixBytes(shader.data, shader.size);
			}
			for (const D3D12_INPUT_ELEMENT_DESC & element : inputElements)
			{
				for (const char * s = element.SemanticName; s != nullptr && *s != '\0'; ++s)
				{
					mixBytes(s, 1);
				}
				mixBytes(&element.SemanticIndex, sizeof(element.SemanticIndex));
				mixBytes(&element.Format, sizeof(element.Format));
				mixBytes(&element.InputSlot, sizeof(element.InputSlot));
				mixBytes(&element.AlignedByteOffset, sizeof(element.AlignedByteOffset));
				mixBytes(&element.InputSlotClass, sizeof(element.InputSlotClass));
				mixBytes(&element.InstanceDataStepRate, sizeof(element.InstanceDataStepRate));
			}
			mixBytes(&psoDesc.RasterizerState, sizeof(psoDesc.RasterizerState));
			mixBytes(&psoDesc.DepthStencilState, sizeof(psoDesc.DepthStencilState));
			mixBytes(&psoDesc.BlendState, sizeof(psoDesc.BlendState));
			mixBytes(&psoDesc.RTVFormats, sizeof(psoDesc.RTVFormats));
			mixBytes(&psoDesc.NumRenderTargets, sizeof(psoDesc.NumRenderTargets));
			mixBytes(&psoDesc.DSVFormat, sizeof(psoDesc.DSVFormat));
			mixBytes(&psoDesc.SampleDesc, sizeof(psoDesc.SampleDesc));
			mixBytes(&psoDesc.SampleMask, sizeof(psoDesc.SampleMask));
			mixBytes(&psoDesc.PrimitiveTopologyType, sizeof(psoDesc.PrimitiveTopologyType));
			mixBytes(&psoDesc.IBStripCutValue, sizeof(psoDesc.IBStripCutValue));
			mixBytes(&desc.layout, sizeof(desc.layout));
			const std::wstring name = L"gfx" + std::to_wstring(h);
			if (FAILED(library->LoadGraphicsPipeline(name.c_str(), &psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()))))
			{
				hr = device->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()));
				if (SUCCEEDED(hr))
				{
					library->StorePipeline(name.c_str(), pipeline.Get());
				}
			}
		}
		else
		{
			hr = device->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()));
		}
		if (FAILED(hr))
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateGraphicsPipelineState failed");
		}

		const Flags<DynamicState> dynamic = desc.dynamicStates;

		/*
		 * D3D12 carries a vertex buffer's stride in its view and not the PSO so the per-binding strides are recorded on the slot for setVertexBuffer to read back.
		 * Without them every buffer gets a zero stride, collapsing all vertices onto the first element into invisible geometry, while a pass with no vertex buffer
		 * such as the sky looks fine.
		 */
		std::array<std::uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};
		for (const VertexBindingDesc & binding : vertexInput.bindings)
		{
			if (binding.binding < vertexStrides.size())
			{
				// Creation refuses a count past these arrays. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				vertexStrides[binding.binding] = binding.stride;
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}
		}

		return ReturnValue(device->graphicsPipelineSlots.Store(GraphicsPipelineSlot{ .pipeline = std::move(pipeline),
							   .rootSignature												   = layout->rootSignature,
							   .topology													   = MapPrimitiveTopology(vertexInput.topology),
							   .vertexStrides												   = vertexStrides,
							   .bakeBlendConstants											   = !dynamic.Contains(DynamicState::eBlendConstants),
							   .blendConstants												   = desc.blend.blendConstants,
							   .bakeStencilReference										   = !dynamic.Contains(DynamicState::eStencilReference),
							   .stencilReference											   = desc.depthStencil.front.reference,
							   .depthBoundsTestEnable										   = desc.depthStencil.depthBoundsTestEnable,
							   .minDepthBounds												   = desc.depthStencil.minDepthBounds,
							   .maxDepthBounds												   = desc.depthStencil.maxDepthBounds }),
			error);
	}

	ComputePipelineHandle D3D12CreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createComputePipeline");

		/*
		 * Required on every backend, not only the one that reads it. SPIR-V and DXIL carry the size inside the binary so Vulkan and Direct3D 12 never look at this
		 * field, but refusing it here too is what stops a shader developed against one of them reaching Metal with the size forgotten, where the failure would be a
		 * dispatch that quietly does a fraction of the work.
		 */
		if (!desc.shader.threadgroupSize.IsStated())
		{
			return FailValue<ComputePipelineHandle>(error,
				ErrorCode::eInvalidArgument,
				"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
		}

		auto * device				= static_cast<D3D12Device *>(impl);
		PipelineLayoutSlot * layout = ResolvePipelineLayout(device, desc.layout);
		if (layout == nullptr)
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eInvalidHandle, "compute pipeline references an invalid pipeline layout");
		}

		const std::array<ShaderBinary, 1> stages{ desc.shader };
		if (!BindingMapsAgree(device, *layout, stages, error))
		{
			return {};
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = layout->rootSignature.Get();
		if (!ShaderBytesUsable(desc.shader, error))
		{
			return {};
		}

		psoDesc.CS = D3D12_SHADER_BYTECODE{ desc.shader.data, desc.shader.size };

		ComPtr<ID3D12PipelineState> pipeline;
		ID3D12PipelineLibrary * library = ResolvePipelineLibrary(device, desc.pipelineCache);
		HRESULT hr						= S_OK;
		if (library != nullptr)
		{
			const std::wstring name = L"cmp" + std::to_wstring(reinterpret_cast<std::uintptr_t>(&desc) ^ device->computePipelineSlots.LiveCount());
			if (FAILED(library->LoadComputePipeline(name.c_str(), &psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()))))
			{
				hr = device->device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()));
				if (SUCCEEDED(hr))
				{
					library->StorePipeline(name.c_str(), pipeline.Get());
				}
			}
		}
		else
		{
			hr = device->device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pipeline.GetAddressOf()));
		}
		if (FAILED(hr))
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device::CreateComputePipelineState failed");
		}

		return ReturnValue(device->computePipelineSlots.Store(ComputePipelineSlot{
							   .pipeline	  = std::move(pipeline),
							   .rootSignature = layout->rootSignature,
						   }),
			error);
	}

	PipelineCacheHandle D3D12CreatePipelineCache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.createPipelineCache");

		auto * device = static_cast<D3D12Device *>(impl);

		ComPtr<ID3D12Device1> device1;
		if (FAILED(device->device.As(&device1)))
		{
			return FailValue<PipelineCacheHandle>(error, ErrorCode::eUnsupportedFeature, "pipeline cache requires ID3D12Device1");
		}

		ComPtr<ID3D12PipelineLibrary> library;
		HRESULT hr = device1->CreatePipelineLibrary(desc.initialData, desc.initialSize, IID_PPV_ARGS(library.GetAddressOf()));
		if (FAILED(hr) && desc.initialData != nullptr)
		{
			// A stale or mismatched seed blob falls back to an empty library, since the cache is a hint and not a correctness input.
			hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(library.GetAddressOf()));
		}
		if (FAILED(hr))
		{
			return FailValue<PipelineCacheHandle>(error, ErrorCode::eNativeApiError, "ID3D12Device1::CreatePipelineLibrary failed");
		}

		return ReturnValue(device->pipelineCacheSlots.Store(PipelineCacheSlot{ .library = std::move(library) }), error);
	}

	bool D3D12GetPipelineCacheData(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.d3d12.getPipelineCacheData");

		auto * device			 = static_cast<D3D12Device *>(impl);
		PipelineCacheSlot * slot = device->pipelineCacheSlots.Resolve(cache, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "getPipelineCacheData with an invalid or stale cache handle");
		}

		const SIZE_T size = slot->library->GetSerializedSize();
		slot->data.resize(size);
		if (size != 0 && FAILED(slot->library->Serialize(slot->data.data(), size)))
		{
			return Fail(error, ErrorCode::eNativeApiError, "ID3D12PipelineLibrary::Serialize failed");
		}
		if (out != nullptr)
		{
			out->data = slot->data.data();
			out->size = slot->data.size();
		}
		return Succeed(error);
	}

	bool D3D12DestroyGraphicsPipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const GraphicsPipelineHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		GraphicsPipelineSlot * slot = device->graphicsPipelineSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid graphics pipeline handle");
		}

		slot->pipeline.Reset();
		slot->rootSignature.Reset();
		static_cast<void>(device->graphicsPipelineSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	bool D3D12DestroyComputePipeline(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const ComputePipelineHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		ComputePipelineSlot * slot = device->computePipelineSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid compute pipeline handle");
		}

		slot->pipeline.Reset();
		slot->rootSignature.Reset();
		static_cast<void>(device->computePipelineSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

	bool D3D12DestroyPipelineCache(D3D12Device * device, RawHandle handle, Error * error) noexcept
	{
		const PipelineCacheHandle slotHandle{
			.index		= handle.index,
			.generation = handle.generation,
		};
		PipelineCacheSlot * slot = device->pipelineCacheSlots.Resolve(slotHandle, true);
		if (slot == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidHandle, "destroy of an invalid pipeline cache handle");
		}

		slot->library.Reset();
		slot->data.clear();
		slot->data.shrink_to_fit();
		static_cast<void>(device->pipelineCacheSlots.Retire(slotHandle, true));
		return Succeed(error);
	}

} // namespace azo::rhi::d3d12

#endif // _WIN32

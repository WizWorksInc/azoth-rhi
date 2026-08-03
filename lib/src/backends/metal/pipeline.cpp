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

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	[[nodiscard]] MTL::Texture * ResolveTextureView(MetalDevice * device, TextureViewHandle handle) noexcept
	{
		const auto * tracked = device->textureViews.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	namespace
	{
		/*
		 * Refuses a pipeline whose shaders claim their bindings landed at argument-table indices other than the ones this backend binds them at.
		 *
		 * Metal has no layout object to disagree with, so the disagreement is between what the binary says and what MetalCmdBindDescriptorSet will do. Nothing here
		 * changes where anything is bound. It changes reading a stale texture at the wrong index into a refusal that names the binding.
		 */
		[[nodiscard]] bool BindingMapsAgreeImpl(
			MetalDevice * device, const PipelineLayoutHandle layoutHandle, const std::span<const ShaderBinary> shaders, Error * error) noexcept
		{
			if (std::ranges::none_of(shaders,
					[](const ShaderBinary & shader) noexcept
					{
						return shader.bindingMap != nullptr;
					}))
			{
				return true;
			}

			const MetalPipelineLayout * const layout = device->pipelineLayouts.Resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			detail::HostVector<DescriptorSetLayoutDesc> abiSets;
			abiSets.reserve(layout->sets.size());
			for (const DescriptorSetLayoutHandle setHandle : layout->sets)
			{
				const MetalDescriptorSetLayout * const setLayout = device->descriptorSetLayouts.Resolve(setHandle, kHandleAlreadyChecked);
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

				const ShaderBindingDisagreement bad = CheckShaderBindingMap(MetalApi::id, device->caps.bindingTier, abiLayout, *shader.bindingMap);
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
					// Also what a set above zero looks like here, the ABI addressing only set zero on this backend.
					return Fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding this backend does not bind for that pipeline layout");
				}

				return Fail(
					error, ErrorCode::eInvalidArgument, "a shader binary put a binding at a different argument-table index than this backend binds it at");
			}

			return true;
		}

		/*
		 * Every buffer the compiled function reads, against every index this backend will write one to.
		 *
		 * BindingMapsAgree above answers the same question, but only for a caller who volunteered a map, and one who knew enough to write a correct one was not
		 * going to be caught out. Metal says what the function actually asks for.
		 *
		 * What this catches is a Slang shader declaring no push constant, whose first set wants the index below where this binds it.
		 */
		[[nodiscard]] bool FunctionBuffersAreBoundImpl(
			MetalDevice * device, const PipelineLayoutHandle layoutHandle, const NS::Array * bindings, Error * error) noexcept
		{
			const MetalPipelineLayout * const layout = device->pipelineLayouts.Resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			if (bindings == nullptr)
			{
				return true;
			}

			for (NS::UInteger entry = 0; entry < bindings->count(); ++entry)
			{
				const auto * binding = static_cast<const MTL::Binding *>(bindings->object(entry));

				// Only buffers, and only the ones the compiler kept. Textures and samplers reach a shader through the argument buffer and not a slot of their own on a
				// device with argument buffers, and an unused binding is not one anything has to bind.
				if (binding == nullptr || binding->type() != MTL::BindingTypeBuffer || !binding->isUsed())
				{
					continue;
				}

				// Vertex buffers are bound from their own base, well above where sets go, so they are not a set's index failing to match.
				const auto index = static_cast<std::uint32_t>(binding->index());
				if (index >= kMetalVertexBufferBase)
				{
					continue;
				}

				/*
				 * Buffer 0 only when the layout has a push constant to put there. A layout with none binds nothing at that index, so a shader reading it reads
				 * something that will never be written, which is exactly the shape of the bug: Slang gave its first set buffer 0 because the shader declared no
				 * push constant.
				 *
				 * The reverse pairing still slips through. Both want buffer 0 and reflection reports a buffer either way.
				 */
				if (index == kMetalPushConstantIndex && layout->hasPushConstants)
				{
					continue;
				}

				bool bound = false;
				for (std::uint32_t set = 0; set < layout->sets.size() && !bound; ++set)
				{
					bound = MetalArgumentBufferIndexForSet(set) == index;
				}

				if (!bound)
				{
					return Fail(error,
						ErrorCode::eInvalidArgument,
						"a shader wants a buffer at an index this pipeline layout never binds one to, which on a Slang shader usually means it declares no "
						"push constant and so numbers its sets one below where this ABI reserves buffer 0 for one");
				}
			}

			return true;
		}
	} // namespace

	PipelineLayoutHandle MetalCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPipelineLayout");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorSetLayoutHandle set : desc.sets)
		{
			if (!Resolves(device, set))
			{
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
			}
		}

		MetalPipelineLayout slot;
		slot.sets.assign(desc.sets.begin(), desc.sets.end());
		slot.hasPushConstants = !desc.pushConstants.empty();

		const PipelineLayoutHandle handle = device->pipelineLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal pipeline layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	GraphicsPipelineHandle MetalCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createGraphicsPipeline");
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
		if (desc.raster.conservativeRasterEnable)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal has no conservative rasterization, which conservativeRasterTier reports as eNone");
		}

		if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
		}

		// Both counts index arrays of a fixed size, so a desc naming more than those hold is refused here, not read past their end. The same refusal the Vulkan and
		// Direct3D paths make, so a malformed desc earns one answer, not three.
		if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
		}

		if (vertexInput.topology == PrimitiveTopology::ePatchList)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal tessellates through a compute pre-pass, which this backend does not build");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		if (!BindingMapsAgreeImpl(device, desc.layout, desc.shaders, error))
		{
			return {};
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Function> vertexFunction;
		NS::SharedPtr<MTL::Function> fragmentFunction;
		for (const ShaderBinary & shader : desc.shaders)
		{
			if (shader.stage == ShaderStage::eVertex)
			{
				vertexFunction = CompileFunction(device->device.get(), shader, error);
				if (vertexFunction.get() == nullptr)
				{
					return {};
				}
			}
			else if (shader.stage == ShaderStage::eFragment)
			{
				fragmentFunction = CompileFunction(device->device.get(), shader, error);
				if (fragmentFunction.get() == nullptr)
				{
					return {};
				}
			}
		}
		if (vertexFunction.get() == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires a vertex shader");
		}

		NS::SharedPtr<MTL::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
		descriptor->setVertexFunction(vertexFunction.get());
		if (fragmentFunction.get() != nullptr)
		{
			descriptor->setFragmentFunction(fragmentFunction.get());
		}

		// Build the vertex descriptor so [[stage_in]] shaders fetch attributes. Buffer indices are offset above the resource buffers, matching where setVertexBuffer
		// binds.
		if (!vertexInput.attributes.empty())
		{
			NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
			for (const VertexAttributeDesc & attribute : vertexInput.attributes)
			{
				MTL::VertexAttributeDescriptor * attr = vertexDescriptor->attributes()->object(attribute.location);
				attr->setFormat(MetalVertexFormat(attribute.format));
				attr->setOffset(attribute.offset);
				attr->setBufferIndex(kMetalVertexBufferBase + attribute.binding);
			}
			for (const VertexBindingDesc & binding : vertexInput.bindings)
			{
				MTL::VertexBufferLayoutDescriptor * layout = vertexDescriptor->layouts()->object(kMetalVertexBufferBase + binding.binding);
				layout->setStride(binding.stride);
				layout->setStepFunction(binding.perInstance ? MTL::VertexStepFunctionPerInstance : MTL::VertexStepFunctionPerVertex);
				layout->setStepRate(1);
			}
			descriptor->setVertexDescriptor(vertexDescriptor.get());
		}

		for (std::uint32_t i = 0; i < desc.renderTarget.colorFormatCount; ++i)
		{
			MTL::RenderPipelineColorAttachmentDescriptor * attachment = descriptor->colorAttachments()->object(i);
			// Creation refuses a count past these arrays. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			attachment->setPixelFormat(MetalPixelFormat(desc.renderTarget.colorFormats[i]));
			if (i < desc.blend.attachmentCount)
			{
				const ColorBlendAttachmentDesc & blend = desc.blend.attachments[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				attachment->setBlendingEnabled(blend.blendEnable);
				attachment->setSourceRGBBlendFactor(MetalBlendFactor(blend.srcColorBlendFactor));
				attachment->setDestinationRGBBlendFactor(MetalBlendFactor(blend.dstColorBlendFactor));
				attachment->setRgbBlendOperation(MetalBlendOp(blend.colorBlendOp));
				attachment->setSourceAlphaBlendFactor(MetalBlendFactor(blend.srcAlphaBlendFactor));
				attachment->setDestinationAlphaBlendFactor(MetalBlendFactor(blend.dstAlphaBlendFactor));
				attachment->setAlphaBlendOperation(MetalBlendOp(blend.alphaBlendOp));
				attachment->setWriteMask(MetalColorWriteMask(blend.colorWriteMask));
			}
		}

		if (desc.renderTarget.depthStencilFormat != Format::eUndefined)
		{
			const MTL::PixelFormat depthFormat = MetalPixelFormat(desc.renderTarget.depthStencilFormat);
			descriptor->setDepthAttachmentPixelFormat(depthFormat);
			if (IsStencilFormat(desc.renderTarget.depthStencilFormat))
			{
				descriptor->setStencilAttachmentPixelFormat(depthFormat);
			}
		}

		descriptor->setRasterSampleCount(static_cast<NS::UInteger>(desc.renderTarget.samples));
		descriptor->setAlphaToCoverageEnabled(desc.renderTarget.alphaToCoverageEnable);

		NS::Error * pipelineError					   = nullptr;
		MTL::AutoreleasedRenderPipelineReflection info = nullptr;
		MTL::RenderPipelineState * rawState = device->device->newRenderPipelineState(descriptor.get(), MTL::PipelineOptionBindingInfo, &info, &pipelineError);
		if (rawState == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Metal render pipeline creation failed");
		}

		// Both stages, since either can be the one asking for a set the layout does not bind.
		const bool bound = info == nullptr || (FunctionBuffersAreBoundImpl(device, desc.layout, info->vertexBindings(), error) &&
												  FunctionBuffersAreBoundImpl(device, desc.layout, info->fragmentBindings(), error));
		if (!bound)
		{
			rawState->release();
			return {};
		}

		MetalGraphicsPipeline pipeline{};
		pipeline.state			   = NS::TransferPtr(rawState);
		pipeline.depthStencil	   = BuildDepthStencilState(device->device.get(), desc.depthStencil);
		pipeline.primitive		   = MetalPrimitiveType(vertexInput.topology);
		pipeline.cull			   = MetalCullMode(desc.raster.cullMode);
		pipeline.winding		   = MetalWinding(desc.raster.frontFace);
		pipeline.fill			   = MetalFillMode(desc.raster.fillMode);
		pipeline.depthBiasEnable   = desc.raster.depthBiasEnable;
		pipeline.depthBiasConstant = desc.raster.depthBiasConstantFactor;
		pipeline.depthBiasSlope	   = desc.raster.depthBiasSlopeFactor;
		pipeline.depthBiasClamp	   = desc.raster.depthBiasClamp;

		const GraphicsPipelineHandle handle = device->graphicsPipelines.Store(std::move(pipeline));
		if (!handle.IsValid())
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal graphics pipeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

	ComputePipelineHandle MetalCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createComputePipeline");

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

		auto * device = static_cast<MetalDevice *>(impl);

		const std::array<ShaderBinary, 1> stages{ desc.shader };
		if (!BindingMapsAgreeImpl(device, desc.layout, stages, error))
		{
			return {};
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Function> function = CompileFunction(device->device.get(), desc.shader, error);
		if (function.get() == nullptr)
		{
			return {};
		}

		// Asked for the binding info the check below reads. Metal produces it as part of the same call, so this costs one flag and not a second compile.
		NS::Error * pipelineError						= nullptr;
		MTL::AutoreleasedComputePipelineReflection info = nullptr;
		MTL::ComputePipelineState * rawState = device->device->newComputePipelineState(function.get(), MTL::PipelineOptionBindingInfo, &info, &pipelineError);
		if (rawState == nullptr)
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Metal compute pipeline creation failed");
		}

		if (!FunctionBuffersAreBoundImpl(device, desc.layout, info != nullptr ? info->bindings() : nullptr, error))
		{
			rawState->release();
			return {};
		}

		MetalComputePipeline pipeline{};
		pipeline.state				   = NS::TransferPtr(rawState);
		pipeline.threadsPerThreadgroup = MTL::Size::Make(desc.shader.threadgroupSize.x, desc.shader.threadgroupSize.y, desc.shader.threadgroupSize.z);

		const ComputePipelineHandle handle = device->computePipelines.Store(std::move(pipeline));
		if (!handle.IsValid())
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal compute pipeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal

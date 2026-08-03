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

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	[[nodiscard]] MTL::Texture * ResolveTextureView(Metal4Device * device, TextureViewHandle handle) noexcept
	{
		const auto * tracked = device->textureViews.Resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	namespace
	{
		/*
		 * Refuses a pipeline whose shaders claim their bindings landed at argument-table indices other than the ones this backend binds them at.
		 *
		 * Metal has no layout object to disagree with, so the disagreement is between what the binary says and what CmdBindDescriptorSet will do. Nothing here
		 * changes where anything is bound. It changes reading a stale texture at the wrong index into a refusal that names the binding.
		 */
		[[nodiscard]] bool BindingMapsAgreeImpl(
			Metal4Device * device, const PipelineLayoutHandle layoutHandle, const std::span<const ShaderBinary> shaders, Error * error) noexcept
		{
			if (std::ranges::none_of(shaders,
					[](const ShaderBinary & shader) noexcept
					{
						return shader.bindingMap != nullptr;
					}))
			{
				return true;
			}

			const Metal4PipelineLayout * const layout = device->pipelineLayouts.Resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			detail::HostVector<DescriptorSetLayoutDesc> abiSets;
			abiSets.reserve(layout->sets.size());
			for (const DescriptorSetLayoutHandle setHandle : layout->sets)
			{
				const Metal4DescriptorSetLayout * const setLayout = device->descriptorSetLayouts.Resolve(setHandle, kHandleAlreadyChecked);
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

				const ShaderBindingDisagreement bad = CheckShaderBindingMap(Metal4Api::id, device->caps.bindingTier, abiLayout, *shader.bindingMap);
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
			Metal4Device * device, const PipelineLayoutHandle layoutHandle, const NS::Array * bindings, Error * error) noexcept
		{
			const Metal4PipelineLayout * const layout = device->pipelineLayouts.Resolve(layoutHandle, kHandleAlreadyChecked);
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

	/*
	 * The two checks above, out of the anonymous namespace so the pipeline creation below can reach them.
	 *
	 * The second is the one that matters more: without it the pipeline builds, the dispatch runs, and the shader reads zeros from a buffer index nothing was
	 * ever put at, which is the failure the reserved push constant slot makes easy to hit and impossible to see.
	 */
	bool BindingMapsAgree(Metal4Device * device, const PipelineLayoutHandle layout, const std::span<const ShaderBinary> shaders, Error * error) noexcept
	{
		return BindingMapsAgreeImpl(device, layout, shaders, error);
	}

	bool FunctionBuffersAreBound(Metal4Device * device, const PipelineLayoutHandle layout, const NS::Array * bindings, Error * error) noexcept
	{
		return FunctionBuffersAreBoundImpl(device, layout, bindings, error);
	}

	PipelineLayoutHandle CreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createPipelineLayout");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorSetLayoutHandle set : desc.sets)
		{
			if (!Resolves(device, set))
			{
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
			}
		}

		Metal4PipelineLayout slot;
		slot.sets.assign(desc.sets.begin(), desc.sets.end());
		slot.hasPushConstants = !desc.pushConstants.empty();

		const PipelineLayoutHandle handle = device->pipelineLayouts.Store(std::move(slot));
		if (!handle.IsValid())
		{
			return FailValue<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal pipeline layout handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	/*
	 * Pipelines, which fork for a reason that is easy to miss.
	 *
	 * MTL4Compiler::newComputePipelineState hands back an MTL::ComputePipelineState, the same type the other generation builds, so reading the signatures says
	 * pipelines are shared. They are not.
	 *
	 * What differs is how the state was compiled. A pipeline built the classic way expects its buffers bound by setBuffer on the encoder, so an argument table
	 * binds nothing it is looking for and the dispatch reads whatever was already there. It does not fail.
	 */

	namespace
	{
		/*
		 * A function descriptor naming one entry point of a library.
		 *
		 * Where the other generation makes an MTLFunction. The library underneath is the same object built by the same shared code, so a metallib does not have
		 * to be compiled twice to serve both paths.
		 */
		[[nodiscard]] NS::SharedPtr<MTL4::LibraryFunctionDescriptor> FunctionDescriptorFor(
			Metal4Device * device, const ShaderBinary & shader, Error * error)
		{
			NS::SharedPtr<MTL::Library> library = MetalCompileLibrary(device->device.get(), shader, error);
			if (library.get() == nullptr)
			{
				return {};
			}

			const NS::SharedPtr<NS::String> name = NS::TransferPtr(NS::String::alloc()->init(shader.entryPoint, NS::UTF8StringEncoding));

			NS::SharedPtr<MTL4::LibraryFunctionDescriptor> descriptor = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
			descriptor->setLibrary(library.get());
			descriptor->setName(name.get());

			/*
			 * The library is referenced by the descriptor and released when this returns, so the pipeline the caller builds has to be built before then. Every
			 * caller here does, compiling inside the same call.
			 */
			return descriptor;
		}
	} // namespace

	ComputePipelineHandle CreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createComputePipeline");

		// The same requirement the other generation states, and for the same reason: Metal takes the threadgroup size at dispatch and MSL has no numthreads
		// attribute to recover it from.
		if (!desc.shader.threadgroupSize.IsStated())
		{
			return FailValue<ComputePipelineHandle>(error,
				ErrorCode::eInvalidArgument,
				"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		const std::array<ShaderBinary, 1> stages{ desc.shader };
		if (!BindingMapsAgree(device, desc.layout, stages, error))
		{
			return {};
		}

		MTL4::Compiler * compiler = device->compiler.get();
		if (compiler == nullptr)
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "this device has no Metal 4 compiler");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		const NS::SharedPtr<MTL4::LibraryFunctionDescriptor> function = FunctionDescriptorFor(device, desc.shader, error);
		if (function.get() == nullptr)
		{
			return {};
		}

		const NS::SharedPtr<MTL4::ComputePipelineDescriptor> pipelineDesc = NS::TransferPtr(MTL4::ComputePipelineDescriptor::alloc()->init());
		pipelineDesc->setComputeFunctionDescriptor(function.get());

		// Asked for so the check below has something to read. Metal produces it as part of the same compile.
		const NS::SharedPtr<MTL4::PipelineOptions> options = NS::TransferPtr(MTL4::PipelineOptions::alloc()->init());
		options->setShaderReflection(MTL4::ShaderReflectionBindingInfo);
		pipelineDesc->setOptions(options.get());

		NS::Error * pipelineError			 = nullptr;
		MTL::ComputePipelineState * rawState = compiler->newComputePipelineState(pipelineDesc.get(), nullptr, &pipelineError);
		if (rawState == nullptr)
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Metal 4 compute pipeline creation failed");
		}

		if (MTL::ComputePipelineReflection * info = rawState->reflection();
			info != nullptr && !FunctionBuffersAreBound(device, desc.layout, info->bindings(), error))
		{
			rawState->release();
			return {};
		}

		Metal4ComputePipeline pipeline{};
		pipeline.state				   = NS::TransferPtr(rawState);
		pipeline.threadsPerThreadgroup = MTL::Size::Make(desc.shader.threadgroupSize.x, desc.shader.threadgroupSize.y, desc.shader.threadgroupSize.z);

		const ComputePipelineHandle handle = device->computePipelines.Store(std::move(pipeline));
		if (!handle.IsValid())
		{
			return FailValue<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 compute pipeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

	GraphicsPipelineHandle CreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createGraphicsPipeline");

		// The same refusals the other generation makes, on the same grounds. A Metal 4 compiler does not add a mesh stage, conservative rasterization or a
		// tessellator to a backend that has none of them.
		if (desc.vertexInput == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
		}

		const VertexInputDesc & vertexInput = *desc.vertexInput;
		if (desc.raster.conservativeRasterEnable)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal has no conservative rasterization, which conservativeRasterTier reports as eNone");
		}
		if (vertexInput.topology == PrimitiveTopology::ePatchList)
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal tessellates through a compute pre-pass, which this backend does not build");
		}
		if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
		{
			return FailValue<GraphicsPipelineHandle>(
				error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		if (!BindingMapsAgree(device, desc.layout, desc.shaders, error))
		{
			return {};
		}

		MTL4::Compiler * compiler = device->compiler.get();
		if (compiler == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "this device has no Metal 4 compiler");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// Held past the loop, because the pipeline descriptor references them and the compile happens after it.
		NS::SharedPtr<MTL4::LibraryFunctionDescriptor> vertex;
		NS::SharedPtr<MTL4::LibraryFunctionDescriptor> fragment;

		const NS::SharedPtr<MTL4::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());

		const NS::SharedPtr<MTL4::PipelineOptions> options = NS::TransferPtr(MTL4::PipelineOptions::alloc()->init());
		options->setShaderReflection(MTL4::ShaderReflectionBindingInfo);
		descriptor->setOptions(options.get());

		for (const ShaderBinary & shader : desc.shaders)
		{
			if (shader.stage == ShaderStage::eVertex)
			{
				vertex = FunctionDescriptorFor(device, shader, error);
				if (vertex.get() == nullptr)
				{
					return {};
				}

				descriptor->setVertexFunctionDescriptor(vertex.get());
			}
			else if (shader.stage == ShaderStage::eFragment)
			{
				fragment = FunctionDescriptorFor(device, shader, error);
				if (fragment.get() == nullptr)
				{
					return {};
				}

				descriptor->setFragmentFunctionDescriptor(fragment.get());
			}
		}

		if (vertex.get() == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires a vertex shader");
		}

		// The vertex descriptor is an MTLVertexDescriptor on both generations, and the buffer indices are the same ones setVertexBuffer binds at, so a shader's
		// stage_in works the same either way.
		if (!vertexInput.attributes.empty())
		{
			const NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
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
			// The attachment descriptor is this generation's own type, unlike the render pass one, so this is the one place the blend state is written against
			// a different object.
			MTL4::RenderPipelineColorAttachmentDescriptor * attachment = descriptor->colorAttachments()->object(i);

			// Creation refuses a count past these arrays. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			attachment->setPixelFormat(MetalPixelFormat(desc.renderTarget.colorFormats[i]));
			if (i < desc.blend.attachmentCount)
			{
				const ColorBlendAttachmentDesc & blend = desc.blend.attachments[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				attachment->setBlendingState(blend.blendEnable ? MTL4::BlendStateEnabled : MTL4::BlendStateDisabled);
				attachment->setSourceRGBBlendFactor(MetalBlendFactor(blend.srcColorBlendFactor));
				attachment->setDestinationRGBBlendFactor(MetalBlendFactor(blend.dstColorBlendFactor));
				attachment->setRgbBlendOperation(MetalBlendOp(blend.colorBlendOp));
				attachment->setSourceAlphaBlendFactor(MetalBlendFactor(blend.srcAlphaBlendFactor));
				attachment->setDestinationAlphaBlendFactor(MetalBlendFactor(blend.dstAlphaBlendFactor));
				attachment->setAlphaBlendOperation(MetalBlendOp(blend.alphaBlendOp));
				attachment->setWriteMask(MetalColorWriteMask(blend.colorWriteMask));
			}
		}

		/*
		 * No depth or stencil attachment format here, and its absence is the API's, not an omission. MTL4RenderPipelineDescriptor carries color formats and not
		 * depth ones: on this generation the depth format comes from the render pass at encoding time, which is why a pipeline built here is usable against
		 * more than one depth target.
		 *
		 * GraphicsPipelineDesc::renderTarget::depthStencilFormat is therefore read by the other generation and not by this one.
		 */

		descriptor->setRasterSampleCount(static_cast<NS::UInteger>(desc.renderTarget.samples));
		descriptor->setAlphaToCoverageState(desc.renderTarget.alphaToCoverageEnable ? MTL4::AlphaToCoverageStateEnabled : MTL4::AlphaToCoverageStateDisabled);

		NS::Error * pipelineError			= nullptr;
		MTL::RenderPipelineState * rawState = compiler->newRenderPipelineState(descriptor.get(), nullptr, &pipelineError);
		if (rawState == nullptr)
		{
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Metal 4 render pipeline creation failed");
		}

		// Both stages, since either can be the one asking for a set the layout does not bind.
		if (MTL::RenderPipelineReflection * info = rawState->reflection();
			info != nullptr && !(FunctionBuffersAreBound(device, desc.layout, info->vertexBindings(), error) &&
									FunctionBuffersAreBound(device, desc.layout, info->fragmentBindings(), error)))
		{
			rawState->release();
			return {};
		}

		// The rest of a Metal pipeline is state the encoder takes and not the pipeline object, so it is recorded here and applied at bind, exactly as on
		// the other generation.
		Metal4GraphicsPipeline pipeline{};
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
			return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 graphics pipeline tracking failed");
		}

		return ReturnValue(handle, error);
	}

} // namespace azo::rhi::metal4

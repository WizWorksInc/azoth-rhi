// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/native_slot.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"
#include <Foundation/NSArray.hpp>
#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSString.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTL4Compiler.hpp>
#include <Metal/MTL4ComputePipeline.hpp>
#include <Metal/MTL4LibraryFunctionDescriptor.hpp>
#include <Metal/MTL4PipelineState.hpp>
#include <Metal/MTL4RenderPipeline.hpp>
#include <Metal/MTLArgument.hpp>
#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLLibrary.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/MTLTexture.hpp>
#include <Metal/MTLVertexDescriptor.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace azo::rhi::metal4
{
	[[nodiscard]] MTL::Texture * resolve_texture_view(Metal4Device * device, TextureViewHandle handle) noexcept
	{
		const auto * tracked = device->textureViews.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	namespace
	{
		[[nodiscard]] bool binding_maps_agree_impl(
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

			const Metal4PipelineLayout * const layout = device->pipelineLayouts.resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			detail::HostVector<DescriptorSetLayoutDesc> abiSets;
			abiSets.reserve(layout->sets.size());
			for (const DescriptorSetLayoutHandle setHandle : layout->sets)
			{
				const Metal4DescriptorSetLayout * const setLayout = device->descriptorSetLayouts.resolve(setHandle, kHandleAlreadyChecked);
				if (setLayout == nullptr)
				{
					return fail(error, ErrorCode::eInvalidHandle, "a descriptor set layout this pipeline layout was built from has been destroyed");
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

				const ShaderBindingDisagreement bad = check_shader_binding_map(Metal4Api::kId, device->caps.bindingTier, abiLayout, *shader.bindingMap);
				if (!bad.found)
				{
					continue;
				}

				if (bad.wrongAbiVersion)
				{
					return fail(
						error, ErrorCode::eUnsupportedFormat, "a shader binary was built against a revision of the binding ABI this build does not implement");
				}

				if (bad.unknownToLayout)
				{
					return fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding this backend does not bind for that pipeline layout");
				}

				return fail(
					error, ErrorCode::eInvalidArgument, "a shader binary put a binding at a different argument-table index than this backend binds it at");
			}

			return true;
		}

		[[nodiscard]] bool function_buffers_are_bound_impl(
			Metal4Device * device, const PipelineLayoutHandle layoutHandle, const NS::Array * bindings, Error * error) noexcept
		{
			const Metal4PipelineLayout * const layout = device->pipelineLayouts.resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			if (bindings == nullptr)
			{
				return true;
			}

			for (NS::UInteger entry = 0; entry < bindings->count(); ++entry)
			{
				const auto * binding = static_cast<const MTL::Binding *>(bindings->object(entry));

				if (binding == nullptr || binding->type() != MTL::BindingTypeBuffer || !binding->isUsed())
				{
					continue;
				}

				const auto index = static_cast<std::uint32_t>(binding->index());
				if (index >= kMetalVertexBufferBase)
				{
					continue;
				}

				if (index == kMetalPushConstantIndex && layout->hasPushConstants)
				{
					continue;
				}

				bool bound = false;
				for (std::uint32_t set = 0; set < layout->sets.size() && !bound; ++set)
				{
					bound = metal_argument_buffer_index_for_set(set) == index;
				}

				if (!bound)
				{
					return fail(error,
						ErrorCode::eInvalidArgument,
						"a shader wants a buffer at an index this pipeline layout never binds one to, which on a Slang shader usually means it declares no "
						"push constant and so numbers its sets one below where this ABI reserves buffer 0 for one");
				}
			}

			return true;
		}
	}

	bool binding_maps_agree(Metal4Device * device, const PipelineLayoutHandle layout, const std::span<const ShaderBinary> shaders, Error * error) noexcept
	{
		return binding_maps_agree_impl(device, layout, shaders, error);
	}

	bool function_buffers_are_bound(Metal4Device * device, const PipelineLayoutHandle layout, const NS::Array * bindings, Error * error) noexcept
	{
		return function_buffers_are_bound_impl(device, layout, bindings, error);
	}

	PipelineLayoutHandle metal4_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createPipelineLayout");

		auto * device = static_cast<Metal4Device *>(impl);
		for (const DescriptorSetLayoutHandle set : desc.sets)
		{
			if (!resolves(device, set))
			{
				return fail_value<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
			}
		}

		Metal4PipelineLayout slot;
		slot.sets.assign(desc.sets.begin(), desc.sets.end());
		slot.hasPushConstants = !desc.pushConstants.empty();

		const PipelineLayoutHandle handle = device->pipelineLayouts.store(std::move(slot));
		if (!handle.is_valid())
		{
			return fail_value<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal pipeline layout handle tracking failed");
		}

		return return_value(handle, error);
	}

	namespace
	{
		[[nodiscard]] NS::SharedPtr<MTL4::LibraryFunctionDescriptor> function_descriptor_for(Metal4Device * device, const ShaderBinary & shader, Error * error)
		{
			NS::SharedPtr<MTL::Library> library = metal_compile_library(device->device.get(), shader, error);
			if (library.get() == nullptr)
			{
				return {};
			}

			const NS::SharedPtr<NS::String> name = NS::TransferPtr(NS::String::alloc()->init(shader.entryPoint, NS::UTF8StringEncoding));

			NS::SharedPtr<MTL4::LibraryFunctionDescriptor> descriptor = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
			descriptor->setLibrary(library.get());
			descriptor->setName(name.get());

			return descriptor;
		}
	}

	ComputePipelineHandle metal4_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createComputePipeline");

		if (!desc.shader.threadgroupSize.is_stated())
		{
			return fail_value<ComputePipelineHandle>(error,
				ErrorCode::eInvalidArgument,
				"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		const std::array<ShaderBinary, 1> stages{ desc.shader };
		if (!binding_maps_agree(device, desc.layout, stages, error))
		{
			return {};
		}

		MTL4::Compiler * compiler = device->compiler.get();
		if (compiler == nullptr)
		{
			return fail_value<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "this device has no Metal 4 compiler");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		const NS::SharedPtr<MTL4::LibraryFunctionDescriptor> function = function_descriptor_for(device, desc.shader, error);
		if (function.get() == nullptr)
		{
			return {};
		}

		const NS::SharedPtr<MTL4::ComputePipelineDescriptor> pipelineDesc = NS::TransferPtr(MTL4::ComputePipelineDescriptor::alloc()->init());
		pipelineDesc->setComputeFunctionDescriptor(function.get());

		const NS::SharedPtr<MTL4::PipelineOptions> options = NS::TransferPtr(MTL4::PipelineOptions::alloc()->init());
		options->setShaderReflection(MTL4::ShaderReflectionBindingInfo);
		pipelineDesc->setOptions(options.get());

		NS::Error * pipelineError			 = nullptr;
		MTL::ComputePipelineState * rawState = compiler->newComputePipelineState(pipelineDesc.get(), nullptr, &pipelineError);
		if (rawState == nullptr)
		{
			return fail_value<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Metal 4 compute pipeline creation failed");
		}

		if (MTL::ComputePipelineReflection * info = rawState->reflection();
			info != nullptr && !function_buffers_are_bound(device, desc.layout, info->bindings(), error))
		{
			rawState->release();
			return {};
		}

		Metal4ComputePipeline pipeline{};
		pipeline.state				   = NS::TransferPtr(rawState);
		pipeline.threadsPerThreadgroup = MTL::Size::Make(desc.shader.threadgroupSize.x, desc.shader.threadgroupSize.y, desc.shader.threadgroupSize.z);

		const ComputePipelineHandle handle = device->computePipelines.store(std::move(pipeline));
		if (!handle.is_valid())
		{
			return fail_value<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 compute pipeline tracking failed");
		}

		return return_value(handle, error);
	}

	GraphicsPipelineHandle metal4_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createGraphicsPipeline");

		if (desc.vertexInput == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
		}

		const VertexInputDesc & vertexInput = *desc.vertexInput;
		if (desc.raster.conservativeRasterEnable)
		{
			return fail_value<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal has no conservative rasterization, which conservativeRasterTier reports as eNone");
		}
		if (vertexInput.topology == PrimitiveTopology::ePatchList)
		{
			return fail_value<GraphicsPipelineHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal tessellates through a compute pre-pass, which this backend does not build");
		}
		if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
		{
			return fail_value<GraphicsPipelineHandle>(
				error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		if (!binding_maps_agree(device, desc.layout, desc.shaders, error))
		{
			return {};
		}

		MTL4::Compiler * compiler = device->compiler.get();
		if (compiler == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "this device has no Metal 4 compiler");
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL4::LibraryFunctionDescriptor> vertex;
		NS::SharedPtr<MTL4::LibraryFunctionDescriptor> fragment;

		const NS::SharedPtr<MTL4::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());

		const NS::SharedPtr<MTL4::PipelineOptions> options = NS::TransferPtr(MTL4::PipelineOptions::alloc()->init());
		options->setShaderReflection(MTL4::ShaderReflectionBindingInfo);
		descriptor->setOptions(options.get());

		for (const ShaderBinary & shader : desc.shaders)
		{
			if (!metal_refuse_unbuildable_graphics_stage(shader.stage, error))
			{
				return {};
			}

			if (shader.stage == ShaderStage::eVertex)
			{
				vertex = function_descriptor_for(device, shader, error);
				if (vertex.get() == nullptr)
				{
					return {};
				}

				descriptor->setVertexFunctionDescriptor(vertex.get());
			}
			else if (shader.stage == ShaderStage::eFragment)
			{
				fragment = function_descriptor_for(device, shader, error);
				if (fragment.get() == nullptr)
				{
					return {};
				}

				descriptor->setFragmentFunctionDescriptor(fragment.get());
			}
		}

		if (vertex.get() == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires a vertex shader");
		}

		if (!vertexInput.attributes.empty())
		{
			const NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
			for (const VertexAttributeDesc & attribute : vertexInput.attributes)
			{
				const MTL::VertexFormat vertexFormat = metal_vertex_format(attribute.format);
				if (vertexFormat == MTL::VertexFormatInvalid)
				{
					return fail_value<GraphicsPipelineHandle>(
						error, ErrorCode::eUnsupportedFeature, "a vertex attribute names a format this backend has no Metal vertex format for");
				}

				MTL::VertexAttributeDescriptor * attr = vertexDescriptor->attributes()->object(attribute.location);
				attr->setFormat(vertexFormat);
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
			MTL4::RenderPipelineColorAttachmentDescriptor * attachment = descriptor->colorAttachments()->object(i);

			// Creation refuses a count past these arrays. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (!metal_refuse_unrenderable_attachment(desc.renderTarget.colorFormats[i], error))
			{
				return {};
			}
			attachment->setPixelFormat(metal_pixel_format(desc.renderTarget.colorFormats[i]));
			if (i < desc.blend.attachmentCount)
			{
				const ColorBlendAttachmentDesc & blend = desc.blend.attachments[i];
				if (blend.blendEnable && !metal_refuse_unblendable_attachment(desc.renderTarget.colorFormats[i], error))
				{
					return {};
				}
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				attachment->setBlendingState(blend.blendEnable ? MTL4::BlendStateEnabled : MTL4::BlendStateDisabled);
				attachment->setSourceRGBBlendFactor(metal_blend_factor(blend.srcColorBlendFactor));
				attachment->setDestinationRGBBlendFactor(metal_blend_factor(blend.dstColorBlendFactor));
				attachment->setRgbBlendOperation(metal_blend_op(blend.colorBlendOp));
				attachment->setSourceAlphaBlendFactor(metal_blend_factor(blend.srcAlphaBlendFactor));
				attachment->setDestinationAlphaBlendFactor(metal_blend_factor(blend.dstAlphaBlendFactor));
				attachment->setAlphaBlendOperation(metal_blend_op(blend.alphaBlendOp));
				attachment->setWriteMask(metal_color_write_mask(blend.colorWriteMask));
			}
		}

		descriptor->setRasterSampleCount(static_cast<NS::UInteger>(desc.renderTarget.samples));
		descriptor->setAlphaToCoverageState(desc.renderTarget.alphaToCoverageEnable ? MTL4::AlphaToCoverageStateEnabled : MTL4::AlphaToCoverageStateDisabled);

		NS::Error * pipelineError			= nullptr;
		MTL::RenderPipelineState * rawState = compiler->newRenderPipelineState(descriptor.get(), nullptr, &pipelineError);
		if (rawState == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Metal 4 render pipeline creation failed");
		}

		if (MTL::RenderPipelineReflection * info = rawState->reflection();
			info != nullptr && !(function_buffers_are_bound(device, desc.layout, info->vertexBindings(), error) &&
								   function_buffers_are_bound(device, desc.layout, info->fragmentBindings(), error)))
		{
			rawState->release();
			return {};
		}

		Metal4GraphicsPipeline pipeline{};
		pipeline.state			   = NS::TransferPtr(rawState);
		pipeline.depthStencil	   = build_depth_stencil_state(device->device.get(), desc.depthStencil);
		pipeline.primitive		   = metal_primitive_type(vertexInput.topology);
		pipeline.cull			   = metal_cull_mode(desc.raster.cullMode);
		pipeline.winding		   = metal_winding(desc.raster.frontFace);
		pipeline.fill			   = metal_fill_mode(desc.raster.fillMode);
		pipeline.depthBiasEnable   = desc.raster.depthBiasEnable;
		pipeline.depthBiasConstant = desc.raster.depthBiasConstantFactor;
		pipeline.depthBiasSlope	   = desc.raster.depthBiasSlopeFactor;
		pipeline.depthBiasClamp	   = desc.raster.depthBiasClamp;

		const GraphicsPipelineHandle handle = device->graphicsPipelines.store(std::move(pipeline));
		if (!handle.is_valid())
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal 4 graphics pipeline tracking failed");
		}

		return return_value(handle, error);
	}

}

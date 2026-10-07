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

#include "azoth/rhi/resources/pipeline.hpp"

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

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSArray.hpp>
#include <Foundation/NSAutoreleasePool.hpp>
#include <Foundation/NSError.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTLArgument.hpp>
#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLLibrary.hpp>
#include <Metal/MTLPixelFormat.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/MTLTexture.hpp>
#include <Metal/MTLVertexDescriptor.hpp>

#include <algorithm>
#include <array>
#include <cstdint> // NOLINT
#include <span>
#include <utility>

namespace azo::rhi::metal
{
	[[nodiscard]] MTL::Texture * resolve_texture_view(MetalDevice * device, TextureViewHandle handle) noexcept
	{
		const auto * tracked = device->textureViews.resolve(handle, kHandleAlreadyChecked);
		return tracked != nullptr ? tracked->texture.get() : nullptr;
	}

	namespace
	{
		[[nodiscard]] bool binding_maps_agree_impl(
			MetalDevice * device,
			const PipelineLayoutHandle layoutHandle,
			const std::span<const ShaderBinary> shaders,
			Error * error
		) noexcept
		{
			if (std::ranges::none_of(
					shaders,
					[](const ShaderBinary & shader) noexcept
					{
						return shader.bindingMap != nullptr;
					}
				))
			{
				return true;
			}

			const MetalPipelineLayout * const layout = device->pipelineLayouts.resolve(layoutHandle, kHandleAlreadyChecked);
			if (layout == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
			}

			detail::HostVector<DescriptorSetLayoutDesc> abiSets;
			abiSets.reserve(layout->sets.size());
			for (const DescriptorSetLayoutHandle setHandle : layout->sets)
			{
				const MetalDescriptorSetLayout * const setLayout = device->descriptorSetLayouts.resolve(setHandle, kHandleAlreadyChecked);
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

				const ShaderBindingDisagreement bad = check_shader_binding_map(MetalApi::kId, device->caps.bindingTier, abiLayout, *shader.bindingMap);
				if (!bad.found)
				{
					continue;
				}

				if (bad.wrongAbiVersion)
				{
					return fail(
						error,
						ErrorCode::eUnsupportedFormat,
						"a shader binary was built against a revision of the binding ABI this build does not implement"
					);
				}

				if (bad.unknownToLayout)
				{
					return fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding this backend does not bind for that pipeline layout");
				}

				return fail(
					error,
					ErrorCode::eInvalidArgument,
					"a shader binary put a binding at a different argument-table index than this backend binds it at"
				);
			}

			return true;
		}

		[[nodiscard]] bool function_buffers_are_bound_impl(
			MetalDevice * device,
			const PipelineLayoutHandle layoutHandle,
			const NS::Array * bindings,
			Error * error
		) noexcept
		{
			const MetalPipelineLayout * const layout = device->pipelineLayouts.resolve(layoutHandle, kHandleAlreadyChecked);
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
					return fail(
						error,
						ErrorCode::eInvalidArgument,
						"a shader wants a buffer at an index this pipeline layout never binds one to, which on a Slang shader usually means it declares no "
						"push constant and so numbers its sets one below where this ABI reserves buffer 0 for one"
					);
				}
			}

			return true;
		}
	} // namespace

	PipelineLayoutHandle metal_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createPipelineLayout");

		auto * device = static_cast<MetalDevice *>(impl);
		for (const DescriptorSetLayoutHandle set : desc.sets)
		{
			if (!resolves(device, set))
			{
				return fail_value<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
			}
		}

		MetalPipelineLayout slot;
		slot.sets.assign(desc.sets.begin(), desc.sets.end());
		slot.hasPushConstants = !desc.pushConstants.empty();

		const PipelineLayoutHandle handle = device->pipelineLayouts.store(std::move(slot));
		if (!handle.is_valid())
		{
			return fail_value<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Metal pipeline layout handle tracking failed");
		}

		return return_value(handle, error);
	}

	GraphicsPipelineHandle metal_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createGraphicsPipeline");
		if (desc.vertexInput == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have"
			);
		}

		const VertexInputDesc & vertexInput = *desc.vertexInput;
		if (desc.raster.conservativeRasterEnable)
		{
			return fail_value<GraphicsPipelineHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal has no conservative rasterization, which conservativeRasterTier reports as eNone"
			);
		}

		if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
		}

		if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
		{
			return fail_value<GraphicsPipelineHandle>(
				error,
				ErrorCode::eInvalidArgument,
				"graphics pipeline names more color attachments than a render target can hold"
			);
		}

		if (vertexInput.topology == PrimitiveTopology::ePatchList)
		{
			return fail_value<GraphicsPipelineHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal tessellates through a compute pre-pass, which this backend does not build"
			);
		}

		auto * device = static_cast<MetalDevice *>(impl);

		if (!binding_maps_agree_impl(device, desc.layout, desc.shaders, error))
		{
			return {};
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Function> vertexFunction;
		NS::SharedPtr<MTL::Function> fragmentFunction;
		for (const ShaderBinary & shader : desc.shaders)
		{
			if (!metal_refuse_unbuildable_graphics_stage(shader.stage, error))
			{
				return {};
			}

			if (shader.stage == ShaderStage::eVertex)
			{
				vertexFunction = compile_function(device->device.get(), shader, error);
				if (vertexFunction.get() == nullptr)
				{
					return {};
				}
			}
			else if (shader.stage == ShaderStage::eFragment)
			{
				fragmentFunction = compile_function(device->device.get(), shader, error);
				if (fragmentFunction.get() == nullptr)
				{
					return {};
				}
			}
		}
		if (vertexFunction.get() == nullptr)
		{
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires a vertex shader");
		}

		NS::SharedPtr<MTL::RenderPipelineDescriptor> descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
		descriptor->setVertexFunction(vertexFunction.get());
		if (fragmentFunction.get() != nullptr)
		{
			descriptor->setFragmentFunction(fragmentFunction.get());
		}

		if (!vertexInput.attributes.empty())
		{
			NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
			for (const VertexAttributeDesc & attribute : vertexInput.attributes)
			{
				const MTL::VertexFormat vertexFormat = metal_vertex_format(attribute.format);
				if (vertexFormat == MTL::VertexFormatInvalid)
				{
					return fail_value<GraphicsPipelineHandle>(
						error,
						ErrorCode::eUnsupportedFeature,
						"a vertex attribute names a format this backend has no Metal vertex format for"
					);
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
			MTL::RenderPipelineColorAttachmentDescriptor * attachment = descriptor->colorAttachments()->object(i);
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
				attachment->setBlendingEnabled(blend.blendEnable);
				attachment->setSourceRGBBlendFactor(metal_blend_factor(blend.srcColorBlendFactor));
				attachment->setDestinationRGBBlendFactor(metal_blend_factor(blend.dstColorBlendFactor));
				attachment->setRgbBlendOperation(metal_blend_op(blend.colorBlendOp));
				attachment->setSourceAlphaBlendFactor(metal_blend_factor(blend.srcAlphaBlendFactor));
				attachment->setDestinationAlphaBlendFactor(metal_blend_factor(blend.dstAlphaBlendFactor));
				attachment->setAlphaBlendOperation(metal_blend_op(blend.alphaBlendOp));
				attachment->setWriteMask(metal_color_write_mask(blend.colorWriteMask));
			}
		}

		if (desc.renderTarget.depthStencilFormat != Format::eUndefined)
		{
			const MTL::PixelFormat depthFormat = metal_pixel_format(desc.renderTarget.depthStencilFormat);
			descriptor->setDepthAttachmentPixelFormat(depthFormat);
			if (is_stencil_format(desc.renderTarget.depthStencilFormat))
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
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Metal render pipeline creation failed");
		}

		const bool bound = info == nullptr || (function_buffers_are_bound_impl(device, desc.layout, info->vertexBindings(), error) &&
												  function_buffers_are_bound_impl(device, desc.layout, info->fragmentBindings(), error));
		if (!bound)
		{
			rawState->release();
			return {};
		}

		MetalGraphicsPipeline pipeline{};
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
			return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal graphics pipeline tracking failed");
		}

		return return_value(handle, error);
	}

	ComputePipelineHandle metal_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createComputePipeline");

		if (!desc.shader.threadgroupSize.is_stated())
		{
			return fail_value<ComputePipelineHandle>(
				error,
				ErrorCode::eInvalidArgument,
				"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary"
			);
		}

		auto * device = static_cast<MetalDevice *>(impl);

		const std::array<ShaderBinary, 1> stages{ desc.shader };
		if (!binding_maps_agree_impl(device, desc.layout, stages, error))
		{
			return {};
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Function> function = compile_function(device->device.get(), desc.shader, error);
		if (function.get() == nullptr)
		{
			return {};
		}

		NS::Error * pipelineError						= nullptr;
		MTL::AutoreleasedComputePipelineReflection info = nullptr;
		MTL::ComputePipelineState * rawState = device->device->newComputePipelineState(function.get(), MTL::PipelineOptionBindingInfo, &info, &pipelineError);
		if (rawState == nullptr)
		{
			return fail_value<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Metal compute pipeline creation failed");
		}

		if (!function_buffers_are_bound_impl(device, desc.layout, info != nullptr ? info->bindings() : nullptr, error))
		{
			rawState->release();
			return {};
		}

		MetalComputePipeline pipeline{};
		pipeline.state				   = NS::TransferPtr(rawState);
		pipeline.threadsPerThreadgroup = MTL::Size::Make(desc.shader.threadgroupSize.x, desc.shader.threadgroupSize.y, desc.shader.threadgroupSize.z);

		const ComputePipelineHandle handle = device->computePipelines.store(std::move(pipeline));
		if (!handle.is_valid())
		{
			return fail_value<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Metal compute pipeline tracking failed");
		}

		return return_value(handle, error);
	}

} // namespace azo::rhi::metal

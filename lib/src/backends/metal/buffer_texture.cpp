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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSRange.hpp>
#include <Foundation/NSSharedPtr.hpp>
#include <Foundation/NSTypes.hpp>
#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLPixelFormat.hpp>
#include <Metal/MTLSampler.hpp>
#include <Metal/MTLTexture.hpp>

#include <utility>

namespace azo::rhi::metal
{
	BufferHandle metal_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept
	{
		if (!metal_refuse_unexportable(desc.exportableHandleTypes, {}, "Metal exports no buffers, so a buffer cannot be created exportable", error))
		{
			return BufferHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createBuffer");

		if (desc.size == 0)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be non-zero");
		}

		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.allowSparseBinding)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "Metal cannot bind sparse memory to a buffer");
		}

		MTL::Buffer * raw = device->device->newBuffer(static_cast<NS::UInteger>(desc.size), metal_buffer_storage(desc.memory));
		if (raw == nullptr)
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal buffer allocation failed");
		}
		set_metal_label(raw, desc.debugName);

		NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(raw);

		device->note_allocation(MetalDevice::Residency::eBuffers, buffer.get());

		const BufferHandle handle = device->buffers.store(MetalBufferSlot{ .buffer = std::move(buffer), .desc = detail::recorded(desc) });
		if (!handle.is_valid())
		{
			return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal buffer handle tracking failed");
		}

		return return_value(handle, error);
	}

	TextureHandle metal_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept
	{
		if (!metal_refuse_unexportable(
				desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedTexture,
				"Metal exports a texture only through MTLSharedTextureHandle, and this asked for another handle type",
				error
			))
		{
			return TextureHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal.createTexture");

		auto * device = static_cast<MetalDevice *>(impl);

		if (desc.allowSparseBinding)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal cannot bind sparse memory to a texture");
		}

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = build_texture_descriptor(desc, error);
		if (descriptor.get() == nullptr)
		{
			return {};
		}

		const bool shared  = !desc.exportableHandleTypes.empty();
		MTL::Texture * raw = shared ? device->device->newSharedTexture(descriptor.get()) : device->device->newTexture(descriptor.get());
		if (raw == nullptr)
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal texture allocation failed");
		}
		set_metal_label(raw, desc.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		device->note_allocation(MetalDevice::Residency::eTextures, texture.get());

		const TextureHandle handle = device->textures.store(
			MetalTextureSlot{
				.texture	   = std::move(texture),
				.format		   = desc.format,
				.usage		   = desc.usage,
				.mutableFormat = desc.allowFormatViews,
				.shared		   = shared,
				.desc		   = detail::recorded(desc),
			}
		);
		if (!handle.is_valid())
		{
			return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal texture handle tracking failed");
		}

		return return_value(handle, error);
	}

	TextureViewHandle metal_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createTextureView");

		auto * device = static_cast<MetalDevice *>(impl);

		MTL::Texture * source = nullptr;
		bool mutableFormat	  = false;
		Flags<TextureUsage> texUsage;
		{
			const auto * tracked = device->textures.resolve(texture, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of a texture this device never created");
			}
			source		  = tracked->texture.get();
			mutableFormat = tracked->mutableFormat;
			texUsage	  = tracked->usage;
		}

		if (!desc.swizzle.is_identity() && usage_forbids_swizzle(resolve_view_usage(desc.usage, texUsage)))
		{
			return fail_value<TextureViewHandle>(
				error,
				ErrorCode::eInvalidArgument,
				"a swizzled texture view must be sampled only, so narrow TextureViewDesc::usage to eSampled"
			);
		}

		const MTL::PixelFormat viewFormat = (desc.format == Format::eUndefined) ? source->pixelFormat() : metal_pixel_format(desc.format);
		if (viewFormat == MTL::PixelFormatInvalid)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eUnsupportedFormat, "texture view format is not supported by Metal");
		}

		if (viewFormat != source->pixelFormat() && !mutableFormat)
		{
			return fail_value<TextureViewHandle>(
				error,
				ErrorCode::eInvalidArgument,
				"texture view names a format the source texture was not created with allowFormatViews for"
			);
		}

		if (!view_range_fits_texture(source, desc.range, error))
		{
			return TextureViewHandle{};
		}

		const NS::Range levels = NS::Range::Make(desc.range.baseMip, desc.range.mipCount);
		const NS::Range slices = NS::Range::Make(desc.range.baseLayer, desc.range.layerCount);

		MTL::Texture * raw = desc.swizzle.is_identity()
								 ? source->newTextureView(viewFormat, metal_view_type(desc.type), levels, slices)
								 : source->newTextureView(viewFormat, metal_view_type(desc.type), levels, slices, metal_swizzle_channels(desc.swizzle));
		if (raw == nullptr)
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eNativeApiError, "Metal texture view creation failed");
		}
		set_metal_label(raw, desc.debugName);
		NS::SharedPtr<MTL::Texture> view = NS::TransferPtr(raw);

		const TextureViewHandle handle = device->textureViews.store(MetalTextureViewSlot{ .texture = std::move(view) });
		if (!handle.is_valid())
		{
			return fail_value<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Metal texture view handle tracking failed");
		}

		return return_value(handle, error);
	}

	SamplerHandle metal_create_sampler(void * impl, const SamplerDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal.createSampler");

		if (desc.ycbcrConversion != nullptr)
		{
			return fail_value<SamplerHandle>(
				error,
				ErrorCode::eUnsupportedFeature,
				"Metal has no sampler Y'CbCr conversion, so convert in the shader over per-plane textures"
			);
		}

		auto * device = static_cast<MetalDevice *>(impl);

		const NS::SharedPtr<MTL::SamplerDescriptor> descriptor = build_sampler_descriptor(desc);

		MTL::SamplerState * raw = device->device->newSamplerState(descriptor.get());
		if (raw == nullptr)
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eNativeApiError, "Metal sampler creation failed");
		}
		NS::SharedPtr<MTL::SamplerState> sampler = NS::TransferPtr(raw);

		const SamplerHandle handle = device->samplers.store(std::move(sampler));
		if (!handle.is_valid())
		{
			return fail_value<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Metal sampler handle tracking failed");
		}

		return return_value(handle, error);
	}

	bool metal_get_texture_memory_info(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "texture memory info output pointer is null");
		}

		*out		  = {};
		auto * device = static_cast<MetalDevice *>(impl);

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = build_texture_descriptor(desc, error);
		if (descriptor.get() == nullptr)
		{
			return false;
		}

		const MTL::SizeAndAlign sizeAndAlign = device->device->heapTextureSizeAndAlign(descriptor.get());
		*out								 = MemoryInfo{
			.size	   = sizeAndAlign.size,
			.alignment = sizeAndAlign.align,
		};
		return succeed(error);
	}

	bool metal_get_buffer_memory_info(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return fail(error, ErrorCode::eInvalidArgument, "buffer memory info output pointer is null");
		}

		if (desc.size == 0)
		{
			return fail(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
		}

		auto * device						 = static_cast<MetalDevice *>(impl);
		const MTL::SizeAndAlign sizeAndAlign = device->device->heapBufferSizeAndAlign(static_cast<NS::UInteger>(desc.size), metal_buffer_storage(desc.memory));
		*out								 = MemoryInfo{
			.size	   = sizeAndAlign.size,
			.alignment = sizeAndAlign.align,
		};
		return succeed(error);
	}

}

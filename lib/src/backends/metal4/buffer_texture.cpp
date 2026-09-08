// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal4/internal.hpp"

namespace azo::rhi::metal4
{
	BufferHandle Metal4CreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept
	{
		if (!Metal4RefuseUnexportable(desc.exportableHandleTypes, {}, "Metal exports no buffers, so a buffer cannot be created exportable", error))
		{
			return BufferHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createBuffer");

		if (desc.size == 0)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be non-zero");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.allowSparseBinding)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "Metal cannot bind sparse memory to a buffer");
		}

		MTL::Buffer * raw = device->device->newBuffer(static_cast<NS::UInteger>(desc.size), MetalBufferStorage(desc.memory));
		if (raw == nullptr)
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal buffer allocation failed");
		}
		SetMetalLabel(raw, desc.debugName);

		NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(raw);

		device->NoteAllocation(Metal4Device::Residency::eBuffers, buffer.get());

		const BufferHandle handle = device->buffers.Store(Metal4BufferSlot{ .buffer = std::move(buffer), .desc = detail::Recorded(desc) });
		if (!handle.IsValid())
		{
			return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Metal buffer handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TextureHandle Metal4CreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept
	{
		if (!Metal4RefuseUnexportable(desc.exportableHandleTypes,
				ExternalHandleType::eMtlSharedTexture,
				"Metal exports a texture only through MTLSharedTextureHandle, and this asked for another handle type",
				error))
		{
			return TextureHandle{};
		}

		AZO_RHI_PROFILE_ZONE("rhi.metal4.createTexture");

		auto * device = static_cast<Metal4Device *>(impl);

		if (desc.allowSparseBinding)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "Metal cannot bind sparse memory to a texture");
		}

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = BuildTextureDescriptor(desc, error);
		if (descriptor.get() == nullptr)
		{
			return {};
		}

		const bool shared  = !desc.exportableHandleTypes.Empty();
		MTL::Texture * raw = shared ? device->device->newSharedTexture(descriptor.get()) : device->device->newTexture(descriptor.get());
		if (raw == nullptr)
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "Metal texture allocation failed");
		}
		SetMetalLabel(raw, desc.debugName);
		NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(raw);

		device->NoteAllocation(Metal4Device::Residency::eTextures, texture.get());

		const TextureHandle handle = device->textures.Store(Metal4TextureSlot{ .texture = std::move(texture),
			.format																		= desc.format,
			.usage																		= desc.usage,
			.mutableFormat																= desc.allowFormatViews,
			.shared																		= shared,
			.desc																		= detail::Recorded(desc) });
		if (!handle.IsValid())
		{
			return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Metal texture handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	TextureViewHandle Metal4CreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createTextureView");

		auto * device = static_cast<Metal4Device *>(impl);

		MTL::Texture * source = nullptr;
		bool mutableFormat	  = false;
		Flags<TextureUsage> texUsage;
		{
			const auto * tracked = device->textures.Resolve(texture, kHandleAlreadyChecked);
			if (tracked == nullptr)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of a texture this device never created");
			}
			source		  = tracked->texture.get();
			mutableFormat = tracked->mutableFormat;
			texUsage	  = tracked->usage;
		}

		if (!desc.swizzle.IsIdentity() && UsageForbidsSwizzle(ResolveViewUsage(desc.usage, texUsage)))
		{
			return FailValue<TextureViewHandle>(
				error, ErrorCode::eInvalidArgument, "a swizzled texture view must be sampled only, so narrow TextureViewDesc::usage to eSampled");
		}

		const MTL::PixelFormat viewFormat = (desc.format == Format::eUndefined) ? source->pixelFormat() : MetalPixelFormat(desc.format);
		if (viewFormat == MTL::PixelFormatInvalid)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedFormat, "texture view format is not supported by Metal");
		}

		if (viewFormat != source->pixelFormat() && !mutableFormat)
		{
			return FailValue<TextureViewHandle>(
				error, ErrorCode::eInvalidArgument, "texture view names a format the source texture was not created with allowFormatViews for");
		}

		if (!ViewRangeFitsTexture(source, desc.range, error))
		{
			return TextureViewHandle{};
		}

		const NS::Range levels = NS::Range::Make(desc.range.baseMip, desc.range.mipCount);
		const NS::Range slices = NS::Range::Make(desc.range.baseLayer, desc.range.layerCount);

		MTL::Texture * raw = desc.swizzle.IsIdentity()
								 ? source->newTextureView(viewFormat, MetalViewType(desc.type), levels, slices)
								 : source->newTextureView(viewFormat, MetalViewType(desc.type), levels, slices, MetalSwizzleChannels(desc.swizzle));
		if (raw == nullptr)
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eNativeApiError, "Metal texture view creation failed");
		}
		SetMetalLabel(raw, desc.debugName);
		NS::SharedPtr<MTL::Texture> view = NS::TransferPtr(raw);

		const TextureViewHandle handle = device->textureViews.Store(Metal4TextureViewSlot{ .texture = std::move(view) });
		if (!handle.IsValid())
		{
			return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Metal texture view handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	SamplerHandle Metal4CreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept
	{
		AZO_RHI_PROFILE_ZONE("rhi.metal4.createSampler");

		if (desc.ycbcrConversion != nullptr)
		{
			return FailValue<SamplerHandle>(
				error, ErrorCode::eUnsupportedFeature, "Metal has no sampler Y'CbCr conversion, so convert in the shader over per-plane textures");
		}

		auto * device = static_cast<Metal4Device *>(impl);

		const NS::SharedPtr<MTL::SamplerDescriptor> descriptor = BuildSamplerDescriptor(desc);

		MTL::SamplerState * raw = device->device->newSamplerState(descriptor.get());
		if (raw == nullptr)
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eNativeApiError, "Metal sampler creation failed");
		}
		NS::SharedPtr<MTL::SamplerState> sampler = NS::TransferPtr(raw);

		const SamplerHandle handle = device->samplers.Store(std::move(sampler));
		if (!handle.IsValid())
		{
			return FailValue<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Metal sampler handle tracking failed");
		}

		return ReturnValue(handle, error);
	}

	bool Metal4GetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "texture memory info output pointer is null");
		}

		*out		  = {};
		auto * device = static_cast<Metal4Device *>(impl);

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = BuildTextureDescriptor(desc, error);
		if (descriptor.get() == nullptr)
		{
			return false;
		}

		const MTL::SizeAndAlign sizeAndAlign = device->device->heapTextureSizeAndAlign(descriptor.get());
		*out								 = MemoryInfo{
			.size	   = sizeAndAlign.size,
			.alignment = sizeAndAlign.align,
		};
		return Succeed(error);
	}

	bool Metal4GetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept
	{
		if (out == nullptr)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "buffer memory info output pointer is null");
		}

		if (desc.size == 0)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
		}

		auto * device						 = static_cast<Metal4Device *>(impl);
		const MTL::SizeAndAlign sizeAndAlign = device->device->heapBufferSizeAndAlign(static_cast<NS::UInteger>(desc.size), MetalBufferStorage(desc.memory));
		*out								 = MemoryInfo{
			.size	   = sizeAndAlign.size,
			.alignment = sizeAndAlign.align,
		};
		return Succeed(error);
	}

}

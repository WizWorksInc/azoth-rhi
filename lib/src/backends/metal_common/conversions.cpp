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

#include "backends/metal_common/conversions.hpp"

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"

// newLibrary takes a dispatch_data_t for a compiled library, which metal-cpp declares without including this itself.
#include <dispatch/dispatch.h>

#include <algorithm>

namespace azo::rhi::metal_common
{

	bool Succeed(Error * error) noexcept
	{
		if (error != nullptr)
		{
			*error = {};
		}
		return true;
	}

	bool Fail(Error * error, const ErrorCode code, const char * message) noexcept
	{
		if (error != nullptr)
		{
			*error = Error{
				.code	 = code,
				.message = message,
			};
		}
		return false;
	}

	void SetMetalLabel(MTL::Resource * resource, const CString debugName) noexcept
	{
		if (resource == nullptr || debugName == nullptr)
		{
			return;
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		resource->setLabel(NS::String::string(debugName, NS::UTF8StringEncoding));
	}

	MTL::PixelFormat MetalPixelFormat(const Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UNorm:		  return MTL::PixelFormatR8Unorm;
		case Format::eR8UInt:		  return MTL::PixelFormatR8Uint;
		case Format::eR8SInt:		  return MTL::PixelFormatR8Sint;
		case Format::eRG8UNorm:		  return MTL::PixelFormatRG8Unorm;
		case Format::eRGBA8UNorm:	  return MTL::PixelFormatRGBA8Unorm;
		case Format::eRGBA8Srgb:	  return MTL::PixelFormatRGBA8Unorm_sRGB;
		case Format::eBGRA8UNorm:	  return MTL::PixelFormatBGRA8Unorm;
		case Format::eBGRA8Srgb:	  return MTL::PixelFormatBGRA8Unorm_sRGB;
		case Format::eR16UInt:		  return MTL::PixelFormatR16Uint;
		case Format::eR16SInt:		  return MTL::PixelFormatR16Sint;
		case Format::eR16Float:		  return MTL::PixelFormatR16Float;
		case Format::eRG16Float:	  return MTL::PixelFormatRG16Float;
		case Format::eRGBA16Float:	  return MTL::PixelFormatRGBA16Float;
		case Format::eR11G11B10Float: return MTL::PixelFormatRG11B10Float;
		case Format::eRGB10A2UNorm:	  return MTL::PixelFormatRGB10A2Unorm;
		case Format::eRGB9E5Float:	  return MTL::PixelFormatRGB9E5Float;
		case Format::eR32UInt:		  return MTL::PixelFormatR32Uint;
		case Format::eR32SInt:		  return MTL::PixelFormatR32Sint;
		case Format::eR32Float:		  return MTL::PixelFormatR32Float;
		case Format::eRG32Float:	  return MTL::PixelFormatRG32Float;
		case Format::eRGBA32Float:	  return MTL::PixelFormatRGBA32Float;
		case Format::eD16UNorm:		  return MTL::PixelFormatDepth16Unorm;
		case Format::eD24UNormS8UInt: return MTL::PixelFormatDepth24Unorm_Stencil8;
		case Format::eD32Float:		  return MTL::PixelFormatDepth32Float;
		case Format::eD32FloatS8UInt: return MTL::PixelFormatDepth32Float_Stencil8;
		case Format::eBC1RGBAUNorm:	  return MTL::PixelFormatBC1_RGBA;
		case Format::eBC1RGBASrgb:	  return MTL::PixelFormatBC1_RGBA_sRGB;
		case Format::eBC3UNorm:		  return MTL::PixelFormatBC3_RGBA;
		case Format::eBC3Srgb:		  return MTL::PixelFormatBC3_RGBA_sRGB;
		case Format::eBC5UNorm:		  return MTL::PixelFormatBC5_RGUnorm;
		case Format::eBC5SNorm:		  return MTL::PixelFormatBC5_RGSnorm;
		case Format::eBC7UNorm:		  return MTL::PixelFormatBC7_RGBAUnorm;
		case Format::eBC7Srgb:		  return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
		case Format::eBC6HUFloat:	  return MTL::PixelFormatBC6H_RGBUfloat;
		case Format::eBC6HSFloat:	  return MTL::PixelFormatBC6H_RGBFloat;
		case Format::eUndefined:
		case Format::eRGB32Float:
		// Metal has no pure 24-bit depth format so eX8D24UNorm reports invalid here.
		case Format::eX8D24UNorm:
		// MTLPixelFormat has no multi-planar member. These reach Metal only on a texture wrapping an IOSurface, never from a descriptor, which is what
		// supportsMultiPlanarFormats reports false for.
		case Format::eG8B8R8Biplanar420UNorm:
		case Format::eG8B8R8Triplanar420UNorm:
		case Format::eG10B10R10Biplanar420UNorm: return MTL::PixelFormatInvalid;
		}

		return MTL::PixelFormatInvalid;
	}

	MTL::VertexFormat MetalVertexFormat(const Format format) noexcept
	{
		switch (format)
		{
		case Format::eR32Float:	   return MTL::VertexFormatFloat;
		case Format::eRG32Float:   return MTL::VertexFormatFloat2;
		case Format::eRGB32Float:  return MTL::VertexFormatFloat3;
		case Format::eRGBA32Float: return MTL::VertexFormatFloat4;
		case Format::eRG16Float:   return MTL::VertexFormatHalf2;
		case Format::eRGBA16Float: return MTL::VertexFormatHalf4;
		case Format::eRGBA8UNorm:  return MTL::VertexFormatUChar4Normalized;
		case Format::eRG8UNorm:	   return MTL::VertexFormatUChar2Normalized;
		case Format::eR32UInt:	   return MTL::VertexFormatUInt;
		default:				   return MTL::VertexFormatInvalid;
		}
	}

	/*
	 * Maps a portability memory hint to a Metal storage mode. Every CPU-visible hint takes shared storage, which is coherent on all Metal devices. Managed storage
	 * would be faster on a discrete GPU, where shared reaches the device over PCIe, but it publishes a CPU write only on a didModifyRange and both backends answer
	 * flushMappedRange and invalidateMappedRange with no-ops. Selecting it without adding those first would leave writes the GPU never sees.
	 */
	MTL::ResourceOptions MetalBufferStorage(const MemoryUsage usage) noexcept
	{
		switch (usage)
		{
		case MemoryUsage::eGpuOnly:
		case MemoryUsage::eTransient:
		case MemoryUsage::eReserved:	return MTL::ResourceStorageModePrivate;
		case MemoryUsage::eCpuUpload:
		case MemoryUsage::eCpuReadback:
		case MemoryUsage::eCpuToGpu:
		case MemoryUsage::eGpuToCpu:	return MTL::ResourceStorageModeShared;
		}
		return MTL::ResourceStorageModeShared;
	}

	MTL::StorageMode MetalHeapStorage(const HeapType type) noexcept
	{
		switch (type)
		{
		case HeapType::eGpuLocal:
		case HeapType::eTransient:	 return MTL::StorageModePrivate;
		case HeapType::eCpuUpload:
		case HeapType::eCpuReadback: return MTL::StorageModeShared;
		}
		return MTL::StorageModePrivate;
	}

	MTL::ResourceOptions MetalResourceOptions(const MTL::StorageMode mode) noexcept
	{
		switch (mode)
		{
		case MTL::StorageModeShared:	 return MTL::ResourceStorageModeShared;
		case MTL::StorageModeManaged:	 return MTL::ResourceStorageModeManaged;
		case MTL::StorageModePrivate:	 return MTL::ResourceStorageModePrivate;
		case MTL::StorageModeMemoryless: return MTL::ResourceStorageModeMemoryless;
		}
		return MTL::ResourceStorageModeShared;
	}

	MTL::TextureType MetalViewType(const TextureViewType type) noexcept
	{
		switch (type)
		{
		case TextureViewType::eTex1D:		 return MTL::TextureType1D;
		case TextureViewType::eTex1DArray:	 return MTL::TextureType1DArray;
		case TextureViewType::eTex2D:		 return MTL::TextureType2D;
		case TextureViewType::eTex2DArray:	 return MTL::TextureType2DArray;
		case TextureViewType::eTex3D:		 return MTL::TextureType3D;
		case TextureViewType::eTexCube:		 return MTL::TextureTypeCube;
		case TextureViewType::eTexCubeArray: return MTL::TextureTypeCubeArray;
		}

		return MTL::TextureType2D;
	}

	namespace
	{
		[[nodiscard]] MTL::TextureSwizzle MetalSwizzle(const ComponentSwizzle swizzle, const MTL::TextureSwizzle self) noexcept
		{
			switch (swizzle)
			{
			case ComponentSwizzle::eIdentity: return self;
			case ComponentSwizzle::eZero:	  return MTL::TextureSwizzleZero;
			case ComponentSwizzle::eOne:	  return MTL::TextureSwizzleOne;
			case ComponentSwizzle::eR:		  return MTL::TextureSwizzleRed;
			case ComponentSwizzle::eG:		  return MTL::TextureSwizzleGreen;
			case ComponentSwizzle::eB:		  return MTL::TextureSwizzleBlue;
			case ComponentSwizzle::eA:		  return MTL::TextureSwizzleAlpha;
			}

			return self;
		}
	} // namespace

	// Metal has no identity enumerant, each channel naming a concrete source, so eIdentity resolves to the channel's own position.
	MTL::TextureSwizzleChannels MetalSwizzleChannels(const ComponentMapping mapping) noexcept
	{
		return MTL::TextureSwizzleChannels{ MetalSwizzle(mapping.r, MTL::TextureSwizzleRed),
			MetalSwizzle(mapping.g, MTL::TextureSwizzleGreen),
			MetalSwizzle(mapping.b, MTL::TextureSwizzleBlue),
			MetalSwizzle(mapping.a, MTL::TextureSwizzleAlpha) };
	}

	MTL::SamplerMinMagFilter MetalMinMagFilter(const Filter filter) noexcept
	{
		return filter == Filter::eLinear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest;
	}

	MTL::SamplerMipFilter MetalMipFilter(const MipmapMode mode) noexcept
	{
		return mode == MipmapMode::eLinear ? MTL::SamplerMipFilterLinear : MTL::SamplerMipFilterNearest;
	}

	MTL::SamplerAddressMode MetalAddressMode(const AddressMode mode) noexcept
	{
		switch (mode)
		{
		case AddressMode::eRepeat:			  return MTL::SamplerAddressModeRepeat;
		case AddressMode::eMirroredRepeat:	  return MTL::SamplerAddressModeMirrorRepeat;
		case AddressMode::eClampToEdge:		  return MTL::SamplerAddressModeClampToEdge;
		case AddressMode::eClampToBorder:	  return MTL::SamplerAddressModeClampToBorderColor;
		case AddressMode::eMirrorClampToEdge: return MTL::SamplerAddressModeMirrorClampToEdge;
		}

		return MTL::SamplerAddressModeRepeat;
	}

	MTL::CompareFunction MetalCompareFunction(const CompareOp op) noexcept
	{
		switch (op)
		{
		case CompareOp::eNever:			 return MTL::CompareFunctionNever;
		case CompareOp::eLess:			 return MTL::CompareFunctionLess;
		case CompareOp::eEqual:			 return MTL::CompareFunctionEqual;
		case CompareOp::eLessOrEqual:	 return MTL::CompareFunctionLessEqual;
		case CompareOp::eGreater:		 return MTL::CompareFunctionGreater;
		case CompareOp::eNotEqual:		 return MTL::CompareFunctionNotEqual;
		case CompareOp::eGreaterOrEqual: return MTL::CompareFunctionGreaterEqual;
		case CompareOp::eAlways:		 return MTL::CompareFunctionAlways;
		}

		return MTL::CompareFunctionAlways;
	}

	MTL::SamplerBorderColor MetalBorderColor(const BorderColor color) noexcept
	{
		switch (color)
		{
		case BorderColor::eTransparentBlackFloat:
		case BorderColor::eTransparentBlackInt:	  return MTL::SamplerBorderColorTransparentBlack;
		case BorderColor::eOpaqueBlackFloat:
		case BorderColor::eOpaqueBlackInt:		  return MTL::SamplerBorderColorOpaqueBlack;
		case BorderColor::eOpaqueWhiteFloat:
		case BorderColor::eOpaqueWhiteInt:		  return MTL::SamplerBorderColorOpaqueWhite;
		}
		return MTL::SamplerBorderColorOpaqueBlack;
	}

	NS::SharedPtr<MTL::TextureDescriptor> BuildTextureDescriptor(const TextureDesc & desc, Error * error) noexcept
	{
		const MTL::PixelFormat pixelFormat = MetalPixelFormat(desc.format);
		if (pixelFormat == MTL::PixelFormatInvalid)
		{
			Fail(error, ErrorCode::eUnsupportedFormat, "texture format is not supported by Metal");
			return {};
		}

		/*
		 * Checked here rather than left to Metal, which does not refuse a zero extent, it asserts inside validateWithDevice and takes the process with it. An
		 * abort is worse than the throw the API already rules out, since a caller cannot catch it either. Vulkan refuses the same descriptor by hand.
		 */
		if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
		{
			Fail(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			return {};
		}

		const std::uint32_t volumeDepth = desc.type == TextureType::eTex3D ? desc.depth : 1;
		if (desc.mipLevels > detail::MaxMipLevels(desc.width, desc.height, volumeDepth))
		{
			Fail(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			return {};
		}

		NS::SharedPtr<MTL::TextureDescriptor> descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
		descriptor->setPixelFormat(pixelFormat);
		descriptor->setWidth(desc.width);
		descriptor->setHeight(desc.height);
		descriptor->setDepth(desc.type == TextureType::eTex3D ? desc.depth : 1);
		descriptor->setMipmapLevelCount(desc.mipLevels);
		descriptor->setSampleCount(static_cast<NS::UInteger>(desc.samples));

		const bool array = desc.arrayLayers > 1;
		switch (desc.type)
		{
		case TextureType::eTex1D:
			descriptor->setTextureType(array ? MTL::TextureType1DArray : MTL::TextureType1D);
			descriptor->setArrayLength(desc.arrayLayers);
			break;
		case TextureType::eTex2D:
			if (desc.samples != SampleCount::e1)
			{
				descriptor->setTextureType(array ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DMultisample);
			}
			else
			{
				descriptor->setTextureType(array ? MTL::TextureType2DArray : MTL::TextureType2D);
			}
			descriptor->setArrayLength(desc.arrayLayers);
			break;
		case TextureType::eTex3D:
			descriptor->setTextureType(MTL::TextureType3D);
			descriptor->setArrayLength(1);
			break;
		case TextureType::eTexCube:
			descriptor->setTextureType(desc.arrayLayers > 6 ? MTL::TextureTypeCubeArray : MTL::TextureTypeCube);
			descriptor->setArrayLength(desc.arrayLayers > 6 ? desc.arrayLayers / 6 : 1);
			break;
		}

		MTL::TextureUsage usage = MTL::TextureUsageUnknown;
		if (desc.usage.Contains(TextureUsage::eSampled))
		{
			usage |= MTL::TextureUsageShaderRead;
		}
		if (desc.usage.Contains(TextureUsage::eStorage))
		{
			usage |= MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite;
		}
		if (desc.usage.Contains(TextureUsage::eColorAttachment) || desc.usage.Contains(TextureUsage::eDepthStencilAttachment) ||
			desc.usage.Contains(TextureUsage::eTransientAttachment) || desc.usage.Contains(TextureUsage::ePresent))
		{
			usage |= MTL::TextureUsageRenderTarget;
		}
		if (desc.allowFormatViews)
		{
			usage |= MTL::TextureUsagePixelFormatView;
		}
		descriptor->setUsage(usage);

		if (desc.usage.Contains(TextureUsage::eTransientAttachment))
		{
			descriptor->setStorageMode(MTL::StorageModeMemoryless);
		}
		else if (desc.memory == MemoryUsage::eGpuOnly || desc.memory == MemoryUsage::eReserved || desc.memory == MemoryUsage::eTransient)
		{
			descriptor->setStorageMode(MTL::StorageModePrivate);
		}
		else
		{
			descriptor->setStorageMode(MTL::StorageModeShared);
		}

		return descriptor;
	}

	NS::SharedPtr<MTL::SamplerDescriptor> BuildSamplerDescriptor(const SamplerDesc & desc) noexcept
	{
		NS::SharedPtr<MTL::SamplerDescriptor> descriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

		// Without this a sampler has no resource id, so it cannot be named from an argument buffer, which is what a descriptor set is on a device that has them. Set
		// unconditionally because the cost is a sampler that can also be used the discrete way and nothing else.
		descriptor->setSupportArgumentBuffers(true);
		descriptor->setMinFilter(MetalMinMagFilter(desc.minFilter));
		descriptor->setMagFilter(MetalMinMagFilter(desc.magFilter));
		descriptor->setMipFilter(MetalMipFilter(desc.mipmapMode));
		descriptor->setSAddressMode(MetalAddressMode(desc.addressU));
		descriptor->setTAddressMode(MetalAddressMode(desc.addressV));
		descriptor->setRAddressMode(MetalAddressMode(desc.addressW));
		descriptor->setLodMinClamp(desc.minLod);
		descriptor->setLodMaxClamp(desc.maxLod);
		descriptor->setMaxAnisotropy(desc.anisotropyEnable ? static_cast<NS::UInteger>(std::max(1.0f, desc.maxAnisotropy)) : 1);
		if (desc.compareEnable)
		{
			descriptor->setCompareFunction(MetalCompareFunction(desc.compareOp));
		}
		descriptor->setNormalizedCoordinates(true);
		if (desc.addressU == AddressMode::eClampToBorder || desc.addressV == AddressMode::eClampToBorder || desc.addressW == AddressMode::eClampToBorder)
		{
			descriptor->setBorderColor(MetalBorderColor(desc.borderColor));
		}

		return descriptor;
	}

	MTL::PrimitiveType MetalPrimitiveType(const PrimitiveTopology topology) noexcept
	{
		switch (topology)
		{
		case PrimitiveTopology::ePointList:		return MTL::PrimitiveTypePoint;
		case PrimitiveTopology::eLineList:		return MTL::PrimitiveTypeLine;
		case PrimitiveTopology::eLineStrip:		return MTL::PrimitiveTypeLineStrip;
		case PrimitiveTopology::eTriangleList:	return MTL::PrimitiveTypeTriangle;
		case PrimitiveTopology::eTriangleStrip: return MTL::PrimitiveTypeTriangleStrip;
		// No MTLPrimitiveType for patches. Metal tessellates through a compute pre-pass instead, which neither backend builds, so creation refuses before
		// reaching here and this only has to return something.
		case PrimitiveTopology::ePatchList: return MTL::PrimitiveTypeTriangle;
		}
		return MTL::PrimitiveTypeTriangle;
	}

	MTL::CullMode MetalCullMode(const CullMode mode) noexcept
	{
		switch (mode)
		{
		case CullMode::eNone:  return MTL::CullModeNone;
		case CullMode::eFront: return MTL::CullModeFront;
		case CullMode::eBack:  return MTL::CullModeBack;
		}
		return MTL::CullModeBack;
	}

	MTL::Winding MetalWinding(const FrontFace face) noexcept
	{
		return face == FrontFace::eClockwise ? MTL::WindingClockwise : MTL::WindingCounterClockwise;
	}

	MTL::TriangleFillMode MetalFillMode(const FillMode mode) noexcept
	{
		return mode == FillMode::eWireframe ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill;
	}

	MTL::BlendFactor MetalBlendFactor(const BlendFactor factor) noexcept
	{
		switch (factor)
		{
		case BlendFactor::eZero:				  return MTL::BlendFactorZero;
		case BlendFactor::eOne:					  return MTL::BlendFactorOne;
		case BlendFactor::eSrcColor:			  return MTL::BlendFactorSourceColor;
		case BlendFactor::eOneMinusSrcColor:	  return MTL::BlendFactorOneMinusSourceColor;
		case BlendFactor::eDstColor:			  return MTL::BlendFactorDestinationColor;
		case BlendFactor::eOneMinusDstColor:	  return MTL::BlendFactorOneMinusDestinationColor;
		case BlendFactor::eSrcAlpha:			  return MTL::BlendFactorSourceAlpha;
		case BlendFactor::eOneMinusSrcAlpha:	  return MTL::BlendFactorOneMinusSourceAlpha;
		case BlendFactor::eDstAlpha:			  return MTL::BlendFactorDestinationAlpha;
		case BlendFactor::eOneMinusDstAlpha:	  return MTL::BlendFactorOneMinusDestinationAlpha;
		case BlendFactor::eConstantColor:		  return MTL::BlendFactorBlendColor;
		case BlendFactor::eOneMinusConstantColor: return MTL::BlendFactorOneMinusBlendColor;
		case BlendFactor::eConstantAlpha:		  return MTL::BlendFactorBlendAlpha;
		case BlendFactor::eOneMinusConstantAlpha: return MTL::BlendFactorOneMinusBlendAlpha;
		}
		return MTL::BlendFactorOne;
	}

	MTL::BlendOperation MetalBlendOp(const BlendOp op) noexcept
	{
		switch (op)
		{
		case BlendOp::eAdd:				return MTL::BlendOperationAdd;
		case BlendOp::eSubtract:		return MTL::BlendOperationSubtract;
		case BlendOp::eReverseSubtract: return MTL::BlendOperationReverseSubtract;
		case BlendOp::eMin:				return MTL::BlendOperationMin;
		case BlendOp::eMax:				return MTL::BlendOperationMax;
		}
		return MTL::BlendOperationAdd;
	}

	MTL::ColorWriteMask MetalColorWriteMask(const Flags<ColorWrite> mask) noexcept
	{
		MTL::ColorWriteMask out = MTL::ColorWriteMaskNone;
		if (mask.Contains(ColorWrite::eR))
		{
			out |= MTL::ColorWriteMaskRed;
		}
		if (mask.Contains(ColorWrite::eG))
		{
			out |= MTL::ColorWriteMaskGreen;
		}
		if (mask.Contains(ColorWrite::eB))
		{
			out |= MTL::ColorWriteMaskBlue;
		}
		if (mask.Contains(ColorWrite::eA))
		{
			out |= MTL::ColorWriteMaskAlpha;
		}
		return out;
	}

	MTL::LoadAction MetalLoadAction(const LoadOp op) noexcept
	{
		switch (op)
		{
		case LoadOp::eLoad:		return MTL::LoadActionLoad;
		case LoadOp::eClear:	return MTL::LoadActionClear;
		case LoadOp::eDontCare: return MTL::LoadActionDontCare;
		}
		return MTL::LoadActionLoad;
	}

	MTL::StoreAction MetalStoreAction(const StoreOp op) noexcept
	{
		return op == StoreOp::eStore ? MTL::StoreActionStore : MTL::StoreActionDontCare;
	}

	MTL::FunctionType MetalFunctionType(const ShaderStage stage) noexcept
	{
		switch (stage)
		{
		case ShaderStage::eVertex:	 return MTL::FunctionTypeVertex;
		case ShaderStage::eFragment: return MTL::FunctionTypeFragment;
		case ShaderStage::eCompute:	 return MTL::FunctionTypeKernel;
		default:					 return MTL::FunctionTypeVertex;
		}
	}

	MTL::IndexType MetalIndexType(const bool index32) noexcept
	{
		return index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
	}

	namespace
	{
		/*
		 * Loads a compiled Metal library, which is what eBackendNative means here.
		 *
		 * dispatch_data_t is what newLibrary takes and it does not copy, so the block is created over the caller's bytes with a destructor that does nothing.
		 * ShaderBinary borrows its data for the duration of creation, which outlives this call, and the library owns its own copy afterwards.
		 */
		[[nodiscard]] NS::SharedPtr<MTL::Library> LoadLibrary(MTL::Device * device, const ShaderBinary & shader, Error * error)
		{
			dispatch_data_t blob = dispatch_data_create(shader.data, shader.size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
			if (blob == nullptr)
			{
				Fail(error, ErrorCode::eOutOfHostMemory, "could not wrap the shader bytes for Metal");
				return {};
			}

			NS::Error * loadError	  = nullptr;
			MTL::Library * rawLibrary = device->newLibrary(blob, &loadError);
			dispatch_release(blob);

			if (rawLibrary == nullptr)
			{
				Fail(error, ErrorCode::eUnsupportedFormat, "the shader bytes are not a Metal library; eBackendNative is a compiled metallib here");
				return {};
			}

			return NS::TransferPtr(rawLibrary);
		}

		/*
		 * Compiles Metal Shading Language source, which is what isSource means and which a caller has to ask for.
		 *
		 * No cache: this runs once per stage per pipeline. That is the cost the format's documentation warns about and the reason it is not the default.
		 */
		[[nodiscard]] NS::SharedPtr<MTL::Library> CompileSource(MTL::Device * device, const ShaderBinary & shader, Error * error)
		{
			const detail::HostString sourceText(static_cast<const char *>(shader.data), shader.size);
			NS::SharedPtr<NS::String> source = NS::TransferPtr(NS::String::alloc()->init(sourceText.c_str(), NS::UTF8StringEncoding));

			NS::Error * compileError  = nullptr;
			MTL::Library * rawLibrary = device->newLibrary(source.get(), nullptr, &compileError);
			if (rawLibrary == nullptr)
			{
				Fail(error, ErrorCode::eNativeApiError, "Metal shader compilation failed");
				return {};
			}

			return NS::TransferPtr(rawLibrary);
		}
	} // namespace

	NS::SharedPtr<MTL::Library> MetalCompileLibrary(MTL::Device * device, const ShaderBinary & shader, Error * error)
	{
		if (shader.data == nullptr || shader.size == 0)
		{
			Fail(error, ErrorCode::eInvalidArgument, "shader binary has no bytes");
			return {};
		}

		if (shader.entryPoint == nullptr || *shader.entryPoint == '\0')
		{
			Fail(error, ErrorCode::eInvalidArgument, "Metal selects a shader function by name, so entryPoint cannot be empty");
			return {};
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		switch (shader.format)
		{
		case ShaderBinaryFormat::eBackendNative: return shader.isSource ? CompileSource(device, shader, error) : LoadLibrary(device, shader, error);
		default:
			Fail(error, ErrorCode::eUnsupportedFormat, "the Metal backend takes eBackendNative, which is a compiled metallib or, with isSource, MSL source");
			return {};
		}
	}

	NS::SharedPtr<MTL::Function> CompileFunction(MTL::Device * device, const ShaderBinary & shader, Error * error)
	{
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Library> library = MetalCompileLibrary(device, shader, error);
		if (library.get() == nullptr)
		{
			return {};
		}

		NS::SharedPtr<NS::String> name = NS::TransferPtr(NS::String::alloc()->init(shader.entryPoint, NS::UTF8StringEncoding));

		NS::SharedPtr<MTL::Function> function = NS::TransferPtr(library->newFunction(name.get()));
		if (function.get() == nullptr)
		{
			Fail(error, ErrorCode::eInvalidArgument, "the shader library has no function with the name entryPoint gave");
			return {};
		}

		// Naming a function of the wrong stage would bind it where the pipeline expects another, so it is refused, not trusted.
		if (function->functionType() != MetalFunctionType(shader.stage))
		{
			Fail(error, ErrorCode::eInvalidArgument, "the named Metal function is not of the stage the shader binary declared");
			return {};
		}

		Succeed(error);
		return function;
	}

	NS::SharedPtr<MTL::DepthStencilState> BuildDepthStencilState(MTL::Device * device, const DepthStencilStateDesc & desc)
	{
		if (!desc.depthTestEnable && !desc.stencilTestEnable)
		{
			return {};
		}

		NS::SharedPtr<MTL::DepthStencilDescriptor> descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
		descriptor->setDepthCompareFunction(desc.depthTestEnable ? MetalCompareFunction(desc.depthCompareOp) : MTL::CompareFunctionAlways);
		descriptor->setDepthWriteEnabled(desc.depthWriteEnable);
		return NS::TransferPtr(device->newDepthStencilState(descriptor.get()));
	}

} // namespace azo::rhi::metal_common

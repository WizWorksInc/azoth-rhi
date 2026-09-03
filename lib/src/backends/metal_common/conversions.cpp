// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal_common/conversions.hpp"

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"

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
		case Format::eR8UNorm:					 return MTL::PixelFormatR8Unorm;
		case Format::eR8UInt:					 return MTL::PixelFormatR8Uint;
		case Format::eR8SInt:					 return MTL::PixelFormatR8Sint;
		case Format::eRG8UNorm:					 return MTL::PixelFormatRG8Unorm;
		case Format::eRGBA8UNorm:				 return MTL::PixelFormatRGBA8Unorm;
		case Format::eRGBA8Srgb:				 return MTL::PixelFormatRGBA8Unorm_sRGB;
		case Format::eBGRA8UNorm:				 return MTL::PixelFormatBGRA8Unorm;
		case Format::eBGRA8Srgb:				 return MTL::PixelFormatBGRA8Unorm_sRGB;
		case Format::eR16UInt:					 return MTL::PixelFormatR16Uint;
		case Format::eR16SInt:					 return MTL::PixelFormatR16Sint;
		case Format::eR16Float:					 return MTL::PixelFormatR16Float;
		case Format::eRG16Float:				 return MTL::PixelFormatRG16Float;
		case Format::eRGBA16Float:				 return MTL::PixelFormatRGBA16Float;
		case Format::eR11G11B10Float:			 return MTL::PixelFormatRG11B10Float;
		case Format::eRGB10A2UNorm:				 return MTL::PixelFormatRGB10A2Unorm;
		case Format::eRGB9E5Float:				 return MTL::PixelFormatRGB9E5Float;
		case Format::eR32UInt:					 return MTL::PixelFormatR32Uint;
		case Format::eR32SInt:					 return MTL::PixelFormatR32Sint;
		case Format::eR32Float:					 return MTL::PixelFormatR32Float;
		case Format::eRG32Float:				 return MTL::PixelFormatRG32Float;
		case Format::eRGBA32Float:				 return MTL::PixelFormatRGBA32Float;
		case Format::eD16UNorm:					 return MTL::PixelFormatDepth16Unorm;
		case Format::eD24UNormS8UInt:			 return MTL::PixelFormatDepth24Unorm_Stencil8;
		case Format::eD32Float:					 return MTL::PixelFormatDepth32Float;
		case Format::eD32FloatS8UInt:			 return MTL::PixelFormatDepth32Float_Stencil8;
		case Format::eBC1RGBAUNorm:				 return MTL::PixelFormatBC1_RGBA;
		case Format::eBC1RGBASrgb:				 return MTL::PixelFormatBC1_RGBA_sRGB;
		case Format::eBC3UNorm:					 return MTL::PixelFormatBC3_RGBA;
		case Format::eBC3Srgb:					 return MTL::PixelFormatBC3_RGBA_sRGB;
		case Format::eBC5UNorm:					 return MTL::PixelFormatBC5_RGUnorm;
		case Format::eBC5SNorm:					 return MTL::PixelFormatBC5_RGSnorm;
		case Format::eBC7UNorm:					 return MTL::PixelFormatBC7_RGBAUnorm;
		case Format::eBC7Srgb:					 return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
		case Format::eBC6HUFloat:				 return MTL::PixelFormatBC6H_RGBUfloat;
		case Format::eBC6HSFloat:				 return MTL::PixelFormatBC6H_RGBFloat;
		case Format::eUndefined:
		case Format::eRGB32Float:
		case Format::eX8D24UNorm:
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
		case Format::eR32Float:		return MTL::VertexFormatFloat;
		case Format::eRG32Float:	return MTL::VertexFormatFloat2;
		case Format::eRGB32Float:	return MTL::VertexFormatFloat3;
		case Format::eRGBA32Float:	return MTL::VertexFormatFloat4;
		case Format::eRG16Float:	return MTL::VertexFormatHalf2;
		case Format::eRGBA16Float:	return MTL::VertexFormatHalf4;
		case Format::eRGBA8UNorm:	return MTL::VertexFormatUChar4Normalized;
		case Format::eRG8UNorm:		return MTL::VertexFormatUChar2Normalized;
		case Format::eR32UInt:		return MTL::VertexFormatUInt;
		case Format::eR32SInt:		return MTL::VertexFormatInt;
		case Format::eR16Float:		return MTL::VertexFormatHalf;
		case Format::eR16UInt:		return MTL::VertexFormatUShort;
		case Format::eR16SInt:		return MTL::VertexFormatShort;
		case Format::eR8UNorm:		return MTL::VertexFormatUCharNormalized;
		case Format::eR8UInt:		return MTL::VertexFormatUChar;
		case Format::eR8SInt:		return MTL::VertexFormatChar;
		case Format::eBGRA8UNorm:	return MTL::VertexFormatUChar4Normalized_BGRA;
		case Format::eRGB10A2UNorm: return MTL::VertexFormatUInt1010102Normalized;
		default:					return MTL::VertexFormatInvalid;
		}
	}

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
	}

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

	bool ViewRangeFitsTexture(const MTL::Texture * texture, const TextureSubresourceRange & range, Error * error) noexcept
	{
		if (range.mipCount == kAllMips || range.layerCount == kAllLayers)
		{
			return Fail(error,
				ErrorCode::eInvalidArgument,
				"kAllMips and kAllLayers are barrier counts, so a texture view has to name how many levels and layers it takes");
		}

		const auto mips				= static_cast<std::uint32_t>(texture->mipmapLevelCount());
		const MTL::TextureType type = texture->textureType();
		const bool cube				= type == MTL::TextureTypeCube || type == MTL::TextureTypeCubeArray;
		const auto slices			= static_cast<std::uint32_t>(texture->arrayLength()) * (cube ? 6u : 1u);

		if (range.baseMip >= mips || range.mipCount > mips - range.baseMip)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "texture view mip range is outside the source texture");
		}

		if (range.baseLayer >= slices || range.layerCount > slices - range.baseLayer)
		{
			return Fail(error, ErrorCode::eInvalidArgument, "texture view layer range is outside the source texture");
		}

		return true;
	}

	NS::SharedPtr<MTL::SamplerDescriptor> BuildSamplerDescriptor(const SamplerDesc & desc) noexcept
	{
		NS::SharedPtr<MTL::SamplerDescriptor> descriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

		descriptor->setSupportArgumentBuffers(true);
		descriptor->setMinFilter(MetalMinMagFilter(desc.minFilter));
		descriptor->setMagFilter(MetalMinMagFilter(desc.magFilter));
		descriptor->setMipFilter(MetalMipFilter(desc.mipmapMode));
		descriptor->setSAddressMode(MetalAddressMode(desc.addressU));
		descriptor->setTAddressMode(MetalAddressMode(desc.addressV));
		descriptor->setRAddressMode(MetalAddressMode(desc.addressW));
		descriptor->setLodMinClamp(desc.minLod);
		descriptor->setLodMaxClamp(desc.maxLod);
		descriptor->setLodBias(desc.mipLodBias);
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
		case PrimitiveTopology::ePatchList:		return MTL::PrimitiveTypeTriangle;
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
	}

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

	bool MetalRefuseUnblendableAttachment(const Format format, Error * error)
	{
		if (IsBlendableFormat(format))
		{
			return true;
		}

		Fail(error, ErrorCode::eInvalidArgument, "a colour attachment enables blending on a format Metal cannot blend");
		return false;
	}

	bool MetalRefuseUnrenderableAttachment(const Format format, Error * error)
	{
		if (IsColorRenderableFormat(format))
		{
			return true;
		}

		Fail(error, ErrorCode::eInvalidArgument, "a graphics pipeline writes a colour attachment in a format Metal cannot render to");
		return false;
	}

	bool MetalRefuseUnbuildableGraphicsStage(const ShaderStage stage, Error * error)
	{
		if (stage == ShaderStage::eVertex || stage == ShaderStage::eFragment)
		{
			return true;
		}

		if (stage == ShaderStage::eTessellationControl || stage == ShaderStage::eTessellationEvaluation)
		{
			Fail(error, ErrorCode::eUnsupportedFeature, "Metal tessellates through a compute pre-pass, which this backend does not build");
			return false;
		}

		if (stage == ShaderStage::eGeometry)
		{
			Fail(error, ErrorCode::eUnsupportedFeature, "Metal has no geometry shader stage");
			return false;
		}

		Fail(error, ErrorCode::eInvalidArgument, "a graphics pipeline names a shader stage that is not part of one");
		return false;
	}

	NS::SharedPtr<MTL::Function> CompileFunction(MTL::Device * device, const ShaderBinary & shader, Error * error)
	{
		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::Library> library = MetalCompileLibrary(device, shader, error);
		if (library.get() == nullptr)
		{
			return {};
		}

		if (shader.stage != ShaderStage::eVertex && shader.stage != ShaderStage::eFragment && shader.stage != ShaderStage::eCompute)
		{
			Fail(error, ErrorCode::eUnsupportedFeature, "the shader binary declares a stage this backend has no Metal function type for");
			return {};
		}

		NS::SharedPtr<NS::String> name = NS::TransferPtr(NS::String::alloc()->init(shader.entryPoint, NS::UTF8StringEncoding));

		NS::SharedPtr<MTL::Function> function = NS::TransferPtr(library->newFunction(name.get()));
		if (function.get() == nullptr)
		{
			Fail(error, ErrorCode::eInvalidArgument, "the shader library has no function with the name entryPoint gave");
			return {};
		}

		if (function->functionType() != MetalFunctionType(shader.stage))
		{
			Fail(error, ErrorCode::eInvalidArgument, "the named Metal function is not of the stage the shader binary declared");
			return {};
		}

		Succeed(error);
		return function;
	}

	MTL::StencilOperation MetalStencilOp(const StencilOp op) noexcept
	{
		switch (op)
		{
		case StencilOp::eKeep:			 return MTL::StencilOperationKeep;
		case StencilOp::eZero:			 return MTL::StencilOperationZero;
		case StencilOp::eReplace:		 return MTL::StencilOperationReplace;
		case StencilOp::eIncrementClamp: return MTL::StencilOperationIncrementClamp;
		case StencilOp::eDecrementClamp: return MTL::StencilOperationDecrementClamp;
		case StencilOp::eInvert:		 return MTL::StencilOperationInvert;
		case StencilOp::eIncrementWrap:	 return MTL::StencilOperationIncrementWrap;
		case StencilOp::eDecrementWrap:	 return MTL::StencilOperationDecrementWrap;
		}

		return MTL::StencilOperationKeep;
	}

	namespace
	{
		NS::SharedPtr<MTL::StencilDescriptor> BuildStencilFace(const StencilFaceDesc & face)
		{
			NS::SharedPtr<MTL::StencilDescriptor> stencil = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
			stencil->setStencilCompareFunction(MetalCompareFunction(face.compareOp));
			stencil->setStencilFailureOperation(MetalStencilOp(face.failOp));
			stencil->setDepthFailureOperation(MetalStencilOp(face.depthFailOp));
			stencil->setDepthStencilPassOperation(MetalStencilOp(face.passOp));
			stencil->setReadMask(face.compareMask);
			stencil->setWriteMask(face.writeMask);
			return stencil;
		}
	} // namespace

	NS::SharedPtr<MTL::DepthStencilState> BuildDepthStencilState(MTL::Device * device, const DepthStencilStateDesc & desc)
	{
		if (!desc.depthTestEnable && !desc.stencilTestEnable)
		{
			return {};
		}

		NS::SharedPtr<MTL::DepthStencilDescriptor> descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
		descriptor->setDepthCompareFunction(desc.depthTestEnable ? MetalCompareFunction(desc.depthCompareOp) : MTL::CompareFunctionAlways);
		descriptor->setDepthWriteEnabled(desc.depthWriteEnable);

		if (desc.stencilTestEnable)
		{
			const NS::SharedPtr<MTL::StencilDescriptor> front = BuildStencilFace(desc.front);
			const NS::SharedPtr<MTL::StencilDescriptor> back  = BuildStencilFace(desc.back);
			descriptor->setFrontFaceStencil(front.get());
			descriptor->setBackFaceStencil(back.get());
		}

		return NS::TransferPtr(device->newDepthStencilState(descriptor.get()));
	}

}

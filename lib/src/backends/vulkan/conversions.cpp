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

#include "backends/vulkan/internal.hpp"

namespace azo::rhi::vulkan
{
	[[nodiscard]] vk::BufferUsageFlags MapBufferUsage(Flags<BufferUsage> usage) noexcept
	{
		vk::BufferUsageFlags out{};
		if (usage.Contains(BufferUsage::eCopySrc))
		{
			out |= vk::BufferUsageFlagBits::eTransferSrc;
		}

		if (usage.Contains(BufferUsage::eCopyDst))
		{
			out |= vk::BufferUsageFlagBits::eTransferDst;
		}

		if (usage.Contains(BufferUsage::eVertex))
		{
			out |= vk::BufferUsageFlagBits::eVertexBuffer;
		}

		if (usage.Contains(BufferUsage::eIndex))
		{
			out |= vk::BufferUsageFlagBits::eIndexBuffer;
		}

		if (usage.Contains(BufferUsage::eUniform))
		{
			out |= vk::BufferUsageFlagBits::eUniformBuffer;
		}

		if (usage.Contains(BufferUsage::eStorage))
		{
			out |= vk::BufferUsageFlagBits::eStorageBuffer;
		}

		if (usage.Contains(BufferUsage::eIndirect))
		{
			out |= vk::BufferUsageFlagBits::eIndirectBuffer;
		}
		return out;
	}

	// Maps the RHI memory placement hint onto VMA. AUTO plus the host access flags lets VMA pick the memory type from the access pattern and the buffer usage.
	// outFlags receives the allocation flags.
	[[nodiscard]] VmaMemoryUsage MapMemoryUsage(MemoryUsage memory, bool persistentMap, VmaAllocationCreateFlags & outFlags) noexcept
	{
		outFlags = 0;
		switch (memory)
		{
		case MemoryUsage::eCpuUpload:
		case MemoryUsage::eCpuToGpu:	outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; break;
		case MemoryUsage::eCpuReadback:
		case MemoryUsage::eGpuToCpu:	outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT; break;
		case MemoryUsage::eGpuOnly:
		case MemoryUsage::eTransient:
		case MemoryUsage::eReserved:	break;
		}

		if (persistentMap && outFlags != 0)
		{
			outFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		return VMA_MEMORY_USAGE_AUTO;
	}

	// Maps the RHI format onto a Vulkan format. eUndefined (the default) and any unmapped value return eUndefined, which texture creation rejects.
	[[nodiscard]] vk::Format MapFormat(Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UNorm:		  return vk::Format::eR8Unorm;
		case Format::eR8UInt:		  return vk::Format::eR8Uint;
		case Format::eR8SInt:		  return vk::Format::eR8Sint;
		case Format::eRG8UNorm:		  return vk::Format::eR8G8Unorm;
		case Format::eRGBA8UNorm:	  return vk::Format::eR8G8B8A8Unorm;
		case Format::eRGBA8Srgb:	  return vk::Format::eR8G8B8A8Srgb;
		case Format::eBGRA8UNorm:	  return vk::Format::eB8G8R8A8Unorm;
		case Format::eBGRA8Srgb:	  return vk::Format::eB8G8R8A8Srgb;
		case Format::eR16UInt:		  return vk::Format::eR16Uint;
		case Format::eR16SInt:		  return vk::Format::eR16Sint;
		case Format::eR16Float:		  return vk::Format::eR16Sfloat;
		case Format::eRG16Float:	  return vk::Format::eR16G16Sfloat;
		case Format::eRGBA16Float:	  return vk::Format::eR16G16B16A16Sfloat;
		case Format::eR11G11B10Float: return vk::Format::eB10G11R11UfloatPack32;
		case Format::eRGB10A2UNorm:	  return vk::Format::eA2B10G10R10UnormPack32;
		case Format::eRGB9E5Float:	  return vk::Format::eE5B9G9R9UfloatPack32;
		case Format::eR32UInt:		  return vk::Format::eR32Uint;
		case Format::eR32SInt:		  return vk::Format::eR32Sint;
		case Format::eR32Float:		  return vk::Format::eR32Sfloat;
		case Format::eRG32Float:	  return vk::Format::eR32G32Sfloat;
		case Format::eRGB32Float:	  return vk::Format::eR32G32B32Sfloat;
		case Format::eRGBA32Float:	  return vk::Format::eR32G32B32A32Sfloat;
		case Format::eD16UNorm:		  return vk::Format::eD16Unorm;
		case Format::eD24UNormS8UInt: return vk::Format::eD24UnormS8Uint;
		case Format::eD32Float:		  return vk::Format::eD32Sfloat;
		case Format::eD32FloatS8UInt: return vk::Format::eD32SfloatS8Uint;
		case Format::eX8D24UNorm:	  return vk::Format::eX8D24UnormPack32;
		case Format::eBC1RGBAUNorm:	  return vk::Format::eBc1RgbaUnormBlock;
		case Format::eBC1RGBASrgb:	  return vk::Format::eBc1RgbaSrgbBlock;
		case Format::eBC3UNorm:		  return vk::Format::eBc3UnormBlock;
		case Format::eBC3Srgb:		  return vk::Format::eBc3SrgbBlock;
		case Format::eBC5UNorm:		  return vk::Format::eBc5UnormBlock;
		case Format::eBC5SNorm:		  return vk::Format::eBc5SnormBlock;
		case Format::eBC7UNorm:		  return vk::Format::eBc7UnormBlock;
		case Format::eBC7Srgb:		  return vk::Format::eBc7SrgbBlock;
		case Format::eBC6HUFloat:	  return vk::Format::eBc6HUfloatBlock;
		case Format::eBC6HSFloat:	  return vk::Format::eBc6HSfloatBlock;
		// Core since 1.1 through VK_KHR_sampler_ycbcr_conversion, which the backend floor of 1.2 already has. Whether an adapter can actually create one is a
		// format-feature query and not a version question, which is what supportsMultiPlanarFormats reports.
		case Format::eG8B8R8Biplanar420UNorm:	 return vk::Format::eG8B8R82Plane420Unorm;
		case Format::eG8B8R8Triplanar420UNorm:	 return vk::Format::eG8B8R83Plane420Unorm;
		case Format::eG10B10R10Biplanar420UNorm: return vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16;
		case Format::eUndefined:				 return vk::Format::eUndefined;
		}

		return vk::Format::eUndefined;
	}

	[[nodiscard]] vk::ImageUsageFlags MapTextureUsage(Flags<TextureUsage> usage) noexcept
	{
		vk::ImageUsageFlags out{};
		if (usage.Contains(TextureUsage::eCopySrc))
		{
			out |= vk::ImageUsageFlagBits::eTransferSrc;
		}

		if (usage.Contains(TextureUsage::eCopyDst))
		{
			out |= vk::ImageUsageFlagBits::eTransferDst;
		}

		if (usage.Contains(TextureUsage::eSampled))
		{
			out |= vk::ImageUsageFlagBits::eSampled;
		}

		if (usage.Contains(TextureUsage::eStorage))
		{
			out |= vk::ImageUsageFlagBits::eStorage;
		}

		if (usage.Contains(TextureUsage::eColorAttachment))
		{
			out |= vk::ImageUsageFlagBits::eColorAttachment;
		}

		if (usage.Contains(TextureUsage::eDepthStencilAttachment))
		{
			out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
		}

		if (usage.Contains(TextureUsage::eTransientAttachment))
		{
			out |= vk::ImageUsageFlagBits::eTransientAttachment;
		}

		return out;
	}

	[[nodiscard]] vk::ImageType MapImageType(TextureType type) noexcept
	{
		switch (type)
		{
		case TextureType::eTex1D:	return vk::ImageType::e1D;
		case TextureType::eTex3D:	return vk::ImageType::e3D;
		case TextureType::eTex2D:
		case TextureType::eTexCube: return vk::ImageType::e2D;
		}

		return vk::ImageType::e2D;
	}

	[[nodiscard]] vk::ImageViewType MapViewType(TextureType type) noexcept
	{
		switch (type)
		{
		case TextureType::eTex1D:	return vk::ImageViewType::e1D;
		case TextureType::eTex3D:	return vk::ImageViewType::e3D;
		case TextureType::eTexCube: return vk::ImageViewType::eCube;
		case TextureType::eTex2D:	return vk::ImageViewType::e2D;
		}

		return vk::ImageViewType::e2D;
	}

	[[nodiscard]] vk::SampleCountFlagBits MapSampleCount(SampleCount samples) noexcept
	{
		switch (samples)
		{
		case SampleCount::e2:  return vk::SampleCountFlagBits::e2;
		case SampleCount::e4:  return vk::SampleCountFlagBits::e4;
		case SampleCount::e8:  return vk::SampleCountFlagBits::e8;
		case SampleCount::e16: return vk::SampleCountFlagBits::e16;
		case SampleCount::e1:  return vk::SampleCountFlagBits::e1;
		}

		return vk::SampleCountFlagBits::e1;
	}

	[[nodiscard]] vk::PresentModeKHR MapPresentMode(PresentMode mode) noexcept
	{
		switch (mode)
		{
		case PresentMode::eMailbox:		return vk::PresentModeKHR::eMailbox;
		case PresentMode::eImmediate:	return vk::PresentModeKHR::eImmediate;
		case PresentMode::eFifo:		return vk::PresentModeKHR::eFifo;
		case PresentMode::eFifoRelaxed: return vk::PresentModeKHR::eFifoRelaxed;
		}

		return vk::PresentModeKHR::eFifo;
	}

	// Reverse of MapPresentMode, used to report the mode the surface actually settled on. Modes the RHI does not name report eFifo, which is also where
	// SelectPresentMode lands when nothing in the requested chain was advertised.
	[[nodiscard]] PresentMode MapVkPresentMode(vk::PresentModeKHR mode) noexcept
	{
		switch (mode)
		{
		case vk::PresentModeKHR::eMailbox:	   return PresentMode::eMailbox;
		case vk::PresentModeKHR::eImmediate:   return PresentMode::eImmediate;
		case vk::PresentModeKHR::eFifoRelaxed: return PresentMode::eFifoRelaxed;
		default:							   return PresentMode::eFifo;
		}
	}

	// Reverse of MapFormat for a swapchain color format. Only the common swapchain formats are mapped. Anything else reports eUndefined.
	[[nodiscard]] Format MapVkFormat(vk::Format format) noexcept
	{
		switch (format)
		{
		case vk::Format::eB8G8R8A8Unorm:		  return Format::eBGRA8UNorm;
		case vk::Format::eB8G8R8A8Srgb:			  return Format::eBGRA8Srgb;
		case vk::Format::eR8G8B8A8Unorm:		  return Format::eRGBA8UNorm;
		case vk::Format::eR8G8B8A8Srgb:			  return Format::eRGBA8Srgb;
		case vk::Format::eA2B10G10R10UnormPack32: return Format::eRGB10A2UNorm;
		case vk::Format::eR16G16B16A16Sfloat:	  return Format::eRGBA16Float;
		case vk::Format::eB10G11R11UfloatPack32:  return Format::eR11G11B10Float;
		case vk::Format::eE5B9G9R9UfloatPack32:	  return Format::eRGB9E5Float;
		case vk::Format::eX8D24UnormPack32:		  return Format::eX8D24UNorm;
		case vk::Format::eBc6HUfloatBlock:		  return Format::eBC6HUFloat;
		case vk::Format::eBc6HSfloatBlock:		  return Format::eBC6HSFloat;
		default:								  return Format::eUndefined;
		}
	}

	namespace
	{
		[[nodiscard]] vk::ComponentSwizzle MapComponentSwizzle(ComponentSwizzle swizzle) noexcept
		{
			switch (swizzle)
			{
			case ComponentSwizzle::eIdentity: return vk::ComponentSwizzle::eIdentity;
			case ComponentSwizzle::eZero:	  return vk::ComponentSwizzle::eZero;
			case ComponentSwizzle::eOne:	  return vk::ComponentSwizzle::eOne;
			case ComponentSwizzle::eR:		  return vk::ComponentSwizzle::eR;
			case ComponentSwizzle::eG:		  return vk::ComponentSwizzle::eG;
			case ComponentSwizzle::eB:		  return vk::ComponentSwizzle::eB;
			case ComponentSwizzle::eA:		  return vk::ComponentSwizzle::eA;
			}

			return vk::ComponentSwizzle::eIdentity;
		}
	} // namespace

	vk::ComponentMapping MapComponentMapping(const ComponentMapping mapping) noexcept
	{
		return vk::ComponentMapping{
			MapComponentSwizzle(mapping.r), MapComponentSwizzle(mapping.g), MapComponentSwizzle(mapping.b), MapComponentSwizzle(mapping.a)
		};
	}

	bool QueryPortabilitySubsetFeatures(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, PortabilitySubsetFeatures & out) noexcept
	{
		// Straight through the loader and not the Vulkan-Hpp wrapper: this fills a struct the C++ bindings only declare under VK_ENABLE_BETA_EXTENSIONS, so the whole
		// query stays on the C entry points instead of splitting across the two.
		auto *const physical	   = static_cast<VkPhysicalDevice>(phys);
		std::uint32_t extCount = 0;
		if (dispatch.vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, nullptr) != VK_SUCCESS || extCount == 0)
		{
			return false;
		}

		detail::HostVector<VkExtensionProperties> exts(extCount);
		if (dispatch.vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, exts.data()) != VK_SUCCESS)
		{
			return false;
		}

		const bool portability = std::ranges::any_of(exts,
			[](const VkExtensionProperties & ep) noexcept
			{
				return std::strcmp(ep.extensionName, "VK_KHR_portability_subset") == 0;
			});
		if (!portability)
		{
			return false;
		}

		out = PortabilitySubsetFeatures{};

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &out;
		dispatch.vkGetPhysicalDeviceFeatures2(physical, &features2);

		return true;
	}

	// Component mapping on an image view is core Vulkan, so a full implementation always has it. The portability subset is what makes it optional and MoltenVK is
	// the implementation that can withhold it, reporting through imageViewFormatSwizzle.
	[[nodiscard]] bool AdapterSupportsViewSwizzle(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept
	{
		PortabilitySubsetFeatures portabilityFeatures{};
		if (!QueryPortabilitySubsetFeatures(phys, dispatch, portabilityFeatures))
		{
			return true;
		}

		return portabilityFeatures.imageViewFormatSwizzle == VK_TRUE;
	}

	namespace
	{
		[[nodiscard]] vk::SamplerYcbcrModelConversion MapYcbcrModel(YcbcrModel model) noexcept
		{
			switch (model)
			{
			case YcbcrModel::eRgbIdentity:	 return vk::SamplerYcbcrModelConversion::eRgbIdentity;
			case YcbcrModel::eYcbcrIdentity: return vk::SamplerYcbcrModelConversion::eYcbcrIdentity;
			case YcbcrModel::eYcbcr709:		 return vk::SamplerYcbcrModelConversion::eYcbcr709;
			case YcbcrModel::eYcbcr601:		 return vk::SamplerYcbcrModelConversion::eYcbcr601;
			case YcbcrModel::eYcbcr2020:	 return vk::SamplerYcbcrModelConversion::eYcbcr2020;
			}

			return vk::SamplerYcbcrModelConversion::eYcbcr601;
		}

		[[nodiscard]] vk::SamplerYcbcrRange MapYcbcrRange(YcbcrRange range) noexcept
		{
			return range == YcbcrRange::eFull ? vk::SamplerYcbcrRange::eItuFull : vk::SamplerYcbcrRange::eItuNarrow;
		}

		[[nodiscard]] vk::ChromaLocation MapChromaLocation(ChromaLocation location) noexcept
		{
			return location == ChromaLocation::eCositedEven ? vk::ChromaLocation::eCositedEven : vk::ChromaLocation::eMidpoint;
		}
	} // namespace

	vk::SamplerYcbcrConversion AcquireYcbcrConversion(VulkanDevice * device, const SamplerYcbcrConversionDesc & desc) noexcept
	{
		for (const auto & [cached, conversion] : device->ycbcrConversions)
		{
			if (cached == desc)
			{
				return conversion;
			}
		}

		vk::SamplerYcbcrConversionCreateInfo info{};
		info.format						 = MapFormat(desc.format);
		info.ycbcrModel					 = MapYcbcrModel(desc.model);
		info.ycbcrRange					 = MapYcbcrRange(desc.range);
		info.components					 = MapComponentMapping(desc.components);
		info.xChromaOffset				 = MapChromaLocation(desc.xChromaOffset);
		info.yChromaOffset				 = MapChromaLocation(desc.yChromaOffset);
		info.chromaFilter				 = MapFilter(desc.chromaFilter);
		info.forceExplicitReconstruction = VK_FALSE;

		const auto created = device->device.createSamplerYcbcrConversion(info, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			return {};
		}

		if (!detail::TryPushBack(device->ycbcrConversions, std::pair{ desc, created.value }))
		{
			device->device.destroySamplerYcbcrConversion(created.value, nullptr, device->dispatch);
			return {};
		}

		return created.value;
	}

	// Multi-planar formats are core from Vulkan 1.1 but an adapter still has to offer the sampling format features for one, so the answer comes from the format
	// and not the version. NV12 is the format to ask about: an adapter with any multi-planar support has it.
	[[nodiscard]] bool AdapterSupportsMultiPlanarFormats(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept
	{
		VkFormatProperties props{};
		dispatch.vkGetPhysicalDeviceFormatProperties(static_cast<VkPhysicalDevice>(phys), static_cast<VkFormat>(vk::Format::eG8B8R82Plane420Unorm), &props);

		if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
		{
			return false;
		}

		// A plane is only reachable through a view whose format differs from the image's, so an implementation that refuses to reinterpret a view format cannot offer
		// these however well it samples them. MoltenVK is the one that says so, through imageViewFormatReinterpretation.
		PortabilitySubsetFeatures portabilityFeatures{};
		if (!QueryPortabilitySubsetFeatures(phys, dispatch, portabilityFeatures))
		{
			return true;
		}

		return portabilityFeatures.imageViewFormatReinterpretation == VK_TRUE;
	}

	// Optional features adapter selection can require or prefer. Each maps to the same physical-device query that fills DeviceCaps so a required feature is one
	// the created device can actually report.
	[[nodiscard]] bool AdapterSupportsFeature(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, DeviceFeature feature) noexcept
	{
		const vk::PhysicalDeviceFeatures feats = phys.getFeatures(dispatch);
		switch (feature)
		{
		case DeviceFeature::eTimestampQueries:			return static_cast<bool>(phys.getProperties(dispatch).limits.timestampComputeAndGraphics);
		case DeviceFeature::eSamplerAnisotropy:			return static_cast<bool>(feats.samplerAnisotropy);
		case DeviceFeature::eIndependentBlend:			return static_cast<bool>(feats.independentBlend);
		case DeviceFeature::eDepthBounds:				return static_cast<bool>(feats.depthBounds);
		case DeviceFeature::ePipelineStatisticsQueries: return static_cast<bool>(feats.pipelineStatisticsQuery);
		case DeviceFeature::eMultiDrawIndirect:			return static_cast<bool>(feats.multiDrawIndirect);
		case DeviceFeature::eDrawIndirectFirstInstance: return static_cast<bool>(feats.drawIndirectFirstInstance);
		case DeviceFeature::eShaderDrawParameters:
		{
			// Not a base feature: shaderDrawParameters lives in the Vulkan 1.1 feature struct, which is queried through the features2 chain. Core from 1.2, the backend
			// floor.
			const auto chain = phys.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features>(dispatch);
			return static_cast<bool>(chain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters);
		}
		case DeviceFeature::eSparseResources:	 return static_cast<bool>(feats.sparseBinding);
		case DeviceFeature::eSparseBuffers:		 return static_cast<bool>(feats.sparseResidencyBuffer);
		case DeviceFeature::eSparseTextures:	 return static_cast<bool>(feats.sparseResidencyImage2D);
		case DeviceFeature::eSparseVolumes:		 return static_cast<bool>(feats.sparseResidencyImage3D);
		case DeviceFeature::eTextureViewSwizzle: return AdapterSupportsViewSwizzle(phys, dispatch);
		case DeviceFeature::eMultiPlanarFormats: return AdapterSupportsMultiPlanarFormats(phys, dispatch);
		case DeviceFeature::eSamplerYcbcrConversion:
		{
			/*
			 * Both halves, because either alone is useless: the formats have to be samplable and the 1.1 feature bit has to be there, creating a conversion with it off
			 * being a validation error. The bit is read, not assumed from the formats, since enabling one the adapter does not have fails device creation outright.
			 */
			const auto chain = phys.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features>(dispatch);
			return AdapterSupportsMultiPlanarFormats(phys, dispatch) &&
				   static_cast<bool>(chain.get<vk::PhysicalDeviceVulkan11Features>().samplerYcbcrConversion);
		}
		}

		return false;
	}

	[[nodiscard]] bool AdapterSupportsAllFeatures(
		vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, std::span<const DeviceFeature> features) noexcept
	{
		return std::ranges::all_of(features,
			[phys, &dispatch](const DeviceFeature feature) noexcept
			{
				return AdapterSupportsFeature(phys, dispatch, feature);
			});
	}

	// Enables the feature bit a negotiated feature needs, in whichever struct of the create chain holds it: most are base features, shaderDrawParameters is a
	// Vulkan 1.1 one. Timestamp queries are a queue property and not a device feature so they enable nothing here.
	void EnableFeatureBit(vk::PhysicalDeviceFeatures & features, vk::PhysicalDeviceVulkan11Features & features11, DeviceFeature feature) noexcept
	{
		switch (feature)
		{
		case DeviceFeature::eShaderDrawParameters:		features11.shaderDrawParameters = VK_TRUE; break;
		case DeviceFeature::eSamplerAnisotropy:			features.samplerAnisotropy = VK_TRUE; break;
		case DeviceFeature::eIndependentBlend:			features.independentBlend = VK_TRUE; break;
		case DeviceFeature::eDepthBounds:				features.depthBounds = VK_TRUE; break;
		case DeviceFeature::ePipelineStatisticsQueries: features.pipelineStatisticsQuery = VK_TRUE; break;
		case DeviceFeature::eMultiDrawIndirect:			features.multiDrawIndirect = VK_TRUE; break;
		case DeviceFeature::eDrawIndirectFirstInstance: features.drawIndirectFirstInstance = VK_TRUE; break;
		case DeviceFeature::eSparseResources:			features.sparseBinding = VK_TRUE; break;
		case DeviceFeature::eSparseBuffers:				features.sparseResidencyBuffer = VK_TRUE; break;
		case DeviceFeature::eSparseTextures:			features.sparseResidencyImage2D = VK_TRUE; break;
		case DeviceFeature::eSparseVolumes:				features.sparseResidencyImage3D = VK_TRUE; break;
		// A 1.1 feature bit and not something core and unconditional: creating a conversion with it off is a validation error.
		case DeviceFeature::eSamplerYcbcrConversion: features11.samplerYcbcrConversion = VK_TRUE; break;

		// None of these enables anything at creation: timestamp queries are a queue property, view swizzle is core Vulkan the portability subset only reports on, and
		// multi-planar support is a format feature. Nothing in the create chain to turn on for any of them.
		case DeviceFeature::eTimestampQueries:
		case DeviceFeature::eTextureViewSwizzle:
		case DeviceFeature::eMultiPlanarFormats: break;
		}
	}

	[[nodiscard]] const char * RequiredFeatureMessage(DeviceFeature feature) noexcept
	{
		switch (feature)
		{
		case DeviceFeature::eTimestampQueries:			return "no Vulkan adapter supports the required feature: timestamp queries";
		case DeviceFeature::eSamplerAnisotropy:			return "no Vulkan adapter supports the required feature: sampler anisotropy";
		case DeviceFeature::eIndependentBlend:			return "no Vulkan adapter supports the required feature: independent blend";
		case DeviceFeature::eDepthBounds:				return "no Vulkan adapter supports the required feature: depth bounds";
		case DeviceFeature::ePipelineStatisticsQueries: return "no Vulkan adapter supports the required feature: pipeline statistics queries";
		case DeviceFeature::eMultiDrawIndirect:			return "no Vulkan adapter supports the required feature: multi-draw indirect";
		case DeviceFeature::eDrawIndirectFirstInstance: return "no Vulkan adapter supports the required feature: indirect draw first instance";
		case DeviceFeature::eShaderDrawParameters:		return "no Vulkan adapter supports the required feature: shader draw parameters";
		case DeviceFeature::eSparseResources:			return "no Vulkan adapter supports the required feature: sparse resources";
		case DeviceFeature::eSparseBuffers:				return "no Vulkan adapter supports the required feature: sparse buffers";
		case DeviceFeature::eSparseTextures:			return "no Vulkan adapter supports the required feature: sparse textures";
		case DeviceFeature::eSparseVolumes:				return "no Vulkan adapter supports the required feature: sparse volumes";
		case DeviceFeature::eTextureViewSwizzle:		return "no Vulkan adapter supports the required feature: texture view swizzle";
		case DeviceFeature::eMultiPlanarFormats:		return "no Vulkan adapter supports the required feature: multi-planar formats";
		case DeviceFeature::eSamplerYcbcrConversion:	return "no Vulkan adapter supports the required feature: sampler Y'CbCr conversion";
		}

		return "no Vulkan adapter supports a required device feature";
	}

	// Graphics pipeline state mappers. Each is a plain one to one lowering of an RHI enum onto its Vulkan counterpart, used by VulkanCreatePipelineLayout and
	// VulkanCreateGraphicsPipeline.

	// The shader stage mask a push constant range or descriptor binding is visible to.
	[[nodiscard]] vk::ShaderStageFlags MapShaderStages(Flags<ShaderStage> stages) noexcept
	{
		if (stages.Contains(ShaderStage::eAll))
		{
			return vk::ShaderStageFlagBits::eAll;
		}

		vk::ShaderStageFlags out{};
		if (stages.Contains(ShaderStage::eVertex))
		{
			out |= vk::ShaderStageFlagBits::eVertex;
		}

		if (stages.Contains(ShaderStage::eTessellationControl))
		{
			out |= vk::ShaderStageFlagBits::eTessellationControl;
		}

		if (stages.Contains(ShaderStage::eTessellationEvaluation))
		{
			out |= vk::ShaderStageFlagBits::eTessellationEvaluation;
		}

		if (stages.Contains(ShaderStage::eGeometry))
		{
			out |= vk::ShaderStageFlagBits::eGeometry;
		}

		if (stages.Contains(ShaderStage::eFragment))
		{
			out |= vk::ShaderStageFlagBits::eFragment;
		}

		if (stages.Contains(ShaderStage::eCompute))
		{
			out |= vk::ShaderStageFlagBits::eCompute;
		}

		if (stages.Contains(ShaderStage::eAllGraphics))
		{
			out |= vk::ShaderStageFlagBits::eAllGraphics;
		}

		return out;
	}

	// The single stage of one shader binary.
	[[nodiscard]] vk::ShaderStageFlagBits MapShaderStageBit(ShaderStage stage) noexcept
	{
		switch (stage)
		{
		case ShaderStage::eVertex:				   return vk::ShaderStageFlagBits::eVertex;
		case ShaderStage::eTessellationControl:	   return vk::ShaderStageFlagBits::eTessellationControl;
		case ShaderStage::eTessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
		case ShaderStage::eGeometry:			   return vk::ShaderStageFlagBits::eGeometry;
		case ShaderStage::eFragment:			   return vk::ShaderStageFlagBits::eFragment;
		case ShaderStage::eCompute:				   return vk::ShaderStageFlagBits::eCompute;
		case ShaderStage::eRayGeneration:		   return vk::ShaderStageFlagBits::eRaygenKHR;
		case ShaderStage::eAnyHit:				   return vk::ShaderStageFlagBits::eAnyHitKHR;
		case ShaderStage::eClosestHit:			   return vk::ShaderStageFlagBits::eClosestHitKHR;
		case ShaderStage::eMiss:				   return vk::ShaderStageFlagBits::eMissKHR;
		case ShaderStage::eIntersection:		   return vk::ShaderStageFlagBits::eIntersectionKHR;
		case ShaderStage::eCallable:			   return vk::ShaderStageFlagBits::eCallableKHR;
		default:								   return vk::ShaderStageFlagBits::eVertex;
		}
	}

	[[nodiscard]] vk::PrimitiveTopology MapTopology(PrimitiveTopology topology) noexcept
	{
		switch (topology)
		{
		case PrimitiveTopology::ePointList:		return vk::PrimitiveTopology::ePointList;
		case PrimitiveTopology::eLineList:		return vk::PrimitiveTopology::eLineList;
		case PrimitiveTopology::eLineStrip:		return vk::PrimitiveTopology::eLineStrip;
		case PrimitiveTopology::eTriangleList:	return vk::PrimitiveTopology::eTriangleList;
		case PrimitiveTopology::eTriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
		case PrimitiveTopology::ePatchList:		return vk::PrimitiveTopology::ePatchList;
		}

		return vk::PrimitiveTopology::eTriangleList;
	}

	[[nodiscard]] vk::PolygonMode MapFillMode(FillMode mode) noexcept
	{
		switch (mode)
		{
		case FillMode::eWireframe: return vk::PolygonMode::eLine;
		case FillMode::eSolid:	   return vk::PolygonMode::eFill;
		}

		return vk::PolygonMode::eFill;
	}

	[[nodiscard]] vk::CullModeFlags MapCullMode(CullMode mode) noexcept
	{
		switch (mode)
		{
		case CullMode::eFront: return vk::CullModeFlagBits::eFront;
		case CullMode::eBack:  return vk::CullModeFlagBits::eBack;
		case CullMode::eNone:  return vk::CullModeFlagBits::eNone;
		}

		return vk::CullModeFlagBits::eNone;
	}

	[[nodiscard]] vk::FrontFace MapFrontFace(FrontFace face) noexcept
	{
		return face == FrontFace::eClockwise ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
	}

	[[nodiscard]] vk::CompareOp MapCompareOp(CompareOp op) noexcept
	{
		switch (op)
		{
		case CompareOp::eNever:			 return vk::CompareOp::eNever;
		case CompareOp::eLess:			 return vk::CompareOp::eLess;
		case CompareOp::eEqual:			 return vk::CompareOp::eEqual;
		case CompareOp::eLessOrEqual:	 return vk::CompareOp::eLessOrEqual;
		case CompareOp::eGreater:		 return vk::CompareOp::eGreater;
		case CompareOp::eNotEqual:		 return vk::CompareOp::eNotEqual;
		case CompareOp::eGreaterOrEqual: return vk::CompareOp::eGreaterOrEqual;
		case CompareOp::eAlways:		 return vk::CompareOp::eAlways;
		}

		return vk::CompareOp::eAlways;
	}

	[[nodiscard]] vk::StencilOp MapStencilOp(StencilOp op) noexcept
	{
		switch (op)
		{
		case StencilOp::eKeep:			 return vk::StencilOp::eKeep;
		case StencilOp::eZero:			 return vk::StencilOp::eZero;
		case StencilOp::eReplace:		 return vk::StencilOp::eReplace;
		case StencilOp::eIncrementClamp: return vk::StencilOp::eIncrementAndClamp;
		case StencilOp::eDecrementClamp: return vk::StencilOp::eDecrementAndClamp;
		case StencilOp::eInvert:		 return vk::StencilOp::eInvert;
		case StencilOp::eIncrementWrap:	 return vk::StencilOp::eIncrementAndWrap;
		case StencilOp::eDecrementWrap:	 return vk::StencilOp::eDecrementAndWrap;
		}

		return vk::StencilOp::eKeep;
	}

	[[nodiscard]] vk::StencilOpState MapStencilFace(const StencilFaceDesc & face) noexcept
	{
		return { MapStencilOp(face.failOp),
			MapStencilOp(face.passOp),
			MapStencilOp(face.depthFailOp),
			MapCompareOp(face.compareOp),
			face.compareMask,
			face.writeMask,
			face.reference };
	}

	[[nodiscard]] vk::BlendFactor MapBlendFactor(BlendFactor factor) noexcept
	{
		switch (factor)
		{
		case BlendFactor::eZero:				  return vk::BlendFactor::eZero;
		case BlendFactor::eOne:					  return vk::BlendFactor::eOne;
		case BlendFactor::eSrcColor:			  return vk::BlendFactor::eSrcColor;
		case BlendFactor::eOneMinusSrcColor:	  return vk::BlendFactor::eOneMinusSrcColor;
		case BlendFactor::eDstColor:			  return vk::BlendFactor::eDstColor;
		case BlendFactor::eOneMinusDstColor:	  return vk::BlendFactor::eOneMinusDstColor;
		case BlendFactor::eSrcAlpha:			  return vk::BlendFactor::eSrcAlpha;
		case BlendFactor::eOneMinusSrcAlpha:	  return vk::BlendFactor::eOneMinusSrcAlpha;
		case BlendFactor::eDstAlpha:			  return vk::BlendFactor::eDstAlpha;
		case BlendFactor::eOneMinusDstAlpha:	  return vk::BlendFactor::eOneMinusDstAlpha;
		case BlendFactor::eConstantColor:		  return vk::BlendFactor::eConstantColor;
		case BlendFactor::eOneMinusConstantColor: return vk::BlendFactor::eOneMinusConstantColor;
		case BlendFactor::eConstantAlpha:		  return vk::BlendFactor::eConstantAlpha;
		case BlendFactor::eOneMinusConstantAlpha: return vk::BlendFactor::eOneMinusConstantAlpha;
		}

		return vk::BlendFactor::eZero;
	}

	[[nodiscard]] vk::BlendOp MapBlendOp(BlendOp op) noexcept
	{
		switch (op)
		{
		case BlendOp::eAdd:				return vk::BlendOp::eAdd;
		case BlendOp::eSubtract:		return vk::BlendOp::eSubtract;
		case BlendOp::eReverseSubtract: return vk::BlendOp::eReverseSubtract;
		case BlendOp::eMin:				return vk::BlendOp::eMin;
		case BlendOp::eMax:				return vk::BlendOp::eMax;
		}

		return vk::BlendOp::eAdd;
	}

	[[nodiscard]] vk::ColorComponentFlags MapColorWriteMask(Flags<ColorWrite> mask) noexcept
	{
		vk::ColorComponentFlags out{};
		if (mask.Contains(ColorWrite::eR))
		{
			out |= vk::ColorComponentFlagBits::eR;
		}

		if (mask.Contains(ColorWrite::eG))
		{
			out |= vk::ColorComponentFlagBits::eG;
		}

		if (mask.Contains(ColorWrite::eB))
		{
			out |= vk::ColorComponentFlagBits::eB;
		}

		if (mask.Contains(ColorWrite::eA))
		{
			out |= vk::ColorComponentFlagBits::eA;
		}
		return out;
	}

	[[nodiscard]] detail::HostVector<vk::DynamicState> MapDynamicStates(Flags<DynamicState> states)
	{
		detail::HostVector<vk::DynamicState> out;

		/*
		 * Viewport and scissor go in whether or not the caller asked. GraphicsPipelineDesc carries no viewport and no scissor rectangle and both are command-list
		 * state on every backend. This one has to be told: the create info below passes viewportCount 1 with a null pViewports, which Vulkan permits only when both
		 * are declared dynamic.
		 *
		 * Once, not twice. Vulkan requires every element of pDynamicStates to be unique and the caller's own flags for these two are ignored, not appended.
		 */
		out.push_back(vk::DynamicState::eViewport);
		out.push_back(vk::DynamicState::eScissor);

		if (states.Contains(DynamicState::eBlendConstants))
		{
			out.push_back(vk::DynamicState::eBlendConstants);
		}

		if (states.Contains(DynamicState::eStencilReference))
		{
			out.push_back(vk::DynamicState::eStencilReference);
		}

		if (states.Contains(DynamicState::eDepthBias))
		{
			out.push_back(vk::DynamicState::eDepthBias);
		}

		return out;
	}

	// True for the depth stencil formats that carry a stencil aspect so dynamic rendering binds the stencil attachment format only when one exists.
	[[nodiscard]] bool HasStencilAspect(Format format) noexcept
	{
		return format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
	}

	// Command recording mappers: the dimensionality of a texture view, the synchronization2 stage and access masks a barrier lowers to, the image layout, the
	// aspect mask and the attachment load and store ops a rendering pass uses.
	[[nodiscard]] vk::ImageViewType MapImageViewType(TextureViewType type) noexcept
	{
		switch (type)
		{
		case TextureViewType::eTex1D:		 return vk::ImageViewType::e1D;
		case TextureViewType::eTex1DArray:	 return vk::ImageViewType::e1DArray;
		case TextureViewType::eTex2DArray:	 return vk::ImageViewType::e2DArray;
		case TextureViewType::eTex3D:		 return vk::ImageViewType::e3D;
		case TextureViewType::eTexCube:		 return vk::ImageViewType::eCube;
		case TextureViewType::eTexCubeArray: return vk::ImageViewType::eCubeArray;
		case TextureViewType::eTex2D:		 return vk::ImageViewType::e2D;
		}

		return vk::ImageViewType::e2D;
	}

	[[nodiscard]] vk::PipelineStageFlags2 MapStages2(Flags<PipelineStage> stages) noexcept
	{
		vk::PipelineStageFlags2 out{}; // eNone maps to an empty mask, which synchronization2 accepts
		if (stages.Contains(PipelineStage::eDrawIndirect))
		{
			out |= vk::PipelineStageFlagBits2::eDrawIndirect;
		}

		if (stages.Contains(PipelineStage::eVertexInput))
		{
			out |= vk::PipelineStageFlagBits2::eVertexInput;
		}

		if (stages.Contains(PipelineStage::eVertexShader))
		{
			out |= vk::PipelineStageFlagBits2::eVertexShader;
		}

		if (stages.Contains(PipelineStage::eTessellationControlShader))
		{
			out |= vk::PipelineStageFlagBits2::eTessellationControlShader;
		}

		if (stages.Contains(PipelineStage::eTessellationEvaluationShader))
		{
			out |= vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
		}

		if (stages.Contains(PipelineStage::eGeometryShader))
		{
			out |= vk::PipelineStageFlagBits2::eGeometryShader;
		}

		if (stages.Contains(PipelineStage::eFragmentShader))
		{
			out |= vk::PipelineStageFlagBits2::eFragmentShader;
		}

		if (stages.Contains(PipelineStage::eEarlyFragmentTests))
		{
			out |= vk::PipelineStageFlagBits2::eEarlyFragmentTests;
		}

		if (stages.Contains(PipelineStage::eLateFragmentTests))
		{
			out |= vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		if (stages.Contains(PipelineStage::eColorOutput))
		{
			out |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}

		if (stages.Contains(PipelineStage::eComputeShader))
		{
			out |= vk::PipelineStageFlagBits2::eComputeShader;
		}

		if (stages.Contains(PipelineStage::eCopy))
		{
			out |= vk::PipelineStageFlagBits2::eCopy;
		}

		if (stages.Contains(PipelineStage::eResolve))
		{
			out |= vk::PipelineStageFlagBits2::eResolve;
		}

		if (stages.Contains(PipelineStage::eClear))
		{
			out |= vk::PipelineStageFlagBits2::eClear;
		}

		if (stages.Contains(PipelineStage::eHost))
		{
			out |= vk::PipelineStageFlagBits2::eHost;
		}

		if (stages.Contains(PipelineStage::eRayTracingShader))
		{
			out |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}

		if (stages.Contains(PipelineStage::eAccelerationStructureBuild))
		{
			out |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
		}

		if (stages.Contains(PipelineStage::eAllGraphics))
		{
			out |= vk::PipelineStageFlagBits2::eAllGraphics;
		}

		if (stages.Contains(PipelineStage::eAllCommands))
		{
			out |= vk::PipelineStageFlagBits2::eAllCommands;
		}

		return out;
	}

	[[nodiscard]] vk::AccessFlags2 MapAccess2(Flags<Access> access) noexcept
	{
		vk::AccessFlags2 out{};
		if (access.Contains(Access::eIndirectRead))
		{
			out |= vk::AccessFlagBits2::eIndirectCommandRead;
		}

		if (access.Contains(Access::eVertexRead))
		{
			out |= vk::AccessFlagBits2::eVertexAttributeRead;
		}

		if (access.Contains(Access::eIndexRead))
		{
			out |= vk::AccessFlagBits2::eIndexRead;
		}

		if (access.Contains(Access::eConstantRead))
		{
			out |= vk::AccessFlagBits2::eUniformRead;
		}

		if (access.Contains(Access::eShaderRead))
		{
			out |= vk::AccessFlagBits2::eShaderRead;
		}

		if (access.Contains(Access::eShaderWrite))
		{
			out |= vk::AccessFlagBits2::eShaderWrite;
		}

		if (access.Contains(Access::eColorRead))
		{
			out |= vk::AccessFlagBits2::eColorAttachmentRead;
		}

		if (access.Contains(Access::eColorWrite))
		{
			out |= vk::AccessFlagBits2::eColorAttachmentWrite;
		}

		if (access.Contains(Access::eDepthStencilRead))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		}

		if (access.Contains(Access::eDepthStencilWrite))
		{
			out |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
		}

		if (access.Contains(Access::eCopyRead))
		{
			out |= vk::AccessFlagBits2::eTransferRead;
		}

		if (access.Contains(Access::eCopyWrite))
		{
			out |= vk::AccessFlagBits2::eTransferWrite;
		}

		if (access.Contains(Access::eHostRead))
		{
			out |= vk::AccessFlagBits2::eHostRead;
		}

		if (access.Contains(Access::eHostWrite))
		{
			out |= vk::AccessFlagBits2::eHostWrite;
		}

		if (access.Contains(Access::eMemoryRead))
		{
			out |= vk::AccessFlagBits2::eMemoryRead;
		}

		if (access.Contains(Access::eMemoryWrite))
		{
			out |= vk::AccessFlagBits2::eMemoryWrite;
		}

		if (access.Contains(Access::eAccelerationStructureRead))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureReadKHR;
		}

		if (access.Contains(Access::eAccelerationStructureWrite))
		{
			out |= vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
		}
		return out;
	}

	[[nodiscard]] vk::ImageLayout MapTextureLayout(TextureLayout layout) noexcept
	{
		switch (layout)
		{
		case TextureLayout::eGeneral:				 return vk::ImageLayout::eGeneral;
		case TextureLayout::eColorAttachment:		 return vk::ImageLayout::eColorAttachmentOptimal;
		case TextureLayout::eDepthStencilAttachment: return vk::ImageLayout::eDepthStencilAttachmentOptimal;
		case TextureLayout::eDepthStencilReadOnly:	 return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
		case TextureLayout::eShaderReadOnly:		 return vk::ImageLayout::eShaderReadOnlyOptimal;
		case TextureLayout::eCopySrc:				 return vk::ImageLayout::eTransferSrcOptimal;
		case TextureLayout::eCopyDst:				 return vk::ImageLayout::eTransferDstOptimal;
		case TextureLayout::eResolveSrc:			 return vk::ImageLayout::eTransferSrcOptimal;
		case TextureLayout::eResolveDst:			 return vk::ImageLayout::eTransferDstOptimal;
		case TextureLayout::ePresent:				 return vk::ImageLayout::ePresentSrcKHR;
		case TextureLayout::eUndefined:				 return vk::ImageLayout::eUndefined;
		}

		return vk::ImageLayout::eUndefined;
	}

	[[nodiscard]] vk::ImageAspectFlags MapAspect(Flags<TextureAspect> aspects) noexcept
	{
		vk::ImageAspectFlags out{};
		if (aspects.Contains(TextureAspect::eColor))
		{
			out |= vk::ImageAspectFlagBits::eColor;
		}

		if (aspects.Contains(TextureAspect::eDepth))
		{
			out |= vk::ImageAspectFlagBits::eDepth;
		}

		if (aspects.Contains(TextureAspect::eStencil))
		{
			out |= vk::ImageAspectFlagBits::eStencil;
		}

		if (aspects.Contains(TextureAspect::ePlane0))
		{
			out |= vk::ImageAspectFlagBits::ePlane0;
		}

		if (aspects.Contains(TextureAspect::ePlane1))
		{
			out |= vk::ImageAspectFlagBits::ePlane1;
		}

		if (aspects.Contains(TextureAspect::ePlane2))
		{
			out |= vk::ImageAspectFlagBits::ePlane2;
		}
		return out;
	}

	[[nodiscard]] vk::AttachmentLoadOp MapLoadOp(LoadOp op) noexcept
	{
		switch (op)
		{
		case LoadOp::eClear:	return vk::AttachmentLoadOp::eClear;
		case LoadOp::eDontCare: return vk::AttachmentLoadOp::eDontCare;
		case LoadOp::eLoad:		return vk::AttachmentLoadOp::eLoad;
		}
		return vk::AttachmentLoadOp::eLoad;
	}

	[[nodiscard]] vk::AttachmentStoreOp MapStoreOp(StoreOp op) noexcept
	{
		switch (op)
		{
		case StoreOp::eDontCare: return vk::AttachmentStoreOp::eDontCare;
		case StoreOp::eStore:	 return vk::AttachmentStoreOp::eStore;
		}
		return vk::AttachmentStoreOp::eStore;
	}

	[[nodiscard]] vk::ImageSubresourceRange MapSubresourceRange(const TextureSubresourceRange & range) noexcept
	{
		return {MapAspect(range.aspects), range.baseMip, range.mipCount, range.baseLayer, range.layerCount};
	}

	// The aspect a view of this format must name. Depth-stencil formats report the depth aspect, matching the implicit default view a texture gets alongside its
	// image so the same texture cannot end up with an implicit view and an explicit one that disagree.
	[[nodiscard]] vk::ImageAspectFlags AspectForViewFormat(vk::Format format) noexcept
	{
		switch (format)
		{
		case vk::Format::eD16Unorm:
		case vk::Format::eX8D24UnormPack32:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:	return vk::ImageAspectFlagBits::eDepth;
		case vk::Format::eS8Uint:			return vk::ImageAspectFlagBits::eStencil;
		default:							return vk::ImageAspectFlagBits::eColor;
		}
	}

	/*
	 * Process lifetime owner of the Vulkan backend objects, plus the loader that backs the dynamic dispatcher when nothing else has initialized it. Function local
	 * static so it is torn down after main returns: devices first (VMA and logical device), then instances, then the loader last, which keeps the dispatcher's
	 * function pointers valid through every teardown call.
	 */

	[[nodiscard]] AdapterType MapAdapterType(vk::PhysicalDeviceType type) noexcept
	{
		switch (type)
		{
		case vk::PhysicalDeviceType::eIntegratedGpu: return AdapterType::eIntegrated;
		case vk::PhysicalDeviceType::eDiscreteGpu:	 return AdapterType::eDiscrete;
		case vk::PhysicalDeviceType::eVirtualGpu:	 return AdapterType::eVirtual;
		case vk::PhysicalDeviceType::eCpu:			 return AdapterType::eCpu;
		default:									 return AdapterType::eUnknown;
		}
	}

	// Maps a Vulkan driver ID onto the normalized RHI enum. The RHI DriverId values mirror the VkDriverId registry one to one so this is a widening cast: an ID
	// this build has not named still carries through as its numeric value and formats via the default scheme.
	[[nodiscard]] DriverId MapDriverId(const vk::DriverId id) noexcept
	{
		return static_cast<DriverId>(static_cast<std::uint32_t>(id));
	}

	/*
	 * The identity an importer matches on, which is a different question from the PCI pair beside it: two identical adapters in one machine share a vendorID and
	 * deviceID and do not share these.
	 *
	 * deviceLUID is read only where Vulkan says it holds anything. The struct leaves the bytes undefined, not zeroed when deviceLUIDValid is false, so copying
	 * them across unconditionally would hand a caller whatever was on the stack.
	 */
	void FillAdapterIdentity(AdapterInfo & adapter, const vk::PhysicalDeviceIDProperties & id) noexcept
	{
		std::ranges::copy(id.deviceUUID, adapter.deviceUUID.begin());
		std::ranges::copy(id.driverUUID, adapter.driverUUID.begin());

		adapter.deviceLUIDValid = static_cast<bool>(id.deviceLUIDValid);
		if (adapter.deviceLUIDValid)
		{
			std::ranges::copy(id.deviceLUID, adapter.deviceLUID.begin());
		}
	}

} // namespace azo::rhi::vulkan

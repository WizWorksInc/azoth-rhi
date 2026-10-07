// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "vulkan/vulkan.hpp"

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace azo::rhi::vulkan
{
	[[nodiscard]] vk::BufferUsageFlags map_buffer_usage(Flags<BufferUsage> usage) noexcept
	{
		vk::BufferUsageFlags out{};
		if (usage.contains(BufferUsage::eCopySrc))
		{
			out |= vk::BufferUsageFlagBits::eTransferSrc;
		}

		if (usage.contains(BufferUsage::eCopyDst))
		{
			out |= vk::BufferUsageFlagBits::eTransferDst;
		}

		if (usage.contains(BufferUsage::eVertex))
		{
			out |= vk::BufferUsageFlagBits::eVertexBuffer;
		}

		if (usage.contains(BufferUsage::eIndex))
		{
			out |= vk::BufferUsageFlagBits::eIndexBuffer;
		}

		if (usage.contains(BufferUsage::eUniform))
		{
			out |= vk::BufferUsageFlagBits::eUniformBuffer;
		}

		if (usage.contains(BufferUsage::eStorage))
		{
			out |= vk::BufferUsageFlagBits::eStorageBuffer;
		}

		if (usage.contains(BufferUsage::eIndirect))
		{
			out |= vk::BufferUsageFlagBits::eIndirectBuffer;
		}
		return out;
	}

	bool vulkan_refuse_ray_tracing_usage(const Flags<BufferUsage> usage, const bool supportsRayTracing, Error * error) noexcept
	{
		if (supportsRayTracing)
		{
			return true;
		}

		if (usage.contains(BufferUsage::eShaderBindingTable) || usage.contains(BufferUsage::eAccelerationStructureInput) ||
			usage.contains(BufferUsage::eAccelerationStructureStorage))
		{
			return fail(
				error,
				ErrorCode::eUnsupportedFeature,
				"a shader binding table or acceleration structure buffer needs ray tracing, which this device declines"
			);
		}

		return true;
	}

	[[nodiscard]] VmaMemoryUsage map_memory_usage(MemoryUsage memory, VmaAllocationCreateFlags & outFlags) noexcept
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

		return VMA_MEMORY_USAGE_AUTO;
	}

	[[nodiscard]] VmaMemoryUsage map_buffer_memory_usage(MemoryUsage memory, VmaAllocationCreateFlags & outFlags) noexcept
	{
		const VmaMemoryUsage usage = map_memory_usage(memory, outFlags);

		// A mappable buffer stays mapped so Map only counts, which keeps VMA's 255-map ceiling out of reach.
		if (outFlags != 0)
		{
			outFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		return usage;
	}

	[[nodiscard]] vk::Format map_format(Format format) noexcept
	{
		switch (format)
		{
		case Format::eR8UNorm:					 return vk::Format::eR8Unorm;
		case Format::eR8UInt:					 return vk::Format::eR8Uint;
		case Format::eR8SInt:					 return vk::Format::eR8Sint;
		case Format::eRG8UNorm:					 return vk::Format::eR8G8Unorm;
		case Format::eRGBA8UNorm:				 return vk::Format::eR8G8B8A8Unorm;
		case Format::eRGBA8Srgb:				 return vk::Format::eR8G8B8A8Srgb;
		case Format::eBGRA8UNorm:				 return vk::Format::eB8G8R8A8Unorm;
		case Format::eBGRA8Srgb:				 return vk::Format::eB8G8R8A8Srgb;
		case Format::eR16UInt:					 return vk::Format::eR16Uint;
		case Format::eR16SInt:					 return vk::Format::eR16Sint;
		case Format::eR16Float:					 return vk::Format::eR16Sfloat;
		case Format::eRG16Float:				 return vk::Format::eR16G16Sfloat;
		case Format::eRGBA16Float:				 return vk::Format::eR16G16B16A16Sfloat;
		case Format::eR11G11B10Float:			 return vk::Format::eB10G11R11UfloatPack32;
		case Format::eRGB10A2UNorm:				 return vk::Format::eA2B10G10R10UnormPack32;
		case Format::eRGB9E5Float:				 return vk::Format::eE5B9G9R9UfloatPack32;
		case Format::eR32UInt:					 return vk::Format::eR32Uint;
		case Format::eR32SInt:					 return vk::Format::eR32Sint;
		case Format::eR32Float:					 return vk::Format::eR32Sfloat;
		case Format::eRG32Float:				 return vk::Format::eR32G32Sfloat;
		case Format::eRGB32Float:				 return vk::Format::eR32G32B32Sfloat;
		case Format::eRGBA32Float:				 return vk::Format::eR32G32B32A32Sfloat;
		case Format::eD16UNorm:					 return vk::Format::eD16Unorm;
		case Format::eD24UNormS8UInt:			 return vk::Format::eD24UnormS8Uint;
		case Format::eD32Float:					 return vk::Format::eD32Sfloat;
		case Format::eD32FloatS8UInt:			 return vk::Format::eD32SfloatS8Uint;
		case Format::eX8D24UNorm:				 return vk::Format::eX8D24UnormPack32;
		case Format::eBC1RGBAUNorm:				 return vk::Format::eBc1RgbaUnormBlock;
		case Format::eBC1RGBASrgb:				 return vk::Format::eBc1RgbaSrgbBlock;
		case Format::eBC3UNorm:					 return vk::Format::eBc3UnormBlock;
		case Format::eBC3Srgb:					 return vk::Format::eBc3SrgbBlock;
		case Format::eBC5UNorm:					 return vk::Format::eBc5UnormBlock;
		case Format::eBC5SNorm:					 return vk::Format::eBc5SnormBlock;
		case Format::eBC7UNorm:					 return vk::Format::eBc7UnormBlock;
		case Format::eBC7Srgb:					 return vk::Format::eBc7SrgbBlock;
		case Format::eBC6HUFloat:				 return vk::Format::eBc6HUfloatBlock;
		case Format::eBC6HSFloat:				 return vk::Format::eBc6HSfloatBlock;
		case Format::eG8B8R8Biplanar420UNorm:	 return vk::Format::eG8B8R82Plane420Unorm;
		case Format::eG8B8R8Triplanar420UNorm:	 return vk::Format::eG8B8R83Plane420Unorm;
		case Format::eG10B10R10Biplanar420UNorm: return vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16;
		case Format::eUndefined:				 return vk::Format::eUndefined;
		}

		return vk::Format::eUndefined;
	}

	[[nodiscard]] vk::ImageUsageFlags map_texture_usage(Flags<TextureUsage> usage) noexcept
	{
		vk::ImageUsageFlags out{};
		if (usage.contains(TextureUsage::eCopySrc))
		{
			out |= vk::ImageUsageFlagBits::eTransferSrc;
		}

		if (usage.contains(TextureUsage::eCopyDst))
		{
			out |= vk::ImageUsageFlagBits::eTransferDst;
		}

		if (usage.contains(TextureUsage::eSampled))
		{
			out |= vk::ImageUsageFlagBits::eSampled;
		}

		if (usage.contains(TextureUsage::eStorage))
		{
			out |= vk::ImageUsageFlagBits::eStorage;
		}

		if (usage.contains(TextureUsage::eColorAttachment))
		{
			out |= vk::ImageUsageFlagBits::eColorAttachment;
		}

		if (usage.contains(TextureUsage::eDepthStencilAttachment))
		{
			out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
		}

		if (usage.contains(TextureUsage::eTransientAttachment))
		{
			out |= vk::ImageUsageFlagBits::eTransientAttachment;
		}

		return out;
	}

	[[nodiscard]] vk::ImageType map_image_type(TextureType type) noexcept
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

	[[nodiscard]] vk::ImageViewType map_view_type(TextureType type) noexcept
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

	[[nodiscard]] vk::SampleCountFlagBits map_sample_count(SampleCount samples) noexcept
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

	[[nodiscard]] vk::PresentModeKHR map_present_mode(PresentMode mode) noexcept
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

	[[nodiscard]] PresentMode map_vk_present_mode(vk::PresentModeKHR mode) noexcept
	{
		switch (mode)
		{
		case vk::PresentModeKHR::eMailbox:	   return PresentMode::eMailbox;
		case vk::PresentModeKHR::eImmediate:   return PresentMode::eImmediate;
		case vk::PresentModeKHR::eFifoRelaxed: return PresentMode::eFifoRelaxed;
		default:							   return PresentMode::eFifo;
		}
	}

	[[nodiscard]] Format map_vk_format(vk::Format format) noexcept
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
		[[nodiscard]] vk::ComponentSwizzle map_component_swizzle(ComponentSwizzle swizzle) noexcept
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
	}

	vk::ComponentMapping map_component_mapping(const ComponentMapping mapping) noexcept
	{
		return vk::ComponentMapping{ map_component_swizzle(mapping.r),
			map_component_swizzle(mapping.g),
			map_component_swizzle(mapping.b),
			map_component_swizzle(mapping.a) };
	}

	bool query_portability_subset_features(
		vk::PhysicalDevice phys,
		const vk::detail::DispatchLoaderDynamic & dispatch,
		PortabilitySubsetFeatures & out
	) noexcept
	{
		auto * const physical  = static_cast<VkPhysicalDevice>(phys);
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

		const bool portability = std::ranges::any_of(
			exts,
			[](const VkExtensionProperties & ep) noexcept
			{
				return std::strcmp(ep.extensionName, "VK_KHR_portability_subset") == 0;
			}
		);
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

	[[nodiscard]] bool adapter_supports_view_swizzle(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept
	{
		PortabilitySubsetFeatures portabilityFeatures{};
		if (!query_portability_subset_features(phys, dispatch, portabilityFeatures))
		{
			return true;
		}

		return portabilityFeatures.imageViewFormatSwizzle == VK_TRUE;
	}

	namespace
	{
		[[nodiscard]] vk::SamplerYcbcrModelConversion map_ycbcr_model(YcbcrModel model) noexcept
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

		[[nodiscard]] vk::SamplerYcbcrRange map_ycbcr_range(YcbcrRange range) noexcept
		{
			return range == YcbcrRange::eFull ? vk::SamplerYcbcrRange::eItuFull : vk::SamplerYcbcrRange::eItuNarrow;
		}

		[[nodiscard]] vk::ChromaLocation map_chroma_location(ChromaLocation location) noexcept
		{
			return location == ChromaLocation::eCositedEven ? vk::ChromaLocation::eCositedEven : vk::ChromaLocation::eMidpoint;
		}
	}

	vk::SamplerYcbcrConversion acquire_ycbcr_conversion(VulkanDevice * device, const SamplerYcbcrConversionDesc & desc, vk::Result & outResult) noexcept
	{
		outResult = vk::Result::eSuccess;
		for (const auto & [cached, conversion] : device->ycbcrConversions)
		{
			if (cached == desc)
			{
				return conversion;
			}
		}

		vk::SamplerYcbcrConversionCreateInfo info{};
		info.format						 = map_format(desc.format);
		info.ycbcrModel					 = map_ycbcr_model(desc.model);
		info.ycbcrRange					 = map_ycbcr_range(desc.range);
		info.components					 = map_component_mapping(desc.components);
		info.xChromaOffset				 = map_chroma_location(desc.xChromaOffset);
		info.yChromaOffset				 = map_chroma_location(desc.yChromaOffset);
		info.chromaFilter				 = map_filter(desc.chromaFilter);
		info.forceExplicitReconstruction = VK_FALSE;

		const auto created = device->device.createSamplerYcbcrConversion(info, nullptr, device->dispatch);
		if (created.result != vk::Result::eSuccess)
		{
			outResult = created.result;
			return {};
		}

		if (!detail::try_push_back(device->ycbcrConversions, std::pair{ desc, created.value }))
		{
			device->device.destroySamplerYcbcrConversion(created.value, nullptr, device->dispatch);
			return {};
		}

		return created.value;
	}

	[[nodiscard]] bool adapter_supports_multi_planar_formats(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept
	{
		VkFormatProperties props{};
		dispatch.vkGetPhysicalDeviceFormatProperties(static_cast<VkPhysicalDevice>(phys), static_cast<VkFormat>(vk::Format::eG8B8R82Plane420Unorm), &props);

		if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
		{
			return false;
		}

		PortabilitySubsetFeatures portabilityFeatures{};
		if (!query_portability_subset_features(phys, dispatch, portabilityFeatures))
		{
			return true;
		}

		return portabilityFeatures.imageViewFormatReinterpretation == VK_TRUE;
	}

	[[nodiscard]] bool adapter_supports_feature(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, DeviceFeature feature) noexcept
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
			const auto chain = phys.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features>(dispatch);
			return static_cast<bool>(chain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters);
		}
		case DeviceFeature::eSparseResources:	 return static_cast<bool>(feats.sparseBinding);
		case DeviceFeature::eSparseBuffers:		 return static_cast<bool>(feats.sparseResidencyBuffer);
		case DeviceFeature::eSparseTextures:	 return static_cast<bool>(feats.sparseResidencyImage2D);
		case DeviceFeature::eSparseVolumes:		 return static_cast<bool>(feats.sparseResidencyImage3D);
		case DeviceFeature::eTextureViewSwizzle: return adapter_supports_view_swizzle(phys, dispatch);
		case DeviceFeature::eMultiPlanarFormats: return adapter_supports_multi_planar_formats(phys, dispatch);
		case DeviceFeature::eSamplerYcbcrConversion:
		{
			const auto chain = phys.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features>(dispatch);
			return adapter_supports_multi_planar_formats(phys, dispatch) &&
				   static_cast<bool>(chain.get<vk::PhysicalDeviceVulkan11Features>().samplerYcbcrConversion);
		}
		}

		return false;
	}

	[[nodiscard]] bool adapter_supports_all_features(
		vk::PhysicalDevice phys,
		const vk::detail::DispatchLoaderDynamic & dispatch,
		std::span<const DeviceFeature> features
	) noexcept
	{
		return std::ranges::all_of(
			features,
			[phys, &dispatch](const DeviceFeature feature) noexcept
			{
				return adapter_supports_feature(phys, dispatch, feature);
			}
		);
	}

	void enable_feature_bit(vk::PhysicalDeviceFeatures & features, vk::PhysicalDeviceVulkan11Features & features11, DeviceFeature feature) noexcept
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
		case DeviceFeature::eSamplerYcbcrConversion:	features11.samplerYcbcrConversion = VK_TRUE; break;

		case DeviceFeature::eTimestampQueries:
		case DeviceFeature::eTextureViewSwizzle:
		case DeviceFeature::eMultiPlanarFormats: break;
		}
	}

	[[nodiscard]] const char * required_feature_message(DeviceFeature feature) noexcept
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

	[[nodiscard]] vk::ShaderStageFlags map_shader_stages(Flags<ShaderStage> stages) noexcept
	{
		if (stages.contains(ShaderStage::eAll))
		{
			return vk::ShaderStageFlagBits::eAll;
		}

		vk::ShaderStageFlags out{};
		if (stages.contains(ShaderStage::eVertex))
		{
			out |= vk::ShaderStageFlagBits::eVertex;
		}

		if (stages.contains(ShaderStage::eTessellationControl))
		{
			out |= vk::ShaderStageFlagBits::eTessellationControl;
		}

		if (stages.contains(ShaderStage::eTessellationEvaluation))
		{
			out |= vk::ShaderStageFlagBits::eTessellationEvaluation;
		}

		if (stages.contains(ShaderStage::eGeometry))
		{
			out |= vk::ShaderStageFlagBits::eGeometry;
		}

		if (stages.contains(ShaderStage::eFragment))
		{
			out |= vk::ShaderStageFlagBits::eFragment;
		}

		if (stages.contains(ShaderStage::eCompute))
		{
			out |= vk::ShaderStageFlagBits::eCompute;
		}

		if (stages.contains(ShaderStage::eAllGraphics))
		{
			out |= vk::ShaderStageFlagBits::eAllGraphics;
		}

		if (stages.contains(ShaderStage::eRayGeneration))
		{
			out |= vk::ShaderStageFlagBits::eRaygenKHR;
		}

		if (stages.contains(ShaderStage::eAnyHit))
		{
			out |= vk::ShaderStageFlagBits::eAnyHitKHR;
		}

		if (stages.contains(ShaderStage::eClosestHit))
		{
			out |= vk::ShaderStageFlagBits::eClosestHitKHR;
		}

		if (stages.contains(ShaderStage::eMiss))
		{
			out |= vk::ShaderStageFlagBits::eMissKHR;
		}

		if (stages.contains(ShaderStage::eIntersection))
		{
			out |= vk::ShaderStageFlagBits::eIntersectionKHR;
		}

		if (stages.contains(ShaderStage::eCallable))
		{
			out |= vk::ShaderStageFlagBits::eCallableKHR;
		}

		if (stages.contains(ShaderStage::eAllRayTracing))
		{
			out |= vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eClosestHitKHR |
				   vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eIntersectionKHR | vk::ShaderStageFlagBits::eCallableKHR;
		}

		return out;
	}

	[[nodiscard]] vk::ShaderStageFlagBits map_shader_stage_bit(ShaderStage stage) noexcept
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

	[[nodiscard]] vk::PrimitiveTopology map_topology(PrimitiveTopology topology) noexcept
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

	[[nodiscard]] vk::PolygonMode map_fill_mode(FillMode mode) noexcept
	{
		switch (mode)
		{
		case FillMode::eWireframe: return vk::PolygonMode::eLine;
		case FillMode::eSolid:	   return vk::PolygonMode::eFill;
		}

		return vk::PolygonMode::eFill;
	}

	[[nodiscard]] vk::CullModeFlags map_cull_mode(CullMode mode) noexcept
	{
		switch (mode)
		{
		case CullMode::eFront: return vk::CullModeFlagBits::eFront;
		case CullMode::eBack:  return vk::CullModeFlagBits::eBack;
		case CullMode::eNone:  return vk::CullModeFlagBits::eNone;
		}

		return vk::CullModeFlagBits::eNone;
	}

	[[nodiscard]] vk::FrontFace map_front_face(FrontFace face) noexcept
	{
		return face == FrontFace::eClockwise ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
	}

	[[nodiscard]] vk::CompareOp map_compare_op(CompareOp op) noexcept
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

	[[nodiscard]] vk::StencilOp map_stencil_op(StencilOp op) noexcept
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

	[[nodiscard]] vk::StencilOpState map_stencil_face(const StencilFaceDesc & face) noexcept
	{
		return { map_stencil_op(face.failOp),
			map_stencil_op(face.passOp),
			map_stencil_op(face.depthFailOp),
			map_compare_op(face.compareOp),
			face.compareMask,
			face.writeMask,
			face.reference };
	}

	[[nodiscard]] vk::BlendFactor map_blend_factor(BlendFactor factor) noexcept
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

	[[nodiscard]] vk::BlendOp map_blend_op(BlendOp op) noexcept
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

	[[nodiscard]] vk::ColorComponentFlags map_color_write_mask(Flags<ColorWrite> mask) noexcept
	{
		vk::ColorComponentFlags out{};
		if (mask.contains(ColorWrite::eR))
		{
			out |= vk::ColorComponentFlagBits::eR;
		}

		if (mask.contains(ColorWrite::eG))
		{
			out |= vk::ColorComponentFlagBits::eG;
		}

		if (mask.contains(ColorWrite::eB))
		{
			out |= vk::ColorComponentFlagBits::eB;
		}

		if (mask.contains(ColorWrite::eA))
		{
			out |= vk::ColorComponentFlagBits::eA;
		}
		return out;
	}

	[[nodiscard]] detail::HostVector<vk::DynamicState> map_dynamic_states(Flags<DynamicState> states)
	{
		detail::HostVector<vk::DynamicState> out;

		out.push_back(vk::DynamicState::eViewport);
		out.push_back(vk::DynamicState::eScissor);

		if (states.contains(DynamicState::eBlendConstants))
		{
			out.push_back(vk::DynamicState::eBlendConstants);
		}

		if (states.contains(DynamicState::eStencilReference))
		{
			out.push_back(vk::DynamicState::eStencilReference);
		}

		if (states.contains(DynamicState::eDepthBias))
		{
			out.push_back(vk::DynamicState::eDepthBias);
		}

		return out;
	}

	[[nodiscard]] bool has_stencil_aspect(Format format) noexcept
	{
		return format == Format::eD24UNormS8UInt || format == Format::eD32FloatS8UInt;
	}

	[[nodiscard]] vk::ImageViewType map_image_view_type(TextureViewType type) noexcept
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

	[[nodiscard]] vk::ImageAspectFlags map_aspect(Flags<TextureAspect> aspects) noexcept
	{
		vk::ImageAspectFlags out{};
		if (aspects.contains(TextureAspect::eColor))
		{
			out |= vk::ImageAspectFlagBits::eColor;
		}

		if (aspects.contains(TextureAspect::eDepth))
		{
			out |= vk::ImageAspectFlagBits::eDepth;
		}

		if (aspects.contains(TextureAspect::eStencil))
		{
			out |= vk::ImageAspectFlagBits::eStencil;
		}

		if (aspects.contains(TextureAspect::ePlane0))
		{
			out |= vk::ImageAspectFlagBits::ePlane0;
		}

		if (aspects.contains(TextureAspect::ePlane1))
		{
			out |= vk::ImageAspectFlagBits::ePlane1;
		}

		if (aspects.contains(TextureAspect::ePlane2))
		{
			out |= vk::ImageAspectFlagBits::ePlane2;
		}
		return out;
	}

	[[nodiscard]] vk::AttachmentLoadOp map_load_op(LoadOp op) noexcept
	{
		switch (op)
		{
		case LoadOp::eClear:	return vk::AttachmentLoadOp::eClear;
		case LoadOp::eDontCare: return vk::AttachmentLoadOp::eDontCare;
		case LoadOp::eLoad:		return vk::AttachmentLoadOp::eLoad;
		}
		return vk::AttachmentLoadOp::eLoad;
	}

	[[nodiscard]] vk::AttachmentStoreOp map_store_op(StoreOp op) noexcept
	{
		switch (op)
		{
		case StoreOp::eDontCare: return vk::AttachmentStoreOp::eDontCare;
		case StoreOp::eStore:	 return vk::AttachmentStoreOp::eStore;
		}
		return vk::AttachmentStoreOp::eStore;
	}

	[[nodiscard]] vk::ImageSubresourceRange map_subresource_range(const TextureSubresourceRange & range) noexcept
	{
		return { map_aspect(range.aspects), range.baseMip, range.mipCount, range.baseLayer, range.layerCount };
	}

	[[nodiscard]] vk::ImageAspectFlags aspect_for_view_format(vk::Format format) noexcept
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

	[[nodiscard]] AdapterType map_adapter_type(vk::PhysicalDeviceType type) noexcept
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

	[[nodiscard]] DriverId map_driver_id(const vk::DriverId id) noexcept
	{
		return static_cast<DriverId>(static_cast<std::uint32_t>(id));
	}

	void fill_adapter_identity(AdapterInfo & adapter, const vk::PhysicalDeviceIDProperties & id) noexcept
	{
		std::ranges::copy(id.deviceUUID, adapter.deviceUUID.begin());
		std::ranges::copy(id.driverUUID, adapter.driverUUID.begin());

		adapter.deviceLUIDValid = static_cast<bool>(id.deviceLUIDValid);
		if (adapter.deviceLUIDValid)
		{
			std::ranges::copy(id.deviceLUID, adapter.deviceLUID.begin());
		}
	}

}

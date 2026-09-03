// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN
	#include "azoth/rhi/native/vulkan_config.hpp"
	#include "azoth/rhi/native/vulkan_native.hpp"
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_METAL
	#include "azoth/rhi/native/metal_config.hpp"
	#include "azoth/rhi/native/metal_native.hpp"
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12
	#include "azoth/rhi/native/d3d12_native.hpp"
#endif

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	template <rhi::GraphicsApiTag Api>
	[[nodiscard]] rhi::Result<rhi::UniqueDevice> MakeDevice()
	{
		static constexpr std::array<rhi::DeviceFeature, 1> kPreferred{ rhi::DeviceFeature::eSamplerYcbcrConversion };

		rhi::DeviceDesc desc{};
		desc.validation		   = rhi::ValidationMode::eDeveloper;
		desc.preferredFeatures = kPreferred;
		return rhi::CreateDevice<Api>(desc);
	}

#if defined(AZOTH_RHI_TEST_ADOPTION_VULKAN) || defined(AZOTH_RHI_TEST_ADOPTION_METAL)

	template <rhi::GraphicsApiTag Api, class Config>
	[[nodiscard]] rhi::Result<rhi::UniqueDevice> CreateWith(const rhi::GraphicsApiId key, const Config & config)
	{
		const std::array<rhi::DeviceConfigEntry, 1> entries{ rhi::DeviceConfigEntry{ .api = key, .config = &config } };

		rhi::DeviceDesc desc{};
		desc.validation		= rhi::ValidationMode::eDeveloper;
		desc.backendConfigs = entries;
		return rhi::CreateDevice<Api>(desc);
	}

#endif

#if defined(AZOTH_RHI_TEST_ADOPTION_VULKAN) || defined(AZOTH_RHI_TEST_ADOPTION_METAL) || defined(AZOTH_RHI_TEST_ADOPTION_D3D12)

	template <rhi::GraphicsApiTag Api, class FromView, class FromAccessor>
	void ExpectANativeScopeSeesTheBackendsOwnCommandList(FromView fromView, FromAccessor fromAccessor)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<Api>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no device for this backend on this machine: " << test::Describe(created.GetError());
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		rhi::Error error{};
		rhi::CommandPool pool = device.CreateCommandPool(rhi::CommandPoolDesc{ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.nativeScope", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		ASSERT_TRUE(static_cast<bool>(fromAccessor(list))) << "the accessor reports no native command list for an open recording";

		bool ran = false;
		EXPECT_TRUE(test::Ok(list.ModifyNative<Api>(
								 rhi::NativeMutationDesc{},
								 [&](const auto & view)
								 {
									 ran = true;
									 EXPECT_EQ(fromView(view), fromAccessor(list))
										 << "the native scope handed back an object the accessor for the same command list does not agree with, which is what "
											"casting the facade impl without resolving the validation decorator produces";
								 },
								 error),
			error));
		EXPECT_TRUE(ran) << "the callback the scope brackets never ran";
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(VulkanAdoption, AdoptsAnImageAViewAndASamplerAndLeavesThemForTheCallerToDestroy)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a Vulkan device did not hand back its native handles";

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType		= vk::ImageType::e2D;
		imageInfo.format		= vk::Format::eR8G8B8A8Unorm;
		imageInfo.extent		= vk::Extent3D{ 4, 4, 1 };
		imageInfo.mipLevels		= 1;
		imageInfo.arrayLayers	= 1;
		imageInfo.samples		= vk::SampleCountFlagBits::e1;
		imageInfo.tiling		= vk::ImageTiling::eOptimal;
		imageInfo.usage			= vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.sharingMode	= vk::SharingMode::eExclusive;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;

		const auto createdImage = vkDevice.createImage(imageInfo, nullptr, dispatch);
		ASSERT_EQ(createdImage.result, vk::Result::eSuccess);
		const vk::Image image = createdImage.value;

		const vk::MemoryRequirements requirements			 = vkDevice.getImageMemoryRequirements(image, dispatch);
		const vk::PhysicalDeviceMemoryProperties memoryProps = native.Value().physicalDevice.getMemoryProperties(dispatch);

		std::uint32_t typeIndex = 0;
		bool foundType			= false;
		for (std::uint32_t i = 0; i < memoryProps.memoryTypeCount; ++i)
		{
			if ((requirements.memoryTypeBits & (1u << i)) != 0)
			{
				typeIndex = i;
				foundType = true;
				break;
			}
		}
		ASSERT_TRUE(foundType) << "no memory type accepts this image";

		const auto allocated = vkDevice.allocateMemory(vk::MemoryAllocateInfo(requirements.size, typeIndex), nullptr, dispatch);
		ASSERT_EQ(allocated.result, vk::Result::eSuccess);
		const vk::DeviceMemory memory = allocated.value;
		ASSERT_EQ(vkDevice.bindImageMemory(image, memory, 0, dispatch), vk::Result::eSuccess);

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image			  = image;
		viewInfo.viewType		  = vk::ImageViewType::e2D;
		viewInfo.format			  = vk::Format::eR8G8B8A8Unorm;
		viewInfo.subresourceRange = vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

		const auto createdView = vkDevice.createImageView(viewInfo, nullptr, dispatch);
		ASSERT_EQ(createdView.result, vk::Result::eSuccess);
		const vk::ImageView view = createdView.value;

		const auto createdSampler = vkDevice.createSampler(vk::SamplerCreateInfo{}, nullptr, dispatch);
		ASSERT_EQ(createdSampler.result, vk::Result::eSuccess);
		const vk::Sampler sampler = createdSampler.value;

		rhi::TextureDesc textureDesc{};
		textureDesc.width  = 4;
		textureDesc.height = 4;
		textureDesc.format = rhi::Format::eRGBA8UNorm;
		textureDesc.usage  = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst;

		rhi::Error error{};
		const rhi::TextureHandle adoptedTexture =
			device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = image }, { .desc = textureDesc }, error);
		ASSERT_TRUE(adoptedTexture.IsValid()) << error.message;

		const rhi::TextureViewHandle adoptedView = device.AdoptTextureView<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = view }, { .texture = adoptedTexture, .format = rhi::Format::eRGBA8UNorm }, error);
		ASSERT_TRUE(adoptedView.IsValid()) << error.message;

		const rhi::SamplerHandle adoptedSampler = device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
		ASSERT_TRUE(adoptedSampler.IsValid()) << error.message;

		rhi::NativeTexture<rhi::VulkanApi> readBack{};
		EXPECT_TRUE(device.GetNativeTexture<rhi::VulkanApi>(adoptedTexture, readBack, error)) << error.message;
		EXPECT_EQ(readBack.image, image) << "the native read handed back a different image than was adopted";

		EXPECT_TRUE(device.Destroy(adoptedView, {}, error)) << error.message;
		EXPECT_TRUE(device.Destroy(adoptedSampler, {}, error)) << error.message;
		EXPECT_TRUE(device.Destroy(adoptedTexture, {}, error)) << error.message;

		vkDevice.destroySampler(sampler, nullptr, dispatch);
		vkDevice.destroyImageView(view, nullptr, dispatch);
		vkDevice.destroyImage(image, nullptr, dispatch);
		vkDevice.freeMemory(memory, nullptr, dispatch);

		if (device.GetCaps().reportsValidationMessageCounts)
		{
			EXPECT_EQ(device.GetValidationMessageCounts().errors, 0u)
				<< "adoption produced Vulkan validation errors, which is what freeing an adopted object looks like from here";
		}
	}

	TEST(VulkanAdoption, RepeatedAdoptAndDestroyReturnsTheSlots)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;

		const auto createdSampler = vkDevice.createSampler(vk::SamplerCreateInfo{}, nullptr, dispatch);
		ASSERT_EQ(createdSampler.result, vk::Result::eSuccess);
		const vk::Sampler sampler = createdSampler.value;

		rhi::Error error{};
		for (int round = 0; round < 8; ++round)
		{
			const rhi::SamplerHandle adopted = device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
			ASSERT_TRUE(adopted.IsValid()) << "round " << round << ": " << error.message;
			ASSERT_TRUE(device.Destroy(adopted, {}, error)) << "round " << round << ": " << error.message;
		}

		vkDevice.destroySampler(sampler, nullptr, dispatch);
	}

	TEST(VulkanAdoption, AdoptsATimelineSemaphore)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;

		const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 7);
		const auto createdSemaphore = vkDevice.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, dispatch);
		ASSERT_EQ(createdSemaphore.result, vk::Result::eSuccess);
		const vk::Semaphore semaphore = createdSemaphore.value;

		rhi::Error error{};
		const rhi::TimelineHandle adopted = device.AdoptTimeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{ .semaphore = semaphore }, {}, error);
		ASSERT_TRUE(adopted.IsValid()) << error.message;

		std::uint64_t value = 0;
		EXPECT_TRUE(device.GetQueue(rhi::QueueType::eGraphics).GetCompletedValue(adopted, value, error)) << error.message;
		EXPECT_EQ(value, 7u) << "an adopted timeline did not carry the value its producer left it at";

		EXPECT_TRUE(device.Destroy(adopted, {}, error)) << error.message;
		vkDevice.destroySemaphore(semaphore, nullptr, dispatch);
	}

	TEST(VulkanAdoption, RefusesAPayloadCarryingNoObject)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		rhi::Error error{};
		const rhi::SamplerHandle adopted = device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error);
		EXPECT_FALSE(adopted.IsValid()) << "a null VkSampler was adopted anyway";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST(VulkanAdoption, RefusesAViewNamingATextureThisDeviceNeverHandedOut)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		rhi::Error error{};
		const rhi::TextureViewHandle adopted = device.AdoptTextureView<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = vk::ImageView{} }, { .texture = rhi::TextureHandle{} }, error);
		EXPECT_FALSE(adopted.IsValid()) << "a view naming no texture was adopted anyway";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST(VulkanAdoption, AdoptsAVideoFrameViewAndSamplerCarryingAConversion)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();
		if (!device.GetCaps().supportsSamplerYcbcrConversion)
		{
			GTEST_SKIP() << "this adapter has no Y'CbCr conversion, so there is nothing to adopt one of";
		}

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;

		vk::SamplerYcbcrConversionCreateInfo conversionInfo{};
		conversionInfo.format		= vk::Format::eG8B8R82Plane420Unorm;
		conversionInfo.ycbcrModel	= vk::SamplerYcbcrModelConversion::eYcbcr709;
		conversionInfo.ycbcrRange	= vk::SamplerYcbcrRange::eItuNarrow;
		conversionInfo.chromaFilter = vk::Filter::eLinear;

		const auto createdConversion = vkDevice.createSamplerYcbcrConversion(conversionInfo, nullptr, dispatch);
		ASSERT_EQ(createdConversion.result, vk::Result::eSuccess);
		const vk::SamplerYcbcrConversion conversion = createdConversion.value;

		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType		= vk::ImageType::e2D;
		imageInfo.format		= vk::Format::eG8B8R82Plane420Unorm;
		imageInfo.extent		= vk::Extent3D{ 4, 4, 1 };
		imageInfo.mipLevels		= 1;
		imageInfo.arrayLayers	= 1;
		imageInfo.samples		= vk::SampleCountFlagBits::e1;
		imageInfo.tiling		= vk::ImageTiling::eOptimal;
		imageInfo.usage			= vk::ImageUsageFlagBits::eSampled;
		imageInfo.sharingMode	= vk::SharingMode::eExclusive;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;

		const auto createdImage = vkDevice.createImage(imageInfo, nullptr, dispatch);
		ASSERT_EQ(createdImage.result, vk::Result::eSuccess);
		const vk::Image image = createdImage.value;

		const vk::MemoryRequirements requirements			 = vkDevice.getImageMemoryRequirements(image, dispatch);
		const vk::PhysicalDeviceMemoryProperties memoryProps = native.Value().physicalDevice.getMemoryProperties(dispatch);

		std::uint32_t typeIndex = 0;
		bool foundType			= false;
		for (std::uint32_t i = 0; i < memoryProps.memoryTypeCount; ++i)
		{
			if ((requirements.memoryTypeBits & (1u << i)) != 0)
			{
				typeIndex = i;
				foundType = true;
				break;
			}
		}
		ASSERT_TRUE(foundType);

		const auto allocated = vkDevice.allocateMemory(vk::MemoryAllocateInfo(requirements.size, typeIndex), nullptr, dispatch);
		ASSERT_EQ(allocated.result, vk::Result::eSuccess);
		const vk::DeviceMemory memory = allocated.value;
		ASSERT_EQ(vkDevice.bindImageMemory(image, memory, 0, dispatch), vk::Result::eSuccess);

		vk::SamplerYcbcrConversionInfo conversionLink{};
		conversionLink.conversion = conversion;

		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.pNext			  = &conversionLink;
		viewInfo.image			  = image;
		viewInfo.viewType		  = vk::ImageViewType::e2D;
		viewInfo.format			  = vk::Format::eG8B8R82Plane420Unorm;
		viewInfo.subresourceRange = vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

		const auto createdView = vkDevice.createImageView(viewInfo, nullptr, dispatch);
		ASSERT_EQ(createdView.result, vk::Result::eSuccess);
		const vk::ImageView view = createdView.value;

		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.pNext		 = &conversionLink;
		samplerInfo.magFilter	 = vk::Filter::eLinear;
		samplerInfo.minFilter	 = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

		const auto createdSampler = vkDevice.createSampler(samplerInfo, nullptr, dispatch);
		ASSERT_EQ(createdSampler.result, vk::Result::eSuccess);
		const vk::Sampler sampler = createdSampler.value;

		rhi::TextureDesc textureDesc{};
		textureDesc.width  = 4;
		textureDesc.height = 4;
		textureDesc.format = rhi::Format::eG8B8R8Biplanar420UNorm;
		textureDesc.usage  = rhi::TextureUsage::eSampled;

		rhi::Error error{};
		const rhi::TextureHandle adoptedTexture =
			device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = image }, { .desc = textureDesc }, error);
		ASSERT_TRUE(adoptedTexture.IsValid()) << error.message;

		const rhi::TextureViewHandle adoptedView = device.AdoptTextureView<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = view }, { .texture = adoptedTexture, .format = rhi::Format::eG8B8R8Biplanar420UNorm }, error);
		ASSERT_TRUE(adoptedView.IsValid()) << error.message;

		const rhi::SamplerHandle adoptedSampler = device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
		ASSERT_TRUE(adoptedSampler.IsValid()) << error.message;

		const std::array<rhi::SamplerHandle, 1> immutable{ adoptedSampler };
		const std::array<rhi::DescriptorBinding, 1> bindings{
			rhi::DescriptorBinding{
				.binding		   = 0,
				.type			   = rhi::DescriptorType::eCombinedImageSampler,
				.count			   = 1,
				.stages			   = rhi::ShaderStage::eFragment,
				.immutableSamplers = immutable,
			},
		};

		const rhi::DescriptorSetLayoutHandle layout = device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings }, error);
		EXPECT_TRUE(layout.IsValid()) << "a layout baking in an adopted Y'CbCr sampler was refused: " << error.message;

		if (layout.IsValid())
		{
			EXPECT_TRUE(device.Destroy(layout, {}, error)) << error.message;
		}
		EXPECT_TRUE(device.Destroy(adoptedSampler, {}, error)) << error.message;
		EXPECT_TRUE(device.Destroy(adoptedView, {}, error)) << error.message;
		EXPECT_TRUE(device.Destroy(adoptedTexture, {}, error)) << error.message;

		vkDevice.destroySampler(sampler, nullptr, dispatch);
		vkDevice.destroyImageView(view, nullptr, dispatch);
		vkDevice.destroyImage(image, nullptr, dispatch);
		vkDevice.freeMemory(memory, nullptr, dispatch);
		vkDevice.destroySamplerYcbcrConversion(conversion, nullptr, dispatch);

		if (device.GetCaps().reportsValidationMessageCounts)
		{
			EXPECT_EQ(device.GetValidationMessageCounts().errors, 0u) << "adopting a video frame tripped Vulkan validation";
		}
	}

	namespace
	{
		struct OwnedImage final
		{
			vk::Image image;
			vk::DeviceMemory memory;
		};

		[[nodiscard]] OwnedImage MakeImage(const vk::Device vkDevice, const vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch)
		{
			vk::ImageCreateInfo imageInfo{};
			imageInfo.imageType		= vk::ImageType::e2D;
			imageInfo.format		= vk::Format::eR8G8B8A8Unorm;
			imageInfo.extent		= vk::Extent3D{ 4, 4, 1 };
			imageInfo.mipLevels		= 1;
			imageInfo.arrayLayers	= 1;
			imageInfo.samples		= vk::SampleCountFlagBits::e1;
			imageInfo.tiling		= vk::ImageTiling::eOptimal;
			imageInfo.usage			= vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
			imageInfo.sharingMode	= vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;

			const auto created = vkDevice.createImage(imageInfo, nullptr, dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return {};
			}

			const vk::MemoryRequirements requirements	   = vkDevice.getImageMemoryRequirements(created.value, dispatch);
			const vk::PhysicalDeviceMemoryProperties props = phys.getMemoryProperties(dispatch);

			std::uint32_t typeIndex = 0;
			for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
			{
				if ((requirements.memoryTypeBits & (1u << i)) != 0)
				{
					typeIndex = i;
					break;
				}
			}

			const auto allocated = vkDevice.allocateMemory(vk::MemoryAllocateInfo(requirements.size, typeIndex), nullptr, dispatch);
			if (allocated.result != vk::Result::eSuccess)
			{
				vkDevice.destroyImage(created.value, nullptr, dispatch);
				return {};
			}

			static_cast<void>(vkDevice.bindImageMemory(created.value, allocated.value, 0, dispatch));
			return OwnedImage{ .image = created.value, .memory = allocated.value };
		}

		[[nodiscard]] rhi::TextureDesc SharedTextureDesc()
		{
			rhi::TextureDesc desc{};
			desc.width	= 4;
			desc.height = 4;
			desc.format = rhi::Format::eRGBA8UNorm;
			desc.usage	= rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopySrc | rhi::TextureUsage::eCopyDst;
			return desc;
		}
	}

	TEST(VulkanAdoption, ABarrierNamingTheDeclaredStateAndFamilyIsAccepted)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.Value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		const rhi::ResourceState arrived{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc				  = SharedTextureDesc(),
				.initialState	  = arrived,
				.initialOwnership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } },
			error);
		ASSERT_TRUE(adopted.IsValid()) << error.message;

		rhi::CommandPool pool = device.CreateCommandPool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.IsValid()) << error.message;
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.adoptedAcquire", error);
		ASSERT_TRUE(list.IsValid()) << error.message;
		ASSERT_TRUE(list.Begin(error)) << error.message;

		const std::array acquire{ rhi::TextureBarrier{
			.texture   = adopted,
			.before	   = arrived,
			.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.ownership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute },
		} };

		EXPECT_TRUE(list.Barriers(rhi::BarrierBatch{ .textures = acquire }, error))
			<< "a barrier naming exactly what the adoption declared was refused: " << error.message;

		static_cast<void>(list.End(error));
		EXPECT_TRUE(device.Destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	TEST(VulkanAdoption, ABarrierNamingAStateTheObjectDidNotArriveInIsRefused)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned							  = std::move(created).Value();
		rhi::Device device								  = owned.Get();
		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.Value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		const rhi::ResourceState arrived{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.AdoptTexture<rhi::VulkanApi>(
			rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image }, { .desc = SharedTextureDesc(), .initialState = arrived }, error);
		ASSERT_TRUE(adopted.IsValid()) << error.message;

		rhi::CommandPool pool = device.CreateCommandPool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.IsValid()) << error.message;
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.adoptedWrongState", error);
		ASSERT_TRUE(list.IsValid()) << error.message;
		ASSERT_TRUE(list.Begin(error)) << error.message;

		const std::array wrong{ rhi::TextureBarrier{
			.texture = adopted,
			.before	 = { .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading },
			.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
		} };

		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .textures = wrong }, error))
			<< "a barrier claiming a state the object never arrived in was accepted, so the declaration is not read";

		static_cast<void>(list.End(error));
		EXPECT_TRUE(device.Destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	TEST(VulkanAdoption, ABarrierReleasingFromTheWrongFamilyIsRefused)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned							  = std::move(created).Value();
		rhi::Device device								  = owned.Get();
		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue());

		const vk::Device vkDevice						   = native.Value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.Value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc = SharedTextureDesc(), .initialOwnership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } },
			error);
		ASSERT_TRUE(adopted.IsValid()) << error.message;

		rhi::CommandPool pool = device.CreateCommandPool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.IsValid()) << error.message;
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.adoptedWrongFamily", error);
		ASSERT_TRUE(list.IsValid()) << error.message;
		ASSERT_TRUE(list.Begin(error)) << error.message;

		const std::array wrong{ rhi::TextureBarrier{
			.texture   = adopted,
			.before	   = {},
			.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.ownership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCopy },
		} };

		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .textures = wrong }, error))
			<< "a barrier acquired the object from a queue it was never declared to be owned by";

		static_cast<void>(list.End(error));
		EXPECT_TRUE(device.Destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	namespace
	{
		template <typename Value>
		void ExpectFormsAgree(
			const rhi::CString operation, const bool plainSucceeded, const bool erroredSucceeded, const rhi::Error & error, const rhi::Result<Value> & resulted)
		{
			SCOPED_TRACE(operation);

			EXPECT_EQ(plainSucceeded, erroredSucceeded) << "the form carrying no diagnostic disagreed with the one that does";
			EXPECT_EQ(erroredSucceeded, resulted.HasValue()) << "which form the caller reached for decided whether the call was reported as done";
			EXPECT_EQ(error.code, resulted.HasValue() ? rhi::ErrorCode::eOk : resulted.GetError().code) << "the two diagnostic forms named different codes";
		}
	}

	TEST(VulkanAdoption, EveryTemplatedEntryAgreesAcrossItsForms)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		rhi::Error error{};

		ExpectFormsAgree("Device::AdoptBuffer",
			device.AdoptBuffer<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptBuffer<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptBufferWithResult<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {}));

		ExpectFormsAgree("Device::AdoptTexture",
			device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptTextureWithResult<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {}));

		ExpectFormsAgree("Device::AdoptTextureView",
			device.AdoptTextureView<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptTextureView<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptTextureViewWithResult<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {}));

		ExpectFormsAgree("Device::AdoptSampler",
			device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptSamplerWithResult<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}));

		ExpectFormsAgree("Device::AdoptTimeline",
			device.AdoptTimeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptTimeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptTimelineWithResult<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {}));

		ExpectFormsAgree("Device::AdoptBinarySemaphore",
			device.AdoptBinarySemaphore<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {}).IsValid(),
			device.AdoptBinarySemaphore<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {}, error).IsValid(),
			error,
			device.AdoptBinarySemaphoreWithResult<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {}));

		rhi::NativeBuffer<rhi::VulkanApi> readBackBuffer{};
		ExpectFormsAgree("Device::GetNativeBuffer",
			device.GetNativeBuffer<rhi::VulkanApi>(rhi::BufferHandle{}, readBackBuffer),
			device.GetNativeBuffer<rhi::VulkanApi>(rhi::BufferHandle{}, readBackBuffer, error),
			error,
			device.GetNativeBufferWithResult<rhi::VulkanApi>(rhi::BufferHandle{}));

		rhi::NativeTexture<rhi::VulkanApi> readBackTexture{};
		ExpectFormsAgree("Device::GetNativeTexture",
			device.GetNativeTexture<rhi::VulkanApi>(rhi::TextureHandle{}, readBackTexture),
			device.GetNativeTexture<rhi::VulkanApi>(rhi::TextureHandle{}, readBackTexture, error),
			error,
			device.GetNativeTextureWithResult<rhi::VulkanApi>(rhi::TextureHandle{}));

		rhi::NativeTextureView<rhi::VulkanApi> readBackView{};
		ExpectFormsAgree("Device::GetNativeTextureView",
			device.GetNativeTextureView<rhi::VulkanApi>(rhi::TextureViewHandle{}, readBackView),
			device.GetNativeTextureView<rhi::VulkanApi>(rhi::TextureViewHandle{}, readBackView, error),
			error,
			device.GetNativeTextureViewWithResult<rhi::VulkanApi>(rhi::TextureViewHandle{}));

		rhi::NativeSampler<rhi::VulkanApi> readBackSampler{};
		ExpectFormsAgree("Device::GetNativeSampler",
			device.GetNativeSampler<rhi::VulkanApi>(rhi::SamplerHandle{}, readBackSampler),
			device.GetNativeSampler<rhi::VulkanApi>(rhi::SamplerHandle{}, readBackSampler, error),
			error,
			device.GetNativeSamplerWithResult<rhi::VulkanApi>(rhi::SamplerHandle{}));

		rhi::NativeTimeline<rhi::VulkanApi> readBackTimeline{};
		ExpectFormsAgree("Device::GetNativeTimeline",
			device.GetNativeTimeline<rhi::VulkanApi>(rhi::TimelineHandle{}, readBackTimeline),
			device.GetNativeTimeline<rhi::VulkanApi>(rhi::TimelineHandle{}, readBackTimeline, error),
			error,
			device.GetNativeTimelineWithResult<rhi::VulkanApi>(rhi::TimelineHandle{}));

		rhi::NativeBinarySemaphore<rhi::VulkanApi> readBackSemaphore{};
		ExpectFormsAgree("Device::GetNativeBinarySemaphore",
			device.GetNativeBinarySemaphore<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{}, readBackSemaphore),
			device.GetNativeBinarySemaphore<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{}, readBackSemaphore, error),
			error,
			device.GetNativeBinarySemaphoreWithResult<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{}));
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(AdoptionCapability, AReportedCapabilityIsOneTheEntriesHonor)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		rhi::Error error{};
		const rhi::SamplerHandle adopted = device.AdoptSampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error);
		EXPECT_FALSE(adopted.IsValid());

		if (device.GetCaps().supportsResourceAdoption)
		{
			EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature)
				<< "a device reporting adoption declined the surface as absent, so the block was not published";
		}
		else
		{
			EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "a device reporting no adoption accepted the surface anyway";
		}
	}

	TEST(AdoptionCapability, TheConservativeRasterTierAgreesWithWhatTheDriverAdvertises)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a Vulkan device did not hand back its native handles";

		const auto extensions = native.Value().physicalDevice.enumerateDeviceExtensionProperties(nullptr, *native.Value().dispatch);
		ASSERT_EQ(extensions.result, vk::Result::eSuccess);

		const bool advertised = std::ranges::any_of(extensions.value,
			[](const vk::ExtensionProperties & extension) noexcept
			{
				return std::string_view{ extension.extensionName } == VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME;
			});

		const bool reported = device.GetCaps().conservativeRasterTier != rhi::ConservativeRasterTier::eNone;
		EXPECT_EQ(reported, advertised) << (advertised ? "the driver advertises conservative rasterization and the tier reports none"
													   : "a tier was reported on a driver without the extension");
	}

	TEST(VulkanAdoption, AQueueExposesItsFamilyIndexThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a Vulkan device did not hand back its native handles";

		const rhi::Result<rhi::native::VulkanQueueView> view = rhi::GetVulkanQueueView(device.GetQueue(rhi::QueueType::eGraphics));
		ASSERT_TRUE(view.HasValue()) << "a Vulkan graphics queue did not hand back a queue view";

		EXPECT_NE(static_cast<VkQueue>(view.Value().queue), VK_NULL_HANDLE) << "the queue view carries no queue";
		EXPECT_EQ(view.Value().familyIndex, native.Value().graphicsQueueFamily)
			<< "the family index on the queue view disagrees with the one the device reports for its graphics queue";
	}

	TEST(VulkanAdoption, ANativeScopeRecordsIntoTheCommandBufferTheAccessorReports)
	{
		ExpectANativeScopeSeesTheBackendsOwnCommandList<rhi::VulkanApi>(
			[](const rhi::native::VulkanCommandListView & view)
			{
				return view.commandBuffer;
			},
			[](rhi::CommandList list)
			{
				return rhi::GetVulkanCommandBuffer(list);
			});
	}

	TEST(VulkanRayTracingUsage, RefusesABufferOnlyRayTracingCouldUse)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();
		if (device.GetCaps().supportsRayTracing)
		{
			GTEST_SKIP() << "this Vulkan device reports ray tracing, so the refusal under test no longer applies";
		}

		for (const rhi::BufferUsage usage :
			{ rhi::BufferUsage::eAccelerationStructureStorage, rhi::BufferUsage::eAccelerationStructureInput, rhi::BufferUsage::eShaderBindingTable })
		{
			rhi::BufferDesc desc{};
			desc.size  = 256;
			desc.usage = usage;

			rhi::Error error{};
			EXPECT_FALSE(device.CreateBuffer(desc, error).IsValid()) << "a ray tracing buffer usage was accepted on a device that declines ray tracing";
			EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature);
		}

		rhi::BufferDesc ordinary{};
		ordinary.size  = 256;
		ordinary.usage = rhi::BufferUsage::eStorage;

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.CreateBuffer(ordinary, error);
		EXPECT_TRUE(buffer.IsValid()) << "an ordinary storage buffer was refused";
		if (buffer.IsValid())
		{
			EXPECT_TRUE(device.Destroy(buffer, {}, error));
		}
	}

	TEST(VulkanConfigBlock, ADeviceVersionTheAdapterCannotMeetIsRefused)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.GetError());
		}

		rhi::native::VulkanDeviceConfig pastTheAdapter{};
		pastTheAdapter.deviceVersion = rhi::ApiVersion{ .major = 1, .minor = 9 };

		const rhi::Result<rhi::UniqueDevice> refused = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::id, pastTheAdapter);
		EXPECT_FALSE(refused.HasValue()) << "a device version past what the adapter supports was accepted";
		if (!refused.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(refused.GetError()));
		}

		rhi::native::VulkanDeviceConfig belowTheFloor{};
		belowTheFloor.deviceVersion = rhi::ApiVersion{ .major = 1, .minor = 1 };

		const rhi::Result<rhi::UniqueDevice> floored = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::id, belowTheFloor);
		EXPECT_FALSE(floored.HasValue()) << "a device version below the 1.2 floor was accepted";
	}

	TEST(VulkanConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.GetError());
		}

		rhi::native::VulkanDeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::id, truncated);
		EXPECT_FALSE(tooShort.HasValue()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.HasValue())
		{
			EXPECT_EQ(tooShort.GetError().code, rhi::ErrorCode::eInvalidArgument);
		}

		rhi::native::VulkanDeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::id, otherVersion);
		EXPECT_FALSE(wrongVersion.HasValue()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.HasValue())
		{
			EXPECT_EQ(wrongVersion.GetError().code, rhi::ErrorCode::eInvalidArgument);
		}
	}

	TEST(VulkanConfigBlock, ADeviceExtensionTheAdapterDoesNotAdvertiseIsRefused)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.GetError());
		}

		static constexpr std::array<const char * const, 1> kNoSuchExtension{ "VK_AZO_not_an_extension" };

		rhi::native::VulkanDeviceConfig asking{};
		asking.deviceExtensions = kNoSuchExtension;

		const rhi::Result<rhi::UniqueDevice> refused = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::id, asking);
		EXPECT_FALSE(refused.HasValue()) << "an extension no adapter advertises was accepted, so the block's list is being dropped";
		if (!refused.HasValue())
		{
			EXPECT_EQ(refused.GetError().code, rhi::ErrorCode::eUnsupportedFeature);
		}
	}

#endif

#if defined(AZOTH_RHI_TEST_ADOPTION_METAL3) && defined(AZOTH_RHI_TEST_ADOPTION_METAL4)

	TEST(MetalConfigBlock, EitherGenerationComesUpWithNoBlockAtAll)
	{
		rhi::DeviceDesc plain{};
		plain.validation = rhi::ValidationMode::eDeveloper;

		if (rhi::Result<rhi::UniqueDevice> three = rhi::CreateDevice<rhi::MetalApi>(plain); !three.HasValue())
		{
			GTEST_SKIP() << "no Metal 3 device on this machine: " << test::Describe(three.GetError());
		}

		rhi::Result<rhi::UniqueDevice> four = rhi::CreateDevice<rhi::Metal4Api>(plain);
		if (!four.HasValue() && four.GetError().code == rhi::ErrorCode::eUnsupportedFeature)
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(four.GetError());
		}

		EXPECT_TRUE(four.HasValue()) << "a Metal 4 device carrying no configuration block was refused";
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL3

	TEST(MetalConfigBlock, RefusesABlockPinningItToTheOtherGeneration)
	{
		rhi::native::MetalDeviceConfig pinnedToFour{};
		pinnedToFour.generation = rhi::ApiVersion{ .major = 4, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> three = CreateWith<rhi::MetalApi>(rhi::MetalApi::id, pinnedToFour);
		EXPECT_FALSE(three.HasValue()) << "Metal 3 accepted a block pinning it to a generation it is not";
		if (!three.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(three.GetError()));
		}
	}

	TEST(MetalConfigBlock, AGenerationIgnoresABlockKeyedToTheOther)
	{
		rhi::native::Metal4DeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> three = CreateWith<rhi::MetalApi>(rhi::Metal4Api::id, pinnedToThree);
		EXPECT_TRUE(three.HasValue()) << "Metal 3 read a block keyed to Metal 4, so the entry is not matched on its api field";
	}

	TEST(MetalConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		rhi::native::MetalDeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::MetalApi>(rhi::MetalApi::id, truncated);
		EXPECT_FALSE(tooShort.HasValue()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(tooShort.GetError()));
		}

		rhi::native::MetalDeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::MetalApi>(rhi::MetalApi::id, otherVersion);
		EXPECT_FALSE(wrongVersion.HasValue()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(wrongVersion.GetError()));
		}
	}

	TEST(MetalAdoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::MetalApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Metal 3 device on this machine: " << test::Describe(created.GetError());
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::MetalNativeDevice> native = rhi::GetMetalNativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a Metal 3 device did not hand back its native handles";

		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			const rhi::Result<rhi::native::MetalQueueView> view = rhi::GetMetalQueueView(device.GetQueue(type));
			ASSERT_TRUE(view.HasValue()) << "a Metal 3 queue did not hand back a queue view";
			EXPECT_NE(view.Value().queue, nullptr) << "the queue view carries no command queue";

			if (type == rhi::QueueType::eGraphics)
			{
				EXPECT_EQ(view.Value().queue, native.Value().queue) << "the graphics queue view disagrees with the queue the device reports";
			}
			else
			{
				EXPECT_NE(view.Value().queue, native.Value().queue) << "a queue of another type answered with the graphics queue";
			}
		}
	}

	TEST(MetalAdoption, ANativeScopeRecordsIntoTheCommandBufferTheAccessorReports)
	{
		ExpectANativeScopeSeesTheBackendsOwnCommandList<rhi::MetalApi>(
			[](const rhi::native::MetalCommandListView & view)
			{
				return view.commandBuffer;
			},
			[](rhi::CommandList list)
			{
				return rhi::GetMetalCommandBuffer(list);
			});
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL4

	TEST(Metal4ConfigBlock, RefusesABlockPinningItToTheOtherGeneration)
	{
		rhi::native::Metal4DeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> four = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::id, pinnedToThree);
		EXPECT_FALSE(four.HasValue()) << "Metal 4 accepted a block pinning it to a generation it is not";
		if (!four.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(four.GetError()));
		}
	}

	TEST(Metal4ConfigBlock, AGenerationIgnoresABlockKeyedToTheOther)
	{
		rhi::DeviceDesc plain{};
		plain.validation = rhi::ValidationMode::eDeveloper;

		if (rhi::Result<rhi::UniqueDevice> unconfigured = rhi::CreateDevice<rhi::Metal4Api>(plain); !unconfigured.HasValue())
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(unconfigured.GetError());
		}

		rhi::native::MetalDeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> four = CreateWith<rhi::Metal4Api>(rhi::MetalApi::id, pinnedToThree);
		EXPECT_TRUE(four.HasValue()) << "Metal 4 read a block keyed to Metal 3, so the entry is not matched on its api field";
	}

	TEST(Metal4ConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		rhi::native::Metal4DeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::id, truncated);
		EXPECT_FALSE(tooShort.HasValue()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(tooShort.GetError()));
		}

		rhi::native::Metal4DeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::id, otherVersion);
		EXPECT_FALSE(wrongVersion.HasValue()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.HasValue())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(wrongVersion.GetError()));
		}
	}

	TEST(Metal4Adoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::Metal4Api>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(created.GetError());
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::Metal4NativeDevice> native = rhi::GetMetal4NativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a Metal 4 device did not hand back its native handles";

		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			const rhi::Result<rhi::native::Metal4QueueView> view = rhi::GetMetal4QueueView(device.GetQueue(type));
			ASSERT_TRUE(view.HasValue()) << "a Metal 4 queue did not hand back a queue view";
			EXPECT_NE(view.Value().queue, nullptr) << "the queue view carries no command queue";

			if (type == rhi::QueueType::eGraphics)
			{
				EXPECT_EQ(view.Value().queue, native.Value().queue) << "the graphics queue view disagrees with the queue the device reports";
			}
			else
			{
				EXPECT_NE(view.Value().queue, native.Value().queue) << "a queue of another type answered with the graphics queue";
			}
		}
	}

	TEST(Metal4Adoption, ANativeScopeRecordsIntoTheCommandBufferTheAccessorReports)
	{
		ExpectANativeScopeSeesTheBackendsOwnCommandList<rhi::Metal4Api>(
			[](const rhi::native::Metal4CommandListView & view)
			{
				return view.commandBuffer;
			},
			[](rhi::CommandList list)
			{
				return rhi::GetMetal4CommandBuffer(list);
			});
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12

	TEST(D3D12Adoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::D3D12Api>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no D3D12 device on this machine: " << test::Describe(created.GetError());
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		const rhi::Result<rhi::D3D12NativeDevice> native = rhi::GetD3D12NativeDevice(device);
		ASSERT_TRUE(native.HasValue()) << "a D3D12 device did not hand back its native handles";

		const std::array expected{ std::pair{ rhi::QueueType::eGraphics, native.Value().graphicsQueue },
			std::pair{ rhi::QueueType::eCompute, native.Value().computeQueue },
			std::pair{ rhi::QueueType::eCopy, native.Value().copyQueue } };

		for (const auto [type, reported] : expected)
		{
			const rhi::Result<rhi::native::D3D12QueueView> view = rhi::GetD3D12QueueView(device.GetQueue(type));
			ASSERT_TRUE(view.HasValue()) << "a D3D12 queue did not hand back a queue view";
			EXPECT_NE(view.Value().queue, nullptr) << "the queue view carries no command queue";
			EXPECT_EQ(view.Value().queue, reported) << "the queue view disagrees with the queue the device reports for this type";
		}
	}

	TEST(D3D12Adoption, ANativeScopeRecordsIntoTheCommandListTheAccessorReports)
	{
		ExpectANativeScopeSeesTheBackendsOwnCommandList<rhi::D3D12Api>(
			[](const rhi::native::D3D12CommandListView & view)
			{
				return view.commandList;
			},
			[](rhi::CommandList list)
			{
				return rhi::GetD3D12CommandList(list);
			});
	}

#endif

}

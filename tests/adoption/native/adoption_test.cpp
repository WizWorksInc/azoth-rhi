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

#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN
	#include "azoth/rhi/native/vulkan_native.hpp"
#endif

namespace rhi = azo::rhi;

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

		// Must stay after the caller's own destroys. Asserted before them it catches nothing, since a double free only surfaces on the second destroy.
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

		// The view and the sampler both chain the same conversion, which is what Vulkan requires of the pair.
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

		// The step the whole thing exists for: the conversion sampler baked into the layout, which is the only way Vulkan lets one be bound.
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

		// All four still the caller's, in the order Vulkan requires.
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
	} // namespace

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

		const std::uint32_t producerFamily = device.GetQueue(rhi::QueueType::eGraphics).GetFamilyIndex() + 1;
		const rhi::ResourceState arrived{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.AdoptTexture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc = SharedTextureDesc(), .initialState = arrived, .initialQueueFamily = producerFamily },
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
			.after	   = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead, .layout = rhi::TextureLayout::eCopySrc },
			.ownership = { .src = producerFamily, .dst = device.GetQueue(rhi::QueueType::eGraphics).GetFamilyIndex() },
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

		const rhi::ResourceState arrived{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};

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
			.before	 = { .stages = rhi::PipelineStage::eFragmentShader, .access = rhi::Access::eShaderRead, .layout = rhi::TextureLayout::eShaderReadOnly },
			.after	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead, .layout = rhi::TextureLayout::eCopySrc },
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

		const std::uint32_t here		   = device.GetQueue(rhi::QueueType::eGraphics).GetFamilyIndex();
		const std::uint32_t producerFamily = here + 1;

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.AdoptTexture<rhi::VulkanApi>(
			rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image }, { .desc = SharedTextureDesc(), .initialQueueFamily = producerFamily }, error);
		ASSERT_TRUE(adopted.IsValid()) << error.message;

		rhi::CommandPool pool = device.CreateCommandPool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.IsValid()) << error.message;
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.adoptedWrongFamily", error);
		ASSERT_TRUE(list.IsValid()) << error.message;
		ASSERT_TRUE(list.Begin(error)) << error.message;

		const std::array wrong{ rhi::TextureBarrier{
			.texture   = adopted,
			.before	   = {},
			.after	   = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead, .layout = rhi::TextureLayout::eCopySrc },
			.ownership = { .src = producerFamily + 1, .dst = here },
		} };

		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .textures = wrong }, error))
			<< "a barrier released the object from a family it was never declared to be owned by";

		static_cast<void>(list.End(error));
		EXPECT_TRUE(device.Destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	namespace
	{
		// Nothing else instantiates the WithResult bodies, so one wired to the wrong operation compiles until a consumer writes the first call. error is a
		// reference because a copy could be taken before the sibling call that fills it.
		template <typename Value>
		void ExpectFormsAgree(
			const rhi::CString operation, const bool plainSucceeded, const bool erroredSucceeded, const rhi::Error & error, const rhi::Result<Value> & resulted)
		{
			SCOPED_TRACE(operation);

			EXPECT_EQ(plainSucceeded, erroredSucceeded) << "the form carrying no diagnostic disagreed with the one that does";
			EXPECT_EQ(erroredSucceeded, resulted.HasValue()) << "which form the caller reached for decided whether the call was reported as done";
			EXPECT_EQ(error.code, resulted.HasValue() ? rhi::ErrorCode::eOk : resulted.GetError().code) << "the two diagnostic forms named different codes";
		}
	} // namespace

	TEST(VulkanAdoption, EveryTemplatedEntryAgreesAcrossItsForms)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.HasValue())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).Value();
		rhi::Device device		= owned.Get();

		// Empty payloads and handles this device never handed out, so every entry takes its declining path and none of them adopts an object the sweep would
		// then have to give back.
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

#endif // AZOTH_RHI_TEST_ADOPTION_VULKAN

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

#endif // AZOTH_RHI_TEST_ADOPTION_VULKAN

} // namespace

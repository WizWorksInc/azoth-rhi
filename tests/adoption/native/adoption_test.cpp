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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint> // NOLINT
#include <string_view>
#include <utility>

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN
	#include "azoth/rhi/native/vulkan_config.hpp"
	#include "azoth/rhi/native/vulkan_native.hpp"
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_METAL
	#include "azoth/rhi/native/metal_config.hpp"
	#include "azoth/rhi/native/metal_native.hpp"

	#include "conformance/samples.hpp"

	#include <objc/message.h>
	#include <objc/runtime.h>

	#include <bit>
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12
	#include "azoth/rhi/native/d3d12_native.hpp"
#endif

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	// These are plain TESTs, so they name their backend themselves and miss the selection AZO_RHI_BACKEND_SUITE applies to a parameterised one. Refusing here
	// rather than in each test puts the gate on the one door they all go through, and a run pinned to one backend stops reaching for another.
	template <rhi::GraphicsApiTag Api>
	[[nodiscard]] bool ApiIsSelected() noexcept
	{
		const test::Backend * backend = test::FindBackend(Api::kId);
		return backend != nullptr && test::BackendIsSelected(backend->shortName);
	}

	template <rhi::GraphicsApiTag Api>
	[[nodiscard]] rhi::Result<rhi::UniqueDevice> MakeDevice()
	{
		if (!ApiIsSelected<Api>())
		{
			return rhi::Error{ .code = rhi::ErrorCode::eUnsupportedFeature, .message = "this backend is not among the ones this run was asked for" };
		}

		static constexpr std::array<rhi::DeviceFeature, 1> kPreferred{ rhi::DeviceFeature::eSamplerYcbcrConversion };

		rhi::DeviceDesc desc{};
		desc.validation		   = rhi::ValidationMode::eDeveloper;
		desc.preferredFeatures = kPreferred;
		return rhi::create_device<Api>(desc);
	}

#if defined(AZOTH_RHI_TEST_ADOPTION_VULKAN) || defined(AZOTH_RHI_TEST_ADOPTION_METAL)

	template <rhi::GraphicsApiTag Api, class Config>
	[[nodiscard]] rhi::Result<rhi::UniqueDevice> CreateWith(const rhi::GraphicsApiId key, const Config & config)
	{
		if (!ApiIsSelected<Api>())
		{
			return rhi::Error{ .code = rhi::ErrorCode::eUnsupportedFeature, .message = "this backend is not among the ones this run was asked for" };
		}

		const std::array<rhi::DeviceConfigEntry, 1> entries{ rhi::DeviceConfigEntry{ .api = key, .config = &config } };

		rhi::DeviceDesc desc{};
		desc.validation		= rhi::ValidationMode::eDeveloper;
		desc.backendConfigs = entries;
		return rhi::create_device<Api>(desc);
	}

#endif

	template <rhi::GraphicsApiTag Api>
	class SelectedApiTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			if (!ApiIsSelected<Api>())
			{
				GTEST_SKIP() << "this backend is not among the ones AZOTH_RHI_TEST_BACKENDS asked for";
			}

			const auto plain = MakeDevice<Api>();
			if (!plain.has_value())
			{
				const test::Backend * backend = test::FindBackend(Api::kId);
				if (test::BackendIsRequired(backend->shortName) && !test::NoAdapterHere(plain.get_error()))
				{
					FAIL() << backend->displayName << " is required but produced no device: " << test::Describe(plain.get_error());
				}
				GTEST_SKIP() << backend->displayName << " produced no device on this machine: " << test::Describe(plain.get_error());
			}
		}
	};

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN
	using VulkanConfigBlock = SelectedApiTest<rhi::VulkanApi>;
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_METAL3
	using MetalConfigBlock = SelectedApiTest<rhi::MetalApi>;
#endif
#ifdef AZOTH_RHI_TEST_ADOPTION_METAL4
	using Metal4ConfigBlock = SelectedApiTest<rhi::Metal4Api>;
#endif

#if defined(AZOTH_RHI_TEST_ADOPTION_VULKAN) || defined(AZOTH_RHI_TEST_ADOPTION_METAL) || defined(AZOTH_RHI_TEST_ADOPTION_D3D12)

	template <rhi::GraphicsApiTag Api, class FromView, class FromAccessor>
	void ExpectANativeScopeSeesTheBackendsOwnCommandList(FromView fromView, FromAccessor fromAccessor)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<Api>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no device for this backend on this machine: " << test::Describe(created.get_error());
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		rhi::Error error{};
		rhi::CommandPool pool = device.create_command_pool(rhi::CommandPoolDesc{ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.nativeScope", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		ASSERT_TRUE(static_cast<bool>(fromAccessor(list))) << "the accessor reports no native command list for an open recording";

		bool ran = false;
		EXPECT_TRUE(
			test::Ok(
				list.modify_native<Api>(
					rhi::NativeMutationDesc{},
					[&](const auto & view)
					{
						ran = true;
						EXPECT_EQ(fromView(view), fromAccessor(list))
							<< "the native scope handed back an object the accessor for the same command list does not agree with, which is what "
							   "casting the facade impl without resolving the validation decorator produces";
					},
					error
				),
				error
			)
		);
		EXPECT_TRUE(ran) << "the callback the scope brackets never ran";
		EXPECT_TRUE(test::Ok(list.end(error), error));
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(VulkanAdoption, AdoptsAnImageAViewAndASamplerAndLeavesThemForTheCallerToDestroy)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a Vulkan device did not hand back its native handles";

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;

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
		const vk::PhysicalDeviceMemoryProperties memoryProps = native.value().physicalDevice.getMemoryProperties(dispatch);

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
			device.adopt_texture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = image }, { .desc = textureDesc }, error);
		ASSERT_TRUE(adoptedTexture.is_valid()) << error.message;

		const rhi::TextureViewHandle adoptedView = device.adopt_texture_view<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = view },
			{ .texture = adoptedTexture, .format = rhi::Format::eRGBA8UNorm },
			error
		);
		ASSERT_TRUE(adoptedView.is_valid()) << error.message;

		const rhi::SamplerHandle adoptedSampler = device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
		ASSERT_TRUE(adoptedSampler.is_valid()) << error.message;

		rhi::NativeTexture<rhi::VulkanApi> readBack{};
		EXPECT_TRUE(device.get_native_texture<rhi::VulkanApi>(adoptedTexture, readBack, error)) << error.message;
		EXPECT_EQ(readBack.image, image) << "the native read handed back a different image than was adopted";

		EXPECT_TRUE(device.destroy(adoptedView, {}, error)) << error.message;
		EXPECT_TRUE(device.destroy(adoptedSampler, {}, error)) << error.message;
		EXPECT_TRUE(device.destroy(adoptedTexture, {}, error)) << error.message;

		vkDevice.destroySampler(sampler, nullptr, dispatch);
		vkDevice.destroyImageView(view, nullptr, dispatch);
		vkDevice.destroyImage(image, nullptr, dispatch);
		vkDevice.freeMemory(memory, nullptr, dispatch);

		if (device.get_caps().reportsValidationMessageCounts)
		{
			EXPECT_EQ(device.get_validation_message_counts().errors, 0u)
				<< "adoption produced Vulkan validation errors, which is what freeing an adopted object looks like from here";
		}
	}

	TEST(VulkanAdoption, RepeatedAdoptAndDestroyReturnsTheSlots)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;

		const auto createdSampler = vkDevice.createSampler(vk::SamplerCreateInfo{}, nullptr, dispatch);
		ASSERT_EQ(createdSampler.result, vk::Result::eSuccess);
		const vk::Sampler sampler = createdSampler.value;

		rhi::Error error{};
		for (int round = 0; round < 8; ++round)
		{
			const rhi::SamplerHandle adopted = device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
			ASSERT_TRUE(adopted.is_valid()) << "round " << round << ": " << error.message;
			ASSERT_TRUE(device.destroy(adopted, {}, error)) << "round " << round << ": " << error.message;
		}

		vkDevice.destroySampler(sampler, nullptr, dispatch);
	}

	TEST(VulkanAdoption, AdoptsATimelineSemaphore)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;

		const vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 7);
		const auto createdSemaphore = vkDevice.createSemaphore(vk::SemaphoreCreateInfo({}, &typeInfo), nullptr, dispatch);
		ASSERT_EQ(createdSemaphore.result, vk::Result::eSuccess);
		const vk::Semaphore semaphore = createdSemaphore.value;

		rhi::Error error{};
		const rhi::TimelineHandle adopted = device.adopt_timeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{ .semaphore = semaphore }, {}, error);
		ASSERT_TRUE(adopted.is_valid()) << error.message;

		std::uint64_t value = 0;
		EXPECT_TRUE(device.get_queue(rhi::QueueType::eGraphics).get_completed_value(adopted, value, error)) << error.message;
		EXPECT_EQ(value, 7u) << "an adopted timeline did not carry the value its producer left it at";

		EXPECT_TRUE(device.destroy(adopted, {}, error)) << error.message;
		vkDevice.destroySemaphore(semaphore, nullptr, dispatch);
	}

	TEST(VulkanAdoption, RefusesAPayloadCarryingNoObject)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		rhi::Error error{};
		const rhi::SamplerHandle adopted = device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error);
		EXPECT_FALSE(adopted.is_valid()) << "a null VkSampler was adopted anyway";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST(VulkanAdoption, RefusesAViewNamingATextureThisDeviceNeverHandedOut)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		rhi::Error error{};
		const rhi::TextureViewHandle adopted = device.adopt_texture_view<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = vk::ImageView{} },
			{ .texture = rhi::TextureHandle{} },
			error
		);
		EXPECT_FALSE(adopted.is_valid()) << "a view naming no texture was adopted anyway";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

	TEST(VulkanAdoption, AdoptsAVideoFrameViewAndSamplerCarryingAConversion)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();
		if (!device.get_caps().supportsSamplerYcbcrConversion)
		{
			GTEST_SKIP() << "this adapter has no Y'CbCr conversion, so there is nothing to adopt one of";
		}

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;

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
		const vk::PhysicalDeviceMemoryProperties memoryProps = native.value().physicalDevice.getMemoryProperties(dispatch);

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
			device.adopt_texture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{ .image = image }, { .desc = textureDesc }, error);
		ASSERT_TRUE(adoptedTexture.is_valid()) << error.message;

		const rhi::TextureViewHandle adoptedView = device.adopt_texture_view<rhi::VulkanApi>(
			rhi::NativeTextureView<rhi::VulkanApi>{ .view = view },
			{ .texture = adoptedTexture, .format = rhi::Format::eG8B8R8Biplanar420UNorm },
			error
		);
		ASSERT_TRUE(adoptedView.is_valid()) << error.message;

		const rhi::SamplerHandle adoptedSampler = device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{ .sampler = sampler }, {}, error);
		ASSERT_TRUE(adoptedSampler.is_valid()) << error.message;

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

		const rhi::DescriptorSetLayoutHandle layout = device.create_descriptor_set_layout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings }, error);
		EXPECT_TRUE(layout.is_valid()) << "a layout baking in an adopted Y'CbCr sampler was refused: " << error.message;

		if (layout.is_valid())
		{
			EXPECT_TRUE(device.destroy(layout, {}, error)) << error.message;
		}
		EXPECT_TRUE(device.destroy(adoptedSampler, {}, error)) << error.message;
		EXPECT_TRUE(device.destroy(adoptedView, {}, error)) << error.message;
		EXPECT_TRUE(device.destroy(adoptedTexture, {}, error)) << error.message;

		vkDevice.destroySampler(sampler, nullptr, dispatch);
		vkDevice.destroyImageView(view, nullptr, dispatch);
		vkDevice.destroyImage(image, nullptr, dispatch);
		vkDevice.freeMemory(memory, nullptr, dispatch);
		vkDevice.destroySamplerYcbcrConversion(conversion, nullptr, dispatch);

		if (device.get_caps().reportsValidationMessageCounts)
		{
			EXPECT_EQ(device.get_validation_message_counts().errors, 0u) << "adopting a video frame tripped Vulkan validation";
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
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		const rhi::ResourceState arrived{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.adopt_texture<rhi::VulkanApi>(
			rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc				  = SharedTextureDesc(),
				.initialState	  = arrived,
				.initialOwnership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } },
			error
		);
		ASSERT_TRUE(adopted.is_valid()) << error.message;

		rhi::CommandPool pool = device.create_command_pool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.is_valid()) << error.message;
		rhi::CommandList list = pool.allocate("azoth.rhi.test.adoptedAcquire", error);
		ASSERT_TRUE(list.is_valid()) << error.message;
		ASSERT_TRUE(list.begin(error)) << error.message;

		const std::array acquire{ rhi::TextureBarrier{
			.texture   = adopted,
			.before	   = arrived,
			.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.ownership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute },
		} };

		EXPECT_TRUE(list.barriers(rhi::BarrierBatch{ .textures = acquire }, error))
			<< "a barrier naming exactly what the adoption declared was refused: " << error.message;

		static_cast<void>(list.end(error));
		EXPECT_TRUE(device.destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	TEST(VulkanAdoption, ABarrierNamingAStateTheObjectDidNotArriveInIsRefused)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned							  = std::move(created).value();
		rhi::Device device								  = owned.get();
		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		const rhi::ResourceState arrived{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.adopt_texture<rhi::VulkanApi>(
			rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc = SharedTextureDesc(), .initialState = arrived },
			error
		);
		ASSERT_TRUE(adopted.is_valid()) << error.message;

		rhi::CommandPool pool = device.create_command_pool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.is_valid()) << error.message;
		rhi::CommandList list = pool.allocate("azoth.rhi.test.adoptedWrongState", error);
		ASSERT_TRUE(list.is_valid()) << error.message;
		ASSERT_TRUE(list.begin(error)) << error.message;

		const std::array wrong{ rhi::TextureBarrier{
			.texture = adopted,
			.before	 = { .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading },
			.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
		} };

		// An earlier list in the same submit could still leave the object in the claimed state, so the claim is judged at submit.
		ASSERT_TRUE(list.barriers(rhi::BarrierBatch{ .textures = wrong }, error)) << error.message;
		ASSERT_TRUE(list.end(error)) << error.message;

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(queue.is_valid()) << error.message;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = lists }, error))
			<< "a barrier claiming a state the object never arrived in was submitted, so the declaration is not read";
		EXPECT_NE(std::string_view(error.message != nullptr ? error.message : "").find("did not arrive in"), std::string_view::npos)
			<< "the submit was refused for another reason: " << error.message;
		EXPECT_TRUE(device.destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	TEST(VulkanAdoption, ABarrierReleasingFromTheWrongFamilyIsRefused)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned							  = std::move(created).value();
		rhi::Device device								  = owned.get();
		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value());

		const vk::Device vkDevice						   = native.value().device;
		const vk::detail::DispatchLoaderDynamic & dispatch = *native.value().dispatch;
		const OwnedImage produced						   = MakeImage(vkDevice, native.value().physicalDevice, dispatch);
		ASSERT_TRUE(static_cast<bool>(produced.image));

		rhi::Error error{};
		const rhi::TextureHandle adopted = device.adopt_texture<rhi::VulkanApi>(
			rhi::NativeTexture<rhi::VulkanApi>{ .image = produced.image },
			{ .desc = SharedTextureDesc(), .initialOwnership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } },
			error
		);
		ASSERT_TRUE(adopted.is_valid()) << error.message;

		rhi::CommandPool pool = device.create_command_pool({ .queueType = rhi::QueueType::eGraphics }, error);
		ASSERT_TRUE(pool.is_valid()) << error.message;
		rhi::CommandList list = pool.allocate("azoth.rhi.test.adoptedWrongFamily", error);
		ASSERT_TRUE(list.is_valid()) << error.message;
		ASSERT_TRUE(list.begin(error)) << error.message;

		const std::array wrong{ rhi::TextureBarrier{
			.texture   = adopted,
			.before	   = {},
			.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.ownership = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCopy },
		} };

		// Ownership is judged at submit for the same reason, since an earlier list in that submit could hand the object over.
		ASSERT_TRUE(list.barriers(rhi::BarrierBatch{ .textures = wrong }, error)) << error.message;
		ASSERT_TRUE(list.end(error)) << error.message;

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(queue.is_valid()) << error.message;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = lists }, error))
			<< "a barrier acquired the object from a queue it was never declared to be owned by";
		EXPECT_NE(std::string_view(error.message != nullptr ? error.message : "").find("does not hold it"), std::string_view::npos)
			<< "the submit was refused for another reason: " << error.message;
		EXPECT_TRUE(device.destroy(adopted, {}, error)) << error.message;

		vkDevice.destroyImage(produced.image, nullptr, dispatch);
		vkDevice.freeMemory(produced.memory, nullptr, dispatch);
	}

	namespace
	{
		template <typename Value>
		void ExpectFormsAgree(
			const rhi::CString operation,
			const bool plainSucceeded,
			const bool erroredSucceeded,
			const rhi::Error & error,
			const rhi::Result<Value> & resulted
		)
		{
			SCOPED_TRACE(operation);

			EXPECT_EQ(plainSucceeded, erroredSucceeded) << "the form carrying no diagnostic disagreed with the one that does";
			EXPECT_EQ(erroredSucceeded, resulted.has_value()) << "which form the caller reached for decided whether the call was reported as done";
			EXPECT_EQ(error.code, resulted.has_value() ? rhi::ErrorCode::eOk : resulted.get_error().code) << "the two diagnostic forms named different codes";
		}
	} // namespace

	TEST(VulkanAdoption, EveryTemplatedEntryAgreesAcrossItsForms)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		rhi::Error error{};

		ExpectFormsAgree(
			"Device::AdoptBuffer",
			device.adopt_buffer<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_buffer<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_buffer_with_result<rhi::VulkanApi>(rhi::NativeBuffer<rhi::VulkanApi>{}, {})
		);

		ExpectFormsAgree(
			"Device::AdoptTexture",
			device.adopt_texture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_texture<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_texture_with_result<rhi::VulkanApi>(rhi::NativeTexture<rhi::VulkanApi>{}, {})
		);

		ExpectFormsAgree(
			"Device::AdoptTextureView",
			device.adopt_texture_view<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_texture_view<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_texture_view_with_result<rhi::VulkanApi>(rhi::NativeTextureView<rhi::VulkanApi>{}, {})
		);

		ExpectFormsAgree(
			"Device::AdoptSampler",
			device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_sampler_with_result<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {})
		);

		ExpectFormsAgree(
			"Device::AdoptTimeline",
			device.adopt_timeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_timeline<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_timeline_with_result<rhi::VulkanApi>(rhi::NativeTimeline<rhi::VulkanApi>{}, {})
		);

		ExpectFormsAgree(
			"Device::AdoptBinarySemaphore",
			device.adopt_binary_semaphore<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {}).is_valid(),
			device.adopt_binary_semaphore<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {}, error).is_valid(),
			error,
			device.adopt_binary_semaphore_with_result<rhi::VulkanApi>(rhi::NativeBinarySemaphore<rhi::VulkanApi>{}, {})
		);

		rhi::NativeBuffer<rhi::VulkanApi> readBackBuffer{};
		ExpectFormsAgree(
			"Device::GetNativeBuffer",
			device.get_native_buffer<rhi::VulkanApi>(rhi::BufferHandle{}, readBackBuffer),
			device.get_native_buffer<rhi::VulkanApi>(rhi::BufferHandle{}, readBackBuffer, error),
			error,
			device.get_native_buffer_with_result<rhi::VulkanApi>(rhi::BufferHandle{})
		);

		rhi::NativeTexture<rhi::VulkanApi> readBackTexture{};
		ExpectFormsAgree(
			"Device::GetNativeTexture",
			device.get_native_texture<rhi::VulkanApi>(rhi::TextureHandle{}, readBackTexture),
			device.get_native_texture<rhi::VulkanApi>(rhi::TextureHandle{}, readBackTexture, error),
			error,
			device.get_native_texture_with_result<rhi::VulkanApi>(rhi::TextureHandle{})
		);

		rhi::NativeTextureView<rhi::VulkanApi> readBackView{};
		ExpectFormsAgree(
			"Device::GetNativeTextureView",
			device.get_native_texture_view<rhi::VulkanApi>(rhi::TextureViewHandle{}, readBackView),
			device.get_native_texture_view<rhi::VulkanApi>(rhi::TextureViewHandle{}, readBackView, error),
			error,
			device.get_native_texture_view_with_result<rhi::VulkanApi>(rhi::TextureViewHandle{})
		);

		rhi::NativeSampler<rhi::VulkanApi> readBackSampler{};
		ExpectFormsAgree(
			"Device::GetNativeSampler",
			device.get_native_sampler<rhi::VulkanApi>(rhi::SamplerHandle{}, readBackSampler),
			device.get_native_sampler<rhi::VulkanApi>(rhi::SamplerHandle{}, readBackSampler, error),
			error,
			device.get_native_sampler_with_result<rhi::VulkanApi>(rhi::SamplerHandle{})
		);

		rhi::NativeTimeline<rhi::VulkanApi> readBackTimeline{};
		ExpectFormsAgree(
			"Device::GetNativeTimeline",
			device.get_native_timeline<rhi::VulkanApi>(rhi::TimelineHandle{}, readBackTimeline),
			device.get_native_timeline<rhi::VulkanApi>(rhi::TimelineHandle{}, readBackTimeline, error),
			error,
			device.get_native_timeline_with_result<rhi::VulkanApi>(rhi::TimelineHandle{})
		);

		rhi::NativeBinarySemaphore<rhi::VulkanApi> readBackSemaphore{};
		ExpectFormsAgree(
			"Device::GetNativeBinarySemaphore",
			device.get_native_binary_semaphore<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{}, readBackSemaphore),
			device.get_native_binary_semaphore<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{}, readBackSemaphore, error),
			error,
			device.get_native_binary_semaphore_with_result<rhi::VulkanApi>(rhi::BinarySemaphoreHandle{})
		);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_VULKAN

	TEST(AdoptionCapability, AReportedCapabilityIsOneTheEntriesHonor)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		rhi::Error error{};
		const rhi::SamplerHandle adopted = device.adopt_sampler<rhi::VulkanApi>(rhi::NativeSampler<rhi::VulkanApi>{}, {}, error);
		EXPECT_FALSE(adopted.is_valid());

		if (device.get_caps().supportsResourceAdoption)
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
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a Vulkan device did not hand back its native handles";

		const auto extensions = native.value().physicalDevice.enumerateDeviceExtensionProperties(nullptr, *native.value().dispatch);
		ASSERT_EQ(extensions.result, vk::Result::eSuccess);

		const bool advertised = std::ranges::any_of(
			extensions.value,
			[](const vk::ExtensionProperties & extension) noexcept
			{
				return std::string_view{ extension.extensionName } == VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME;
			}
		);

		const bool reported = device.get_caps().conservativeRasterTier != rhi::ConservativeRasterTier::eNone;
		EXPECT_EQ(reported, advertised)
			<< (advertised ? "the driver advertises conservative rasterization and the tier reports none"
						   : "a tier was reported on a driver without the extension");
	}

	TEST(VulkanAdoption, AQueueExposesItsFamilyIndexThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::VulkanNativeDevice> native = rhi::get_vulkan_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a Vulkan device did not hand back its native handles";

		const rhi::Result<rhi::native::VulkanQueueView> view = rhi::get_vulkan_queue_view(device.get_queue(rhi::QueueType::eGraphics));
		ASSERT_TRUE(view.has_value()) << "a Vulkan graphics queue did not hand back a queue view";

		EXPECT_NE(static_cast<VkQueue>(view.value().queue), VK_NULL_HANDLE) << "the queue view carries no queue";
		EXPECT_EQ(view.value().familyIndex, native.value().graphicsQueueFamily)
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
				return rhi::get_vulkan_command_buffer(list);
			}
		);
	}

	TEST(VulkanRayTracingUsage, RefusesABufferOnlyRayTracingCouldUse)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::VulkanApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine";
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();
		if (device.get_caps().supportsRayTracing)
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
			EXPECT_FALSE(device.create_buffer(desc, error).is_valid()) << "a ray tracing buffer usage was accepted on a device that declines ray tracing";
			EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature);
		}

		rhi::BufferDesc ordinary{};
		ordinary.size  = 256;
		ordinary.usage = rhi::BufferUsage::eStorage;

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.create_buffer(ordinary, error);
		EXPECT_TRUE(buffer.is_valid()) << "an ordinary storage buffer was refused";
		if (buffer.is_valid())
		{
			EXPECT_TRUE(device.destroy(buffer, {}, error));
		}
	}

	[[nodiscard]] rhi::Result<rhi::UniqueInstance> CreateConfiguredVulkanInstance(
		const rhi::native::VulkanInstanceConfig & config,
		rhi::GraphicsApiId key = rhi::VulkanApi::kId
	)
	{
		rhi::GraphicsApiRegistry registry;
		if (const auto registered = rhi::register_backend<rhi::VulkanApi>(registry); !registered)
		{
			return registered.get_error();
		}
		const std::array preferred{ rhi::VulkanApi::kId };
		const std::array entries{ rhi::InstanceConfigEntry{ .api = key, .config = &config } };
		rhi::InstanceDesc desc{};
		desc.backendConfigs = entries;
		return rhi::create_instance(registry, preferred, desc);
	}

	TEST_F(VulkanConfigBlock, StandaloneInstancesReadTheirOwnConfiguration)
	{
		rhi::native::VulkanInstanceConfig asking{};
		asking.minimumInstanceVersion = rhi::ApiVersion{ .major = 1, .minor = 9 };
		const auto refused			  = CreateConfiguredVulkanInstance(asking);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.get_error().code, rhi::ErrorCode::eUnsupportedFeature);
		const auto unrelated = CreateConfiguredVulkanInstance(asking, rhi::MetalApi::kId);
		EXPECT_TRUE(test::Ok(unrelated));
	}

	TEST_F(VulkanConfigBlock, StandaloneInstancesRefuseMissingExtensions)
	{
		static constexpr std::array<const char * const, 1> kNoSuchExtension{ "VK_AZO_not_an_extension" };
		rhi::native::VulkanInstanceConfig asking{};
		asking.instanceExtensions = kNoSuchExtension;
		const auto refused		  = CreateConfiguredVulkanInstance(asking);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.get_error().code, rhi::ErrorCode::eUnsupportedFeature);
	}

	TEST_F(VulkanConfigBlock, MalformedInstanceBlocksAreRefused)
	{
		rhi::native::VulkanInstanceConfig asking{};
		asking.header.byteSize = sizeof(rhi::InterfaceHeader);
		const auto shortBlock  = CreateConfiguredVulkanInstance(asking);
		ASSERT_FALSE(shortBlock.has_value());
		EXPECT_EQ(shortBlock.get_error().code, rhi::ErrorCode::eInvalidArgument);
		asking					= {};
		asking.header.version	= 99;
		const auto wrongVersion = CreateConfiguredVulkanInstance(asking);
		ASSERT_FALSE(wrongVersion.has_value());
		EXPECT_EQ(wrongVersion.get_error().code, rhi::ErrorCode::eInvalidArgument);
	}

	TEST_F(VulkanConfigBlock, InstanceExtensionsAcceptNullAndRepeatedSupportedNames)
	{
		static constexpr std::array<const char * const, 3> kExtensions{ nullptr, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
		rhi::native::VulkanInstanceConfig asking{};
		asking.instanceExtensions = kExtensions;
		EXPECT_TRUE(test::Ok(CreateConfiguredVulkanInstance(asking)));
	}

	TEST_F(VulkanConfigBlock, DefaultDeviceVersionHonorsTheInstanceTarget)
	{
		rhi::DeviceBuilder builder;
		builder.headless().configure_instance<rhi::VulkanApi>(
			[](auto & config)
			{
				config.minimumInstanceVersion = rhi::ApiVersion{ .major = 1, .minor = 2 };
			}
		);
		const auto device = builder.build<rhi::VulkanApi>();
		ASSERT_TRUE(test::Ok(device));
		EXPECT_EQ(device.value().get().get_caps().apiVersion.major, 1u);
		EXPECT_EQ(device.value().get().get_caps().apiVersion.minor, 2u);
	}

	TEST_F(VulkanConfigBlock, RegistryBuilderCarriesTheInstanceBlock)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(rhi::register_backend<rhi::VulkanApi>(registry)));
		const std::array preferred{ rhi::VulkanApi::kId };
		rhi::DeviceBuilder builder;
		builder.configure_instance<rhi::VulkanApi>(
			[](auto & config)
			{
				config.minimumInstanceVersion = rhi::ApiVersion{ .major = 1, .minor = 9 };
			}
		);
		EXPECT_TRUE(test::Failed(builder.build(registry, preferred), rhi::ErrorCode::eUnsupportedFeature));
	}

	TEST_F(VulkanConfigBlock, ADeviceCannotTargetAVersionAboveItsInstance)
	{
		rhi::DeviceBuilder builder;
		builder
			.configure_instance<rhi::VulkanApi>(
				[](auto & config)
				{
					config.minimumInstanceVersion = rhi::ApiVersion{ .major = 1, .minor = 2 };
				}
			)
			.configure<rhi::VulkanApi>(
				[](auto & config)
				{
					config.deviceVersion = rhi::ApiVersion{ .major = 1, .minor = 3 };
				}
			);
		EXPECT_TRUE(test::Failed(builder.build<rhi::VulkanApi>(), rhi::ErrorCode::eUnsupportedFeature));
	}

	TEST_F(VulkanConfigBlock, TheOldDeviceBlockVersionIsRefused)
	{
		rhi::native::VulkanDeviceConfig asking{};
		asking.header.version = 1;
		EXPECT_TRUE(test::Failed(CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, asking), rhi::ErrorCode::eInvalidArgument));
	}

	TEST_F(VulkanConfigBlock, AnInstanceVersionTheLoaderCannotMeetIsRefused)
	{
		rhi::native::VulkanInstanceConfig asking{};
		asking.minimumInstanceVersion = rhi::ApiVersion{ .major = 1, .minor = 9 };
		const std::array entries{ rhi::InstanceConfigEntry{ .api = rhi::VulkanApi::kId, .config = &asking } };
		rhi::DeviceDesc desc{};
		desc.instanceConfigs = entries;
		const auto refused	 = rhi::create_device<rhi::VulkanApi>(desc);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.get_error().code, rhi::ErrorCode::eUnsupportedFeature);
	}

	TEST_F(VulkanConfigBlock, AnInstanceExtensionTheLoaderDoesNotAdvertiseIsRefused)
	{
		static constexpr std::array<const char * const, 1> kNoSuchExtension{ "VK_AZO_not_an_extension" };
		rhi::native::VulkanInstanceConfig asking{};
		asking.instanceExtensions = kNoSuchExtension;
		const std::array entries{ rhi::InstanceConfigEntry{ .api = rhi::VulkanApi::kId, .config = &asking } };
		rhi::DeviceDesc desc{};
		desc.instanceConfigs = entries;
		const auto refused	 = rhi::create_device<rhi::VulkanApi>(desc);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.get_error().code, rhi::ErrorCode::eUnsupportedFeature);
	}

	TEST_F(VulkanConfigBlock, ADeviceVersionTheAdapterCannotMeetIsRefused)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.get_error());
		}

		rhi::native::VulkanDeviceConfig pastTheAdapter{};
		pastTheAdapter.deviceVersion = rhi::ApiVersion{ .major = 1, .minor = 9 };

		const rhi::Result<rhi::UniqueDevice> refused = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, pastTheAdapter);
		EXPECT_FALSE(refused.has_value()) << "a device version past what the adapter supports was accepted";
		if (!refused.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(refused.get_error()));
		}

		rhi::native::VulkanDeviceConfig belowTheFloor{};
		belowTheFloor.deviceVersion = rhi::ApiVersion{ .major = 1, .minor = 1 };

		const rhi::Result<rhi::UniqueDevice> floored = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, belowTheFloor);
		EXPECT_FALSE(floored.has_value()) << "a device version below the 1.2 floor was accepted";
	}

	TEST_F(VulkanConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.get_error());
		}

		rhi::native::VulkanDeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, truncated);
		EXPECT_FALSE(tooShort.has_value()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.has_value())
		{
			EXPECT_EQ(tooShort.get_error().code, rhi::ErrorCode::eInvalidArgument);
		}

		rhi::native::VulkanDeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, otherVersion);
		EXPECT_FALSE(wrongVersion.has_value()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.has_value())
		{
			EXPECT_EQ(wrongVersion.get_error().code, rhi::ErrorCode::eInvalidArgument);
		}
	}

	TEST_F(VulkanConfigBlock, ADeviceExtensionTheAdapterDoesNotAdvertiseIsRefused)
	{
		if (const rhi::Result<rhi::UniqueDevice> plain = MakeDevice<rhi::VulkanApi>(); !plain.has_value())
		{
			GTEST_SKIP() << "no Vulkan device on this machine: " << test::Describe(plain.get_error());
		}

		static constexpr std::array<const char * const, 1> kNoSuchExtension{ "VK_AZO_not_an_extension" };

		rhi::native::VulkanDeviceConfig asking{};
		asking.deviceExtensions = kNoSuchExtension;

		const rhi::Result<rhi::UniqueDevice> refused = CreateWith<rhi::VulkanApi>(rhi::VulkanApi::kId, asking);
		EXPECT_FALSE(refused.has_value()) << "an extension no adapter advertises was accepted, so the block's list is being dropped";
		if (!refused.has_value())
		{
			EXPECT_EQ(refused.get_error().code, rhi::ErrorCode::eUnsupportedFeature);
		}
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL3

	TEST_F(MetalConfigBlock, ComesUpWithNoBlockAtAll)
	{
		const rhi::Result<rhi::UniqueDevice> three = MakeDevice<rhi::MetalApi>();
		if (!three.has_value() && test::NoAdapterHere(three.get_error()))
		{
			GTEST_SKIP() << "no Metal 3 device on this machine: " << test::Describe(three.get_error());
		}

		EXPECT_TRUE(three.has_value()) << "a Metal 3 device carrying no configuration block was refused";
	}

	TEST_F(MetalConfigBlock, RefusesABlockPinningItToTheOtherGeneration)
	{
		rhi::native::MetalDeviceConfig pinnedToFour{};
		pinnedToFour.generation = rhi::ApiVersion{ .major = 4, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> three = CreateWith<rhi::MetalApi>(rhi::MetalApi::kId, pinnedToFour);
		EXPECT_FALSE(three.has_value()) << "Metal 3 accepted a block pinning it to a generation it is not";
		if (!three.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(three.get_error()));
		}
	}

	TEST_F(MetalConfigBlock, AGenerationIgnoresABlockKeyedToTheOther)
	{
		rhi::native::Metal4DeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> three = CreateWith<rhi::MetalApi>(rhi::Metal4Api::kId, pinnedToThree);
		EXPECT_TRUE(three.has_value()) << "Metal 3 read a block keyed to Metal 4, so the entry is not matched on its api field";
	}

	TEST_F(MetalConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		rhi::native::MetalDeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::MetalApi>(rhi::MetalApi::kId, truncated);
		EXPECT_FALSE(tooShort.has_value()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(tooShort.get_error()));
		}

		rhi::native::MetalDeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::MetalApi>(rhi::MetalApi::kId, otherVersion);
		EXPECT_FALSE(wrongVersion.has_value()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(wrongVersion.get_error()));
		}
	}

	TEST(MetalAdoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::MetalApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Metal 3 device on this machine: " << test::Describe(created.get_error());
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::MetalNativeDevice> native = rhi::get_metal_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a Metal 3 device did not hand back its native handles";

		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			const rhi::Result<rhi::native::MetalQueueView> view = rhi::get_metal_queue_view(device.get_queue(type));
			ASSERT_TRUE(view.has_value()) << "a Metal 3 queue did not hand back a queue view";
			EXPECT_NE(view.value().queue, nullptr) << "the queue view carries no command queue";

			if (type == rhi::QueueType::eGraphics)
			{
				EXPECT_EQ(view.value().queue, native.value().queue) << "the graphics queue view disagrees with the queue the device reports";
			}
			else
			{
				EXPECT_NE(view.value().queue, native.value().queue) << "a queue of another type answered with the graphics queue";
			}
		}
	}

	TEST(MetalAdoption, APlacedBufferIsHazardTrackedLikeAStandaloneOne)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::MetalApi>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Metal 3 device on this machine: " << test::Describe(created.get_error());
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		rhi::Error error{};
		const rhi::HeapHandle heap = device.create_heap(test::samples::GpuHeap(), error);
		ASSERT_TRUE(test::Ok(heap.is_valid(), error));

		rhi::PlacedBufferDesc placedDesc{};
		placedDesc.buffer			   = test::samples::StorageBuffer();
		placedDesc.heap				   = heap;
		const rhi::BufferHandle placed = device.create_placed_buffer(placedDesc, error);
		ASSERT_TRUE(test::Ok(placed.is_valid(), error));
		const rhi::BufferHandle standalone = device.create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(standalone.is_valid(), error));

		// metal-cpp is kept out of test targets, so the mode is read through the runtime. MTLHazardTrackingModeTracked is 2.
		const auto hazardMode = [&](const rhi::BufferHandle buffer)
		{
			rhi::NativeBuffer<rhi::MetalApi> native{};
			EXPECT_TRUE(device.get_native_buffer<rhi::MetalApi>(buffer, native, error)) << error.message;
			const auto send = std::bit_cast<unsigned long (*)(id, SEL)>(&objc_msgSend);
			return native.buffer != nullptr ? send(std::bit_cast<id>(native.buffer), sel_registerName("hazardTrackingMode")) : 0ul;
		};

		EXPECT_EQ(hazardMode(standalone), 2ul) << "a standalone buffer is not hazard tracked, so the comparison below proves nothing";
		EXPECT_EQ(hazardMode(placed), 2ul) << "a placed buffer is untracked, and this backend's barriers encode nothing that would order it";

		EXPECT_TRUE(test::Ok(device.destroy(standalone, {}, error), error));
		EXPECT_TRUE(test::Ok(device.destroy(placed, {}, error), error));
		EXPECT_TRUE(test::Ok(device.destroy(heap, {}, error), error));
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
				return rhi::get_metal_command_buffer(list);
			}
		);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_METAL4

	TEST_F(Metal4ConfigBlock, ComesUpWithNoBlockAtAll)
	{
		const rhi::Result<rhi::UniqueDevice> four = MakeDevice<rhi::Metal4Api>();
		if (!four.has_value() && test::NoAdapterHere(four.get_error()))
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(four.get_error());
		}

		EXPECT_TRUE(four.has_value()) << "a Metal 4 device carrying no configuration block was refused";
	}

	TEST_F(Metal4ConfigBlock, RefusesABlockPinningItToTheOtherGeneration)
	{
		rhi::native::Metal4DeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> four = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::kId, pinnedToThree);
		EXPECT_FALSE(four.has_value()) << "Metal 4 accepted a block pinning it to a generation it is not";
		if (!four.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(four.get_error()));
		}
	}

	TEST_F(Metal4ConfigBlock, AGenerationIgnoresABlockKeyedToTheOther)
	{
		rhi::DeviceDesc plain{};
		plain.validation = rhi::ValidationMode::eDeveloper;

		if (rhi::Result<rhi::UniqueDevice> unconfigured = MakeDevice<rhi::Metal4Api>(); !unconfigured.has_value())
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(unconfigured.get_error());
		}

		rhi::native::MetalDeviceConfig pinnedToThree{};
		pinnedToThree.generation = rhi::ApiVersion{ .major = 3, .minor = 0 };

		const rhi::Result<rhi::UniqueDevice> four = CreateWith<rhi::Metal4Api>(rhi::MetalApi::kId, pinnedToThree);
		EXPECT_TRUE(four.has_value()) << "Metal 4 read a block keyed to Metal 3, so the entry is not matched on its api field";
	}

	TEST_F(Metal4ConfigBlock, ABlockTooShortOrOfAnotherVersionIsRefusedRatherThanDefaulted)
	{
		rhi::native::Metal4DeviceConfig truncated{};
		truncated.header.byteSize = sizeof(rhi::InterfaceHeader);

		const rhi::Result<rhi::UniqueDevice> tooShort = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::kId, truncated);
		EXPECT_FALSE(tooShort.has_value()) << "a block declaring fewer bytes than the backend reads was accepted";
		if (!tooShort.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(tooShort.get_error()));
		}

		rhi::native::Metal4DeviceConfig otherVersion{};
		otherVersion.header.version = 99;

		const rhi::Result<rhi::UniqueDevice> wrongVersion = CreateWith<rhi::Metal4Api>(rhi::Metal4Api::kId, otherVersion);
		EXPECT_FALSE(wrongVersion.has_value()) << "a block of a version this backend was not built against was accepted";
		if (!wrongVersion.has_value())
		{
			EXPECT_TRUE(test::ErrorIsPopulated(wrongVersion.get_error()));
		}
	}

	TEST(Metal4Adoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::Metal4Api>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no Metal 4 device on this machine: " << test::Describe(created.get_error());
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::Metal4NativeDevice> native = rhi::get_metal4_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a Metal 4 device did not hand back its native handles";

		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			const rhi::Result<rhi::native::Metal4QueueView> view = rhi::get_metal4_queue_view(device.get_queue(type));
			ASSERT_TRUE(view.has_value()) << "a Metal 4 queue did not hand back a queue view";
			EXPECT_NE(view.value().queue, nullptr) << "the queue view carries no command queue";

			if (type == rhi::QueueType::eGraphics)
			{
				EXPECT_EQ(view.value().queue, native.value().queue) << "the graphics queue view disagrees with the queue the device reports";
			}
			else
			{
				EXPECT_NE(view.value().queue, native.value().queue) << "a queue of another type answered with the graphics queue";
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
				return rhi::get_metal4_command_buffer(list);
			}
		);
	}

#endif

#ifdef AZOTH_RHI_TEST_ADOPTION_D3D12

	TEST(D3D12Adoption, EveryQueueTypeExposesItsCommandQueueThroughTheNativePath)
	{
		rhi::Result<rhi::UniqueDevice> created = MakeDevice<rhi::D3D12Api>();
		if (!created.has_value())
		{
			GTEST_SKIP() << "no D3D12 device on this machine: " << test::Describe(created.get_error());
		}

		rhi::UniqueDevice owned = std::move(created).value();
		rhi::Device device		= owned.get();

		const rhi::Result<rhi::D3D12NativeDevice> native = rhi::get_d3d12_native_device(device);
		ASSERT_TRUE(native.has_value()) << "a D3D12 device did not hand back its native handles";

		const std::array expected{ std::pair{ rhi::QueueType::eGraphics, native.value().graphicsQueue },
			std::pair{ rhi::QueueType::eCompute, native.value().computeQueue },
			std::pair{ rhi::QueueType::eCopy, native.value().copyQueue } };

		for (const auto [type, reported] : expected)
		{
			const rhi::Result<rhi::native::D3D12QueueView> view = rhi::get_d3d12_queue_view(device.get_queue(type));
			ASSERT_TRUE(view.has_value()) << "a D3D12 queue did not hand back a queue view";
			EXPECT_NE(view.value().queue, nullptr) << "the queue view carries no command queue";
			EXPECT_EQ(view.value().queue, reported) << "the queue view disagrees with the queue the device reports for this type";
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
				return rhi::get_d3d12_command_list(list);
			}
		);
	}

#endif

} // namespace

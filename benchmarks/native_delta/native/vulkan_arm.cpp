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

// Ensure that a result cannot throw across a noexcept boundary.
#ifndef VULKAN_HPP_NO_EXCEPTIONS
	#define VULKAN_HPP_NO_EXCEPTIONS
#endif
#undef VULKAN_HPP_ASSERT_ON_RESULT
#define VULKAN_HPP_ASSERT_ON_RESULT(expression)

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"
#include "azoth/rhi/resources/native_slot.hpp"

#include "native_delta/native/backend_arms.hpp"
#include "shared/options.hpp"

#include <chrono>
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <print>
#include <string_view>

namespace rhi = azo::rhi;

namespace bench::native
{

	namespace
	{
		struct Arm final
		{
			vk::Device device;
			const vk::detail::DispatchLoaderDynamic * dispatch = nullptr;
			vk::Image image;
			vk::DescriptorSetLayout setLayout;
			vk::DescriptorPool pool;
			vk::DescriptorSet set;
			vk::PipelineLayout layout;

			bool core13 = false;
		};

		Arm g_arm;

		[[nodiscard]] bool PrepareArm(rhi::Device device, const Workload & work)
		{
			const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(device);
			if (!native)
			{
				ReportError("failed to reach the Vulkan device", native.GetError());
				return false;
			}

			g_arm.device   = native.Value().device;
			g_arm.dispatch = native.Value().dispatch;

			rhi::NativeTexture<rhi::VulkanApi> image{};
			rhi::Error error{};
			if (!device.GetNativeTexture<rhi::VulkanApi>(work.target, image, error))
			{
				ReportError("failed to reach the VkImage behind the target", error);
				return false;
			}
			g_arm.image = image.image;

			// Which barrier entry point to record is the same question the backend answers and it answers it from the device's API version.
			g_arm.core13 = native.Value().physicalDevice.getProperties(*g_arm.dispatch).apiVersion >= VK_API_VERSION_1_3;

			constexpr vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics);
			vk::DescriptorSetLayoutCreateInfo layoutInfo;
			layoutInfo.setBindings(binding);

			const auto setLayout = g_arm.device.createDescriptorSetLayout(layoutInfo, nullptr, *g_arm.dispatch);
			if (setLayout.result != vk::Result::eSuccess)
			{
				std::println("the native arm could not create a VkDescriptorSetLayout");
				return false;
			}
			g_arm.setLayout = setLayout.value;

			const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 1);
			vk::DescriptorPoolCreateInfo poolInfo;
			poolInfo.maxSets = 1;
			poolInfo.setPoolSizes(poolSize);

			const auto pool = g_arm.device.createDescriptorPool(poolInfo, nullptr, *g_arm.dispatch);
			if (pool.result != vk::Result::eSuccess)
			{
				std::println("the native arm could not create a VkDescriptorPool");
				return false;
			}
			g_arm.pool = pool.value;

			vk::DescriptorSetAllocateInfo allocateInfo;
			allocateInfo.descriptorPool = g_arm.pool;
			allocateInfo.setSetLayouts(g_arm.setLayout);

			const auto sets = g_arm.device.allocateDescriptorSets(allocateInfo, *g_arm.dispatch);
			if (sets.result != vk::Result::eSuccess || sets.value.empty())
			{
				std::println("the native arm could not allocate a VkDescriptorSet");
				return false;
			}
			g_arm.set = sets.value.front();

			const vk::PushConstantRange range(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, kPushConstantBytes);
			vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
			pipelineLayoutInfo.setSetLayouts(g_arm.setLayout);
			pipelineLayoutInfo.setPushConstantRanges(range);

			const auto layout = g_arm.device.createPipelineLayout(pipelineLayoutInfo, nullptr, *g_arm.dispatch);
			if (layout.result != vk::Result::eSuccess)
			{
				std::println("the native arm could not create a VkPipelineLayout");
				return false;
			}
			g_arm.layout = layout.value;

			return true;
		}

		void ReleaseArm()
		{
			if (!g_arm.device)
			{
				return;
			}

			if (g_arm.layout)
			{
				g_arm.device.destroyPipelineLayout(g_arm.layout, nullptr, *g_arm.dispatch);
			}
			if (g_arm.pool)
			{
				g_arm.device.destroyDescriptorPool(g_arm.pool, nullptr, *g_arm.dispatch);
			}
			if (g_arm.setLayout)
			{
				g_arm.device.destroyDescriptorSetLayout(g_arm.setLayout, nullptr, *g_arm.dispatch);
			}
		}

		// What VulkanCmd* records for the same shape, with the handle resolution and the argument translation taken out because they have already happened.
		[[nodiscard]] std::uint64_t RecordShape(const Kind kind, const vk::CommandBuffer buffer, const Workload & work, const std::size_t commands)
		{
			const vk::detail::DispatchLoaderDynamic & dispatch = *g_arm.dispatch;
			const vk::Viewport viewport(
				work.viewport.x, work.viewport.y, work.viewport.width, work.viewport.height, work.viewport.minDepth, work.viewport.maxDepth);
			const vk::Rect2D scissor(vk::Offset2D(work.scissor.x, work.scissor.y), vk::Extent2D(work.scissor.width, work.scissor.height));

			const vk::ImageMemoryBarrier2 imageBarrier(vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				vk::AccessFlagBits2::eColorAttachmentWrite,
				vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				vk::AccessFlagBits2::eColorAttachmentWrite,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				g_arm.image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			vk::DependencyInfo dependency;
			dependency.setImageMemoryBarriers(imageBarrier);

			const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
			switch (kind)
			{
			case Kind::eSetViewport:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.setViewport(0, viewport, dispatch);
				}
				break;

			case Kind::eSetScissor:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.setScissor(0, scissor, dispatch);
				}
				break;

			case Kind::ePushConstants:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.pushConstants(g_arm.layout,
						vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
						0,
						kPushConstantBytes,
						work.pushConstants.data(),
						dispatch);
				}
				break;

			case Kind::eBindDescriptorSet:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, g_arm.layout, 0, g_arm.set, nullptr, dispatch);
				}
				break;

			case Kind::eDraw:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.draw(3, 1, 0, 0, dispatch);
				}
				break;

			case Kind::eDrawIndexed:
				for (std::size_t index = 0; index < commands; ++index)
				{
					buffer.drawIndexed(3, 1, 0, 0, 0, dispatch);
				}
				break;

			// Left to VulkanGap, this backend having no pipeline the benchmark can build without a shader toolchain.
			case Kind::eSetGraphicsPipeline: break;

			case Kind::eBarrier:
				if (g_arm.core13)
				{
					for (std::size_t index = 0; index < commands; ++index)
					{
						buffer.pipelineBarrier2(dependency, dispatch);
					}
				}
				else
				{
					for (std::size_t index = 0; index < commands; ++index)
					{
						buffer.pipelineBarrier2KHR(dependency, dispatch);
					}
				}
				break;
			}
			const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();

			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
		}

	} // namespace

	bool PrepareVulkan(rhi::Device device, const Workload & work)
	{
		return PrepareArm(device, work);
	}

	void ReleaseVulkan()
	{
		ReleaseArm();
	}

	std::string_view VulkanGap(const Kind kind)
	{
		// This backend has no pipeline the benchmark can build, the only shader here being Metal source and a SPIR-V toolchain being a dependency a benchmark
		// should not put between a caller and a number.
		if (NeedsPipeline(kind))
		{
			return "no pipeline: this device takes compiled binaries and the only shader here is Metal source";
		}

		return {};
	}

	bool RecordVulkan(const Kind kind, rhi::CommandList & list, const rhi::NativeMutationDesc & mutation, const Workload & work, const std::size_t commands,
		std::uint64_t & elapsed)
	{
		rhi::Error error{};
		const bool recorded = list.ModifyNative<rhi::VulkanApi>(
			mutation,
			[&](const rhi::native::VulkanCommandListView & view)
			{
				elapsed = RecordShape(kind, view.commandBuffer, work, commands);
			},
			error);
		if (!recorded)
		{
			ReportError("the native mutation scope was refused", error);
		}

		return recorded;
	}

} // namespace bench::native

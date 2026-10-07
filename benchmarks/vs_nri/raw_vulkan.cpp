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

#include "raw_vulkan.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <bit>
#include <format>
#include <print>
#include <string>
#include <utility>

namespace vsnri::raw
{

	namespace
	{

		struct Table final
		{
			PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout	= nullptr;
			PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
			PFN_vkCreatePipelineLayout createPipelineLayout				= nullptr;
			PFN_vkDestroyPipelineLayout destroyPipelineLayout			= nullptr;
			PFN_vkCreateDescriptorPool createDescriptorPool				= nullptr;
			PFN_vkDestroyDescriptorPool destroyDescriptorPool			= nullptr;
			PFN_vkAllocateDescriptorSets allocateDescriptorSets			= nullptr;
			PFN_vkUpdateDescriptorSets updateDescriptorSets				= nullptr;
			PFN_vkCreateShaderModule createShaderModule					= nullptr;
			PFN_vkDestroyShaderModule destroyShaderModule				= nullptr;
			PFN_vkCreateGraphicsPipelines createGraphicsPipelines		= nullptr;
			PFN_vkDestroyPipeline destroyPipeline						= nullptr;

			PFN_vkCmdSetViewport cmdSetViewport				  = nullptr;
			PFN_vkCmdSetScissor cmdSetScissor				  = nullptr;
			PFN_vkCmdPushConstants cmdPushConstants			  = nullptr;
			PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
			PFN_vkCmdBindPipeline cmdBindPipeline			  = nullptr;
			PFN_vkCmdDraw cmdDraw							  = nullptr;
			PFN_vkCmdDrawIndexed cmdDrawIndexed				  = nullptr;
			PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2	  = nullptr;
		};

		struct State final
		{
			VkDevice device = VK_NULL_HANDLE;
			Table vk{};

			std::array<VkDescriptorSetLayout, 3> setLayouts{};
			VkPipelineLayout layout = VK_NULL_HANDLE;
			VkDescriptorPool pool	= VK_NULL_HANDLE;
			std::array<VkDescriptorSet, 2> materials{};
			std::array<VkPipeline, 2> pipelines{};

			VkImage barrierImage = VK_NULL_HANDLE;
		};

		State g_state;

		template <class Function>
		[[nodiscard]] bool Load(const PFN_vkGetDeviceProcAddr getDeviceProcAddr, const VkDevice device, const char * name, Function & out)
		{
			out = std::bit_cast<Function>(getDeviceProcAddr(device, name));
			if (out == nullptr)
			{
				std::println("raw arm: the device does not expose {}", name);
			}

			return out != nullptr;
		}

		[[nodiscard]] bool LoadTable(const PFN_vkGetDeviceProcAddr get, const VkDevice device, Table & vk)
		{
			bool loaded = Load(get, device, "vkCreateDescriptorSetLayout", vk.createDescriptorSetLayout);
			loaded		= Load(get, device, "vkDestroyDescriptorSetLayout", vk.destroyDescriptorSetLayout) && loaded;
			loaded		= Load(get, device, "vkCreatePipelineLayout", vk.createPipelineLayout) && loaded;
			loaded		= Load(get, device, "vkDestroyPipelineLayout", vk.destroyPipelineLayout) && loaded;
			loaded		= Load(get, device, "vkCreateDescriptorPool", vk.createDescriptorPool) && loaded;
			loaded		= Load(get, device, "vkDestroyDescriptorPool", vk.destroyDescriptorPool) && loaded;
			loaded		= Load(get, device, "vkAllocateDescriptorSets", vk.allocateDescriptorSets) && loaded;
			loaded		= Load(get, device, "vkUpdateDescriptorSets", vk.updateDescriptorSets) && loaded;
			loaded		= Load(get, device, "vkCreateShaderModule", vk.createShaderModule) && loaded;
			loaded		= Load(get, device, "vkDestroyShaderModule", vk.destroyShaderModule) && loaded;
			loaded		= Load(get, device, "vkCreateGraphicsPipelines", vk.createGraphicsPipelines) && loaded;
			loaded		= Load(get, device, "vkDestroyPipeline", vk.destroyPipeline) && loaded;
			loaded		= Load(get, device, "vkCmdSetViewport", vk.cmdSetViewport) && loaded;
			loaded		= Load(get, device, "vkCmdSetScissor", vk.cmdSetScissor) && loaded;
			loaded		= Load(get, device, "vkCmdPushConstants", vk.cmdPushConstants) && loaded;
			loaded		= Load(get, device, "vkCmdBindDescriptorSets", vk.cmdBindDescriptorSets) && loaded;
			loaded		= Load(get, device, "vkCmdBindPipeline", vk.cmdBindPipeline) && loaded;
			loaded		= Load(get, device, "vkCmdDraw", vk.cmdDraw) && loaded;
			loaded		= Load(get, device, "vkCmdDrawIndexed", vk.cmdDrawIndexed) && loaded;

			// Core in 1.3, and the extension's entry point is the same function under its older name.
			vk.cmdPipelineBarrier2 = std::bit_cast<PFN_vkCmdPipelineBarrier2>(get(device, "vkCmdPipelineBarrier2"));
			if (vk.cmdPipelineBarrier2 == nullptr)
			{
				loaded = Load(get, device, "vkCmdPipelineBarrier2KHR", vk.cmdPipelineBarrier2) && loaded;
			}

			return loaded;
		}

		[[nodiscard]] bool CreateLayouts(State & state)
		{
			constexpr VkShaderStageFlags kGraphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

			const std::array objects{ VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kGraphics, nullptr } };
			const std::array material{ VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kGraphics, nullptr } };
			const std::array shadow{
				VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, kGraphics, nullptr },
				VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, kGraphics, nullptr },
			};

			const std::array<std::pair<const VkDescriptorSetLayoutBinding *, std::uint32_t>, 3> sets{
				std::pair{ objects.data(), static_cast<std::uint32_t>(objects.size()) },
				std::pair{ material.data(), static_cast<std::uint32_t>(material.size()) },
				std::pair{ shadow.data(), static_cast<std::uint32_t>(shadow.size()) },
			};

			for (std::size_t set = 0; set < sets.size(); ++set)
			{
				VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				info.bindingCount = sets.at(set).second;
				info.pBindings	  = sets.at(set).first;
				if (state.vk.createDescriptorSetLayout(state.device, &info, nullptr, &state.setLayouts.at(set)) != VK_SUCCESS)
				{
					std::println("raw arm: vkCreateDescriptorSetLayout failed");
					return false;
				}
			}

			const VkPushConstantRange push{ kGraphics, 0, kPushConstantBytes };
			VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layoutInfo.setLayoutCount		  = static_cast<std::uint32_t>(state.setLayouts.size());
			layoutInfo.pSetLayouts			  = state.setLayouts.data();
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges	  = &push;
			if (state.vk.createPipelineLayout(state.device, &layoutInfo, nullptr, &state.layout) != VK_SUCCESS)
			{
				std::println("raw arm: vkCreatePipelineLayout failed");
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreateMaterials(State & state, const VkBuffer materialBuffer)
		{
			const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<std::uint32_t>(state.materials.size()) };
			VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			poolInfo.maxSets	   = static_cast<std::uint32_t>(state.materials.size());
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes	   = &size;
			if (state.vk.createDescriptorPool(state.device, &poolInfo, nullptr, &state.pool) != VK_SUCCESS)
			{
				std::println("raw arm: vkCreateDescriptorPool failed");
				return false;
			}

			const std::array layouts{ state.setLayouts.at(1), state.setLayouts.at(1) };
			VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocateInfo.descriptorPool		= state.pool;
			allocateInfo.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
			allocateInfo.pSetLayouts		= layouts.data();
			if (state.vk.allocateDescriptorSets(state.device, &allocateInfo, state.materials.data()) != VK_SUCCESS)
			{
				std::println("raw arm: vkAllocateDescriptorSets failed");
				return false;
			}

			std::array<VkDescriptorBufferInfo, 2> buffers{};
			std::array<VkWriteDescriptorSet, 2> writes{};
			for (std::size_t material = 0; material < writes.size(); ++material)
			{
				buffers.at(material) = VkDescriptorBufferInfo{ materialBuffer, material * kMaterialStride, kMaterialStride };

				VkWriteDescriptorSet & write = writes.at(material);
				write						 = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet				 = state.materials.at(material);
				write.dstBinding			 = 0;
				write.descriptorCount		 = 1;
				write.descriptorType		 = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				write.pBufferInfo			 = &buffers.at(material);
			}

			state.vk.updateDescriptorSets(state.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
			return true;
		}

		[[nodiscard]] bool CreatePipelines(State & state, const VkFormat colorFormat, const VkFormat depthFormat)
		{
			const auto makeModule = [&state](const std::uint32_t * code, const std::size_t bytes, VkShaderModule & module)
			{
				VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
				info.codeSize = bytes;
				info.pCode	  = code;
				return state.vk.createShaderModule(state.device, &info, nullptr, &module) == VK_SUCCESS;
			};

			VkShaderModule vertex	= VK_NULL_HANDLE;
			VkShaderModule fragment = VK_NULL_HANDLE;
			if (!makeModule(spirv::kMeshVert, sizeof(spirv::kMeshVert), vertex) || !makeModule(spirv::kMeshFrag, sizeof(spirv::kMeshFrag), fragment))
			{
				std::println("raw arm: vkCreateShaderModule failed");
				return false;
			}

			const std::array stages{
				VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					nullptr,
					0,
					VK_SHADER_STAGE_VERTEX_BIT,
					vertex,
					"main",
					nullptr },
				VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					nullptr,
					0,
					VK_SHADER_STAGE_FRAGMENT_BIT,
					fragment,
					"main",
					nullptr },
			};

			const VkVertexInputBindingDescription binding{ 0, kVertexStride, VK_VERTEX_INPUT_RATE_VERTEX };
			const VkVertexInputAttributeDescription attribute{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
			VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
			vertexInput.vertexBindingDescriptionCount	= 1;
			vertexInput.pVertexBindingDescriptions		= &binding;
			vertexInput.vertexAttributeDescriptionCount = 1;
			vertexInput.pVertexAttributeDescriptions	= &attribute;

			VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			viewport.viewportCount = 1;
			viewport.scissorCount  = 1;

			VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			const std::array dynamic{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamic.size());
			dynamicState.pDynamicStates	   = dynamic.data();

			VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			rendering.colorAttachmentCount	  = 1;
			rendering.pColorAttachmentFormats = &colorFormat;
			rendering.depthAttachmentFormat	  = depthFormat;

			bool created = true;
			for (std::uint32_t variant = 0; variant < state.pipelines.size(); ++variant)
			{
				VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
				raster.polygonMode = VK_POLYGON_MODE_FILL;
				raster.cullMode	   = VariantCulls(variant) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
				raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
				raster.lineWidth   = 1.0f;

				VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
				depth.depthTestEnable  = VK_TRUE;
				depth.depthWriteEnable = VK_TRUE;
				depth.depthCompareOp   = VariantLessEqual(variant) ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

				VkPipelineColorBlendAttachmentState attachment{};
				attachment.blendEnable		   = VariantBlends(variant) ? VK_TRUE : VK_FALSE;
				attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				attachment.colorBlendOp		   = VK_BLEND_OP_ADD;
				attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				attachment.alphaBlendOp		   = VK_BLEND_OP_ADD;
				attachment.colorWriteMask	   = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

				VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
				blend.attachmentCount = 1;
				blend.pAttachments	  = &attachment;

				VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
				info.pNext				 = &rendering;
				info.stageCount			 = static_cast<std::uint32_t>(stages.size());
				info.pStages			 = stages.data();
				info.pVertexInputState	 = &vertexInput;
				info.pInputAssemblyState = &assembly;
				info.pViewportState		 = &viewport;
				info.pRasterizationState = &raster;
				info.pMultisampleState	 = &multisample;
				info.pDepthStencilState	 = &depth;
				info.pColorBlendState	 = &blend;
				info.pDynamicState		 = &dynamicState;
				info.layout				 = state.layout;

				if (state.vk.createGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &info, nullptr, &state.pipelines.at(variant)) != VK_SUCCESS)
				{
					std::println("raw arm: vkCreateGraphicsPipelines failed");
					created = false;
					break;
				}
			}

			state.vk.destroyShaderModule(state.device, vertex, nullptr);
			state.vk.destroyShaderModule(state.device, fragment, nullptr);
			return created;
		}

		[[nodiscard]] std::string VersionText(const std::uint32_t version)
		{
			return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
		}

	} // namespace

	bool Prepare(const Handles & handles)
	{
		Release();

		const auto getDeviceProcAddr = std::bit_cast<PFN_vkGetDeviceProcAddr>(handles.getDeviceProcAddr);

		g_state.device		 = static_cast<VkDevice>(handles.device);
		g_state.barrierImage = std::bit_cast<VkImage>(handles.barrierImage);

		if (!LoadTable(getDeviceProcAddr, g_state.device, g_state.vk))
		{
			return false;
		}

		return CreateLayouts(g_state) && CreateMaterials(g_state, std::bit_cast<VkBuffer>(handles.materialBuffer)) &&
			   CreatePipelines(g_state, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT);
	}

	void Release()
	{
		if (g_state.device == VK_NULL_HANDLE)
		{
			return;
		}

		for (const VkPipeline pipeline : g_state.pipelines)
		{
			if (pipeline != VK_NULL_HANDLE)
			{
				g_state.vk.destroyPipeline(g_state.device, pipeline, nullptr);
			}
		}

		if (g_state.pool != VK_NULL_HANDLE)
		{
			g_state.vk.destroyDescriptorPool(g_state.device, g_state.pool, nullptr);
		}

		if (g_state.layout != VK_NULL_HANDLE)
		{
			g_state.vk.destroyPipelineLayout(g_state.device, g_state.layout, nullptr);
		}

		for (const VkDescriptorSetLayout layout : g_state.setLayouts)
		{
			if (layout != VK_NULL_HANDLE)
			{
				g_state.vk.destroyDescriptorSetLayout(g_state.device, layout, nullptr);
			}
		}

		g_state = State{};
	}

	std::uint64_t RecordShape(const Shape shape, void * commandBuffer, const std::uint32_t commands)
	{
		const Table & vk	   = g_state.vk;
		const auto buffer	   = static_cast<VkCommandBuffer>(commandBuffer);
		constexpr auto kExtent = static_cast<float>(kSceneExtent);
		constexpr auto kHalf   = kSceneExtent / 2;
		const std::array viewports{
			VkViewport{ 0.0f, 0.0f, kExtent, kExtent, 0.0f, 1.0f },
			VkViewport{ 0.0f, 0.0f, kExtent / 2.0f, kExtent / 2.0f, 0.0f, 1.0f },
		};
		const std::array scissors{
			VkRect2D{ { 0, 0 }, { kSceneExtent, kSceneExtent } },
			VkRect2D{ { 0, 0 }, { kHalf, kHalf } },
		};

		std::array<VkImageMemoryBarrier2, 2> barriers{};
		std::array<VkDependencyInfo, 2> dependencies{};
		for (std::size_t direction = 0; direction < barriers.size(); ++direction)
		{
			const bool toRead				= direction == 0;
			VkImageMemoryBarrier2 & barrier = barriers.at(direction);
			barrier							= VkImageMemoryBarrier2{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			barrier.srcStageMask			= toRead ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barrier.srcAccessMask			= toRead ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			barrier.dstStageMask			= toRead ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask			= toRead ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout				= toRead ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout				= toRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex		= VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex		= VK_QUEUE_FAMILY_IGNORED;
			barrier.image					= g_state.barrierImage;
			barrier.subresourceRange		= VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo & dependency	   = dependencies.at(direction);
			dependency						   = VkDependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers	   = &barrier;
		}

		PushBlock push{};

		const std::uint64_t started = Now();
		switch (shape)
		{
		case Shape::eSetViewport:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdSetViewport(buffer, 0, 1, &viewports.at(index & 1u));
			}
			break;

		case Shape::eSetScissor:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdSetScissor(buffer, 0, 1, &scissors.at(index & 1u));
			}
			break;

		case Shape::ePushConstants:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				push.object = index;
				vk.cmdPushConstants(buffer, g_state.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, kPushConstantBytes, &push);
			}
			break;

		case Shape::eBindDescriptorSet:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_state.layout, 1, 1, &g_state.materials.at(index & 1u), 0, nullptr);
			}
			break;

		case Shape::eSetPipeline:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_state.pipelines.at(index & 1u));
			}
			break;

		case Shape::eDraw:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdDraw(buffer, 3, 1, 0, 0);
			}
			break;

		case Shape::eDrawIndexed:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdDrawIndexed(buffer, 3, 1, 0, 0, 0);
			}
			break;

		case Shape::eBarrier:
			for (std::uint32_t index = 0; index < commands; ++index)
			{
				vk.cmdPipelineBarrier2(buffer, &dependencies.at(index & 1u));
			}
			break;
		}

		return Now() - started;
	}

	bool Identify(const Handles & handles, Identity & identity)
	{
		const auto getInstanceProcAddr = std::bit_cast<PFN_vkGetInstanceProcAddr>(handles.getInstanceProcAddr);
		const auto instance			   = static_cast<VkInstance>(handles.instance);

		auto getProperties2 = std::bit_cast<PFN_vkGetPhysicalDeviceProperties2>(getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
		if (getProperties2 == nullptr)
		{
			getProperties2 = std::bit_cast<PFN_vkGetPhysicalDeviceProperties2>(getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
		}
		if (getProperties2 == nullptr)
		{
			std::println("raw arm: the instance exposes no vkGetPhysicalDeviceProperties2, so the driver cannot be named");
			return false;
		}

		VkPhysicalDeviceDriverProperties driver{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &driver;
		getProperties2(static_cast<VkPhysicalDevice>(handles.physicalDevice), &properties);

		identity.deviceName	   = properties.properties.deviceName;
		identity.apiVersion	   = VersionText(properties.properties.apiVersion);
		identity.driverVersion = std::to_string(properties.properties.driverVersion);
		identity.driverName	   = driver.driverName;
		identity.driverInfo	   = driver.driverInfo;
		return true;
	}

} // namespace vsnri::raw

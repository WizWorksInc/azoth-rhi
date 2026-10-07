// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/device_tag.hpp"
#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/resource_record.hpp"
#include "azoth/rhi/backend/support/scope_guard.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/core/c_string.hpp"
#include "azoth/rhi/core/platform.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/device_config.hpp"
#include "azoth/rhi/native/vulkan_config.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"
#include "azoth/rhi/resources/binding_abi.hpp"

#include "backends/registration.hpp"
#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "support/driver_version.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace azo::rhi
{
	namespace vulkan
	{
		[[nodiscard]] BufferSlot * resolve_buffer(VulkanDevice * device, BufferHandle handle) noexcept
		{
			return device->bufferSlots.resolve(handle, kHandleAlreadyChecked);
		}

		[[nodiscard]] bool bound_buffer_range(VkDeviceSize bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept
		{
			if (offset > bufferSize)
			{
				return false;
			}

			const std::uint64_t remaining = bufferSize - offset;

			if (size == std::numeric_limits<std::uint64_t>::max())
			{
				size = remaining;
			}
			else if (size > remaining)
			{
				return false;
			}

			return true;
		}

		[[nodiscard]] vk::PipelineLayout resolve_pipeline_layout(VulkanDevice * device, PipelineLayoutHandle handle) noexcept
		{
			const PipelineLayoutSlot * slot = device->pipelineLayoutSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->layout : vk::PipelineLayout{};
		}

		[[nodiscard]] vk::Image resolve_texture(const VulkanDevice * device, TextureHandle handle) noexcept
		{
			const TextureSlot * slot = device->textureSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? vk::Image{ slot->image } : vk::Image{};
		}

		[[nodiscard]] const TextureViewSlot * resolve_texture_view_slot(const VulkanDevice * device, TextureViewHandle handle) noexcept
		{
			return device->textureViewSlots.resolve(handle, kHandleAlreadyChecked);
		}

		[[nodiscard]] vk::ImageView resolve_texture_view(const VulkanDevice * device, TextureViewHandle handle) noexcept
		{
			const TextureViewSlot * slot = resolve_texture_view_slot(device, handle);
			return slot != nullptr ? slot->view : vk::ImageView{};
		}

		[[nodiscard]] vk::Pipeline resolve_graphics_pipeline(VulkanDevice * device, GraphicsPipelineHandle handle) noexcept
		{
			const GraphicsPipelineSlot * slot = device->graphicsPipelineSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->pipeline : vk::Pipeline{};
		}

		[[nodiscard]] vk::Semaphore resolve_timeline(VulkanDevice * device, TimelineHandle handle) noexcept
		{
			const TimelineSlot * slot = device->timelineSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->semaphore : vk::Semaphore{};
		}

		[[nodiscard]] vk::Semaphore resolve_binary_semaphore(VulkanDevice * device, BinarySemaphoreHandle handle) noexcept
		{
			if ((handle.index & kDeviceBinarySemaphoreBit) != 0)
			{
				const std::uint32_t index			   = handle.index & ~kDeviceBinarySemaphoreBit;
				const BinarySemaphoreSlot * const slot = device->binarySemaphoreSlots.resolve(
					BinarySemaphoreHandle{
						.index		= index,
						.generation = handle.generation,
					},
					true
				);
				return slot != nullptr ? slot->semaphore : vk::Semaphore{};
			}

			const std::uint32_t swapchainId = (handle.index >> kSwapchainIdShift) & kSwapchainIdMask;
			const std::uint32_t slot		= handle.index & kSwapchainSlotMask;
			for (const HostUniquePtr<VulkanSwapchain> & sc : device->swapchains)
			{
				if (sc->id == swapchainId && slot < sc->semaphores.size())
				{
					return sc->semaphores[slot]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				}
			}
			return {};
		}

		[[nodiscard]] VulkanBackendOwner & backend_owner()
		{
			static VulkanBackendOwner s_Owner;
			return s_Owner;
		}

		[[nodiscard]] bool ensure_dispatcher_initialized(VulkanBackendOwner & owner)
		{
			if (owner.dispatch.vkGetInstanceProcAddr != nullptr)
			{
				return true;
			}

			auto getInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
			if (getInstanceProcAddr == nullptr)
			{
				if (!owner.loader.has_value())
				{
					owner.loader.emplace();
				}

				if (!owner.loader->success())
				{
					return false;
				}

				getInstanceProcAddr = owner.loader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
				if (getInstanceProcAddr == nullptr)
				{
					return false;
				}
			}

			owner.dispatch.init(getInstanceProcAddr);

			VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);
			return true;
		}

		std::pair<std::uint32_t, std::uint32_t> resolve_api_version(ApiVersion requested) noexcept
		{
			if (requested.major == 0 && requested.minor == 0)
			{
				return { 1, 3 };
			}

			return { requested.major, requested.minor };
		}

		std::uint32_t pack_vk_api_version(std::uint32_t major, std::uint32_t minor) noexcept
		{
			return VK_MAKE_API_VERSION(0, major, minor, 0);
		}

		namespace
		{
			[[nodiscard]] bool resolve_native_policy(const NativeValidationPolicy policy, const ValidationMode mode) noexcept
			{
				switch (policy)
				{
				case NativeValidationPolicy::eEnabled:				return true;
				case NativeValidationPolicy::eDisabled:				return false;
				case NativeValidationPolicy::eFollowValidationMode: return mode == ValidationMode::eDeveloper || mode == ValidationMode::eCapture;
				}

				return false;
			}

		}

		void report_instance_message(
			const VulkanInstance * instance,
			const ValidationMessageSeverity severity,
			const char * source,
			const char * message
		) noexcept
		{
			if (message == nullptr)
			{
				return;
			}

			if (instance != nullptr && instance->onMessage != nullptr)
			{
				instance->onMessage(severity, message, instance->messageUserData);
				return;
			}

			std::cerr << '[' << source << "] " << message << '\n';
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
			vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
			[[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT types,
			const vk::DebugUtilsMessengerCallbackDataEXT * data,
			void * userData
		) noexcept
		{
			auto * instance		 = static_cast<VulkanInstance *>(userData);
			const bool isError	 = severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
			const bool isWarning = !isError && severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
			if (isError)
			{
				instance->validationErrors.fetch_add(1, std::memory_order_relaxed);
			}
			else if (isWarning)
			{
				instance->validationWarnings.fetch_add(1, std::memory_order_relaxed);
			}

			if (data != nullptr)
			{
				report_instance_message(
					instance,
					isError ? ValidationMessageSeverity::eError : ValidationMessageSeverity::eWarning,
					"vulkan validation",
					data->pMessage
				);
			}

			if ((isError && instance->breakOnError) || (isWarning && instance->breakOnWarning))
			{
				AZO_RHI_DEBUG_BREAK();
			}

			return VK_FALSE;
		}

		[[nodiscard]] HostUniquePtr<VulkanInstance> build_instance(const InstanceDesc & desc, Error * error)
		{
			const auto config = native::find_instance_config<VulkanApi>(desc.backendConfigs);
			if (config.malformed)
			{
				*error =
					Error{ .code = ErrorCode::eInvalidArgument, .message = "the Vulkan instance configuration block declares an unsupported size or version" };
				return nullptr;
			}
			VulkanBackendOwner & owner = backend_owner();
			if (!ensure_dispatcher_initialized(owner))
			{
				*error = Error{
					.code	 = ErrorCode::eNativeApiError,
					.message = "no Vulkan loader could be opened",
				};
				return nullptr;
			}

			const auto instanceFail = [error](const char * message, vk::Result result) -> HostUniquePtr<VulkanInstance>
			{
				*error = Error{
					.code		= ErrorCode::eNativeApiError,
					.nativeCode = static_cast<std::int32_t>(result),
					.message	= message,
				};
				return nullptr;
			};

			const auto enumeratedExts = vk::enumerateInstanceExtensionProperties<HostAllocatorAdapter<vk::ExtensionProperties>>(nullptr, owner.dispatch);
			if (enumeratedExts.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance extension enumeration failed", enumeratedExts.result);
			}

			const detail::HostVector<vk::ExtensionProperties> & availExts = enumeratedExts.value;
			auto extAvailable											  = [&](const char * name)
			{
				return std::ranges::any_of(
					availExts,
					[name](const vk::ExtensionProperties & ep)
					{
						return std::strcmp(ep.extensionName, name) == 0;
					}
				);
			};

			detail::HostVector<const char *> instanceExts;
			vk::InstanceCreateFlags instanceFlags{};
			if (extAvailable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
			{
				instanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
				instanceFlags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
			}

			if (extAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}

			for (const char * surfaceExt : {
					 VK_KHR_SURFACE_EXTENSION_NAME,
					 "VK_EXT_metal_surface",
					 "VK_MVK_macos_surface",
					 "VK_KHR_win32_surface",
					 "VK_KHR_xlib_surface",
					 "VK_KHR_xcb_surface",
					 "VK_KHR_wayland_surface",
				 })
			{
				if (extAvailable(surfaceExt))
				{
					instanceExts.push_back(surfaceExt);
				}
			}

			const NativeValidationDesc & native = desc.nativeValidation;
			const auto enumeratedLayers			= vk::enumerateInstanceLayerProperties<HostAllocatorAdapter<vk::LayerProperties>>(owner.dispatch);
			if (enumeratedLayers.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance layer enumeration failed", enumeratedLayers.result);
			}

			const detail::HostVector<vk::LayerProperties> & availLayers = enumeratedLayers.value;
			auto layerAvailable											= [&](const char * name)
			{
				return std::ranges::any_of(
					availLayers,
					[name](const vk::LayerProperties & lp)
					{
						return std::strcmp(lp.layerName, name) == 0;
					}
				);
			};

			detail::HostVector<const char *> layers;
			const bool wantApiValidation = resolve_native_policy(native.apiValidation, desc.validation);
			if (wantApiValidation && layerAvailable("VK_LAYER_KHRONOS_validation"))
			{
				layers.push_back("VK_LAYER_KHRONOS_validation");
			}

			for (const char * extra : native.extraNativeLayers)
			{
				if (extra != nullptr && layerAvailable(extra))
				{
					layers.push_back(extra);
				}
			}

			for (const char * extra : native.extraNativeExtensions)
			{
				if (extra != nullptr && extAvailable(extra))
				{
					instanceExts.push_back(extra);
				}
			}

			if (config.block != nullptr)
			{
				for (const char * extra : config.block->instanceExtensions)
				{
					if (extra == nullptr)
					{
						continue;
					}
					if (!extAvailable(extra))
					{
						*error = Error{
							.code	 = ErrorCode::eUnsupportedFeature,
							.message = "an instance extension named in the Vulkan configuration block is not supported by the loader",
						};
						return nullptr;
					}
					const bool alreadyEnabled = std::ranges::any_of(
						instanceExts,
						[extra](const char * name) noexcept
						{
							return std::strcmp(name, extra) == 0;
						}
					);
					if (!alreadyEnabled)
					{
						instanceExts.push_back(extra);
					}
				}
			}

			detail::HostVector<vk::ValidationFeatureEnableEXT> validationEnables;
			if (wantApiValidation)
			{
				if (resolve_native_policy(native.synchronizationValidation, desc.validation))
				{
					validationEnables.push_back(vk::ValidationFeatureEnableEXT::eSynchronizationValidation);
				}
				if (resolve_native_policy(native.gpuBasedValidation, desc.validation))
				{
					validationEnables.push_back(vk::ValidationFeatureEnableEXT::eGpuAssisted);
				}
				if (resolve_native_policy(native.bestPractices, desc.validation))
				{
					validationEnables.push_back(vk::ValidationFeatureEnableEXT::eBestPractices);
				}
			}

			vk::ValidationFeaturesEXT validationFeatures{};
			const bool useValidationFeatures = !validationEnables.empty() && extAvailable(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
			if (useValidationFeatures)
			{
				instanceExts.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
				validationFeatures.setEnabledValidationFeatures(validationEnables);
			}

			const auto [apiMajor, apiMinor]	 = resolve_api_version(config.block != nullptr ? config.block->minimumInstanceVersion : ApiVersion{});
			const std::uint32_t requestedApi = pack_vk_api_version(apiMajor, apiMinor);
			const auto loaderApi			 = vk::enumerateInstanceVersion(owner.dispatch);
			if (loaderApi.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan loader version query failed", loaderApi.result);
			}

			if (requestedApi > loaderApi.value)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "requested Vulkan version exceeds the loader instance version",
				};
				return nullptr;
			}

			const vk::ApplicationInfo appInfo(
				desc.applicationName,
				VK_MAKE_VERSION(desc.applicationVersionMajor, desc.applicationVersionMinor, 0),
				desc.engineName,
				VK_MAKE_VERSION(desc.engineVersionMajor, desc.engineVersionMinor, 0),
				requestedApi
			);
			vk::InstanceCreateInfo instInfo(instanceFlags, &appInfo);
			instInfo.setPEnabledExtensionNames(instanceExts);
			instInfo.setPEnabledLayerNames(layers);
			if (useValidationFeatures)
			{
				instInfo.pNext = &validationFeatures;
			}

			auto instance = host_new<VulkanInstance>();
			if (instance == nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan instance allocation failed",
				};
				return nullptr;
			}

			instance->object	   = publishing_object<Published<InstanceApi, &instance_block>, Published<ExternalCapabilityApi, &external_capability_block>>();
			instance->breakOnError = native.breakOnError;
			instance->breakOnWarning  = native.breakOnWarning;
			instance->onMessage		  = native.onMessage;
			instance->messageUserData = native.messageUserData;
			instance->dispatch		  = owner.dispatch;

			const auto created = vk::createInstance(instInfo, nullptr, instance->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance creation failed", created.result);
			}

			instance->instance	 = created.value;
			instance->apiVersion = ApiVersion{ .major = apiMajor, .minor = apiMinor };
			instance->debugUtils = extAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

			instance->dispatch.init(instance->instance);

			VULKAN_HPP_DEFAULT_DISPATCHER.init(instance->instance);

			if (instance->debugUtils && wantApiValidation)
			{
				vk::DebugUtilsMessengerCreateInfoEXT messengerInfo;
				messengerInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
				messengerInfo.messageType	  = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
												vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
				messengerInfo.pfnUserCallback = &debug_messenger_callback;
				messengerInfo.pUserData		  = instance.get();

				const auto messenger = instance->instance.createDebugUtilsMessengerEXT(messengerInfo, nullptr, instance->dispatch);
				if (messenger.result == vk::Result::eSuccess)
				{
					instance->debugMessenger = messenger.value;
				}
			}

			return instance;
		}

		[[nodiscard]] VulkanInstance * make_owned_instance(const InstanceDesc & desc, Error * error)
		{
			HostUniquePtr<VulkanInstance> instance = build_instance(desc, error);
			if (!instance)
			{
				return nullptr;
			}
			VulkanBackendOwner & owner = backend_owner();
			VulkanInstance * raw	   = instance.get();
			owner.instances.push_back(std::move(instance));
			return raw;
		}

		[[nodiscard]] static bool vulkan_calibration_domains(
			vk::PhysicalDevice phys,
			const vk::detail::DispatchLoaderDynamic & dispatch,
			vk::TimeDomainEXT & hostDomain
		) noexcept
		{
			const auto enumerated = phys.getCalibrateableTimeDomainsEXT<HostAllocatorAdapter<vk::TimeDomainEXT>>(dispatch);
			if (enumerated.result != vk::Result::eSuccess)
			{
				return false;
			}

			const detail::HostVector<vk::TimeDomainEXT> & domains = enumerated.value;
			const auto hasDomain								  = [&domains](vk::TimeDomainEXT domain) noexcept
			{
				return std::ranges::find(domains, domain) != domains.end();
			};

#ifdef _WIN32
			hostDomain = vk::TimeDomainEXT::eQueryPerformanceCounter;
#else
			hostDomain = hasDomain(vk::TimeDomainEXT::eClockMonotonicRaw) ? vk::TimeDomainEXT::eClockMonotonicRaw : vk::TimeDomainEXT::eClockMonotonic;
#endif
			return hasDomain(vk::TimeDomainEXT::eDevice) && hasDomain(hostDomain);
		}

		[[nodiscard]] static constexpr bool can_back_graphics_queue(const vk::QueueFlags flags) noexcept
		{
			return static_cast<bool>(flags & vk::QueueFlagBits::eGraphics) && static_cast<bool>(flags & vk::QueueFlagBits::eCompute);
		}

		static_assert(can_back_graphics_queue(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
		static_assert(!can_back_graphics_queue(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer), "a graphics family without compute was accepted");
		static_assert(!can_back_graphics_queue(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer), "a family without graphics was accepted");

		[[nodiscard]] const char * vulkan_adapter_refusal(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch) noexcept
		{
			const std::uint32_t adapterApi = phys.getProperties(dispatch).apiVersion;
			if (adapterApi < pack_vk_api_version(1, 2))
			{
				return "this adapter reports a Vulkan version below 1.2, which the backend requires";
			}

			const detail::HostVector<vk::QueueFamilyProperties> qfs = phys.getQueueFamilyProperties<HostAllocatorAdapter<vk::QueueFamilyProperties>>(dispatch);
			bool anyGraphicsFamily									= false;
			bool foundGraphics										= false;
			for (const vk::QueueFamilyProperties & qf : qfs)
			{
				if (!static_cast<bool>(qf.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					continue;
				}

				anyGraphicsFamily = true;
				if (can_back_graphics_queue(qf.queueFlags))
				{
					foundGraphics = true;
					break;
				}
			}

			if (!foundGraphics)
			{
				return anyGraphicsFamily ? "every Vulkan graphics queue family on this adapter lacks compute, which the RHI's graphics queue promises"
										 : "no Vulkan graphics queue family";
			}

			const auto enumeratedExts = phys.enumerateDeviceExtensionProperties<HostAllocatorAdapter<vk::ExtensionProperties>>(nullptr, dispatch);
			if (enumeratedExts.result != vk::Result::eSuccess)
			{
				return "Vulkan device extension enumeration failed on this adapter";
			}

			const detail::HostVector<vk::ExtensionProperties> & availableExts = enumeratedExts.value;
			const auto hasExt												  = [&availableExts](const char * name) noexcept
			{
				return std::ranges::any_of(
					availableExts,
					[name](const vk::ExtensionProperties & ep)
					{
						return std::strcmp(ep.extensionName, name) == 0;
					}
				);
			};

			if (!hasExt(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				return "this adapter does not support VK_KHR_swapchain, which the backend enables on every device";
			}

			if (adapterApi < pack_vk_api_version(1, 3) && !hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
			{
				return "Vulkan below 1.3 requires VK_KHR_synchronization2";
			}

			return nullptr;
		}

		[[nodiscard]] VulkanDevice * make_owned_device(VulkanInstance * instance, const DeviceDesc & desc, Error * error)
		{
			VulkanBackendOwner & owner = backend_owner();

			const auto deviceFail = [error](const char * message, vk::Result result) -> VulkanDevice *
			{
				*error = Error{
					.code		= ErrorCode::eNativeApiError,
					.nativeCode = static_cast<std::int32_t>(result),
					.message	= message,
				};
				return nullptr;
			};

			const auto config = native::find_device_config<VulkanApi>(desc.backendConfigs);
			if (config.malformed)
			{
				*error = Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "the Vulkan configuration block declares a size or version this backend cannot read",
				};
				return nullptr;
			}

			const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
			if (enumerated.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan physical device enumeration failed", enumerated.result);
			}

			const detail::HostVector<vk::PhysicalDevice> & physicals = enumerated.value;
			if (physicals.empty())
			{
				*error = Error{
					.code	 = ErrorCode::eNoCompatibleAdapter,
					.message = "no Vulkan physical devices found",
				};
				return nullptr;
			}

			std::uint32_t adapterIndex = 0;
			vk::PhysicalDevice phys;
			if (desc.preferredAdapterIndex != kInvalidIndex && desc.preferredAdapterIndex < physicals.size())
			{
				adapterIndex = desc.preferredAdapterIndex;
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				phys = physicals[adapterIndex];

				if (const char * refusal = vulkan_adapter_refusal(phys, instance->dispatch); refusal != nullptr)
				{
					*error = Error{
						.code	 = ErrorCode::eNoCompatibleAdapter,
						.message = refusal,
					};
					return nullptr;
				}

				for (const DeviceFeature feature : desc.requiredFeatures)
				{
					if (!adapter_supports_feature(phys, instance->dispatch, feature))
					{
						*error = Error{
							.code	 = ErrorCode::eUnsupportedFeature,
							.message = required_feature_message(feature),
						};
						return nullptr;
					}
				}
			}
			else
			{
				int bestScore			  = -1;
				std::uint32_t usableCount = 0;
				const char * firstRefusal = nullptr;
				for (std::uint32_t i = 0; i < physicals.size(); ++i)
				{
					const vk::PhysicalDevice candidate = physicals[i];
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					if (const char * refusal = vulkan_adapter_refusal(candidate, instance->dispatch); refusal != nullptr)
					{
						if (firstRefusal == nullptr)
						{
							firstRefusal = refusal;
						}

						continue;
					}

					++usableCount;
					if (!adapter_supports_all_features(candidate, instance->dispatch, desc.requiredFeatures))
					{
						continue;
					}

					int score = 0;
					if (desc.preferDiscreteGpu && candidate.getProperties(instance->dispatch).deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
					{
						score += 1000;
					}

					for (const DeviceFeature feature : desc.preferredFeatures)
					{
						if (adapter_supports_feature(candidate, instance->dispatch, feature))
						{
							++score;
						}
					}

					if (score > bestScore)
					{
						bestScore	 = score;
						adapterIndex = i;
						phys		 = candidate;
					}
				}

				if (!phys)
				{
					if (usableCount == 0)
					{
						*error = Error{
							.code	 = ErrorCode::eNoCompatibleAdapter,
							.message = firstRefusal,
						};
						return nullptr;
					}

					for (const DeviceFeature feature : desc.requiredFeatures)
					{
						const bool anySupports = std::ranges::any_of(
							physicals,
							[feature, instance](vk::PhysicalDevice candidate) noexcept
							{
								return vulkan_adapter_refusal(candidate, instance->dispatch) == nullptr &&
									   adapter_supports_feature(candidate, instance->dispatch, feature);
							}
						);

						if (!anySupports)
						{
							*error = Error{
								.code	 = ErrorCode::eUnsupportedFeature,
								.message = required_feature_message(feature),
							};
							return nullptr;
						}
					}

					*error = Error{
						.code	 = ErrorCode::eUnsupportedFeature,
						.message = "no Vulkan adapter satisfies the combination of required device features",
					};

					return nullptr;
				}
			}

			const detail::HostVector<vk::QueueFamilyProperties> qfs =
				phys.getQueueFamilyProperties<HostAllocatorAdapter<vk::QueueFamilyProperties>>(instance->dispatch);
			std::uint32_t graphicsFamily = 0;
			for (std::uint32_t i = 0; i < qfs.size(); ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (can_back_graphics_queue(qfs[i].queueFlags))
				{
					graphicsFamily = i;
					break;
				}
			}

			const bool bindsSparse = static_cast<bool>(qfs[graphicsFamily].queueFlags & vk::QueueFlagBits::eSparseBinding);

			std::uint32_t computeFamily = graphicsFamily;
			std::uint32_t copyFamily	= graphicsFamily;
			for (std::uint32_t i = 0; i < qfs.size(); ++i)
			{
				const vk::QueueFlags flags = qfs[i].queueFlags;
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const bool graphics = static_cast<bool>(flags & vk::QueueFlagBits::eGraphics);
				const bool compute	= static_cast<bool>(flags & vk::QueueFlagBits::eCompute);
				const bool transfer = static_cast<bool>(flags & vk::QueueFlagBits::eTransfer);
				if (compute && !graphics && computeFamily == graphicsFamily)
				{
					computeFamily = i;
				}

				if (transfer && !graphics && !compute && copyFamily == graphicsFamily)
				{
					copyFamily = i;
				}
			}

			ApiVersion deviceVersion = config.block != nullptr ? config.block->deviceVersion : ApiVersion{};
			if (deviceVersion.major == 0 && deviceVersion.minor == 0 && instance->apiVersion.major == 1 && instance->apiVersion.minor < 3)
			{
				deviceVersion = instance->apiVersion;
			}
			const auto [apiMajor, apiMinor]	 = resolve_api_version(deviceVersion);
			const std::uint32_t requestedApi = pack_vk_api_version(apiMajor, apiMinor);
			if (requestedApi > pack_vk_api_version(instance->apiVersion.major, instance->apiVersion.minor))
			{
				*error = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "requested Vulkan device version exceeds the instance's target version" };
				return nullptr;
			}
			if (apiMajor < 1 || (apiMajor == 1 && apiMinor < 2))
			{
				*error =
					Error{ .code = ErrorCode::eUnsupportedFeature, .message = "Vulkan below 1.2 is not supported: the backend requires Vulkan 1.2 or newer" };
				return nullptr;
			}
			if (requestedApi > phys.getProperties(instance->dispatch).apiVersion)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "requested Vulkan version exceeds the adapter's supported version",
				};
				return nullptr;
			}
			const bool core13 = apiMajor > 1 || (apiMajor == 1 && apiMinor >= 3);

			const auto enumeratedExts = phys.enumerateDeviceExtensionProperties<HostAllocatorAdapter<vk::ExtensionProperties>>(nullptr, instance->dispatch);
			if (enumeratedExts.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan device extension enumeration failed", enumeratedExts.result);
			}

			const detail::HostVector<vk::ExtensionProperties> & availableExts = enumeratedExts.value;
			const auto hasExt												  = [&availableExts](const char * name) noexcept
			{
				return std::ranges::any_of(
					availableExts,
					[name](const vk::ExtensionProperties & ep)
					{
						return std::strcmp(ep.extensionName, name) == 0;
					}
				);
			};

			const bool adapterHasDynamicRendering = core13 || hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			bool useDynamicRendering			  = false;
			switch (config.block != nullptr ? config.block->renderingLowering : native::VulkanRenderingLowering::eAutomatic)
			{
			case native::VulkanRenderingLowering::eRenderPassObjects: useDynamicRendering = false; break;
			case native::VulkanRenderingLowering::eAutomatic:		  useDynamicRendering = adapterHasDynamicRendering; break;
			case native::VulkanRenderingLowering::eDynamicRendering:
				if (!adapterHasDynamicRendering)
				{
					*error = Error{
						.code	 = ErrorCode::eUnsupportedFeature,
						.message = "dynamic rendering was required but the adapter does not support it",
					};
					return nullptr;
				}
				useDynamicRendering = true;
				break;
			}

			detail::HostVector<const char *> deviceExts{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
			if (hasExt("VK_KHR_portability_subset"))
			{
				deviceExts.push_back("VK_KHR_portability_subset");
			}
			if (!core13)
			{
				if (!hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
				{
					*error = Error{
						.code	 = ErrorCode::eUnsupportedFeature,
						.message = "Vulkan below 1.3 requires VK_KHR_synchronization2",
					};
					return nullptr;
				}
				deviceExts.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
				if (useDynamicRendering)
				{
					deviceExts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
				}
			}
			const bool hasCalibratedTimestamps = hasExt(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
			if (hasCalibratedTimestamps)
			{
				deviceExts.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
			}

			const bool hasExternalMemoryFd = hasExt(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
			if (hasExternalMemoryFd)
			{
				deviceExts.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
			}

			const bool hasExternalSemaphoreFd = hasExt(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
			if (hasExternalSemaphoreFd)
			{
				deviceExts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
			}

			bool hasExternalMemoryWin32	   = false;
			bool hasExternalSemaphoreWin32 = false;
#ifdef VK_USE_PLATFORM_WIN32_KHR
			hasExternalMemoryWin32 = hasExt(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
			if (hasExternalMemoryWin32)
			{
				deviceExts.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
			}

			hasExternalSemaphoreWin32 = hasExt(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
			if (hasExternalSemaphoreWin32)
			{
				deviceExts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
			}
#endif

			const bool hasConservativeRaster		= hasExt(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
			ConservativeRasterTier conservativeTier = ConservativeRasterTier::eNone;
			if (hasConservativeRaster)
			{
				deviceExts.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);

				vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeProps{};
				vk::PhysicalDeviceProperties2 props2{};
				props2.pNext = &conservativeProps;
				phys.getProperties2(&props2, instance->dispatch);

				conservativeTier = ConservativeRasterTier::eBasic;
				if (conservativeProps.degenerateTrianglesRasterized == 0u)
				{
					conservativeTier = conservativeProps.fullyCoveredFragmentShaderInputVariable != 0u ? ConservativeRasterTier::eInnerCoverage
																									   : ConservativeRasterTier::eDegenerateCulling;
				}
			}

			const bool adapterHasUnifiedLayouts = hasExt(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME);
			bool unifiedImageLayouts			= false;
			if (adapterHasUnifiedLayouts)
			{
				vk::PhysicalDeviceUnifiedImageLayoutsFeaturesKHR supportedUnified{};
				vk::PhysicalDeviceFeatures2 unifiedProbe{};
				unifiedProbe.pNext = &supportedUnified;
				phys.getFeatures2(&unifiedProbe, instance->dispatch);
				unifiedImageLayouts = static_cast<bool>(supportedUnified.unifiedImageLayouts);
			}
			if (unifiedImageLayouts)
			{
				deviceExts.push_back(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME);
			}

			if (config.block != nullptr)
			{
				for (const char * extra : config.block->deviceExtensions)
				{
					if (extra == nullptr)
					{
						continue;
					}

					if (!hasExt(extra))
					{
						*error = Error{
							.code	 = ErrorCode::eUnsupportedFeature,
							.message = "a device extension named in the Vulkan configuration block is not supported by the adapter",
						};
						return nullptr;
					}

					const bool alreadyEnabled = std::ranges::any_of(
						deviceExts,
						[extra](const char * name) noexcept
						{
							return std::strcmp(name, extra) == 0;
						}
					);
					if (!alreadyEnabled)
					{
						deviceExts.push_back(extra);
					}
				}
			}

			vk::PhysicalDeviceVulkan12Features supported12{};
			vk::PhysicalDeviceFeatures2 supportedFeatures2{};
			supportedFeatures2.pNext = &supported12;
			phys.getFeatures2(&supportedFeatures2, instance->dispatch);
			const bool bindless = static_cast<bool>(supported12.runtimeDescriptorArray) &&
								  static_cast<bool>(supported12.shaderSampledImageArrayNonUniformIndexing) &&
								  static_cast<bool>(supported12.descriptorBindingPartiallyBound);

			const bool shaderFloat16 = static_cast<bool>(supported12.shaderFloat16);

			const bool scalarBlockLayout = static_cast<bool>(supported12.scalarBlockLayout);

			const bool drawIndirectCount = static_cast<bool>(supported12.drawIndirectCount);

			vk::PhysicalDeviceFeatures2 features2;
			vk::PhysicalDeviceVulkan13Features features13;
			vk::PhysicalDeviceVulkan12Features features12;
			vk::PhysicalDeviceVulkan11Features features11;
			vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
			vk::PhysicalDeviceSynchronization2Features sync2Features;
			vk::PhysicalDeviceUnifiedImageLayoutsFeaturesKHR unifiedLayoutFeatures;
			if (core13)
			{
				features12.timelineSemaphore = VK_TRUE;
				features13.dynamicRendering	 = useDynamicRendering ? VK_TRUE : VK_FALSE;
				features13.synchronization2	 = VK_TRUE;
				features13.pNext			 = &features12;
				features2.pNext				 = &features13;
			}
			else
			{
				sync2Features.synchronization2 = VK_TRUE;
				features12.timelineSemaphore   = VK_TRUE;
				sync2Features.pNext			   = &features12;

				if (useDynamicRendering)
				{
					dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
					dynamicRenderingFeatures.pNext			  = &sync2Features;
					features2.pNext							  = &dynamicRenderingFeatures;
				}
				else
				{
					features2.pNext = &sync2Features;
				}
			}

			features12.pNext = &features11;

			PortabilitySubsetFeatures portabilityFeatures{};
			if (query_portability_subset_features(phys, instance->dispatch, portabilityFeatures))
			{
				portabilityFeatures.pNext = features11.pNext;
				features11.pNext		  = &portabilityFeatures;
			}
			if (unifiedImageLayouts)
			{
				unifiedLayoutFeatures.unifiedImageLayouts = VK_TRUE;
				unifiedLayoutFeatures.pNext				  = features11.pNext;
				features11.pNext						  = &unifiedLayoutFeatures;
			}
			if (bindless)
			{
				features12.runtimeDescriptorArray					 = VK_TRUE;
				features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
				features12.descriptorBindingPartiallyBound			 = VK_TRUE;
			}
			if (shaderFloat16)
			{
				features12.shaderFloat16 = VK_TRUE;
			}
			if (scalarBlockLayout)
			{
				features12.scalarBlockLayout = VK_TRUE;
			}

			if (drawIndirectCount)
			{
				features12.drawIndirectCount = VK_TRUE;
			}

			vk::PhysicalDeviceFeatures enabledFeatures{};
			for (const DeviceFeature feature : desc.requiredFeatures)
			{
				enable_feature_bit(enabledFeatures, features11, feature);
			}
			for (const DeviceFeature feature : desc.preferredFeatures)
			{
				if (adapter_supports_feature(phys, instance->dispatch, feature))
				{
					enable_feature_bit(enabledFeatures, features11, feature);
				}
			}
			features2.features = enabledFeatures;

			const QueuePlan plan = plan_queues(desc.queues);
			if (plan.computeDedicated && plan.computeCount > 0 && computeFamily == graphicsFamily)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "a dedicated compute queue was required but the adapter has no dedicated compute family",
				};
				return nullptr;
			}

			if (plan.copyDedicated && plan.copyCount > 0 && copyFamily == graphicsFamily)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "a dedicated copy queue was required but the adapter has no dedicated transfer family",
				};
				return nullptr;
			}

			struct FamilyQueues final
			{
				std::uint32_t family = 0;
				std::uint32_t count	 = 0;
				std::uint32_t cursor = 0;
			};

			detail::HostVector<FamilyQueues> families;
			const auto familySlot = [&families](std::uint32_t family) noexcept -> FamilyQueues *
			{
				for (FamilyQueues & entry : families)
				{
					if (entry.family == family)
					{
						return &entry;
					}
				}

				return nullptr;
			};
			const auto addRequest = [&](std::uint32_t family, std::uint32_t requested)
			{
				if (requested == 0)
				{
					return;
				}

				if (FamilyQueues * entry = familySlot(family))
				{
					entry->count += requested;
					return;
				}

				families.push_back(
					FamilyQueues{
						.family = family,
						.count	= requested,
					}
				);
			};
			addRequest(graphicsFamily, plan.graphicsCount);
			addRequest(computeFamily, plan.computeCount);
			addRequest(copyFamily, plan.copyCount);

			std::uint32_t maxQueuesPerFamily = 1;
			for (FamilyQueues & entry : families)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				entry.count = std::min(entry.count, qfs[entry.family].queueCount);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				maxQueuesPerFamily = std::max(maxQueuesPerFamily, entry.count);
			}

			const detail::HostVector<float> queuePriorities(maxQueuesPerFamily, 1.0f);
			detail::HostVector<vk::DeviceQueueCreateInfo> queueInfos;
			queueInfos.reserve(families.size());
			for (const FamilyQueues & entry : families)
			{
				queueInfos.push_back(vk::DeviceQueueCreateInfo({}, entry.family, entry.count, queuePriorities.data()));
			}

			vk::DeviceCreateInfo deviceInfo;
			deviceInfo.setQueueCreateInfos(queueInfos);
			deviceInfo.setPEnabledExtensionNames(deviceExts);
			deviceInfo.pNext = &features2;

			auto record = host_new<VulkanDevice>();
			if (record == nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan device allocation failed",
				};
				return nullptr;
			}

			const bool sharesExternally	   = hasExternalMemoryFd || hasExternalMemoryWin32 || hasExternalSemaphoreFd || hasExternalSemaphoreWin32;
			record->object				   = sharesExternally ? publishing_object<
																	Published<CoreDeviceApi, &core_device_block>,
																	Published<PresentApi, &present_block>,
																	Published<PlacedMemoryApi, &placed_memory_block>,
																	Published<QueryApi, &query_block>,
																	Published<PipelineCacheApi, &pipeline_cache_block>,
																	Published<ResidencyApi, &residency_block>,
																	Published<ResourceIntrospectionApi, &resource_introspection_block>,
																	Published<AdoptionApi, &adoption_block>,
																	Published<ExternalSharingApi, &external_sharing_block>>()
															  : publishing_object<
																	Published<CoreDeviceApi, &core_device_block>,
																	Published<PresentApi, &present_block>,
																	Published<PlacedMemoryApi, &placed_memory_block>,
																	Published<QueryApi, &query_block>,
																	Published<PipelineCacheApi, &pipeline_cache_block>,
																	Published<ResidencyApi, &residency_block>,
																	Published<ResourceIntrospectionApi, &resource_introspection_block>,
																	Published<AdoptionApi, &adoption_block>>();
			record->externalMemoryFd	   = hasExternalMemoryFd;
			record->externalMemoryWin32	   = hasExternalMemoryWin32;
			record->externalSemaphoreFd	   = hasExternalSemaphoreFd;
			record->externalSemaphoreWin32 = hasExternalSemaphoreWin32;
			record->instance			   = instance->instance;
			record->instanceWrapper		   = instance;
			record->phys				   = phys;

			record->dispatch = instance->dispatch;

			const auto createdDevice = phys.createDevice(deviceInfo, nullptr, record->dispatch);
			if (createdDevice.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan device creation failed", createdDevice.result);
			}

			record->device					  = createdDevice.value;
			record->graphicsFamily			  = graphicsFamily;
			record->computeFamily			  = computeFamily;
			record->copyFamily				  = copyFamily;
			record->graphicsFamilyBindsSparse = bindsSparse;
			record->validation				  = desc.validation;
			record->debugUtils				  = instance->debugUtils;
			record->debugNames				  = desc.enableDebugNames;
			record->debugLabels				  = desc.enableDebugLabels;
			record->apiVersionMajor			  = apiMajor;
			record->apiVersionMinor			  = apiMinor;
			record->coreVk13				  = core13;
			record->dynamicRendering		  = useDynamicRendering;
			record->unifiedImageLayouts		  = unifiedImageLayouts;

			std::uint32_t deviceTag = 0;
			if (!detail::device_tags().acquire(deviceTag))
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "no device tag is available, too many devices are alive at once",
				};
				return nullptr;
			}
			record->deviceTag = deviceTag;

			auto tagGuard = detail::make_scope_guard(
				[deviceTag] noexcept
				{
					detail::device_tags().release(deviceTag);
				}
			);

			record->bufferSlots.rebind(deviceTag);
			record->pipelineLayoutSlots.rebind(deviceTag);
			record->graphicsPipelineSlots.rebind(deviceTag);
			record->computePipelineSlots.rebind(deviceTag);
			record->timelineSlots.rebind(deviceTag);
			record->queryPoolSlots.rebind(deviceTag);
			record->samplerSlots.rebind(deviceTag);
			record->descriptorSetLayoutSlots.rebind(deviceTag);
			record->heapSlots.rebind(deviceTag);
			record->pipelineCacheSlots.rebind(deviceTag);
			record->binarySemaphoreSlots.rebind(deviceTag);
			record->textureSlots.rebind(deviceTag);
			record->textureViewSlots.rebind(deviceTag);
			record->descriptorSetSlots.rebind(deviceTag);

			record->dispatch.init(record->device);

			VULKAN_HPP_DEFAULT_DISPATCHER.init(record->device);

			const auto assignQueues = [&](detail::HostVector<vk::Queue> & out, std::uint32_t family, std::uint32_t requested)
			{
				if (requested == 0)
				{
					return;
				}

				FamilyQueues * entry	  = familySlot(family);
				const std::uint32_t avail = (entry->cursor < entry->count) ? std::min(requested, entry->count - entry->cursor) : 0;
				if (avail == 0)
				{
					out.push_back(record->device.getQueue(family, 0, record->dispatch));
					return;
				}

				for (std::uint32_t i = 0; i < avail; ++i)
				{
					out.push_back(record->device.getQueue(family, entry->cursor + i, record->dispatch));
				}

				entry->cursor += avail;
			};
			assignQueues(record->graphicsQueues, graphicsFamily, plan.graphicsCount);
			assignQueues(record->computeQueues, computeFamily, plan.computeCount);
			assignQueues(record->copyQueues, copyFamily, plan.copyCount);

			if (record->graphicsQueues.size() < plan.graphicsCount || record->computeQueues.size() < plan.computeCount ||
				record->copyQueues.size() < plan.copyCount)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "the adapter cannot supply the requested minimum number of queues for a queue type",
				};
				return nullptr;
			}

			record->caps.graphicsQueueCount		   = static_cast<std::uint32_t>(record->graphicsQueues.size());
			record->caps.computeQueueCount		   = static_cast<std::uint32_t>(record->computeQueues.size());
			record->caps.copyQueueCount			   = static_cast<std::uint32_t>(record->copyQueues.size());
			record->caps.hasDedicatedComputeQueue  = !record->computeQueues.empty() && computeFamily != graphicsFamily;
			record->caps.hasDedicatedTransferQueue = !record->copyQueues.empty() && copyFamily != graphicsFamily;

			// A VkCommandBuffer comes out of its pool rather than a capped queue, so nothing here bounds how many lists are open.
			record->caps.maxOpenCommandListsPerQueue = kUnlimitedOpenCommandLists;

			// Begin records no usage flags, so a drained buffer stays executable and may go again, which VUID 00071 allows once it is not pending.
			record->caps.supportsCommandListResubmit = true;

			if (!build_submit_timelines(record.get(), error))
			{
				return nullptr;
			}

			VmaVulkanFunctions vmaFns{};
			vmaFns.vkGetInstanceProcAddr = record->dispatch.vkGetInstanceProcAddr;
			vmaFns.vkGetDeviceProcAddr	 = record->dispatch.vkGetDeviceProcAddr;
			VmaAllocatorCreateInfo allocatorInfo{};
			allocatorInfo.physicalDevice   = phys;
			allocatorInfo.device		   = record->device;
			allocatorInfo.instance		   = instance->instance;
			allocatorInfo.vulkanApiVersion = requestedApi;
			allocatorInfo.pVulkanFunctions = &vmaFns;
#ifdef AZOTH_RHI_ENABLE_PROFILING
			constexpr VmaDeviceMemoryCallbacks kMemoryCallbacks{
				.pfnAllocate =
					[](VmaAllocator, uint32_t, VkDeviceMemory memory, VkDeviceSize size, void *)
				{
					AZO_RHI_PROFILE_ALLOC(static_cast<void *>(memory), size, "GPU");
				},
				.pfnFree =
					[](VmaAllocator, uint32_t, VkDeviceMemory memory, VkDeviceSize, void *)
				{
					AZO_RHI_PROFILE_FREE(static_cast<void *>(memory), "GPU");
				},
				.pUserData = nullptr,
			};

			allocatorInfo.pDeviceMemoryCallbacks = &kMemoryCallbacks;
#endif

			if (vmaCreateAllocator(&allocatorInfo, &record->allocator) != VK_SUCCESS)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfDeviceMemory,
					.message = "vmaCreateAllocator failed",
				};
				return nullptr;
			}

			const auto propsChain =
				phys.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties, vk::PhysicalDeviceIDProperties>(record->dispatch);
			const vk::PhysicalDeviceProperties & props = propsChain.get<vk::PhysicalDeviceProperties2>().properties;
			const auto & driverProps				   = propsChain.get<vk::PhysicalDeviceDriverProperties>();
			const vk::PhysicalDeviceLimits & limits	   = props.limits;
			const vk::PhysicalDeviceFeatures feats	   = phys.getFeatures(record->dispatch);

			record->adapterName		 = detail::HostString(props.deviceName.data());
			record->driverInfo		 = detail::HostString(driverProps.driverInfo.data());
			record->driverVersionStr = format_vulkan_driver_version(map_driver_id(driverProps.driverID), props.driverVersion);

			std::uint64_t deviceLocalBytes					  = 0;
			const vk::PhysicalDeviceMemoryProperties memProps = phys.getMemoryProperties(record->dispatch);
			for (std::uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (static_cast<bool>(memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal))
				{
					deviceLocalBytes += memProps.memoryHeaps[i].size;
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				}
			}

			record->adapter.type					  = map_adapter_type(props.deviceType);
			record->adapter.apiId					  = VulkanApi::kId;
			record->adapter.adapterIndex			  = adapterIndex;
			record->adapter.vendorId				  = props.vendorID;
			record->adapter.deviceId				  = props.deviceID;
			record->adapter.dedicatedVideoMemoryBytes = deviceLocalBytes;
			record->adapter.unifiedMemoryArchitecture =
				props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu || props.deviceType == vk::PhysicalDeviceType::eCpu;
			record->adapter.name			 = record->adapterName.c_str();
			record->adapter.driverId		 = map_driver_id(driverProps.driverID);
			record->adapter.driverVersionRaw = props.driverVersion;
			record->adapter.driverVersion	 = record->driverVersionStr.c_str();
			record->adapter.driverInfo		 = record->driverInfo.c_str();
			fill_adapter_identity(record->adapter, propsChain.get<vk::PhysicalDeviceIDProperties>());

			record->caps.apiId		= VulkanApi::kId;
			record->caps.apiVersion = ApiVersion{
				.major = apiMajor,
				.minor = apiMinor,
			};

			record->caps.supportsTimelineSync = static_cast<bool>(supported12.timelineSemaphore);

			record->caps.reportsValidationMessageCounts	   = instance != nullptr && static_cast<bool>(instance->debugMessenger);
			record->caps.supportsIndirectCount			   = drawIndirectCount;
			record->caps.supportsScaledBlit				   = true;
			record->caps.supportsDynamicBufferOffsets	   = true;
			record->caps.supportsMultiDrawIndirect		   = static_cast<bool>(enabledFeatures.multiDrawIndirect);
			record->caps.supportsDrawIndirectFirstInstance = static_cast<bool>(enabledFeatures.drawIndirectFirstInstance);
			record->caps.supportsShaderDrawParameters	   = static_cast<bool>(features11.shaderDrawParameters);
			record->caps.supportsShaderFloat16			   = shaderFloat16;
			record->caps.supportsScalarBlockLayout		   = scalarBlockLayout;
			record->caps.bindingTier					   = bindless ? BindingTier::eUnbounded : BindingTier::eBasic;
			record->caps.supportsPartiallyBoundDescriptors = bindless;
			record->caps.supportsUpdateAfterBind		   = static_cast<bool>(supported12.descriptorBindingSampledImageUpdateAfterBind);
			vk::TimeDomainEXT calibrationHostDomain		   = vk::TimeDomainEXT::eDevice;
			record->caps.supportsTimestampCalibration	= hasCalibratedTimestamps && vulkan_calibration_domains(phys, record->dispatch, calibrationHostDomain);
			record->caps.maxBindlessSampledTextures		= bindless ? limits.maxPerStageDescriptorSampledImages : 0u;
			record->caps.maxBindlessStorageBuffers		= bindless ? limits.maxPerStageDescriptorStorageBuffers : 0u;
			record->caps.maxSamplerDescriptors			= bindless ? limits.maxPerStageDescriptorSamplers : 0u;
			record->caps.supportsTimestampQueries		= static_cast<bool>(limits.timestampComputeAndGraphics);
			record->caps.supportsTimestampWritesInScope = record->caps.supportsTimestampQueries;
			// The spec forbids a timestamp write that happens after another in the same submission from carrying the lower value.
			record->caps.supportsOrderedTimestamps		   = record->caps.supportsTimestampQueries;
			record->caps.supportsAnisotropy				   = static_cast<bool>(enabledFeatures.samplerAnisotropy);
			record->caps.supportsIndependentBlend		   = static_cast<bool>(enabledFeatures.independentBlend);
			record->caps.supportsTextureViewSwizzle		   = adapter_supports_view_swizzle(phys, record->dispatch);
			record->caps.supportsMultiPlanarFormats		   = adapter_supports_multi_planar_formats(phys, record->dispatch);
			record->caps.supportsSamplerYcbcrConversion	   = static_cast<bool>(features11.samplerYcbcrConversion) && record->caps.supportsMultiPlanarFormats;
			record->caps.supportsDepthBounds			   = static_cast<bool>(feats.depthBounds);
			record->caps.supportsPipelineStatisticsQueries = static_cast<bool>(feats.pipelineStatisticsQuery);
			record->caps.conservativeRasterTier			   = conservativeTier;
			record->caps.shaderBinaryFormat				   = ShaderBinaryFormat::eSpirV;
			record->caps.sparseTier						   = SparseTier::eNone;
			if ((enabledFeatures.sparseBinding != 0u) && (enabledFeatures.sparseResidencyBuffer != 0u) && record->graphicsFamilyBindsSparse)
			{
				record->caps.sparseTier = SparseTier::eBuffers;
				if (enabledFeatures.sparseResidencyImage2D != 0u)
				{
					record->caps.sparseTier = (enabledFeatures.sparseResidencyImage3D != 0u) ? SparseTier::eResidentVolumes : SparseTier::eResidentTextures;
				}
			}

			if (record->caps.sparseTier > SparseTier::eNone)
			{
				vk::BufferCreateInfo probe{};
				probe.size	= 1;
				probe.usage = vk::BufferUsageFlagBits::eTransferDst;
				probe.flags = vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency;

				const auto probed = record->device.createBuffer(probe, nullptr, record->dispatch);
				if (probed.result == vk::Result::eSuccess)
				{
					record->caps.sparseTileSizeBytes = record->device.getBufferMemoryRequirements(probed.value, record->dispatch).alignment;
					record->device.destroyBuffer(probed.value, nullptr, record->dispatch);
				}
			}

			record->caps.maxColorAttachments = limits.maxColorAttachments;
			record->caps.maxRenderTargets	 = limits.maxColorAttachments;

			record->caps.maxDescriptorsPerSet = std::min(
				{
					limits.maxDescriptorSetSamplers,
					limits.maxDescriptorSetUniformBuffers,
					limits.maxDescriptorSetStorageBuffers,
					limits.maxDescriptorSetSampledImages,
					limits.maxDescriptorSetStorageImages,
				}
			);
			record->caps.maxDescriptorSets					= limits.maxBoundDescriptorSets;
			record->caps.maxPushConstantBytes				= limits.maxPushConstantsSize;
			record->caps.maxVertexBindings					= limits.maxVertexInputBindings;
			record->caps.maxVertexAttributes				= limits.maxVertexInputAttributes;
			record->caps.maxViewports						= limits.maxViewports;
			record->caps.maxTextureDimension1D				= limits.maxImageDimension1D;
			record->caps.maxTextureDimension2D				= limits.maxImageDimension2D;
			record->caps.maxTextureDimension3D				= limits.maxImageDimension3D;
			record->caps.maxTextureArrayLayers				= limits.maxImageArrayLayers;
			record->caps.minUniformBufferOffsetAlignment	= limits.minUniformBufferOffsetAlignment;
			record->caps.minStorageBufferOffsetAlignment	= limits.minStorageBufferOffsetAlignment;
			record->caps.minTexelBufferOffsetAlignment		= limits.minTexelBufferOffsetAlignment;
			record->caps.optimalBufferCopyOffsetAlignment	= limits.optimalBufferCopyOffsetAlignment;
			record->caps.optimalBufferCopyRowPitchAlignment = limits.optimalBufferCopyRowPitchAlignment;
			record->caps.timestampPeriodNanoseconds			= limits.timestampPeriod;
			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			record->caps.timestampValidBits = qfs[graphicsFamily].timestampValidBits;
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			VulkanDevice * raw = record.get();
			if (!detail::try_push_back(owner.devices, std::move(record)))
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan device allocation failed",
				};
				return nullptr;
			}

			tagGuard.dismiss();
			return raw;
		}

		[[nodiscard]] static bool InstanceStillUsed(const VulkanBackendOwner & owner, const VulkanInstance * instance) noexcept
		{
			for (const HostUniquePtr<VulkanDevice> & device : owner.devices)
			{
				if (device->instanceWrapper == instance)
				{
					return true;
				}
			}

			return false;
		}

		void vulkan_destroy_device(void * impl) noexcept
		{
			VulkanBackendOwner & owner = backend_owner();

			VulkanInstance * owningInstance = nullptr;
			std::uint32_t releasedTag		= 0;
			bool stalled					= false;
			for (const HostUniquePtr<VulkanDevice> & device : owner.devices)
			{
				if (device.get() == impl)
				{
					owningInstance = device->instanceWrapper;
					releasedTag	   = device->deviceTag;
					stalled		   = static_cast<bool>(device->device) && !device->drain_submit_timelines();
					break;
				}
			}

			// Work that did not drain references this device's objects and the instance they were created on, so both records are
			// released rather than destroyed. The device tag stays taken too, because handles carrying it are still alive.
			if (stalled)
			{
				auto * record = static_cast<VulkanDevice *>(impl);
				record->report_teardown_stall();

				for (HostUniquePtr<VulkanDevice> & device : owner.devices)
				{
					if (device.get() == impl)
					{
						static_cast<void>(device.release());
						break;
					}
				}

				std::erase_if(
					owner.devices,
					[](const HostUniquePtr<VulkanDevice> & device)
					{
						return device == nullptr;
					}
				);

				if (owningInstance != nullptr && !InstanceStillUsed(owner, owningInstance))
				{
					for (HostUniquePtr<VulkanInstance> & instance : owner.instances)
					{
						if (instance.get() == owningInstance)
						{
							static_cast<void>(instance.release());
							break;
						}
					}

					std::erase_if(
						owner.instances,
						[](const HostUniquePtr<VulkanInstance> & instance)
						{
							return instance == nullptr;
						}
					);
				}

				return;
			}

			std::erase_if(
				owner.devices,
				[impl](const HostUniquePtr<VulkanDevice> & device)
				{
					return device.get() == impl;
				}
			);
			detail::device_tags().release(releasedTag);

			if ((owningInstance != nullptr) && (!InstanceStillUsed(owner, owningInstance)))

			{
				std::erase_if(
					owner.instances,
					[owningInstance](const HostUniquePtr<VulkanInstance> & instance)
					{
						return instance.get() == owningInstance;
					}
				);
			}
		}

		void vulkan_destroy_instance(void * impl) noexcept
		{
			VulkanBackendOwner & owner = backend_owner();
			std::erase_if(
				owner.instances,
				[impl](const HostUniquePtr<VulkanInstance> & instance)
				{
					return instance.get() == impl;
				}
			);
		}

		bool succeed(Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = {};
			}

			return true;
		}

		bool fail(Error * error, ErrorCode code, const char * message) noexcept
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

		bool fail_native(Error * error, const char * message, vk::Result result) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{
					.code		= ErrorCode::eNativeApiError,
					.nativeCode = static_cast<std::int32_t>(result),
					.message	= message,
				};
			}

			return false;
		}

		bool fail_allocation(Error * error, const char * message, vk::Result result) noexcept
		{
			if (error != nullptr)
			{
				*error = Error{
					.code		= ErrorCode::eOutOfDeviceMemory,
					.nativeCode = static_cast<std::int32_t>(result),
					.message	= message,
				};
			}

			return false;
		}

		GraphicsApiId vulkan_device_api_id([[maybe_unused]] void * impl) noexcept
		{
			return VulkanApi::kId;
		}

		std::string_view vulkan_device_api_name([[maybe_unused]] void * impl) noexcept
		{
			return VulkanApi::kDisplayName;
		}

		const DeviceCaps & vulkan_device_caps(void * impl) noexcept
		{
			return static_cast<VulkanDevice *>(impl)->caps;
		}

		const AdapterInfo & vulkan_device_adapter_info(void * impl) noexcept
		{
			return static_cast<VulkanDevice *>(impl)->adapter;
		}

		ValidationMessageCounts vulkan_device_validation_message_counts(void * impl) noexcept
		{
			const VulkanInstance * inst = static_cast<VulkanDevice *>(impl)->instanceWrapper;
			if (inst == nullptr)
			{
				return {};
			}

			return ValidationMessageCounts{
				.errors	  = inst->validationErrors.load(std::memory_order_relaxed),
				.warnings = inst->validationWarnings.load(std::memory_order_relaxed),
			};
		}

		bool vulkan_get_texture_info(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getTextureInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
			}

			const TextureSlot * const slot = device->textureSlots.resolve(texture, false);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
			}

			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
			}

			VmaAllocationInfo allocationInfo{};
			if (slot->allocation != nullptr)
			{
				vmaGetAllocationInfo(device->allocator, slot->allocation, &allocationInfo);
			}

			*out = TextureInfo{ .desc = slot->desc, .allocationSize = allocationInfo.size };
			return true;
		}

		bool vulkan_get_buffer_info(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getBufferInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
			}

			const BufferSlot * const slot = device->bufferSlots.resolve(buffer, false);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
			}

			VmaAllocationInfo allocationInfo{};
			if (slot->allocation != nullptr)
			{
				vmaGetAllocationInfo(device->allocator, slot->allocation, &allocationInfo);
			}

			const MemoryAccess access =
				slot->hostVisible ? (slot->coherent ? MemoryAccess::eCpuVisibleCoherent : MemoryAccess::eCpuVisible) : MemoryAccess::eGpuOnly;

			*out = BufferInfo{ .desc = slot->desc, .allocationSize = allocationInfo.size, .memoryAccess = access };
			return true;
		}

		FormatSupport vulkan_device_format_support(void * impl, Format format) noexcept
		{
			const vk::Format vkFormat = map_format(format);
			if (format == Format::eUndefined || vkFormat == vk::Format::eUndefined)
			{
				return FormatSupport{ .format = format };
			}

			const auto * device					 = static_cast<const VulkanDevice *>(impl);
			const vk::FormatProperties props	 = device->phys.getFormatProperties(vkFormat, device->dispatch);
			const vk::FormatFeatureFlags optimal = props.optimalTilingFeatures;
			const auto has						 = [optimal](vk::FormatFeatureFlagBits bit) noexcept
			{
				return static_cast<bool>(optimal & bit);
			};

			return FormatSupport{
				.format					= format,
				.sampled				= has(vk::FormatFeatureFlagBits::eSampledImage),
				.storage				= has(vk::FormatFeatureFlagBits::eStorageImage),
				.colorAttachment		= has(vk::FormatFeatureFlagBits::eColorAttachment),
				.depthStencilAttachment = has(vk::FormatFeatureFlagBits::eDepthStencilAttachment),
				.copySrc				= has(vk::FormatFeatureFlagBits::eTransferSrc),
				.copyDst				= has(vk::FormatFeatureFlagBits::eTransferDst),
				.linearFiltering		= has(vk::FormatFeatureFlagBits::eSampledImageFilterLinear),
				.blendable				= has(vk::FormatFeatureFlagBits::eColorAttachmentBlend),
				.blitSrc				= has(vk::FormatFeatureFlagBits::eBlitSrc),
				.blitDst				= has(vk::FormatFeatureFlagBits::eBlitDst),
			};
		}

		void name_vulkan_object(const VulkanDevice * device, vk::ObjectType type, std::uint64_t handle, CString name) noexcept
		{
			if (!device->debugNames || !device->debugUtils || name == nullptr || *name == '\0' || handle == 0)
			{
				return;
			}

			static_cast<void>(device->device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(type, handle, name), device->dispatch));
		}

		namespace
		{
			[[nodiscard]] BufferHandle vulkan_create_sparse_buffer(VulkanDevice * device, const BufferDesc & desc, Error * error) noexcept
			{
				if (device->caps.sparseTier < SparseTier::eBuffers)
				{
					return fail_value<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "this device cannot bind sparse memory to a buffer");
				}

				const vk::BufferCreateInfo bufferInfo(
					vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency,
					desc.size,
					vk::BufferUsageFlags(map_buffer_usage(desc.usage)),
					vk::SharingMode::eExclusive
				);

				const auto created = device->device.createBuffer(bufferInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return fail_value<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "vkCreateBuffer failed for a sparse buffer");
				}

				auto raw = static_cast<VkBuffer>(created.value);
				name_vulkan_object(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.debugName);

				const BufferHandle handle =
					device->bufferSlots.store(BufferSlot{ .buffer = raw, .size = desc.size, .sparse = true, .desc = detail::recorded(desc) });
				if (!handle.is_valid())
				{
					device->device.destroyBuffer(created.value, nullptr, device->dispatch);
					return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan buffer handle tracking failed");
				}

				return return_value(handle, error);
			}
		}

		BufferHandle vulkan_create_buffer(void * impl, const BufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createBuffer");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!vulkan_refuse_unexportable(
					device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eBuffer,
					Format::eUndefined,
					map_buffer_usage(desc.usage),
					"buffer creation asked for an external handle type this adapter cannot export",
					error
				))
			{
				return BufferHandle{};
			}
			if (desc.size == 0)
			{
				return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}
			if (!vulkan_refuse_ray_tracing_usage(desc.usage, device->caps.supportsRayTracing, error))
			{
				return BufferHandle{};
			}

			if (desc.allowSparseBinding)
			{
				return vulkan_create_sparse_buffer(device, desc, error);
			}

			vk::BufferCreateInfo bufferCreateInfo({}, desc.size, map_buffer_usage(desc.usage), vk::SharingMode::eExclusive);

			vk::ExternalMemoryBufferCreateInfo externalBufferInfo;
			vk::ExportMemoryAllocateInfo exportInfo;
			const bool exportable = !desc.exportableHandleTypes.empty();
			if (exportable)
			{
				externalBufferInfo.handleTypes = map_memory_handle_types(desc.exportableHandleTypes);
				exportInfo.handleTypes		   = externalBufferInfo.handleTypes;
				bufferCreateInfo.pNext		   = &externalBufferInfo;
			}

			const VkBufferCreateInfo bufferInfo = bufferCreateInfo;
			VmaAllocationCreateFlags allocFlags = 0;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = map_buffer_memory_usage(desc.memory, allocFlags);
			allocInfo.flags = allocFlags;

			VkBuffer raw			 = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			if (exportable)
			{
				if (const VkResult made = vmaCreateDedicatedBuffer(device->allocator, &bufferInfo, &allocInfo, &exportInfo, &raw, &allocation, nullptr);
					made != VK_SUCCESS)
				{
					return fail_allocation_value<BufferHandle>(
						error,
						"vmaCreateDedicatedBuffer failed for an exportable buffer",
						static_cast<vk::Result>(made)
					);
				}
			}
			else if (const VkResult made = vmaCreateBuffer(device->allocator, &bufferInfo, &allocInfo, &raw, &allocation, nullptr); made != VK_SUCCESS)
			{
				return fail_allocation_value<BufferHandle>(error, "vmaCreateBuffer failed", static_cast<vk::Result>(made));
			}

			name_vulkan_object(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.debugName);

			VkMemoryPropertyFlags memFlags = 0;
			vmaGetAllocationMemoryProperties(device->allocator, allocation, &memFlags);

			const bool mappable = (allocFlags & (VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)) != 0;

			VmaAllocationInfo allocated{};
			vmaGetAllocationInfo(device->allocator, allocation, &allocated);
			if (mappable && allocated.pMappedData == nullptr)
			{
				vmaDestroyBuffer(device->allocator, raw, allocation);
				return fail_value<BufferHandle>(error, ErrorCode::eNativeApiError, "a host visible buffer came back from VMA without the mapping it asked for");
			}

			const BufferHandle handle = device->bufferSlots.store(
				BufferSlot{
					.buffer				   = raw,
					.allocation			   = allocation,
					.size				   = desc.size,
					.coherent			   = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0,
					.hostVisible		   = mappable,
					.mapped				   = allocated.pMappedData,
					.exportableHandleTypes = desc.exportableHandleTypes,
					.desc				   = detail::recorded(desc),
				}
			);
			if (!handle.is_valid())
			{
				vmaDestroyBuffer(device->allocator, raw, allocation);
				return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan buffer handle tracking failed");
			}

			return return_value(handle, error);
		}

		MappedMemory vulkan_map(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.map");
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = resolve_buffer(device, handle);
			if (slot == nullptr)
			{
				return fail_value<MappedMemory>(error, ErrorCode::eInvalidHandle, "map of an invalid buffer handle");
			}

			if (!slot->hostVisible)
			{
				return fail_value<MappedMemory>(error, ErrorCode::eInvalidArgument, "map of a buffer whose memory is not host visible");
			}

			std::uint64_t mapSize = desc.size;
			if (!bound_buffer_range(slot->size, desc.offset, mapSize))
			{
				return fail_value<MappedMemory>(error, ErrorCode::eInvalidArgument, "map range is outside the buffer");
			}

			void * mapped = slot->mapped;
			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				const HeapSlot * heap = device->heapSlots.resolve(slot->placedHeap, true);
				if (heap == nullptr)
				{
					return fail_value<MappedMemory>(error, ErrorCode::eInvalidState, "map of a placed buffer whose heap was destroyed");
				}

				mapped = static_cast<std::uint8_t *>(heap->mapped) + slot->placedOffset;
			}

			if (!slot->mapCount.try_acquire())
			{
				return fail_value<MappedMemory>(error, ErrorCode::eInvalidState, kMapCountWouldOverflow);
			}

			return return_value(
				MappedMemory{
					.data	  = static_cast<std::uint8_t *>(mapped) + desc.offset,
					.size	  = mapSize,
					.coherent = slot->coherent,
				},
				error
			);
		}

		bool vulkan_unmap(void * impl, BufferHandle handle, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = resolve_buffer(device, handle);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "unmap of an invalid buffer handle");
			}

			if (!slot->mapCount.try_release())
			{
				return fail(error, ErrorCode::eInvalidState, "unmap of a buffer with no map outstanding");
			}

			return succeed(error);
		}

		bool vulkan_flush_mapped_range(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = resolve_buffer(device, handle);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "flush of an invalid buffer handle");
			}

			std::uint64_t bounded = size;
			if (!bound_buffer_range(slot->size, offset, bounded))
			{
				return fail(error, ErrorCode::eInvalidArgument, "flush range is outside the buffer");
			}

			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				return succeed(error);
			}

			if (const VkResult flushed = vmaFlushAllocation(device->allocator, slot->allocation, offset, bounded); flushed != VK_SUCCESS)
			{
				return fail_native(error, "vmaFlushAllocation failed", static_cast<vk::Result>(flushed));
			}

			return succeed(error);
		}

		bool vulkan_invalidate_mapped_range(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = resolve_buffer(device, handle);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "invalidate of an invalid buffer handle");
			}
			std::uint64_t bounded = size;
			if (!bound_buffer_range(slot->size, offset, bounded))
			{
				return fail(error, ErrorCode::eInvalidArgument, "invalidate range is outside the buffer");
			}

			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				return succeed(error);
			}

			if (const VkResult invalidated = vmaInvalidateAllocation(device->allocator, slot->allocation, offset, bounded); invalidated != VK_SUCCESS)
			{
				return fail_native(error, "vmaInvalidateAllocation failed", static_cast<vk::Result>(invalidated));
			}
			return succeed(error);
		}

		namespace
		{
			[[nodiscard]] vk::ImageCreateFlags texture_create_flags(const TextureDesc & desc) noexcept
			{
				vk::ImageCreateFlags flags{};
				if (desc.type == TextureType::eTexCube)
				{
					flags |= vk::ImageCreateFlagBits::eCubeCompatible;
				}
				if (desc.allowFormatViews || is_multi_planar_format(desc.format))
				{
					flags |= vk::ImageCreateFlagBits::eMutableFormat;
				}
				return flags;
			}
		}

		bool vulkan_image_create_info(const TextureDesc & desc, vk::ImageCreateInfo & out, Error * error) noexcept
		{
			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return fail(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}

			if (desc.mipLevels > detail::max_mip_levels(desc.width, desc.height, desc.type == TextureType::eTex3D ? desc.depth : 1))
			{
				return fail(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}

			const vk::Format format = map_format(desc.format);
			if (format == vk::Format::eUndefined)
			{
				return fail(error, ErrorCode::eUnsupportedFormat, "Vulkan texture: undefined or unsupported format");
			}

			out.flags		  = texture_create_flags(desc);
			out.imageType	  = map_image_type(desc.type);
			out.format		  = format;
			out.extent		  = vk::Extent3D{ desc.width, desc.height, desc.depth };
			out.mipLevels	  = desc.mipLevels;
			out.arrayLayers	  = desc.arrayLayers;
			out.samples		  = map_sample_count(desc.samples);
			out.tiling		  = vk::ImageTiling::eOptimal;
			out.usage		  = map_texture_usage(desc.usage);
			out.sharingMode	  = vk::SharingMode::eExclusive;
			out.initialLayout = vk::ImageLayout::eUndefined;
			return succeed(error);
		}

		TextureHandle vulkan_finish_texture(VulkanDevice * device, const TextureDesc & desc, VkImage image, VmaAllocation allocation, Error * error) noexcept
		{
			name_vulkan_object(device, vk::ObjectType::eImage, std::bit_cast<std::uint64_t>(image), desc.debugName);

			constexpr vk::ImageUsageFlags kViewCapableUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
															  vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
															  vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment;

			const vk::Format format = map_format(desc.format);
			VkImageView view		= VK_NULL_HANDLE;
			if ((map_texture_usage(desc.usage) & kViewCapableUsage) && !is_multi_planar_format(desc.format))
			{
				const vk::ImageAspectFlags aspect = is_depth_format(desc.format) ? vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eDepth }
																				 : vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor };
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image			  = image;
				viewInfo.viewType		  = map_view_type(desc.type);
				viewInfo.format			  = format;
				viewInfo.subresourceRange = vk::ImageSubresourceRange{ aspect, 0, desc.mipLevels, 0, desc.arrayLayers };

				const auto created = device->device.createImageView(viewInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					vmaDestroyImage(device->allocator, image, allocation);
					return fail_native_value<TextureHandle>(error, "Vulkan texture creation failed", created.result);
				}

				view = created.value;
			}

			const TextureHandle handle = device->textureSlots.store(
				TextureSlot{
					.image				   = image,
					.allocation			   = allocation,
					.defaultView		   = view,
					.format				   = format,
					.samples			   = map_sample_count(desc.samples),
					.mipLevels			   = desc.mipLevels,
					.arrayLayers		   = desc.arrayLayers,
					.width				   = desc.width,
					.height				   = desc.height,
					.depth				   = desc.depth,
					.rhiFormat			   = desc.format,
					.usage				   = desc.usage,
					.mutableFormat		   = desc.allowFormatViews,
					.sparse				   = desc.allowSparseBinding,
					.exportableHandleTypes = desc.exportableHandleTypes,
					.desc				   = detail::recorded(desc),
				}
			);
			if (!handle.is_valid())
			{
				if (view != VK_NULL_HANDLE)
				{
					device->device.destroyImageView(view, nullptr, device->dispatch);
				}

				vmaDestroyImage(device->allocator, image, allocation);
				return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan texture handle tracking failed");
			}

			return return_value(handle, error);
		}

		TextureHandle vulkan_create_texture(void * impl, const TextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createTexture");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!vulkan_refuse_unexportable(
					device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eTexture,
					desc.format,
					kExternalQueryBufferUsage,
					"texture creation asked for an external handle type this adapter cannot export for that format",
					error
				))
			{
				return TextureHandle{};
			}

			vk::ImageCreateInfo imageInfo{};
			if (!vulkan_image_create_info(desc, imageInfo, error))
			{
				return TextureHandle{};
			}

			VkImage image			 = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			if (desc.allowSparseBinding)
			{
				const SparseTier needed = desc.depth > 1 ? SparseTier::eResidentVolumes : SparseTier::eResidentTextures;
				if (device->caps.sparseTier < needed)
				{
					return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "this device cannot bind sparse memory to a texture of that shape");
				}

				imageInfo.flags |= vk::ImageCreateFlagBits::eSparseBinding | vk::ImageCreateFlagBits::eSparseResidency;
			}

			vk::ExternalMemoryImageCreateInfo externalImageInfo;
			vk::ExportMemoryAllocateInfo exportInfo;
			const bool exportable = !desc.exportableHandleTypes.empty();
			if (exportable)
			{
				externalImageInfo.handleTypes = map_memory_handle_types(desc.exportableHandleTypes);
				exportInfo.handleTypes		  = externalImageInfo.handleTypes;
				imageInfo.pNext				  = &externalImageInfo;
			}

			const VkImageCreateInfo cImageInfo	= imageInfo;
			VmaAllocationCreateFlags allocFlags = 0;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = map_memory_usage(desc.memory, allocFlags);
			allocInfo.flags = allocFlags;

			if (desc.allowSparseBinding)
			{
				const auto created = device->device.createImage(imageInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return fail_allocation_value<TextureHandle>(error, "vkCreateImage failed for a sparse texture", created.result);
				}

				image = static_cast<VkImage>(created.value);
			}
			else if (exportable)
			{
				if (const VkResult made = vmaCreateDedicatedImage(device->allocator, &cImageInfo, &allocInfo, &exportInfo, &image, &allocation, nullptr);
					made != VK_SUCCESS)
				{
					return fail_allocation_value<TextureHandle>(
						error,
						"vmaCreateDedicatedImage failed for an exportable texture",
						static_cast<vk::Result>(made)
					);
				}
			}
			else if (const VkResult made = vmaCreateImage(device->allocator, &cImageInfo, &allocInfo, &image, &allocation, nullptr); made != VK_SUCCESS)
			{
				return fail_allocation_value<TextureHandle>(error, "vmaCreateImage failed", static_cast<vk::Result>(made));
			}

			return vulkan_finish_texture(device, desc, image, allocation, error);
		}

		[[nodiscard]] bool find_memory_type_for_heap(
			vk::PhysicalDevice phys,
			const vk::detail::DispatchLoaderDynamic & dispatch,
			HeapType type,
			std::uint32_t & outIndex,
			bool & outHostVisible,
			bool & outCoherent
		) noexcept
		{
			const vk::PhysicalDeviceMemoryProperties props = phys.getMemoryProperties(dispatch);
			const bool wantHost							   = type == HeapType::eCpuUpload || type == HeapType::eCpuReadback;
			const vk::MemoryPropertyFlags required		   = wantHost ? (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
																	  : vk::MemoryPropertyFlags{ vk::MemoryPropertyFlagBits::eDeviceLocal };
			for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const vk::MemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if ((flags & required) == required)
				{
					outIndex	   = i;
					outHostVisible = static_cast<bool>(flags & vk::MemoryPropertyFlagBits::eHostVisible);
					outCoherent	   = static_cast<bool>(flags & vk::MemoryPropertyFlagBits::eHostCoherent);
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] HeapSlot * resolve_heap(VulkanDevice * device, HeapHandle handle) noexcept
		{
			return device->heapSlots.resolve(handle, kHandleAlreadyChecked);
		}

		HeapHandle vulkan_create_heap(void * impl, const HeapDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createHeap");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!vulkan_refuse_unexportable(
					device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eHeap,
					Format::eUndefined,
					kExternalQueryBufferUsage,
					"heap creation asked for an external handle type this adapter cannot export",
					error
				))
			{
				return HeapHandle{};
			}
			if (desc.size == 0)
			{
				return fail_value<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be greater than zero");
			}

			std::uint32_t typeIndex = 0;
			bool hostVisible		= false;
			bool coherent			= false;
			if (!find_memory_type_for_heap(device->phys, device->dispatch, desc.type, typeIndex, hostVisible, coherent))
			{
				return fail_value<HeapHandle>(error, ErrorCode::eUnsupportedFeature, "no memory type matches the requested heap");
			}

			vk::MemoryAllocateInfo allocateInfo(desc.size, typeIndex);
			vk::ExportMemoryAllocateInfo exportInfo;
			if (!desc.exportableHandleTypes.empty())
			{
				exportInfo.handleTypes = map_memory_handle_types(desc.exportableHandleTypes);
				allocateInfo.pNext	   = &exportInfo;
			}

			const auto allocated = device->device.allocateMemory(allocateInfo, nullptr, device->dispatch);
			if (allocated.result != vk::Result::eSuccess)
			{
				return fail_value<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "Vulkan heap allocation failed");
			}

			// One mapping for the whole heap, taken here and given up only by vkFreeMemory, so the placed
			// buffers sharing it never race to map the memory a sibling already mapped.
			void * mapped = nullptr;
			if (hostVisible)
			{
				if (const vk::Result mapResult = device->device.mapMemory(allocated.value, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &mapped, device->dispatch);
					mapResult != vk::Result::eSuccess)
				{
					device->device.freeMemory(allocated.value, nullptr, device->dispatch);
					return fail_native_value<HeapHandle>(error, "vkMapMemory failed for a host visible heap", mapResult);
				}
			}

			const HeapHandle handle = device->heapSlots.store(
				HeapSlot{
					.memory				   = allocated.value,
					.size				   = desc.size,
					.memoryTypeIndex	   = typeIndex,
					.hostVisible		   = hostVisible,
					.coherent			   = coherent,
					.mapped				   = mapped,
					.exportableHandleTypes = desc.exportableHandleTypes,
				}
			);
			if (!handle.is_valid())
			{
				device->device.freeMemory(allocated.value, nullptr, device->dispatch);
				return fail_value<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan heap handle tracking failed");
			}

			return return_value(handle, error);
		}

		BufferHandle vulkan_create_placed_buffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPlacedBuffer");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (desc.buffer.size == 0)
			{
				return fail_value<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be greater than zero");
			}
			if (!vulkan_refuse_ray_tracing_usage(desc.buffer.usage, device->caps.supportsRayTracing, error))
			{
				return BufferHandle{};
			}

			HeapSlot heap{};
			{
				const HeapSlot * slot = resolve_heap(device, desc.heap);
				if (slot == nullptr)
				{
					return fail_value<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer with an invalid heap handle");
				}
				heap = *slot;
			}

			const auto created = device->device.createBuffer(
				vk::BufferCreateInfo({}, desc.buffer.size, map_buffer_usage(desc.buffer.usage), vk::SharingMode::eExclusive),
				nullptr,
				device->dispatch
			);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<BufferHandle>(error, "Vulkan placed buffer creation failed", created.result);
			}

			const vk::Buffer buffer			  = created.value;
			const vk::MemoryRequirements reqs = device->device.getBufferMemoryRequirements(buffer, device->dispatch);
			if (((reqs.memoryTypeBits & (1u << heap.memoryTypeIndex)) == 0) || (desc.offset % reqs.alignment) != 0 || desc.offset > heap.size ||
				reqs.size > heap.size - desc.offset)
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return fail_value<BufferHandle>(error, ErrorCode::eValidationFailed, "placed buffer does not fit the heap (memory type, alignment, or range)");
			}

			if (const vk::Result bound = device->device.bindBufferMemory(buffer, heap.memory, desc.offset, device->dispatch); bound != vk::Result::eSuccess)
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return fail_native_value<BufferHandle>(error, "Vulkan placed buffer memory binding failed", bound);
			}

			const BufferHandle handle = device->bufferSlots.store(
				BufferSlot{
					.buffer		  = buffer,
					.allocation	  = nullptr,
					.size		  = desc.buffer.size,
					.coherent	  = heap.coherent,
					.hostVisible  = heap.hostVisible,
					.placedMemory = heap.memory,
					.placedOffset = desc.offset,
					.placedHeap	  = desc.heap,
					.desc		  = detail::recorded(desc.buffer),
				}
			);
			if (!handle.is_valid())
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return fail_value<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan placed buffer handle tracking failed");
			}

			return return_value(handle, error);
		}

		TextureHandle vulkan_create_placed_texture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPlacedTexture");
			auto * device		  = static_cast<VulkanDevice *>(impl);
			const TextureDesc & t = desc.texture;
			if (t.width == 0 || t.height == 0 || t.depth == 0)
			{
				return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}
			if (t.mipLevels > detail::max_mip_levels(t.width, t.height, t.type == TextureType::eTex3D ? t.depth : 1))
			{
				return fail_value<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}
			const vk::Format format = map_format(t.format);
			if (format == vk::Format::eUndefined)
			{
				return fail_value<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "placed texture: undefined or unsupported format");
			}

			HeapSlot heap{};
			{
				const HeapSlot * slot = resolve_heap(device, desc.heap);
				if (slot == nullptr)
				{
					return fail_value<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture with an invalid heap handle");
				}
				heap = *slot;
			}

			const std::uint32_t layers = t.arrayLayers;
			vk::ImageCreateInfo imageInfo{};
			imageInfo.flags			= texture_create_flags(t);
			imageInfo.imageType		= map_image_type(t.type);
			imageInfo.format		= format;
			imageInfo.extent		= vk::Extent3D{ t.width, t.height, t.depth };
			imageInfo.mipLevels		= t.mipLevels;
			imageInfo.arrayLayers	= layers;
			imageInfo.samples		= map_sample_count(t.samples);
			imageInfo.tiling		= vk::ImageTiling::eOptimal;
			imageInfo.usage			= map_texture_usage(t.usage);
			imageInfo.sharingMode	= vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;

			const auto createdImage = device->device.createImage(imageInfo, nullptr, device->dispatch);
			if (createdImage.result != vk::Result::eSuccess)
			{
				return fail_native_value<TextureHandle>(error, "Vulkan placed texture creation failed", createdImage.result);
			}

			const vk::Image image			  = createdImage.value;
			const vk::MemoryRequirements reqs = device->device.getImageMemoryRequirements(image, device->dispatch);
			if (((reqs.memoryTypeBits & (1u << heap.memoryTypeIndex)) == 0) || (desc.offset % reqs.alignment) != 0 || desc.offset > heap.size ||
				reqs.size > heap.size - desc.offset)
			{
				device->device.destroyImage(image, nullptr, device->dispatch);
				return fail_value<TextureHandle>(
					error,
					ErrorCode::eValidationFailed,
					"placed texture does not fit the heap (memory type, alignment, or range)"
				);
			}

			if (const vk::Result bound = device->device.bindImageMemory(image, heap.memory, desc.offset, device->dispatch); bound != vk::Result::eSuccess)
			{
				device->device.destroyImage(image, nullptr, device->dispatch);
				return fail_native_value<TextureHandle>(error, "Vulkan placed texture memory binding failed", bound);
			}

			const vk::ImageAspectFlags aspect =
				is_depth_format(t.format) ? vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eDepth } : vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor };
			VkImageView placedView = VK_NULL_HANDLE;
			if (!is_multi_planar_format(t.format))
			{
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image			  = image;
				viewInfo.viewType		  = map_view_type(t.type);
				viewInfo.format			  = format;
				viewInfo.subresourceRange = vk::ImageSubresourceRange{ aspect, 0, t.mipLevels, 0, layers };

				const auto createdView = device->device.createImageView(viewInfo, nullptr, device->dispatch);
				if (createdView.result != vk::Result::eSuccess)
				{
					device->device.destroyImage(image, nullptr, device->dispatch);
					return fail_native_value<TextureHandle>(error, "Vulkan placed texture view creation failed", createdView.result);
				}
				placedView = createdView.value;
			}

			const TextureHandle handle = device->textureSlots.store(
				TextureSlot{
					.image		   = image,
					.allocation	   = nullptr,
					.defaultView   = placedView,
					.format		   = format,
					.samples	   = map_sample_count(t.samples),
					.mipLevels	   = t.mipLevels,
					.arrayLayers   = layers,
					.width		   = t.width,
					.height		   = t.height,
					.depth		   = t.depth,
					.rhiFormat	   = t.format,
					.usage		   = t.usage,
					.mutableFormat = t.allowFormatViews,
					.desc		   = detail::recorded(t),
				}
			);
			if (!handle.is_valid())
			{
				if (placedView != VK_NULL_HANDLE)
				{
					device->device.destroyImageView(placedView, nullptr, device->dispatch);
				}
				device->device.destroyImage(image, nullptr, device->dispatch);
				return fail_value<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan placed texture handle tracking failed");
			}

			return return_value(handle, error);
		}

		bool vulkan_get_texture_memory_info(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getTextureMemoryInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getTextureMemoryInfo output pointer is null");
			}
			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return fail(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}
			const vk::Format format = map_format(desc.format);
			if (format == vk::Format::eUndefined)
			{
				return fail(error, ErrorCode::eUnsupportedFormat, "texture memory query: undefined or unsupported format");
			}

			const std::uint32_t layers = desc.arrayLayers;
			vk::ImageCreateInfo imageInfo{};
			imageInfo.flags			= texture_create_flags(desc);
			imageInfo.imageType		= map_image_type(desc.type);
			imageInfo.format		= format;
			imageInfo.extent		= vk::Extent3D{ desc.width, desc.height, desc.depth };
			imageInfo.mipLevels		= desc.mipLevels;
			imageInfo.arrayLayers	= layers;
			imageInfo.samples		= map_sample_count(desc.samples);
			imageInfo.tiling		= vk::ImageTiling::eOptimal;
			imageInfo.usage			= map_texture_usage(desc.usage);
			imageInfo.sharingMode	= vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;

			const auto created = device->device.createImage(imageInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native(error, "Vulkan texture memory query failed", created.result);
			}

			const vk::MemoryRequirements reqs = device->device.getImageMemoryRequirements(created.value, device->dispatch);
			device->device.destroyImage(created.value, nullptr, device->dispatch);
			out->size	   = reqs.size;
			out->alignment = reqs.alignment;
			return succeed(error);
		}

		bool vulkan_get_buffer_memory_info(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getBufferMemoryInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "getBufferMemoryInfo output pointer is null");
			}
			if (desc.size == 0)
			{
				return fail(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}

			const auto created = device->device.createBuffer(
				vk::BufferCreateInfo({}, desc.size, map_buffer_usage(desc.usage), vk::SharingMode::eExclusive),
				nullptr,
				device->dispatch
			);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native(error, "Vulkan buffer memory query failed", created.result);
			}

			const vk::MemoryRequirements reqs = device->device.getBufferMemoryRequirements(created.value, device->dispatch);
			device->device.destroyBuffer(created.value, nullptr, device->dispatch);
			out->size	   = reqs.size;
			out->alignment = reqs.alignment;
			return succeed(error);
		}

		TextureViewHandle vulkan_create_texture_view(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createTextureView");
			auto * device = static_cast<VulkanDevice *>(impl);
			vk::Image image;
			vk::Format textureFormat		   = vk::Format::eUndefined;
			vk::SampleCountFlagBits texSamples = vk::SampleCountFlagBits::e1;
			std::uint32_t texMips			   = 1;
			std::uint32_t texLayers			   = 1;
			bool texMutableFormat			   = false;
			Flags<TextureUsage> texUsage;
			Format texRhiFormat = Format::eUndefined;
			{
				const TextureSlot * const resolved = device->textureSlots.resolve(texture, kHandleAlreadyChecked);
				if (resolved == nullptr)
				{
					return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of an invalid or stale texture handle");
				}
				const TextureSlot & slot = *resolved;

				image			 = vk::Image(slot.image);
				textureFormat	 = slot.format;
				texSamples		 = slot.samples;
				texMips			 = slot.mipLevels;
				texLayers		 = slot.arrayLayers;
				texMutableFormat = slot.mutableFormat;
				texUsage		 = slot.usage;
				texRhiFormat	 = slot.rhiFormat;
			}

			if (!desc.swizzle.is_identity())
			{
				if (!device->caps.supportsTextureViewSwizzle)
				{
					return fail_value<TextureViewHandle>(error, ErrorCode::eUnsupportedFeature, "texture view swizzle is not supported by this Vulkan adapter");
				}
				if (usage_forbids_swizzle(resolve_view_usage(desc.usage, texUsage)))
				{
					return fail_value<TextureViewHandle>(
						error,
						ErrorCode::eInvalidArgument,
						"a swizzled texture view must be sampled only, so narrow TextureViewDesc::usage to eSampled"
					);
				}
			}

			const TextureSubresourceRange & r = desc.range;
			if (r.mipCount == kAllMips || r.layerCount == kAllLayers)
			{
				return fail_value<TextureViewHandle>(
					error,
					ErrorCode::eInvalidArgument,
					"kAllMips and kAllLayers are barrier counts, so a texture view has to name how many levels and layers it takes"
				);
			}
			if (r.baseMip >= texMips || r.mipCount > texMips - r.baseMip)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view mip range is outside the source texture");
			}
			if (r.baseLayer >= texLayers || r.layerCount > texLayers - r.baseLayer)
			{
				return fail_value<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view layer range is outside the source texture");
			}

			const std::uint32_t plane = plane_index_of(desc.range.aspects);

			vk::SamplerYcbcrConversionInfo conversionInfo{};
			if (desc.ycbcrConversion != nullptr)
			{
				if (!device->caps.supportsSamplerYcbcrConversion)
				{
					return fail_value<TextureViewHandle>(
						error,
						ErrorCode::eUnsupportedFeature,
						"texture view Y'CbCr conversion is not supported by this Vulkan adapter"
					);
				}
				if (plane != kNoPlane)
				{
					return fail_value<TextureViewHandle>(
						error,
						ErrorCode::eInvalidArgument,
						"a texture view naming a plane reads it raw and cannot also carry a Y'CbCr conversion"
					);
				}
				vk::Result conversionResult = vk::Result::eSuccess;
				conversionInfo.conversion	= acquire_ycbcr_conversion(device, *desc.ycbcrConversion, conversionResult);
				if (!conversionInfo.conversion)
				{
					return fail_native_value<TextureViewHandle>(error, "Vulkan texture view Y'CbCr conversion creation failed", conversionResult);
				}
			}
			else if (plane == kNoPlane && is_multi_planar_format(texRhiFormat))
			{
				return fail_value<TextureViewHandle>(
					error,
					ErrorCode::eInvalidArgument,
					"a texture view of a multi-planar format must either name a plane aspect or carry a Y'CbCr conversion"
				);
			}

			vk::Format viewFormat = vk::Format::eUndefined;
			if (plane != kNoPlane)
			{
				const Format planeFormat = plane_format_of(texRhiFormat, plane);
				if (planeFormat == Format::eUndefined)
				{
					return fail_value<TextureViewHandle>(
						error,
						ErrorCode::eInvalidArgument,
						"texture view names a plane the source texture's format does not have"
					);
				}
				viewFormat = desc.format == Format::eUndefined ? map_format(planeFormat) : map_format(desc.format);
			}
			else
			{
				viewFormat = desc.format == Format::eUndefined ? textureFormat : map_format(desc.format);

				if (viewFormat != textureFormat && !texMutableFormat)
				{
					return fail_value<TextureViewHandle>(
						error,
						ErrorCode::eInvalidArgument,
						"texture view names a format the source texture was not created with allowFormatViews for"
					);
				}
			}

			vk::ImageSubresourceRange subresource	= map_subresource_range(desc.range);
			const vk::ImageAspectFlags formatAspect = aspect_for_view_format(viewFormat);
			if (subresource.aspectMask == vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor } && formatAspect != vk::ImageAspectFlagBits::eColor)
			{
				subresource.aspectMask = formatAspect;
			}

			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image			  = image;
			viewInfo.viewType		  = map_image_view_type(desc.type);
			viewInfo.format			  = viewFormat;
			viewInfo.components		  = map_component_mapping(desc.swizzle);
			viewInfo.subresourceRange = subresource;
			if (desc.ycbcrConversion != nullptr)
			{
				viewInfo.pNext = &conversionInfo;
			}

			const auto created = device->device.createImageView(viewInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<TextureViewHandle>(error, "Vulkan texture view creation failed", created.result);
			}

			const TextureViewHandle handle = device->textureViewSlots.store(
				TextureViewSlot{
					.view	 = created.value,
					.format	 = viewFormat,
					.samples = texSamples,
				}
			);
			if (!handle.is_valid())
			{
				device->device.destroyImageView(created.value, nullptr, device->dispatch);
				return fail_value<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan texture view handle tracking failed");
			}

			return return_value(handle, error);
		}

		namespace
		{
			[[nodiscard]] bool binding_maps_agree(
				VulkanDevice * device,
				const PipelineLayoutHandle layoutHandle,
				const std::span<const ShaderBinary> shaders,
				Error * error
			) noexcept
			{
				if (std::ranges::none_of(
						shaders,
						[](const ShaderBinary & shader) noexcept
						{
							return shader.bindingMap != nullptr;
						}
					))
				{
					return true;
				}

				const PipelineLayoutSlot * const layout = device->pipelineLayoutSlots.resolve(layoutHandle, kHandleAlreadyChecked);
				if (layout == nullptr)
				{
					return fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
				}

				detail::HostVector<DescriptorSetLayoutDesc> abiSets;
				if (!detail::try_reserve(abiSets, layout->sets.size()))
				{
					return fail(error, ErrorCode::eOutOfHostMemory, "Vulkan shader binding map check storage allocation failed");
				}

				for (const DescriptorSetLayoutHandle setHandle : layout->sets)
				{
					const DescriptorSetLayoutSlot * const setLayout = device->descriptorSetLayoutSlots.resolve(setHandle, kHandleAlreadyChecked);
					if (setLayout == nullptr)
					{
						return fail(error, ErrorCode::eInvalidHandle, "a descriptor set layout this pipeline layout was built from has been destroyed");
					}

					abiSets.push_back(DescriptorSetLayoutDesc{ .bindings = setLayout->bindings });
				}

				const ShaderAbiLayout abiLayout{ .sets = abiSets };

				for (const ShaderBinary & shader : shaders)
				{
					if (shader.bindingMap == nullptr)
					{
						continue;
					}

					const ShaderBindingDisagreement bad = check_shader_binding_map(VulkanApi::kId, device->caps.bindingTier, abiLayout, *shader.bindingMap);
					if (!bad.found)
					{
						continue;
					}

					if (bad.wrongAbiVersion)
					{
						return fail(
							error,
							ErrorCode::eUnsupportedFormat,
							"a shader binary was built against a revision of the binding ABI this build does not implement"
						);
					}

					if (bad.unknownToLayout)
					{
						return fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding the pipeline layout does not declare");
					}

					return fail(
						error,
						ErrorCode::eInvalidArgument,
						"a shader binary put a binding at a different set and binding than this pipeline layout uses"
					);
				}

				return true;
			}
		}

		PipelineLayoutHandle vulkan_create_pipeline_layout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPipelineLayout");
			auto * device = static_cast<VulkanDevice *>(impl);
			detail::HostVector<vk::DescriptorSetLayout> setLayouts;
			detail::HostVector<vk::PushConstantRange> ranges;
			if (!detail::try_reserve(setLayouts, desc.sets.size()) || !detail::try_reserve(ranges, desc.pushConstants.size()))
			{
				return fail_value<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline layout storage allocation failed");
			}

			for (const DescriptorSetLayoutHandle & handle : desc.sets)
			{
				const vk::DescriptorSetLayout setLayout = resolve_descriptor_set_layout(device, handle);
				if (!setLayout)
				{
					return fail_value<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
				}
				setLayouts.push_back(setLayout);
			}

			for (const PushConstantRange & pc : desc.pushConstants)
			{
				ranges.emplace_back(map_shader_stages(pc.stages), pc.offset, pc.size);
			}

			const auto created = device->device.createPipelineLayout(vk::PipelineLayoutCreateInfo({}, setLayouts, ranges), nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<PipelineLayoutHandle>(error, "Vulkan pipeline layout creation failed", created.result);
			}

			PipelineLayoutSlot slot{ .layout = created.value };
			slot.sets.assign(desc.sets.begin(), desc.sets.end());

			const PipelineLayoutHandle handle = device->pipelineLayoutSlots.store(std::move(slot));
			if (!handle.is_valid())
			{
				device->device.destroyPipelineLayout(created.value, nullptr, device->dispatch);
				return fail_value<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline layout handle tracking failed");
			}

			return return_value(handle, error);
		}

		[[nodiscard]] vk::RenderPass get_or_create_render_pass(
			VulkanDevice * device,
			detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> & cache,
			const RenderPassKey & key,
			vk::Result & outResult
		)
		{
			outResult = vk::Result::eSuccess;
			if (const auto it = cache.find(key); it != cache.end())
			{
				return it->second;
			}

			detail::HostVector<vk::AttachmentDescription> attachments;
			detail::HostVector<vk::AttachmentReference> colorRefs;
			if (!detail::try_reserve(attachments, static_cast<std::size_t>(key.colorCount) + (key.hasDepth ? 1U : 0U)) ||
				!detail::try_reserve(colorRefs, key.colorCount))
			{
				return {};
			}

			for (std::uint32_t i = 0; i < key.colorCount; ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const RenderPassAttachmentKey & a = key.colors[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					a.format,
					a.samples,
					a.loadOp,
					a.storeOp,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eDontCare,
					a.layout,
					a.layout
				);
				colorRefs.emplace_back(i, a.layout);
			}

			vk::AttachmentReference depthRef;
			if (key.hasDepth)
			{
				const bool stencil = key.depth.format == vk::Format::eD24UnormS8Uint || key.depth.format == vk::Format::eD32SfloatS8Uint;
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					key.depth.format,
					key.depth.samples,
					key.depth.loadOp,
					key.depth.storeOp,
					stencil ? key.depth.loadOp : vk::AttachmentLoadOp::eDontCare,
					stencil ? key.depth.storeOp : vk::AttachmentStoreOp::eDontCare,
					key.depth.layout,
					key.depth.layout
				);
				depthRef = vk::AttachmentReference(key.colorCount, key.depth.layout);
			}

			vk::SubpassDescription subpass;
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.setColorAttachments(colorRefs);
			if (key.hasDepth)
			{
				subpass.setPDepthStencilAttachment(&depthRef);
			}

			vk::RenderPassCreateInfo info;
			info.setAttachments(attachments);
			info.setSubpasses(subpass);

			const auto created = device->device.createRenderPass(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				outResult = created.result;
				return {};
			}

			if (!detail::try_insert_or_assign(cache, key, created.value))
			{
				device->device.destroyRenderPass(created.value, nullptr, device->dispatch);
				return {};
			}

			return created.value;
		}

		[[nodiscard]] RenderPassKey make_pipeline_render_pass_key(const GraphicsPipelineDesc & desc) noexcept
		{
			RenderPassKey key;

			const std::uint32_t colorCount = std::min(desc.renderTarget.colorFormatCount, static_cast<std::uint32_t>(key.colors.size()));

			key.colorCount						  = colorCount;
			const vk::SampleCountFlagBits samples = map_sample_count(desc.renderTarget.samples);
			for (std::uint32_t i = 0; i < colorCount; ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				key.colors[i] = RenderPassAttachmentKey{
					.format = map_format(desc.renderTarget.colorFormats[i]),
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					.samples = samples,
					.loadOp	 = vk::AttachmentLoadOp::eLoad,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.layout	 = vk::ImageLayout::eColorAttachmentOptimal,
				};
			}

			if (desc.renderTarget.depthStencilFormat != Format::eUndefined)
			{
				key.hasDepth = true;
				key.depth	 = RenderPassAttachmentKey{
					.format	 = map_format(desc.renderTarget.depthStencilFormat),
					.samples = samples,
					.loadOp	 = vk::AttachmentLoadOp::eLoad,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.layout	 = vk::ImageLayout::eDepthStencilAttachmentOptimal,
				};
			}
			return key;
		}

		[[nodiscard]] vk::PipelineCache resolve_pipeline_cache(VulkanDevice * device, PipelineCacheHandle handle) noexcept
		{
			if (!handle.is_valid())
			{
				return vk::PipelineCache{};
			}
			const PipelineCacheSlot * slot = device->pipelineCacheSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->cache : vk::PipelineCache{};
		}

		namespace
		{
			[[nodiscard]] bool shader_bytes_usable(const ShaderBinary & shader, Error * error) noexcept
			{
				if (shader.format != ShaderBinaryFormat::eSpirV)
				{
					return fail(error, ErrorCode::eUnsupportedFormat, "the Vulkan backend takes SPIR-V shader binaries");
				}

				if (shader.isSource)
				{
					return fail(error, ErrorCode::eUnsupportedFormat, "the Vulkan backend has no shader compiler, so it takes compiled SPIR-V only");
				}

				if (shader.data == nullptr || shader.size == 0)
				{
					return fail(error, ErrorCode::eInvalidArgument, "shader binary has no bytes");
				}

				if (shader.size % sizeof(std::uint32_t) != 0)
				{
					return fail(error, ErrorCode::eUnsupportedFormat, "a SPIR-V binary is a whole number of 32-bit words");
				}

				return true;
			}
		}

		GraphicsPipelineHandle vulkan_create_graphics_pipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createGraphicsPipeline");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (desc.vertexInput == nullptr)
			{
				return fail_value<GraphicsPipelineHandle>(
					error,
					ErrorCode::eUnsupportedFeature,
					"graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have"
				);
			}

			const VertexInputDesc & vertexInput = *desc.vertexInput;
			if (desc.raster.conservativeRasterEnable && device->caps.conservativeRasterTier == ConservativeRasterTier::eNone)
			{
				return fail_value<GraphicsPipelineHandle>(
					error,
					ErrorCode::eUnsupportedFeature,
					"conservative rasterization was requested on a device that reports none"
				);
			}

			if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
			}

			if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
			{
				return fail_value<GraphicsPipelineHandle>(
					error,
					ErrorCode::eInvalidArgument,
					"graphics pipeline names more color attachments than a render target can hold"
				);
			}

			if (desc.shaders.empty())
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires at least one shader stage");
			}

			detail::HostVector<vk::ShaderModule> modules;

			auto moduleGuard = detail::make_scope_guard(
				[&]
				{
					for (const vk::ShaderModule module : modules)
					{
						if (module)
						{
							device->device.destroyShaderModule(module, nullptr, device->dispatch);
						}
					}
				}
			);

			const vk::PipelineLayout layout = resolve_pipeline_layout(device, desc.layout);
			if (!layout)
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eInvalidHandle, "graphics pipeline references an invalid pipeline layout");
			}

			if (!binding_maps_agree(device, desc.layout, desc.shaders, error))
			{
				return {};
			}

			detail::HostVector<vk::PipelineShaderStageCreateInfo> stages;
			detail::HostVector<vk::VertexInputBindingDescription> bindings;
			detail::HostVector<vk::VertexInputAttributeDescription> attrs;
			if (!detail::try_reserve(stages, desc.shaders.size()) || !detail::try_reserve(modules, desc.shaders.size()) ||
				!detail::try_reserve(bindings, vertexInput.bindings.size()) || !detail::try_reserve(attrs, vertexInput.attributes.size()))
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
			}

			for (const ShaderBinary & shader : desc.shaders)
			{
				if (!shader_bytes_usable(shader, error))
				{
					return {};
				}

				const auto created = device->device.createShaderModule(
					vk::ShaderModuleCreateInfo({}, shader.size, static_cast<const std::uint32_t *>(shader.data)),
					nullptr,
					device->dispatch
				);
				if (created.result != vk::Result::eSuccess)
				{
					return fail_native_value<GraphicsPipelineHandle>(error, "Vulkan shader module creation failed", created.result);
				}

				modules.push_back(created.value);
				stages.push_back(vk::PipelineShaderStageCreateInfo({}, map_shader_stage_bit(shader.stage), created.value, shader.entryPoint));
			}

			for (const VertexBindingDesc & b : vertexInput.bindings)
			{
				bindings.emplace_back(b.binding, b.stride, b.perInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex);
			}

			for (const VertexAttributeDesc & a : vertexInput.attributes)
			{
				attrs.emplace_back(a.location, a.binding, map_format(a.format), a.offset);
			}

			const vk::PipelineVertexInputStateCreateInfo vertexInputState({}, bindings, attrs);

			const vk::PipelineInputAssemblyStateCreateInfo inputAssembly(
				{},
				map_topology(vertexInput.topology),
				vertexInput.primitiveRestartEnable ? VK_TRUE : VK_FALSE
			);

			const vk::PipelineTessellationStateCreateInfo tessellation({}, vertexInput.patchControlPoints);
			const bool patches = vertexInput.topology == PrimitiveTopology::ePatchList;
			constexpr vk::PipelineViewportStateCreateInfo kViewportState({}, 1, nullptr, 1, nullptr);

			vk::PipelineRasterizationStateCreateInfo raster;
			raster.depthClampEnable		   = desc.raster.depthClampEnable ? VK_TRUE : VK_FALSE;
			raster.rasterizerDiscardEnable = desc.raster.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
			raster.polygonMode			   = map_fill_mode(desc.raster.fillMode);
			raster.cullMode				   = map_cull_mode(desc.raster.cullMode);
			raster.frontFace			   = map_front_face(desc.raster.frontFace);
			raster.depthBiasEnable		   = desc.raster.depthBiasEnable ? VK_TRUE : VK_FALSE;
			raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
			raster.depthBiasClamp		   = desc.raster.depthBiasClamp;
			raster.depthBiasSlopeFactor	   = desc.raster.depthBiasSlopeFactor;
			raster.lineWidth			   = 1.0f;

			const vk::PipelineRasterizationConservativeStateCreateInfoEXT conservative({}, vk::ConservativeRasterizationModeEXT::eOverestimate, 0.0f);
			if (desc.raster.conservativeRasterEnable)
			{
				raster.pNext = &conservative;
			}

			vk::PipelineMultisampleStateCreateInfo multisample;
			multisample.rasterizationSamples  = map_sample_count(desc.renderTarget.samples);
			multisample.alphaToCoverageEnable = desc.renderTarget.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
			const vk::SampleMask sampleMask	  = desc.renderTarget.sampleMask;

			if (desc.renderTarget.sampleMask != 0xffffffffu)
			{
				multisample.pSampleMask = &sampleMask;
			}

			vk::PipelineDepthStencilStateCreateInfo depthStencil;
			depthStencil.depthTestEnable	   = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.depthWriteEnable	   = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
			depthStencil.depthCompareOp		   = map_compare_op(desc.depthStencil.depthCompareOp);
			depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.stencilTestEnable	   = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.front				   = map_stencil_face(desc.depthStencil.front);
			depthStencil.back				   = map_stencil_face(desc.depthStencil.back);
			depthStencil.minDepthBounds		   = desc.depthStencil.minDepthBounds;
			depthStencil.maxDepthBounds		   = desc.depthStencil.maxDepthBounds;

			detail::HostVector<vk::PipelineColorBlendAttachmentState> blendAttachments;
			if (!detail::try_reserve(blendAttachments, desc.blend.attachmentCount))
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
			}

			for (std::uint32_t i = 0; i < desc.blend.attachmentCount; ++i)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ColorBlendAttachmentDesc & a = desc.blend.attachments[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				vk::PipelineColorBlendAttachmentState state;
				state.blendEnable		  = a.blendEnable ? VK_TRUE : VK_FALSE;
				state.srcColorBlendFactor = map_blend_factor(a.srcColorBlendFactor);
				state.dstColorBlendFactor = map_blend_factor(a.dstColorBlendFactor);
				state.colorBlendOp		  = map_blend_op(a.colorBlendOp);
				state.srcAlphaBlendFactor = map_blend_factor(a.srcAlphaBlendFactor);
				state.dstAlphaBlendFactor = map_blend_factor(a.dstAlphaBlendFactor);
				state.alphaBlendOp		  = map_blend_op(a.alphaBlendOp);
				state.colorWriteMask	  = map_color_write_mask(a.colorWriteMask);
				blendAttachments.push_back(state);
			}

			vk::PipelineColorBlendStateCreateInfo colorBlend({}, desc.blend.logicOpEnable ? VK_TRUE : VK_FALSE, vk::LogicOp::eCopy, blendAttachments);
			colorBlend.setBlendConstants(desc.blend.blendConstants);

			const detail::HostVector<vk::DynamicState> dynamics = map_dynamic_states(desc.dynamicStates);
			const vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamics);

			vk::GraphicsPipelineCreateInfo pipelineInfo(
				{},
				stages,
				&vertexInputState,
				&inputAssembly,
				patches ? &tessellation : nullptr,
				&kViewportState,
				&raster,
				&multisample,
				&depthStencil,
				&colorBlend,
				&dynamicState,
				layout
			);

			detail::HostVector<vk::Format> colorFormats;
			vk::PipelineRenderingCreateInfo renderingInfo;
			if (device->dynamicRendering)
			{
				if (!detail::try_reserve(colorFormats, desc.renderTarget.colorFormatCount))
				{
					return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
				}

				for (std::uint32_t i = 0; i < desc.renderTarget.colorFormatCount; ++i)
				{
					// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					colorFormats.push_back(map_format(desc.renderTarget.colorFormats[i]));
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				}
				const vk::Format depthFormat = map_format(desc.renderTarget.depthStencilFormat);
				renderingInfo.setColorAttachmentFormats(colorFormats);
				renderingInfo.depthAttachmentFormat	  = depthFormat;
				renderingInfo.stencilAttachmentFormat = has_stencil_aspect(desc.renderTarget.depthStencilFormat) ? depthFormat : vk::Format::eUndefined;
				pipelineInfo.pNext					  = &renderingInfo;
			}
			else
			{
				vk::Result renderPassResult = vk::Result::eSuccess;
				const vk::RenderPass renderPass =
					get_or_create_render_pass(device, device->renderPasses, make_pipeline_render_pass_key(desc), renderPassResult);
				if (!renderPass)
				{
					return fail_native_value<GraphicsPipelineHandle>(error, "Vulkan render pass creation failed", renderPassResult);
				}

				pipelineInfo.renderPass = renderPass;
				pipelineInfo.subpass	= 0;
			}

			const vk::PipelineCache pipelineCache = resolve_pipeline_cache(device, desc.pipelineCache);
			const auto created					  = device->device.createGraphicsPipeline(pipelineCache, pipelineInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<GraphicsPipelineHandle>(error, "Vulkan graphics pipeline creation failed", created.result);
			}

			auto pipelineGuard = detail::make_scope_guard(
				[&]
				{
					device->device.destroyPipeline(created.value, nullptr, device->dispatch);
				}
			);

			const GraphicsPipelineHandle storedHandle = device->graphicsPipelineSlots.store(GraphicsPipelineSlot{ .pipeline = created.value });
			if (!storedHandle.is_valid())
			{
				return fail_value<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline handle tracking failed");
			}

			pipelineGuard.dismiss();
			return return_value(storedHandle, error);
		}

		TimelineHandle vulkan_create_timeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createTimeline");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!vulkan_refuse_unexportable(
					device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eTimeline,
					Format::eUndefined,
					kExternalQueryBufferUsage,
					"timeline creation asked for an external handle type this adapter cannot export",
					error
				))
			{
				return TimelineHandle{};
			}
			vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, desc.initialValue);
			vk::ExportSemaphoreCreateInfo exportInfo;
			if (!desc.exportableHandleTypes.empty())
			{
				exportInfo.handleTypes = map_semaphore_handle_types(desc.exportableHandleTypes);
				typeInfo.pNext		   = &exportInfo;
			}

			const vk::SemaphoreCreateInfo info({}, &typeInfo);
			const auto created = device->device.createSemaphore(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<TimelineHandle>(error, "Vulkan timeline creation failed", created.result);
			}

			const TimelineHandle handle =
				device->timelineSlots.store(TimelineSlot{ .semaphore = created.value, .exportableHandleTypes = desc.exportableHandleTypes });
			if (!handle.is_valid())
			{
				device->device.destroySemaphore(created.value, nullptr, device->dispatch);
				return fail_value<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan timeline handle tracking failed");
			}

			return return_value(handle, error);
		}

		[[nodiscard]] vk::QueryType map_query_type(QueryType type) noexcept
		{
			switch (type)
			{
			case QueryType::eOcclusion:			 return vk::QueryType::eOcclusion;
			case QueryType::ePipelineStatistics: return vk::QueryType::ePipelineStatistics;
			case QueryType::eTimestamp:			 break;
			}
			return vk::QueryType::eTimestamp;
		}

		[[nodiscard]] vk::QueryPipelineStatisticFlags map_pipeline_statistics(Flags<PipelineStatistic> stats) noexcept
		{
			vk::QueryPipelineStatisticFlags out{};
			if (stats.contains(PipelineStatistic::eInputAssemblyVertices))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices;
			}

			if (stats.contains(PipelineStatistic::eInputAssemblyPrimitives))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives;
			}

			if (stats.contains(PipelineStatistic::eVertexShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations;
			}

			if (stats.contains(PipelineStatistic::eClippingInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eClippingInvocations;
			}

			if (stats.contains(PipelineStatistic::eClippingPrimitives))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eClippingPrimitives;
			}

			if (stats.contains(PipelineStatistic::eFragmentShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations;
			}

			if (stats.contains(PipelineStatistic::eComputeShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations;
			}

			return out;
		}

		QueryPoolHandle vulkan_create_query_pool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createQueryPool");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (desc.queryCount == 0)
			{
				return fail_value<QueryPoolHandle>(error, ErrorCode::eInvalidArgument, "query pool creation asked for no queries");
			}

			vk::QueryPoolCreateInfo info({}, map_query_type(desc.type), desc.queryCount);
			if (desc.type == QueryType::ePipelineStatistics)
			{
				info.pipelineStatistics = map_pipeline_statistics(desc.statistics);
			}

			const auto created = device->device.createQueryPool(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<QueryPoolHandle>(error, "Vulkan query pool creation failed", created.result);
			}

			const QueryPoolHandle handle = device->queryPoolSlots.store(QueryPoolSlot{ .pool = created.value, .queryCount = desc.queryCount });
			if (!handle.is_valid())
			{
				device->device.destroyQueryPool(created.value, nullptr, device->dispatch);
				return fail_value<QueryPoolHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan query pool handle tracking failed");
			}

			return return_value(handle, error);
		}

		[[nodiscard]] QueryPoolSlot * resolve_query_pool(VulkanDevice * device, QueryPoolHandle handle) noexcept
		{
			return device->queryPoolSlots.resolve(handle, kHandleAlreadyChecked);
		}

		[[nodiscard]] vk::Filter map_filter(Filter filter) noexcept
		{
			return filter == Filter::eNearest ? vk::Filter::eNearest : vk::Filter::eLinear;
		}

		[[nodiscard]] vk::SamplerMipmapMode map_mipmap_mode(MipmapMode mode) noexcept
		{
			return mode == MipmapMode::eNearest ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear;
		}

		[[nodiscard]] vk::SamplerAddressMode map_address_mode(AddressMode mode) noexcept
		{
			switch (mode)
			{
			case AddressMode::eMirroredRepeat:	  return vk::SamplerAddressMode::eMirroredRepeat;
			case AddressMode::eClampToEdge:		  return vk::SamplerAddressMode::eClampToEdge;
			case AddressMode::eClampToBorder:	  return vk::SamplerAddressMode::eClampToBorder;
			case AddressMode::eMirrorClampToEdge: return vk::SamplerAddressMode::eMirrorClampToEdge;
			case AddressMode::eRepeat:			  break;
			}
			return vk::SamplerAddressMode::eRepeat;
		}

		[[nodiscard]] vk::BorderColor map_border_color(BorderColor color) noexcept
		{
			switch (color)
			{
			case BorderColor::eTransparentBlackInt:	  return vk::BorderColor::eIntTransparentBlack;
			case BorderColor::eOpaqueBlackFloat:	  return vk::BorderColor::eFloatOpaqueBlack;
			case BorderColor::eOpaqueBlackInt:		  return vk::BorderColor::eIntOpaqueBlack;
			case BorderColor::eOpaqueWhiteFloat:	  return vk::BorderColor::eFloatOpaqueWhite;
			case BorderColor::eOpaqueWhiteInt:		  return vk::BorderColor::eIntOpaqueWhite;
			case BorderColor::eTransparentBlackFloat: break;
			}
			return vk::BorderColor::eFloatTransparentBlack;
		}

		SamplerHandle vulkan_create_sampler(void * impl, const SamplerDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createSampler");
			auto * device = static_cast<VulkanDevice *>(impl);

			vk::SamplerYcbcrConversionInfo conversionInfo{};
			if (desc.ycbcrConversion != nullptr)
			{
				if (!device->caps.supportsSamplerYcbcrConversion)
				{
					return fail_value<SamplerHandle>(
						error,
						ErrorCode::eUnsupportedFeature,
						"sampler Y'CbCr conversion is not supported by this Vulkan adapter"
					);
				}
				const bool clampsToEdge =
					desc.addressU == AddressMode::eClampToEdge && desc.addressV == AddressMode::eClampToEdge && desc.addressW == AddressMode::eClampToEdge;
				if (!clampsToEdge || desc.anisotropyEnable || desc.compareEnable)
				{
					return fail_value<SamplerHandle>(
						error,
						ErrorCode::eInvalidArgument,
						"a sampler with a Y'CbCr conversion must clamp to edge on every axis and enable neither anisotropy nor depth comparison"
					);
				}

				vk::Result conversionResult = vk::Result::eSuccess;
				conversionInfo.conversion	= acquire_ycbcr_conversion(device, *desc.ycbcrConversion, conversionResult);
				if (!conversionInfo.conversion)
				{
					return fail_native_value<SamplerHandle>(error, "Vulkan sampler Y'CbCr conversion creation failed", conversionResult);
				}
			}

			vk::SamplerCreateInfo info(
				{},
				map_filter(desc.magFilter),
				map_filter(desc.minFilter),
				map_mipmap_mode(desc.mipmapMode),
				map_address_mode(desc.addressU),
				map_address_mode(desc.addressV),
				map_address_mode(desc.addressW),
				desc.mipLodBias,
				static_cast<vk::Bool32>(desc.anisotropyEnable),
				desc.maxAnisotropy,
				static_cast<vk::Bool32>(desc.compareEnable),
				map_compare_op(desc.compareOp),
				desc.minLod,
				desc.maxLod,
				map_border_color(desc.borderColor),
				VK_FALSE
			);
			if (desc.ycbcrConversion != nullptr)
			{
				info.pNext = &conversionInfo;
			}

			const auto created = device->device.createSampler(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<SamplerHandle>(error, "Vulkan sampler creation failed", created.result);
			}

			const SamplerHandle handle = device->samplerSlots.store(SamplerSlot{ .sampler = created.value });
			if (!handle.is_valid())
			{
				device->device.destroySampler(created.value, nullptr, device->dispatch);
				return fail_value<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan sampler handle tracking failed");
			}

			return return_value(handle, error);
		}

		ComputePipelineHandle vulkan_create_compute_pipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createComputePipeline");

			if (!desc.shader.threadgroupSize.is_stated())
			{
				return fail_value<ComputePipelineHandle>(
					error,
					ErrorCode::eInvalidArgument,
					"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary"
				);
			}
			auto * device					= static_cast<VulkanDevice *>(impl);
			const vk::PipelineLayout layout = resolve_pipeline_layout(device, desc.layout);
			if (!layout)
			{
				return fail_value<ComputePipelineHandle>(error, ErrorCode::eInvalidHandle, "compute pipeline with an invalid layout handle");
			}

			const std::array<ShaderBinary, 1> stages{ desc.shader };
			if (!binding_maps_agree(device, desc.layout, stages, error))
			{
				return {};
			}

			if (!shader_bytes_usable(desc.shader, error))
			{
				return {};
			}

			const auto createdModule = device->device.createShaderModule(
				vk::ShaderModuleCreateInfo({}, desc.shader.size, static_cast<const std::uint32_t *>(desc.shader.data)),
				nullptr,
				device->dispatch
			);
			if (createdModule.result != vk::Result::eSuccess)
			{
				return fail_native_value<ComputePipelineHandle>(error, "Vulkan shader module creation failed", createdModule.result);
			}

			const auto moduleGuard = detail::make_scope_guard(
				[&]
				{
					device->device.destroyShaderModule(createdModule.value, nullptr, device->dispatch);
				}
			);

			const vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, createdModule.value, desc.shader.entryPoint);
			const vk::ComputePipelineCreateInfo info({}, stage, layout);
			const vk::PipelineCache pipelineCache = resolve_pipeline_cache(device, desc.pipelineCache);
			const auto created					  = device->device.createComputePipeline(pipelineCache, info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<ComputePipelineHandle>(error, "Vulkan compute pipeline creation failed", created.result);
			}

			auto pipelineGuard = detail::make_scope_guard(
				[&]
				{
					device->device.destroyPipeline(created.value, nullptr, device->dispatch);
				}
			);

			const ComputePipelineHandle storedHandle = device->computePipelineSlots.store(ComputePipelineSlot{ .pipeline = created.value });
			if (!storedHandle.is_valid())
			{
				return fail_value<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan compute pipeline handle tracking failed");
			}

			pipelineGuard.dismiss();
			return return_value(storedHandle, error);
		}

		[[nodiscard]] vk::Pipeline resolve_compute_pipeline(const VulkanDevice * device, ComputePipelineHandle handle) noexcept
		{
			const ComputePipelineSlot * slot = device->computePipelineSlots.resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->pipeline : vk::Pipeline{};
		}

		PipelineCacheHandle vulkan_create_pipeline_cache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPipelineCache");
			auto * device = static_cast<VulkanDevice *>(impl);
			const vk::PipelineCacheCreateInfo info({}, desc.initialSize, desc.initialData);
			const auto created = device->device.createPipelineCache(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<PipelineCacheHandle>(error, "Vulkan pipeline cache creation failed", created.result);
			}

			const PipelineCacheHandle handle = device->pipelineCacheSlots.store(PipelineCacheSlot{ .cache = created.value });
			if (!handle.is_valid())
			{
				device->device.destroyPipelineCache(created.value, nullptr, device->dispatch);
				return fail_value<PipelineCacheHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline cache handle tracking failed");
			}

			return return_value(handle, error);
		}

		bool vulkan_get_pipeline_cache_data(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getPipelineCacheData");
			auto * device			 = static_cast<VulkanDevice *>(impl);
			PipelineCacheSlot * slot = device->pipelineCacheSlots.resolve(cache, true);
			if (slot == nullptr)
			{
				return fail(error, ErrorCode::eInvalidHandle, "getPipelineCacheData with an invalid or stale cache handle");
			}

			auto data = device->device.getPipelineCacheData<HostAllocatorAdapter<std::uint8_t>>(slot->cache, device->dispatch);
			if (data.result != vk::Result::eSuccess)
			{
				return fail_native(error, "Vulkan getPipelineCacheData failed", data.result);
			}

			slot->data = std::move(data.value);
			if (out != nullptr)
			{
				out->data = slot->data.data();
				out->size = slot->data.size();
			}

			return succeed(error);
		}

		BinarySemaphoreHandle vulkan_create_binary_semaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createBinarySemaphore");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!vulkan_refuse_unexportable(
					device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eBinarySemaphore,
					Format::eUndefined,
					kExternalQueryBufferUsage,
					"binary semaphore creation asked for an external handle type this adapter cannot export",
					error
				))
			{
				return BinarySemaphoreHandle{};
			}
			vk::SemaphoreCreateInfo info{};
			vk::ExportSemaphoreCreateInfo exportInfo;
			if (!desc.exportableHandleTypes.empty())
			{
				exportInfo.handleTypes = map_semaphore_handle_types(desc.exportableHandleTypes);
				info.pNext			   = &exportInfo;
			}

			const auto created = device->device.createSemaphore(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return fail_native_value<BinarySemaphoreHandle>(error, "Vulkan binary semaphore creation failed", created.result);
			}

			BinarySemaphoreHandle handle =
				device->binarySemaphoreSlots.store(BinarySemaphoreSlot{ .semaphore = created.value, .exportableHandleTypes = desc.exportableHandleTypes });
			if (!handle.is_valid())
			{
				device->device.destroySemaphore(created.value, nullptr, device->dispatch);
				return fail_value<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan binary semaphore handle tracking failed");
			}

			handle.index |= kDeviceBinarySemaphoreBit;
			return return_value(handle, error);
		}

		bool vulkan_query_memory_budget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept
		{
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "queryMemoryBudget output pointer is null");
			}

			const vk::PhysicalDeviceMemoryProperties memProps = device->phys.getMemoryProperties(device->dispatch);
			const bool wantDeviceLocal						  = heap == HeapType::eGpuLocal || heap == HeapType::eTransient;
			std::uint32_t heapIndex							  = 0;
			for (std::uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
			{
				const bool deviceLocal = static_cast<bool>(azo::rhi::detail::at(memProps.memoryHeaps, i).flags & vk::MemoryHeapFlagBits::eDeviceLocal);
				if (deviceLocal == wantDeviceLocal)
				{
					heapIndex = i;
					break;
				}
			}

			std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
			vmaGetHeapBudgets(device->allocator, budgets.data());
			// memoryHeapCount never exceeds VK_MAX_MEMORY_HEAPS, which is what budgets is sized to.
			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const VmaBudget & budget = budgets[heapIndex];
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			out->heap						  = heap;
			out->budgetBytes				  = budget.budget;
			out->usageBytes					  = budget.usage;
			out->availableForReservationBytes = budget.budget > budget.usage ? budget.budget - budget.usage : 0;
			out->budgetIsPrecise			  = false;
			return succeed(error);
		}

		bool vulkan_set_residency_priority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept
		{
			auto * device = static_cast<VulkanDevice *>(impl);
			for (const ResidencyPriorityDesc & desc : priorities)
			{
				if ((desc.buffer.is_valid()) && (resolve_buffer(device, desc.buffer) == nullptr))

				{
					return fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid buffer handle");
				}

				if ((desc.texture.is_valid()) && (!resolve_texture(device, desc.texture)))

				{
					return fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid texture handle");
				}
			}
			return succeed(error);
		}

		bool vulkan_calibrate_timestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return fail(error, ErrorCode::eInvalidArgument, "timestamp calibration output is null");
			}
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!device->caps.supportsTimestampCalibration)
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "VK_EXT_calibrated_timestamps is not available on this device");
			}

			vk::TimeDomainEXT hostDomain = vk::TimeDomainEXT::eDevice;
			if (!vulkan_calibration_domains(device->phys, device->dispatch, hostDomain))
			{
				return fail(error, ErrorCode::eUnsupportedFeature, "the adapter cannot calibrate the device and host clocks together");
			}

			const std::array<vk::CalibratedTimestampInfoEXT, 2> infos{
				{
					vk::CalibratedTimestampInfoEXT{ vk::TimeDomainEXT::eDevice },
					vk::CalibratedTimestampInfoEXT{ hostDomain },
				},
			};
			const auto calibrated = device->device.getCalibratedTimestampsEXT<HostAllocatorAdapter<std::uint64_t>>(infos, device->dispatch);
			if (calibrated.result != vk::Result::eSuccess)
			{
				return fail_native(error, "Vulkan timestamp calibration failed", calibrated.result);
			}

			const std::pair<detail::HostVector<std::uint64_t>, std::uint64_t> & sampled = calibrated.value;
			if (sampled.first.size() < 2)
			{
				return fail(error, ErrorCode::eNativeApiError, "vkGetCalibratedTimestampsEXT returned too few timestamps");
			}

			out->queueType	  = queueType;
			out->gpuTimestamp = azo::rhi::detail::at(sampled.first, 0);
#ifdef _WIN32
			static const double nanosPerTick = []
			{
				LARGE_INTEGER frequency{};
				return QueryPerformanceFrequency(&frequency) != 0 && frequency.QuadPart != 0 ? 1.0e9 / static_cast<double>(frequency.QuadPart) : 0.0;
			}();
			out->cpuTimestampNanoseconds = static_cast<std::uint64_t>(static_cast<double>(sampled.first[1]) * nanosPerTick);
#else
			out->cpuTimestampNanoseconds = azo::rhi::detail::at(sampled.first, 1);
#endif
			out->gpuPeriodNanoseconds = device->caps.timestampPeriodNanoseconds;
			out->calibrated			  = true;
			return succeed(error);
		}

	}

	Result<VulkanNativeDevice> get_vulkan_native_device(Device device)
	{
		if (device.get_graphics_api_id() != VulkanApi::kId)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeDevice called on a non-Vulkan device",
			};
		}

		auto * impl = static_cast<vulkan::VulkanDevice *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(device), vulkan::core_device_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeDevice reached something other than a Vulkan device behind the facade",
			};
		}

		return VulkanNativeDevice{
			.instance			 = impl->instance,
			.physicalDevice		 = impl->phys,
			.device				 = impl->device,
			.graphicsQueue		 = impl->graphicsQueues.empty() ? vk::Queue{} : impl->graphicsQueues.front(),
			.graphicsQueueFamily = impl->graphicsFamily,
			.allocator			 = impl->allocator,
			.debugUtils			 = impl->debugUtils,
			.dispatch			 = &impl->dispatch,
		};
	}

	Result<void> set_vulkan_device_surface(Device device, vk::SurfaceKHR surface)
	{
		if (device.get_graphics_api_id() != VulkanApi::kId)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "SetVulkanDeviceSurface called on a non-Vulkan device",
			};
		}

		auto * impl = static_cast<vulkan::VulkanDevice *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(device), vulkan::core_device_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "SetVulkanDeviceSurface reached something other than a Vulkan device behind the facade",
			};
		}

		impl->ownedSurface = surface;
		return {};
	}

	Result<VulkanNativeSwapchain> get_vulkan_native_swapchain(Swapchain swapchain)
	{
		auto * impl = static_cast<vulkan::VulkanSwapchain *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(swapchain), vulkan::swapchain_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeSwapchain called on a swapchain that is not a Vulkan one",
			};
		}

		const vulkan::SwapchainBundle & bundle = impl->bundle;
		return VulkanNativeSwapchain{
			.swapchain		= bundle.Swapchain,
			.colorFormat	= bundle.ColorFormat,
			.depthFormat	= bundle.DepthFormat,
			.extent			= bundle.Extent,
			.images			= bundle.Images,
			.views			= bundle.Views,
			.depthImage		= bundle.DepthImage,
			.depthView		= bundle.DepthView,
			.presentMode	= bundle.PresentMode,
			.captureCapable = bundle.CaptureCapable,
		};
	}

	Result<vk::Semaphore> get_vulkan_semaphore(const Swapchain swapchain, BinarySemaphoreHandle semaphore)
	{
		const auto * impl =
			static_cast<vulkan::VulkanSwapchain *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(swapchain), vulkan::swapchain_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanSemaphore called on a swapchain that is not a Vulkan one",
			};
		}

		const std::uint32_t swapchainId = (semaphore.index >> vulkan::kSwapchainIdShift) & vulkan::kSwapchainIdMask;
		const std::uint32_t slot		= semaphore.index & vulkan::kSwapchainSlotMask;
		if ((semaphore.index & vulkan::kDeviceBinarySemaphoreBit) != 0 || swapchainId != impl->id || slot >= impl->semaphores.size())
		{
			return Error{
				.code	 = ErrorCode::eInvalidHandle,
				.message = "swapchain semaphore handle out of range",
			};
		}

		return impl->semaphores[slot]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	}

	vk::CommandBuffer get_vulkan_command_buffer(CommandList commandList)
	{
		const auto * impl =
			static_cast<vulkan::VulkanCommandList *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(commandList), vulkan::render_command_block()));
		return impl != nullptr ? impl->buffer : vk::CommandBuffer{};
	}

	vk::CommandPool get_vulkan_command_pool(CommandPool commandPool)
	{
		const auto * impl =
			static_cast<vulkan::VulkanCommandPool *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(commandPool), vulkan::command_pool_block()));
		return impl != nullptr ? impl->pool : vk::CommandPool{};
	}

	Result<native::VulkanQueueView> get_vulkan_queue_view(Queue queue)
	{
		const auto * impl = static_cast<vulkan::VulkanQueue *>(detail::native_impl_of(detail::FacadeBuilder::impl_of(queue), vulkan::queue_block()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanQueueView called on a queue that is not a Vulkan one",
			};
		}

		return native::VulkanQueueView{ .queue = impl->queue, .familyIndex = impl->familyIndex };
	}

	Result<void> register_vulkan_backend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName		   = VulkanApi::kCanonicalName;
		info.info.displayName		   = VulkanApi::kDisplayName;
		info.info.apiVersionMajor	   = 1;
		info.info.apiVersionMinor	   = 3;
		info.info.supportsSurfaces	   = true;
		info.info.supportsDebugMarkers = true;
		info.createInstance			   = &vulkan::vulkan_create_instance;
		return registry.Register<VulkanApi>(info);
	}

	template <>
	Result<UniqueDevice> create_device<VulkanApi>(const DeviceDesc & desc)
	{
		if (const Result<void> checked = detail::check_device_desc(desc); !checked)
		{
			return checked.get_error();
		}

		Error error{};
		const InstanceDesc instanceDesc				   = instance_desc_for_device(desc);
		HostUniquePtr<vulkan::VulkanInstance> instance = vulkan::build_instance(instanceDesc, &error);
		if (!instance)
		{
			return error;
		}

		vulkan::VulkanInstance * instanceRaw = instance.get();
		void * device						 = vulkan::make_owned_device(instanceRaw, desc, &error);
		if (device == nullptr)
		{
			return error;
		}
		static_cast<vulkan::VulkanDevice *>(device)->ownedInstance = std::move(instance);
		void * deviceImpl										   = device;
		BackendBlockSet * blocks								   = detail::resolve_device_blocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::make_unique_device(deviceImpl, blocks);
	}

	namespace native
	{
		VulkanCommandListView NativeAccess<VulkanApi>::make_command_list_view(void * commandListImpl) noexcept
		{
			const auto * impl = static_cast<vulkan::VulkanCommandList *>(detail::native_impl_of(commandListImpl, vulkan::render_command_block()));
			return VulkanCommandListView{ .commandBuffer = impl != nullptr ? impl->buffer : vk::CommandBuffer{} };
		}
	}

}

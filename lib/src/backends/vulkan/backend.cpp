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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/backend/support/slot_map.hpp"
#include "azoth/rhi/backend/support/subresource.hpp"
#include "azoth/rhi/backend/table_validation.hpp"
#include "azoth/rhi/core/hash.hpp"
#include "azoth/rhi/core/profiling.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"

#include "backends/registration.hpp"
#include "backends/vulkan/internal.hpp"
#include "backends/vulkan/swapchain_bundle.hpp"
#include "support/driver_version.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace azo::rhi
{
	namespace vulkan
	{
		// Resolves a buffer handle to its slot under the device's validation mode. Lockless, like the rest of the resolves here.
		[[nodiscard]] BufferSlot * ResolveBuffer(VulkanDevice * device, BufferHandle handle) noexcept
		{
			return device->bufferSlots.Resolve(handle, kHandleAlreadyChecked);
		}

		// Bounds a request against a buffer. The whole-buffer sentinel clamps to the bytes left after offset, any other overrun returns false, which map, flush and
		// invalidate turn into eInvalidArgument.
		[[nodiscard]] bool BoundBufferRange(VkDeviceSize bufferSize, std::uint64_t offset, std::uint64_t & size) noexcept
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

		// The resolve helpers below are lockless reads, since the recording contract forbids registry mutation while recording. A failed range or generation check
		// returns null.

		[[nodiscard]] vk::PipelineLayout ResolvePipelineLayout(VulkanDevice * device, PipelineLayoutHandle handle) noexcept
		{
			const PipelineLayoutSlot * slot = device->pipelineLayoutSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->layout : vk::PipelineLayout{};
		}

		[[nodiscard]] vk::Image ResolveTexture(const VulkanDevice * device, TextureHandle handle) noexcept
		{
			const TextureSlot * slot = device->textureSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? vk::Image{ slot->image } : vk::Image{};
		}

		[[nodiscard]] const TextureViewSlot * ResolveTextureViewSlot(const VulkanDevice * device, TextureViewHandle handle) noexcept
		{
			return device->textureViewSlots.Resolve(handle, kHandleAlreadyChecked);
		}

		[[nodiscard]] vk::ImageView ResolveTextureView(const VulkanDevice * device, TextureViewHandle handle) noexcept
		{
			const TextureViewSlot * slot = ResolveTextureViewSlot(device, handle);
			return slot != nullptr ? slot->view : vk::ImageView{};
		}

		[[nodiscard]] vk::Pipeline ResolveGraphicsPipeline(VulkanDevice * device, GraphicsPipelineHandle handle) noexcept
		{
			const GraphicsPipelineSlot * slot = device->graphicsPipelineSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->pipeline : vk::Pipeline{};
		}

		[[nodiscard]] vk::Semaphore ResolveTimeline(VulkanDevice * device, TimelineHandle handle) noexcept
		{
			const TimelineSlot * slot = device->timelineSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->semaphore : vk::Semaphore{};
		}

		[[nodiscard]] vk::Semaphore ResolveBinarySemaphore(VulkanDevice * device, BinarySemaphoreHandle handle) noexcept
		{
			if ((handle.index & kDeviceBinarySemaphoreBit) != 0)
			{
				const std::uint32_t index			   = handle.index & ~kDeviceBinarySemaphoreBit;
				const BinarySemaphoreSlot * const slot = device->binarySemaphoreSlots.Resolve(
					BinarySemaphoreHandle{
						.index		= index,
						.generation = handle.generation,
					},
					true);
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

		// Maps buffer usage onto Vulkan bits. Ray tracing and shader binding table usages wait on their device extensions being enabled.

		[[nodiscard]] VulkanBackendOwner & Owner()
		{
			static VulkanBackendOwner owner;
			return owner;
		}

		/*
		 * Brings up the owner's loader-level dispatcher if no one has. A host that already seeded the process-global one from its own loader has that loader reused.
		 * Headless there is nothing to reuse so a DynamicLoader opens the library instead. Reached only from createInstance, which the RHI serializes. False when
		 * there is no Vulkan library to open, or one that has no vkGetInstanceProcAddr in it.
		 */
		[[nodiscard]] bool EnsureDispatcherInitialized(VulkanBackendOwner & owner)
		{
			if (owner.dispatch.vkGetInstanceProcAddr != nullptr)
			{
				return true;
			}

			// The window library's loader, if platform code brought one up through the presentation backend. Sharing it matters because a surface made from one loader
			// cannot be used by an instance from another.
			auto getInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
			if (getInstanceProcAddr == nullptr)
			{
				if (!owner.loader.has_value())
				{
					owner.loader.emplace();
				}

				// A loader that could not open reports it here and never throws, which is the shape it has once Vulkan-Hpp is told there are no exceptions.
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

			// The global stays initialized for whoever reaches past the RHI through GetVulkanNativeDevice and calls Vulkan-Hpp with its own defaults. Nothing in this
			// backend reads it: every call here goes through the table of the instance or device it belongs to.
			VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);
			return true;
		}

		std::pair<std::uint32_t, std::uint32_t> ResolveApiVersion(ApiVersion requested) noexcept
		{
			if (requested.major == 0 && requested.minor == 0)
			{
				return { 1, 3 };
			}

			return { requested.major, requested.minor };
		}

		std::uint32_t PackVkApiVersion(std::uint32_t major, std::uint32_t minor) noexcept
		{
			return VK_MAKE_API_VERSION(0, major, minor, 0);
		}

		namespace
		{
			/*
			 * Resolves one NativeValidationPolicy against the RHI mode.
			 *
			 * eFollowValidationMode is the default on most of these fields and what it follows is whether the mode is one that validates at all so a release build stays
			 * quiet without the caller spelling that out per field.
			 */
			[[nodiscard]] bool ResolveNativePolicy(const NativeValidationPolicy policy, const ValidationMode mode) noexcept
			{
				switch (policy)
				{
				case NativeValidationPolicy::eEnabled:				return true;
				case NativeValidationPolicy::eDisabled:				return false;
				case NativeValidationPolicy::eFollowValidationMode: return mode == ValidationMode::eDeveloper || mode == ValidationMode::eCapture;
				}

				return false;
			}
		} // namespace

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
			[[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT types, const vk::DebugUtilsMessengerCallbackDataEXT * data, void * userData) noexcept
		{
			// Severity bits are ordered and the messenger enables only warning and error so one comparison classifies each message.
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

			if (data != nullptr && data->pMessage != nullptr)
			{
				if (instance->onMessage != nullptr)
				{
					instance->onMessage(
						isError ? ValidationMessageSeverity::eError : ValidationMessageSeverity::eWarning, data->pMessage, instance->messageUserData);
				}
				else
				{
					// Nothing installed, so this is the whole of where a validation message goes. The layer reports through the messenger and nowhere else, and a check that
					// trips silently is worse than one printed somewhere the host did not choose.
					std::cerr << "[vulkan validation] " << data->pMessage << '\n';
				}
			}

			// Breaking here puts the debugger on the call that tripped the check, while the offending frames are still on the stack. Returning VK_TRUE instead would
			// abort the call, which is not what a caller asking to break wants.
			if ((isError && instance->breakOnError) || (isWarning && instance->breakOnWarning))
			{
				AZO_RHI_DEBUG_BREAK();
			}

			return VK_FALSE;
		}

		// Builds an instance from an InstanceDesc without registering it so the caller owns the result. Null with *error set on failure and the whole thing is
		// guarded because Vulkan-Hpp throws.
		[[nodiscard]] HostUniquePtr<VulkanInstance> BuildInstance(const InstanceDesc & desc, Error * error)
		{
			VulkanBackendOwner & owner = Owner();
			if (!EnsureDispatcherInitialized(owner))
			{
				*error = Error{
					.code	 = ErrorCode::eNativeApiError,
					.message = "no Vulkan loader could be opened",
				};
				return nullptr;
			}

			const auto instanceFail = [error](const char * message) -> HostUniquePtr<VulkanInstance>
			{
				*error = Error{
					.code	 = ErrorCode::eNativeApiError,
					.message = message,
				};
				return nullptr;
			};

			// Portability enumeration is what makes MoltenVK report its physical device. Headless needs no surface extensions and debug utils comes along when present,
			// for later labelling.
			const auto enumeratedExts = vk::enumerateInstanceExtensionProperties<HostAllocatorAdapter<vk::ExtensionProperties>>(nullptr, owner.dispatch);
			if (enumeratedExts.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance extension enumeration failed");
			}

			const detail::HostVector<vk::ExtensionProperties> & availExts = enumeratedExts.value;
			auto extAvailable											  = [&](const char * name)
			{
				return std::ranges::any_of(availExts,
					[name](const vk::ExtensionProperties & ep)
					{
						return std::strcmp(ep.extensionName, name) == 0;
					});
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

			// WSI extensions so platform code can later make a window surface from this instance. Only what the loader advertises is enabled, which keeps this a no-op
			// headless and portable across platforms.
			for (const char * surfaceExt : { VK_KHR_SURFACE_EXTENSION_NAME,
					 "VK_EXT_metal_surface",
					 "VK_MVK_macos_surface",
					 "VK_KHR_win32_surface",
					 "VK_KHR_xlib_surface",
					 "VK_KHR_xcb_surface",
					 "VK_KHR_wayland_surface" })
			{
				if (extAvailable(surfaceExt))
				{
					instanceExts.push_back(surfaceExt);
				}
			}

			// The Khronos layer follows nativeValidation.apiValidation, which defaults to the RHI mode so release stays quiet unless a caller asks otherwise. Requested
			// but absent is not an error: a machine without the SDK still gets a working instance.
			const NativeValidationDesc & native = desc.nativeValidation;
			const auto enumeratedLayers			= vk::enumerateInstanceLayerProperties<HostAllocatorAdapter<vk::LayerProperties>>(owner.dispatch);
			if (enumeratedLayers.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance layer enumeration failed");
			}

			const detail::HostVector<vk::LayerProperties> & availLayers = enumeratedLayers.value;
			auto layerAvailable											= [&](const char * name)
			{
				return std::ranges::any_of(availLayers,
					[name](const vk::LayerProperties & lp)
					{
						return std::strcmp(lp.layerName, name) == 0;
					});
			};

			detail::HostVector<const char *> layers;
			const bool wantApiValidation = ResolveNativePolicy(native.apiValidation, desc.validation);
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

			/*
			 * Synchronization validation, GPU-assisted validation and best practices are toggles of the Khronos layer and not layers of their own so they are requested
			 * through VK_EXT_validation_features and only mean anything when that layer is on.
			 */
			detail::HostVector<vk::ValidationFeatureEnableEXT> validationEnables;
			if (wantApiValidation)
			{
				if (ResolveNativePolicy(native.synchronizationValidation, desc.validation))
				{
					validationEnables.push_back(vk::ValidationFeatureEnableEXT::eSynchronizationValidation);
				}
				if (ResolveNativePolicy(native.gpuBasedValidation, desc.validation))
				{
					validationEnables.push_back(vk::ValidationFeatureEnableEXT::eGpuAssisted);
				}
				if (ResolveNativePolicy(native.bestPractices, desc.validation))
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

			const auto [apiMajor, apiMinor]	 = ResolveApiVersion(desc.apiVersion);
			const std::uint32_t requestedApi = PackVkApiVersion(apiMajor, apiMinor);
			const auto loaderApi			 = vk::enumerateInstanceVersion(owner.dispatch);
			if (loaderApi.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan loader version query failed");
			}

			if (requestedApi > loaderApi.value)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "requested Vulkan version exceeds the loader instance version",
				};
				return nullptr;
			}

			const vk::ApplicationInfo appInfo(desc.applicationName,
				VK_MAKE_VERSION(desc.applicationVersionMajor, desc.applicationVersionMinor, 0),
				desc.engineName,
				VK_MAKE_VERSION(desc.engineVersionMajor, desc.engineVersionMinor, 0),
				requestedApi);
			vk::InstanceCreateInfo instInfo(instanceFlags, &appInfo);
			instInfo.setPEnabledExtensionNames(instanceExts);
			instInfo.setPEnabledLayerNames(layers);
			if (useValidationFeatures)
			{
				instInfo.pNext = &validationFeatures;
			}

			auto instance = HostNew<VulkanInstance>();
			if (instance == nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan instance allocation failed",
				};
				return nullptr;
			}

			instance->object		  = PublishingObject<Published<InstanceApi, &InstanceBlock>, Published<ExternalCapabilityApi, &ExternalCapabilityBlock>>();
			instance->breakOnError	  = native.breakOnError;
			instance->breakOnWarning  = native.breakOnWarning;
			instance->onMessage		  = native.onMessage;
			instance->messageUserData = native.messageUserData;
			instance->dispatch		  = owner.dispatch;

			const auto created = vk::createInstance(instInfo, nullptr, instance->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return instanceFail("Vulkan instance creation failed");
			}

			instance->instance	 = created.value;
			instance->debugUtils = extAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

			// From here this instance's table carries the instance-level entry points of the layer chain the create above negotiated, which is what makes it wrong to
			// share with an instance that negotiated a different one.
			instance->dispatch.init(instance->instance);

			// The global follows the newest instance, as it always has. See the note beside the matching device init for what it is still for.
			VULKAN_HPP_DEFAULT_DISPATCHER.init(instance->instance);

			/*
			 * Wherever the layer is on, route its messages through a messenger that counts them and hands the text to onMessage, or to stderr when the caller installed
			 * nothing, so the validation-clean assertions have a number to read, not messages that only print. Tied to the same policy as the layer so a caller enabling
			 * validation in a release build still gets the counts and the break.
			 */
			if (instance->debugUtils && wantApiValidation)
			{
				vk::DebugUtilsMessengerCreateInfoEXT messengerInfo;
				messengerInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
				messengerInfo.messageType	  = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
												vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
				messengerInfo.pfnUserCallback = &DebugMessengerCallback;
				messengerInfo.pUserData		  = instance.get();

				// Best effort, like the object naming it feeds. A messenger that could not be made costs the counts, not the instance.
				const auto messenger = instance->instance.createDebugUtilsMessengerEXT(messengerInfo, nullptr, instance->dispatch);
				if (messenger.result == vk::Result::eSuccess)
				{
					instance->debugMessenger = messenger.value;
				}
			}

			return instance;
		}

		// Builds an instance and registers it with the process owner so the returned pointer is borrowed. The owner releases it when its last device is destroyed or
		// when a UniqueInstance built from it is reset. It is not held until process exit.
		[[nodiscard]] VulkanInstance * MakeOwnedInstance(const InstanceDesc & desc, Error * error)
		{
			HostUniquePtr<VulkanInstance> instance = BuildInstance(desc, error);
			if (!instance)
			{
				return nullptr;
			}
			VulkanBackendOwner & owner = Owner();
			VulkanInstance * raw	   = instance.get();
			owner.instances.push_back(std::move(instance));
			return raw;
		}

		/*
		 * Which host clock a calibration pairs the device clock with, and whether the adapter offers both.
		 *
		 * Not the same question as whether VK_EXT_calibrated_timestamps is enabled: a driver may expose it and enumerate neither domain, and a calibration
		 * naming one the adapter lacks is refused. Shared so the cap and the call cannot disagree.
		 */
		[[nodiscard]] bool VulkanCalibrationDomains(
			vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, vk::TimeDomainEXT & hostDomain) noexcept
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

		// Picks a physical device, creates the logical device, its queues and the VMA allocator, then fills the capability and adapter records from real queries.
		// Null with *error set on failure. The instance is borrowed.
		[[nodiscard]] VulkanDevice * MakeOwnedDevice(VulkanInstance * instance, const DeviceDesc & desc, Error * error)
		{
			VulkanBackendOwner & owner = Owner();

			const auto deviceFail = [error](const char * message) -> VulkanDevice *
			{
				*error = Error{
					.code	 = ErrorCode::eNativeApiError,
					.message = message,
				};
				return nullptr;
			};

			const auto enumerated = instance->instance.enumeratePhysicalDevices<HostAllocatorAdapter<vk::PhysicalDevice>>(instance->dispatch);
			if (enumerated.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan physical device enumeration failed");
			}

			const detail::HostVector<vk::PhysicalDevice> & physicals = enumerated.value;
			if (physicals.empty())
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "no Vulkan physical devices found",
				};
				return nullptr;
			}

			std::uint32_t adapterIndex = 0;
			vk::PhysicalDevice phys;
			if (desc.preferredAdapterIndex != kInvalidIndex && desc.preferredAdapterIndex < physicals.size())
			{
				adapterIndex = desc.preferredAdapterIndex;
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				phys = physicals[adapterIndex];

				// An explicitly chosen adapter must still satisfy the required features.
				for (const DeviceFeature feature : desc.requiredFeatures)
				{
					if (!AdapterSupportsFeature(phys, instance->dispatch, feature))
					{
						*error = Error{
							.code	 = ErrorCode::eUnsupportedFeature,
							.message = RequiredFeatureMessage(feature),
						};
						return nullptr;
					}
				}
			}
			else
			{
				// Score eligible adapters: a discrete GPU outweighs any number of preferred features, which only break ties. Adapters missing a required feature are
				// skipped.
				int bestScore = -1;
				for (std::uint32_t i = 0; i < physicals.size(); ++i)
				{
					const vk::PhysicalDevice candidate = physicals[i];
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					if (!AdapterSupportsAllFeatures(candidate, instance->dispatch, desc.requiredFeatures))
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
						if (AdapterSupportsFeature(candidate, instance->dispatch, feature))
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
					// Name the first required feature no adapter supports, otherwise report the combination.
					for (const DeviceFeature feature : desc.requiredFeatures)
					{
						const bool anySupports = std::ranges::any_of(physicals,
							[feature, instance](vk::PhysicalDevice candidate) noexcept
							{
								return AdapterSupportsFeature(candidate, instance->dispatch, feature);
							});

						if (!anySupports)
						{
							*error = Error{
								.code	 = ErrorCode::eUnsupportedFeature,
								.message = RequiredFeatureMessage(feature),
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
			bool foundGraphics			 = false;
			std::uint32_t graphicsFamily = 0;
			for (std::uint32_t i = 0; i < qfs.size(); ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (static_cast<bool>(qfs[i].queueFlags & vk::QueueFlagBits::eGraphics))
				{
					graphicsFamily = i;
					foundGraphics  = true;
					break;
				}
			}

			if (!foundGraphics)
			{
				*error = Error{
					.code	 = ErrorCode::eUnsupportedFeature,
					.message = "no Vulkan graphics queue family",
				};
				return nullptr;
			}

			// A queue capability, not a device feature, and an adapter can have every sparse feature with no family carrying this. Only the graphics family is asked,
			// that being the one this backend binds on.
			const bool bindsSparse = static_cast<bool>(qfs[graphicsFamily].queueFlags & vk::QueueFlagBits::eSparseBinding);

			/*
			 * Pick the most specific family for compute and copy: async compute without graphics, transfer without graphics or compute. Each falls back to the graphics
			 * family. The spec guarantees a graphics family runs transfer work, but it only guarantees compute on some graphics family and not necessarily on the first
			 * one this picks.
			 */
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

			// Resolve the requested version ({0, 0} means 1.3) and reject what the adapter lacks or anything below the 1.2 floor, where timeline semaphores and
			// descriptor indexing are core and synchronization2 comes from VK_KHR_synchronization2.
			const auto [apiMajor, apiMinor]	 = ResolveApiVersion(desc.apiVersion);
			const std::uint32_t requestedApi = PackVkApiVersion(apiMajor, apiMinor);
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
			const bool core13 = apiMajor > 1 || (apiMajor == 1 && apiMinor >= 3); // dynamic rendering + synchronization2 are core

			// Swapchain is universal, portability subset is opted into when advertised. Below the promotion versions the features come from KHR extensions, which must
			// then be present.
			const auto enumeratedExts = phys.enumerateDeviceExtensionProperties<HostAllocatorAdapter<vk::ExtensionProperties>>(nullptr, instance->dispatch);
			if (enumeratedExts.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan device extension enumeration failed");
			}

			const detail::HostVector<vk::ExtensionProperties> & availableExts = enumeratedExts.value;
			const auto hasExt												  = [&availableExts](const char * name) noexcept
			{
				return std::ranges::any_of(availableExts,
					[name](const vk::ExtensionProperties & ep)
					{
						return std::strcmp(ep.extensionName, name) == 0;
					});
			};

			// Dynamic rendering is core at 1.3 and a KHR extension below. DeviceDesc::dynamicRendering then decides: eDisabled and an ePreferred adapter without it both
			// fall back to render-pass objects, while eRequired fails outright.
			const bool adapterHasDynamicRendering = core13 || hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			bool useDynamicRendering			  = false;
			switch (desc.dynamicRendering)
			{
			case DynamicRenderingMode::eDisabled:  useDynamicRendering = false; break;
			case DynamicRenderingMode::ePreferred: useDynamicRendering = adapterHasDynamicRendering; break;
			case DynamicRenderingMode::eRequired:
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
			// VK_EXT_calibrated_timestamps backs CalibrateTimestamp by sampling the device and host clocks as one pair. Optional so the cap stays false without it and
			// the query fails with eUnsupportedFeature.
			const bool hasCalibratedTimestamps = hasExt(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
			if (hasCalibratedTimestamps)
			{
				deviceExts.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
			}

			/*
			 * The external memory and semaphore transports, each taken only where the driver advertises it. The capability structures and the family that owns the
			 * handle type enumerations are core since 1.1 and this backend floors at 1.2. What is left to enable is the transport: a file descriptor everywhere but
			 * Windows, an NT handle there.
			 *
			 * Both families are asked for separately because they are separate extensions and a driver may answer them differently.
			 */
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

			// Conservative rasterization. The extension carries the property struct the tier is read from, and the fully-covered fragment input is a separate feature on
			// top of it, so the two are queried together and reported as one ladder.
			const bool hasConservativeRaster		= hasExt(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
			ConservativeRasterTier conservativeTier = ConservativeRasterTier::eNone;
			if (hasConservativeRaster)
			{
				deviceExts.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);

				vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeProps{};
				vk::PhysicalDeviceProperties2 props2{};
				props2.pNext = &conservativeProps;
				phys.getProperties2(&props2, instance->dispatch);

				/*
				 * The extension itself is the overestimate the base level names. Degenerates culled, not rasterized, is what the level above it adds, and the fully-covered
				 * fragment input is the top one.
				 *
				 * Climbed in order and not picked, because the levels mirror Direct3D's tiers and those are cumulative. A device with inner coverage but degenerates still
				 * rasterized therefore reports the base level, which understates it and never promises the level below the top.
				 */
				conservativeTier = ConservativeRasterTier::eBasic;
				if (conservativeProps.degenerateTrianglesRasterized == 0u)
				{
					conservativeTier = conservativeProps.fullyCoveredFragmentShaderInputVariable != 0u ? ConservativeRasterTier::eInnerCoverage
																									   : ConservativeRasterTier::eDegenerateCulling;
				}
			}

			// Descriptor indexing is 1.2 core but its features are individually optional so enable only what bindless needs: a runtime-sized, partially-bound,
			// non-uniform indexable sampled-image array. supportsBindless means all three are there.
			vk::PhysicalDeviceVulkan12Features supported12{};
			vk::PhysicalDeviceFeatures2 supportedFeatures2{};
			supportedFeatures2.pNext = &supported12;
			phys.getFeatures2(&supportedFeatures2, instance->dispatch);
			const bool bindless = static_cast<bool>(supported12.runtimeDescriptorArray) &&
								  static_cast<bool>(supported12.shaderSampledImageArrayNonUniformIndexing) &&
								  static_cast<bool>(supported12.descriptorBindingPartiallyBound);

			// shaderFloat16 is 1.2 core but optional like the set above so it is enabled when exposed and the cap reports exactly that.
			const bool shaderFloat16 = static_cast<bool>(supported12.shaderFloat16);

			// Version-aware: the core 1.3 structs at 1.3 and up, the KHR ones they promoted from below. Timeline semaphores are 1.2 core, synchronization2 is always on,
			// dynamic rendering only when this device uses it. The chain hangs off PhysicalDeviceFeatures2.
			vk::PhysicalDeviceFeatures2 features2;
			vk::PhysicalDeviceVulkan13Features features13;
			vk::PhysicalDeviceVulkan12Features features12;
			vk::PhysicalDeviceVulkan11Features features11;
			vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
			vk::PhysicalDeviceSynchronization2Features sync2Features;
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

			// features12 is always chained and sits at the tail so hanging the 1.1 struct off it brings both within reach of the create info, along with the
			// descriptor-indexing fields enabled here.
			features12.pNext = &features11;

			/*
			 * The portability subset is validated against what the device enabled, not what the adapter supports, so a bit left off this chain is off however the
			 * adapter answered. Everything offered is echoed straight back: withholding one would only disable a feature the caps then report as present, and nothing
			 * here wants a portability feature switched off.
			 */
			PortabilitySubsetFeatures portabilityFeatures{};
			if (QueryPortabilitySubsetFeatures(phys, instance->dispatch, portabilityFeatures))
			{
				portabilityFeatures.pNext = features11.pNext;
				features11.pNext		  = &portabilityFeatures;
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

			// Enable the negotiated set: every required feature, which selection already checked for, plus each preferred one the chosen adapter supports. They live in
			// the feature chain's base struct.
			vk::PhysicalDeviceFeatures enabledFeatures{};
			for (const DeviceFeature feature : desc.requiredFeatures)
			{
				EnableFeatureBit(enabledFeatures, features11, feature);
			}
			for (const DeviceFeature feature : desc.preferredFeatures)
			{
				if (AdapterSupportsFeature(phys, instance->dispatch, feature))
				{
					EnableFeatureBit(enabledFeatures, features11, feature);
				}
			}
			features2.features = enabledFeatures;

			// Resolve the DeviceDesc queue requests into per-type counts. A request that requires a dedicated compute or copy queue fails when the adapter folds that
			// capability onto the graphics family.
			const QueuePlan plan = PlanQueues(desc.queues);
			if (plan.computeDedicated && plan.computeCount > 0 && computeFamily == graphicsFamily)
			{
				*error = Error{ .code = ErrorCode::eUnsupportedFeature,
					.message		  = "a dedicated compute queue was required but the adapter has no dedicated compute family" };
				return nullptr;
			}

			// Comparing against the graphics family alone is enough: copyFamily is picked with neither graphics nor compute so a dedicated one differs from
			// computeFamily by construction and the only non-dedicated case is having folded back onto graphics.
			if (plan.copyDedicated && plan.copyCount > 0 && copyFamily == graphicsFamily)
			{
				*error = Error{ .code = ErrorCode::eUnsupportedFeature,
					.message		  = "a dedicated copy queue was required but the adapter has no dedicated transfer family" };
				return nullptr;
			}

			// One DeviceQueueCreateInfo per distinct family, since Vulkan rejects duplicates. Each takes the sum of the requests resolving to it, clamped to its
			// queueCount. On a single all-capable family such as MoltenVK's they collapse and share its queues.
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

				families.push_back(FamilyQueues{
					.family = family,
					.count	= requested,
				});
			};
			addRequest(graphicsFamily, plan.graphicsCount);
			addRequest(computeFamily, plan.computeCount);
			addRequest(copyFamily, plan.copyCount);

			std::uint32_t maxQueuesPerFamily = 1;
			for (FamilyQueues & entry : families)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				entry.count = std::min(entry.count, qfs[entry.family].queueCount);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				maxQueuesPerFamily = std::max(maxQueuesPerFamily, entry.count);
			}

			// All queues take the same priority so one shared array at least as long as the largest family backs every create info.
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

			auto record = HostNew<VulkanDevice>();
			if (record == nullptr)
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan device allocation failed",
				};
				return nullptr;
			}

			/*
			 * Two published sets, chosen by whether this device enabled a transport from either external family.
			 *
			 * Block presence is what this codebase reports capability from, and QueryExternalHandleSupport answers per object kind, so demanding both families
			 * left an adapter with memory transports and no semaphore ones calling a texture shareable through a block nothing published. Each entry already
			 * refuses the transport it lacks, so publishing on either keeps the two answers together.
			 */
			const bool sharesExternally	   = hasExternalMemoryFd || hasExternalMemoryWin32 || hasExternalSemaphoreFd || hasExternalSemaphoreWin32;
			record->object				   = sharesExternally ? PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
																	Published<PresentApi, &PresentBlock>,
																	Published<PlacedMemoryApi, &PlacedMemoryBlock>,
																	Published<QueryApi, &QueryBlock>,
																	Published<PipelineCacheApi, &PipelineCacheBlock>,
																	Published<ResidencyApi, &ResidencyBlock>,
																	Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
																	Published<AdoptionApi, &AdoptionBlock>,
																	Published<ExternalSharingApi, &ExternalSharingBlock>>()
															  : PublishingObject<Published<CoreDeviceApi, &CoreDeviceBlock>,
																	Published<PresentApi, &PresentBlock>,
																	Published<PlacedMemoryApi, &PlacedMemoryBlock>,
																	Published<QueryApi, &QueryBlock>,
																	Published<PipelineCacheApi, &PipelineCacheBlock>,
																	Published<ResidencyApi, &ResidencyBlock>,
																	Published<ResourceIntrospectionApi, &ResourceIntrospectionBlock>,
																	Published<AdoptionApi, &AdoptionBlock>>();
			record->externalMemoryFd	   = hasExternalMemoryFd;
			record->externalMemoryWin32	   = hasExternalMemoryWin32;
			record->externalSemaphoreFd	   = hasExternalSemaphoreFd;
			record->externalSemaphoreWin32 = hasExternalSemaphoreWin32;
			record->instance			   = instance->instance;
			record->instanceWrapper		   = instance;
			record->phys				   = phys;

			// The device's table starts as its instance's so the instance-level entries are already right and createDevice below is dispatched through the same layer
			// chain the instance negotiated.
			record->dispatch = instance->dispatch;

			const auto createdDevice = phys.createDevice(deviceInfo, nullptr, record->dispatch);
			if (createdDevice.result != vk::Result::eSuccess)
			{
				return deviceFail("Vulkan device creation failed");
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

			// A distinct tag per live device, carried in the top bits of every handle's index word so another device rejects it outright. The pool is process-global and
			// returns the tag at teardown so the ceiling is devices alive at once.
			std::uint32_t deviceTag = 0;
			if (!detail::DeviceTags().Acquire(deviceTag))
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "no device tag is available, too many devices are alive at once",
				};
				return nullptr;
			}
			record->deviceTag = deviceTag;

			/*
			 * The tag goes back on every way out of here that does not reach the owner list. The pool is process-global with a ceiling of 255 and is shared by
			 * every backend, so a caller probing adapters in a loop would otherwise drop that ceiling by one per failed create until no backend can make a device
			 * at all. Dismissed once the record is owned, from which point the destroy path releases it.
			 */
			auto tagGuard = detail::MakeScopeGuard(
				[deviceTag]() noexcept
				{
					detail::DeviceTags().Release(deviceTag);
				});

			record->bufferSlots.Rebind(deviceTag);
			record->pipelineLayoutSlots.Rebind(deviceTag);
			record->graphicsPipelineSlots.Rebind(deviceTag);
			record->computePipelineSlots.Rebind(deviceTag);
			record->timelineSlots.Rebind(deviceTag);
			record->queryPoolSlots.Rebind(deviceTag);
			record->samplerSlots.Rebind(deviceTag);
			record->descriptorSetLayoutSlots.Rebind(deviceTag);
			record->heapSlots.Rebind(deviceTag);
			record->pipelineCacheSlots.Rebind(deviceTag);
			record->binarySemaphoreSlots.Rebind(deviceTag);
			record->textureSlots.Rebind(deviceTag);
			record->textureViewSlots.Rebind(deviceTag);
			record->descriptorSetSlots.Rebind(deviceTag);

			// Specializes the device-level entries to this VkDevice, skipping the loader's dispatch trampoline. Only this device's table is touched so a device already
			// alive keeps calling through the entry points its own objects were made with.
			record->dispatch.init(record->device);

			/*
			 * The process-global table follows the newest device, which is all it can do and all it ever did. It exists for callers reaching past the RHI through
			 * GetVulkanNativeDevice with Vulkan-Hpp's own defaults and those holding more than one device should take the table from VulkanNativeDevice::dispatch and
			 * not this one.
			 */
			VULKAN_HPP_DEFAULT_DISPATCHER.init(record->device);

			// Hand each type its slice of its family's queues. When a shared family runs out, the type aliases queue 0 so every requested capability still resolves to a
			// queue.
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

			// minCount is a minimum so a type whose family cannot supply that many distinct queues is rejected and never quietly handed fewer. Aliasing onto a shared
			// family's queue 0 still satisfies a request whose count it can meet.
			if (record->graphicsQueues.size() < plan.graphicsCount || record->computeQueues.size() < plan.computeCount ||
				record->copyQueues.size() < plan.copyCount)
			{
				*error = Error{ .code = ErrorCode::eUnsupportedFeature,
					.message		  = "the adapter cannot supply the requested minimum number of queues for a queue type" };
				return nullptr;
			}

			record->caps.graphicsQueueCount		   = static_cast<std::uint32_t>(record->graphicsQueues.size());
			record->caps.computeQueueCount		   = static_cast<std::uint32_t>(record->computeQueues.size());
			record->caps.copyQueueCount			   = static_cast<std::uint32_t>(record->copyQueues.size());
			record->caps.hasDedicatedComputeQueue  = !record->computeQueues.empty() && computeFamily != graphicsFamily;
			record->caps.hasDedicatedTransferQueue = !record->copyQueues.empty() && copyFamily != graphicsFamily;

			// VMA cannot call Vulkan directly under VK_NO_PROTOTYPES so it takes the dispatcher's instance and device proc-address getters.
			VmaVulkanFunctions vmaFns{};
			vmaFns.vkGetInstanceProcAddr = record->dispatch.vkGetInstanceProcAddr;
			vmaFns.vkGetDeviceProcAddr	 = record->dispatch.vkGetDeviceProcAddr;
			VmaAllocatorCreateInfo allocatorInfo{};
			allocatorInfo.physicalDevice   = phys;
			allocatorInfo.device		   = record->device;
			allocatorInfo.instance		   = instance->instance;
			allocatorInfo.vulkanApiVersion = requestedApi;
			allocatorInfo.pVulkanFunctions = &vmaFns;
// Any installed sink sees these, not just the bundled Tracy one. The guard is on profiling as a whole so a build with it off installs no callbacks at all and
// never pays VMA's per-block call to reach macros that expand to nothing.
#ifdef AZOTH_RHI_ENABLE_PROFILING
			constexpr VmaDeviceMemoryCallbacks memoryCallbacks{
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

			allocatorInfo.pDeviceMemoryCallbacks = &memoryCallbacks;
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
			const vk::PhysicalDeviceProperties & props			   = propsChain.get<vk::PhysicalDeviceProperties2>().properties;
			const vk::PhysicalDeviceDriverProperties & driverProps = propsChain.get<vk::PhysicalDeviceDriverProperties>();
			const vk::PhysicalDeviceLimits & limits				   = props.limits;
			const vk::PhysicalDeviceFeatures feats				   = phys.getFeatures(record->dispatch);

			record->adapterName		 = detail::HostString(props.deviceName.data());
			record->driverInfo		 = detail::HostString(driverProps.driverInfo.data());
			record->driverVersionStr = FormatVulkanDriverVersion(MapDriverId(driverProps.driverID), props.driverVersion);

			std::uint64_t deviceLocalBytes					  = 0;
			const vk::PhysicalDeviceMemoryProperties memProps = phys.getMemoryProperties(record->dispatch);
			for (std::uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (static_cast<bool>(memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal))
				{
					deviceLocalBytes += memProps.memoryHeaps[i].size;
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				}
			}

			record->adapter.type					  = MapAdapterType(props.deviceType);
			record->adapter.apiId					  = VulkanApi::id;
			record->adapter.adapterIndex			  = adapterIndex;
			record->adapter.vendorId				  = props.vendorID;
			record->adapter.deviceId				  = props.deviceID;
			record->adapter.dedicatedVideoMemoryBytes = deviceLocalBytes;
			record->adapter.unifiedMemoryArchitecture =
				props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu || props.deviceType == vk::PhysicalDeviceType::eCpu;
			record->adapter.name			 = record->adapterName.c_str();
			record->adapter.driverId		 = MapDriverId(driverProps.driverID);
			record->adapter.driverVersionRaw = props.driverVersion;
			record->adapter.driverVersion	 = record->driverVersionStr.c_str();
			record->adapter.driverInfo		 = record->driverInfo.c_str();
			FillAdapterIdentity(record->adapter, propsChain.get<vk::PhysicalDeviceIDProperties>());

			record->caps.apiId		= VulkanApi::id;
			record->caps.apiVersion = ApiVersion{
				.major = apiMajor,
				.minor = apiMinor,
			};
			record->caps.supportsDynamicRendering = useDynamicRendering;

			// Read from what the adapter exposes, not assumed from the create call having succeeded. Creation does request this unconditionally, since the sync model
			// has nothing to fall back on without it, so the two agree today. Reading it keeps them agreeing if that ever changes.
			record->caps.supportsTimelineSync = static_cast<bool>(supported12.timelineSemaphore);

			// The messenger is what does the tallying so the counts mean something only where one was installed. Without it they stay zero, which would otherwise be
			// indistinguishable from a run that produced no messages.
			record->caps.reportsValidationMessageCounts = instance != nullptr && static_cast<bool>(instance->debugMessenger);
			/*
			 * vkCmdDrawIndirectCount is core in Vulkan 1.2, the backend floor so the count-buffer draws are always available. Read by VulkanCommandListQueryInterface,
			 * which is what turns it into a published block. Nothing else consults it: what a caller sees comes off the block, not off here.
			 */
			record->caps.supportsIndirectCount = true;
			/*
			 * vkCmdBlitImage is core with no feature to gate it, so the device half is unconditional and whether a given format can take part is the per-format half
			 * getFormatSupport already answers from eBlitSrc and eBlitDst. Both halves are needed: the facade zeroes the per-format pair when this is false, so leaving
			 * it unset would hide a scaled blit this backend has.
			 */
			record->caps.supportsScaledBlit = true;
			// bindDescriptorSet lowers these onto vkCmdBindDescriptorSets' pDynamicOffsets, so the flag says what this backend already does. Direct3D 12 refuses a
			// non-empty list against the same flag, which is what makes it worth reporting without leaving at its default.
			record->caps.supportsDynamicBufferOffsets = true;
			// These read what this device enabled, not what the adapter exposes. Vulkan permits the matching use only when the feature was requested at creation so a
			// cap reporting mere adapter support would invite the illegal state it exists to prevent.
			record->caps.supportsMultiDrawIndirect		   = static_cast<bool>(enabledFeatures.multiDrawIndirect);
			record->caps.supportsDrawIndirectFirstInstance = static_cast<bool>(enabledFeatures.drawIndirectFirstInstance);
			record->caps.supportsShaderDrawParameters	   = static_cast<bool>(features11.shaderDrawParameters);
			record->caps.supportsShaderFloat16			   = shaderFloat16;
			// The three features bindless needs arrive together on every driver that has any of them, so this backend reads one level or the base one and does not
			// distinguish dynamic indexing from unbounded arrays.
			record->caps.bindingTier					   = bindless ? BindingTier::eUnbounded : BindingTier::eBasic;
			record->caps.supportsPartiallyBoundDescriptors = bindless;
			record->caps.supportsUpdateAfterBind		   = static_cast<bool>(supported12.descriptorBindingSampledImageUpdateAfterBind);
			// The extension alone is not the capability. An adapter offering it without enumerating both clocks refuses every calibration, so the domains are
			// asked for here and the answer is what the cap reports.
			vk::TimeDomainEXT calibrationHostDomain	  = vk::TimeDomainEXT::eDevice;
			record->caps.supportsTimestampCalibration = hasCalibratedTimestamps && VulkanCalibrationDomains(phys, record->dispatch, calibrationHostDomain);
			record->caps.maxBindlessSampledTextures	  = bindless ? limits.maxPerStageDescriptorSampledImages : 0u;
			record->caps.maxBindlessStorageBuffers	  = bindless ? limits.maxPerStageDescriptorStorageBuffers : 0u;
			record->caps.maxSamplerDescriptors		  = bindless ? limits.maxPerStageDescriptorSamplers : 0u;
			record->caps.supportsTimestampQueries	  = static_cast<bool>(limits.timestampComputeAndGraphics);
			// vkCmdWriteTimestamp2 is legal inside a render-pass instance, so the scope a timestamp sits in is not a restriction Vulkan carries.
			record->caps.supportsTimestampWritesInScope = record->caps.supportsTimestampQueries;
			record->caps.supportsAnisotropy				= static_cast<bool>(enabledFeatures.samplerAnisotropy);
			record->caps.supportsIndependentBlend		= static_cast<bool>(enabledFeatures.independentBlend);
			record->caps.supportsTextureViewSwizzle		= AdapterSupportsViewSwizzle(phys, record->dispatch);
			record->caps.supportsMultiPlanarFormats		= AdapterSupportsMultiPlanarFormats(phys, record->dispatch);
			// Reports what this device enabled, not what the adapter offers: a conversion created with the feature off is a validation error, so a cap reading adapter
			// support would invite exactly the illegal state it exists to prevent. The bit is on only where it was declared.
			record->caps.supportsSamplerYcbcrConversion	   = static_cast<bool>(features11.samplerYcbcrConversion) && record->caps.supportsMultiPlanarFormats;
			record->caps.supportsDepthBounds			   = static_cast<bool>(feats.depthBounds);
			record->caps.supportsPipelineStatisticsQueries = static_cast<bool>(feats.pipelineStatisticsQuery);
			record->caps.conservativeRasterTier			   = conservativeTier;
			record->caps.shaderBinaryFormat				   = ShaderBinaryFormat::eSpirV;
			/*
			 * One ladder, not a bit per resource kind. Image residency without the binding feature under it is not a state Vulkan can be in, so the levels were never
			 * independent. Binding is what the queue does, so dispatch holds this at eNone for as long as this backend declines SparseApi and the level becomes live the
			 * moment it publishes one.
			 */
			record->caps.sparseTier = SparseTier::eNone;
			if ((enabledFeatures.sparseBinding != 0u) && (enabledFeatures.sparseResidencyBuffer != 0u) && record->graphicsFamilyBindsSparse)
			{
				record->caps.sparseTier = SparseTier::eBuffers;
				if (enabledFeatures.sparseResidencyImage2D != 0u)
				{
					record->caps.sparseTier = (enabledFeatures.sparseResidencyImage3D != 0u) ? SparseTier::eResidentVolumes : SparseTier::eResidentTextures;
				}
			}

			/*
			 * The sparse block size, which Vulkan states per resource and not as a device limit.
			 *
			 * Read off the memory requirements of a throwaway sparse buffer, the same way getBufferMemoryInfo answers an alignment, because every sparse bind is
			 * expressed in whole tiles and a tier without a tile size is a ladder a caller cannot climb.
			 */
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

			/*
			 * The most descriptors of any one type a single set may declare.
			 *
			 * Vulkan states a ceiling per descriptor type and not one for the set, so the smallest of them is the number a caller can rely on whatever they fill the set
			 * with. Taking the largest instead would be a ceiling that only holds for one type.
			 */
			record->caps.maxDescriptorsPerSet				= std::min({ limits.maxDescriptorSetSamplers,
				limits.maxDescriptorSetUniformBuffers,
				limits.maxDescriptorSetStorageBuffers,
				limits.maxDescriptorSetSampledImages,
				limits.maxDescriptorSetStorageImages });
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
			// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			record->caps.timestampValidBits = qfs[graphicsFamily].timestampValidBits;
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			VulkanDevice * raw = record.get();
			if (!detail::TryPushBack(owner.devices, std::move(record)))
			{
				*error = Error{
					.code	 = ErrorCode::eOutOfHostMemory,
					.message = "Vulkan device allocation failed",
				};
				return nullptr;
			}

			tagGuard.Dismiss();
			return raw;
		}

		// Tears down a device created by this backend: drop its owning record, which runs ~VulkanDevice. Called once from UniqueDevice's destructor through the
		// dispatch table.
		void VulkanDestroyDevice(void * impl) noexcept
		{
			VulkanBackendOwner & owner = Owner();

			// Capture the owning instance before the device record is dropped so we can retire the instance alongside its last device (below). The tag goes back at the
			// same time so the ceiling is devices alive at once, not devices ever created.
			VulkanInstance * owningInstance = nullptr;
			std::uint32_t releasedTag		= 0;
			for (const HostUniquePtr<VulkanDevice> & device : owner.devices)
			{
				if (device.get() == impl)
				{
					owningInstance = device->instanceWrapper;
					releasedTag	   = device->deviceTag;
					break;
				}
			}

			std::erase_if(owner.devices,
				[impl](const HostUniquePtr<VulkanDevice> & device)
				{
					return device.get() == impl;
				});
			detail::DeviceTags().Release(releasedTag);

			/*
			 * Retire the instance once its last device is gone, destroying the VkInstance here and never deferring to the static owner's destructor at process exit. By
			 * then the app's SDL loader or headless the DynamicLoader this owner holds, has already unloaded the Vulkan library so a deferred vkDestroyInstance would
			 * jump into an unloaded module and fault. An instance with no device left has no other owner and its VkDevice children were destroyed just above so the
			 * teardown order is right.
			 */
			if (owningInstance != nullptr)
			{
				bool stillUsed = false;
				for (const HostUniquePtr<VulkanDevice> & device : owner.devices)
				{
					if (device->instanceWrapper == owningInstance)
					{
						stillUsed = true;
						break;
					}
				}
				if (!stillUsed)
				{
					std::erase_if(owner.instances,
						[owningInstance](const HostUniquePtr<VulkanInstance> & instance)
						{
							return instance.get() == owningInstance;
						});
				}
			}
		}

		// Tears down an instance nothing built a device from: drop its owning record, which runs ~VulkanInstance. Called once from UniqueInstance's destructor
		// through the dispatch table, for the same reason the device entry above retires its instance and does not leave it to the static.
		void VulkanDestroyInstance(void * impl) noexcept
		{
			VulkanBackendOwner & owner = Owner();
			std::erase_if(owner.instances,
				[impl](const HostUniquePtr<VulkanInstance> & instance)
				{
					return instance.get() == impl;
				});
		}

		bool Succeed(Error * error) noexcept
		{
			if (error != nullptr)
			{
				*error = {};
			}

			return true;
		}

		bool Fail(Error * error, ErrorCode code, const char * message) noexcept
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

		// Every not-yet-implemented slot routes here. Value outputs are reset before failure.

		// Device identity and capability entries, the part this slice implements for real.
		GraphicsApiId VulkanDeviceApiId([[maybe_unused]] void * impl) noexcept
		{
			return VulkanApi::id;
		}

		std::string_view VulkanDeviceApiName([[maybe_unused]] void * impl) noexcept
		{
			return VulkanApi::displayName;
		}

		const DeviceCaps & VulkanDeviceCaps(void * impl) noexcept
		{
			return static_cast<VulkanDevice *>(impl)->caps;
		}

		const AdapterInfo & VulkanDeviceAdapterInfo(void * impl) noexcept
		{
			return static_cast<VulkanDevice *>(impl)->adapter;
		}

		ValidationMessageCounts VulkanDeviceValidationMessageCounts(void * impl) noexcept
		{
			const VulkanInstance * inst = static_cast<VulkanDevice *>(impl)->instanceWrapper;
			if (inst == nullptr)
			{
				return {};
			}

			return ValidationMessageCounts{ .errors = inst->validationErrors.load(std::memory_order_relaxed),
				.warnings							= inst->validationWarnings.load(std::memory_order_relaxed) };
		}

		bool VulkanGetTextureInfo(void * impl, const TextureHandle texture, TextureInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getTextureInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getTextureInfo output pointer is null");
			}

			const TextureSlot * const slot = device->textureSlots.Resolve(texture, false);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "getTextureInfo names a texture this device did not create");
			}

			// A back buffer never came from a TextureDesc, so there is nothing here to report that would not be invented. The swapchain owns that description and
			// answers for it.
			if (slot->lifetime == SlotLifetime::eSwapchainBorrowed)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "a swapchain back buffer has no texture description; ask the swapchain instead");
			}

			VmaAllocationInfo allocationInfo{};
			if (slot->allocation != nullptr)
			{
				vmaGetAllocationInfo(device->allocator, slot->allocation, &allocationInfo);
			}

			// Zero where this image owns no allocation, which is a placed, adopted or sparse one. Its memory is the heap's or the caller's to account for.
			*out = TextureInfo{ .desc = slot->desc, .allocationSize = allocationInfo.size };
			return true;
		}

		bool VulkanGetBufferInfo(void * impl, const BufferHandle buffer, BufferInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getBufferInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getBufferInfo output pointer is null");
			}

			const BufferSlot * const slot = device->bufferSlots.Resolve(buffer, false);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "getBufferInfo names a buffer this device did not create");
			}

			VmaAllocationInfo allocationInfo{};
			if (slot->allocation != nullptr)
			{
				vmaGetAllocationInfo(device->allocator, slot->allocation, &allocationInfo);
			}

			// Read off what the memory turned out to be, not what was asked for, since this query runs after creation.
			const MemoryAccess access =
				slot->hostVisible ? (slot->coherent ? MemoryAccess::eCpuVisibleCoherent : MemoryAccess::eCpuVisible) : MemoryAccess::eGpuOnly;

			*out = BufferInfo{ .desc = slot->desc, .allocationSize = allocationInfo.size, .memoryAccess = access };
			return true;
		}

		FormatSupport VulkanDeviceFormatSupport(void * impl, Format format) noexcept
		{
			const vk::Format vkFormat = MapFormat(format);
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
				// The same two bits VulkanCmdBlit checks before recording, so a caller can now ask the question the backend was already answering.
				.blitSrc = has(vk::FormatFeatureFlagBits::eBlitSrc),
				.blitDst = has(vk::FormatFeatureFlagBits::eBlitDst),
			};
		}

		// Names an object through VK_EXT_debug_utils so RenderDoc, the validation layer and captures show it by name. A no-op when naming is off, debug-utils is
		// absent or no name was given. Best effort so an error never fails the create that asked for it.
		void NameVulkanObject(const VulkanDevice * device, vk::ObjectType type, std::uint64_t handle, CString name) noexcept
		{
			if (!device->debugNames || !device->debugUtils || name == nullptr || *name == '\0' || handle == 0)
			{
				return;
			}

			// Dropping the result is the behaviour, not an oversight. There is nothing to report to: the caller asked to name a resource, not to be told the debug layer
			// was unavailable.
			static_cast<void>(device->device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(type, handle, name), device->dispatch));
		}

		namespace
		{
			// A buffer whose pages arrive through bindSparse. Refused where the device cannot bind them and never handed back as a buffer that can never be written to.
			[[nodiscard]] BufferHandle VulkanCreateSparseBuffer(VulkanDevice * device, const BufferDesc & desc, Error * error) noexcept
			{
				if (device->caps.sparseTier < SparseTier::eBuffers)
				{
					return FailValue<BufferHandle>(error, ErrorCode::eUnsupportedFeature, "this device cannot bind sparse memory to a buffer");
				}

				const vk::BufferCreateInfo bufferInfo(vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency,
					desc.size,
					vk::BufferUsageFlags(MapBufferUsage(desc.usage)),
					vk::SharingMode::eExclusive);

				const auto created = device->device.createBuffer(bufferInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "vkCreateBuffer failed for a sparse buffer");
				}

				VkBuffer raw = static_cast<VkBuffer>(created.value);
				NameVulkanObject(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.debugName);

				// Never host visible: mapping is a property of the memory, and this buffer has none until bindSparse gives it some.
				const BufferHandle handle =
					device->bufferSlots.Store(BufferSlot{ .buffer = raw, .size = desc.size, .sparse = true, .desc = detail::Recorded(desc) });
				if (!handle.IsValid())
				{
					device->device.destroyBuffer(created.value, nullptr, device->dispatch);
					return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan buffer handle tracking failed");
				}

				return ReturnValue(handle, error);
			}
		} // namespace

		BufferHandle VulkanCreateBuffer(void * impl, const BufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createBuffer");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!VulkanRefuseUnexportable(device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eBuffer,
					Format::eUndefined,
					MapBufferUsage(desc.usage),
					"buffer creation asked for an external handle type this adapter cannot export",
					error))
			{
				return BufferHandle{};
			}
			if (desc.size == 0)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}

			/*
			 * A sparse buffer is a virtual range with no memory behind it, filled a page at a time through bindSparse. Created straight through vkCreateBuffer and not
			 * VMA, since there is no allocation to make and vmaCreateBuffer would bind memory the buffer exists to bind later.
			 *
			 * Residency comes with binding here because a buffer that can only be fully resident is not what the RHI means by sparse.
			 */
			if (desc.allowSparseBinding)
			{
				return VulkanCreateSparseBuffer(device, desc, error);
			}

			vk::BufferCreateInfo bufferCreateInfo({}, desc.size, MapBufferUsage(desc.usage), vk::SharingMode::eExclusive);

			// The exportable pair, and a dedicated allocation with it. See VulkanCreateTexture for why sharing a block is not an option.
			vk::ExternalMemoryBufferCreateInfo externalBufferInfo;
			vk::ExportMemoryAllocateInfo exportInfo;
			const bool exportable = !desc.exportableHandleTypes.Empty();
			if (exportable)
			{
				externalBufferInfo.handleTypes = MapMemoryHandleTypes(desc.exportableHandleTypes);
				exportInfo.handleTypes		   = externalBufferInfo.handleTypes;
				bufferCreateInfo.pNext		   = &externalBufferInfo;
			}

			const VkBufferCreateInfo bufferInfo = bufferCreateInfo;
			VmaAllocationCreateFlags allocFlags = 0;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = MapMemoryUsage(desc.memory, desc.persistentMap, allocFlags);
			allocInfo.flags = allocFlags;

			VkBuffer raw			 = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			if (exportable)
			{
				if (vmaCreateDedicatedBuffer(device->allocator, &bufferInfo, &allocInfo, &exportInfo, &raw, &allocation, nullptr) != VK_SUCCESS)
				{
					return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "vmaCreateDedicatedBuffer failed for an exportable buffer");
				}
			}
			else if (vmaCreateBuffer(device->allocator, &bufferInfo, &allocInfo, &raw, &allocation, nullptr) != VK_SUCCESS)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eOutOfDeviceMemory, "vmaCreateBuffer failed");
			}

			NameVulkanObject(device, vk::ObjectType::eBuffer, std::bit_cast<std::uint64_t>(raw), desc.debugName);

			VkMemoryPropertyFlags memFlags = 0;
			vmaGetAllocationMemoryProperties(device->allocator, allocation, &memFlags);

			// VMA maps only an allocation that asked for host access. On unified memory a device-local buffer can still land in a host-visible heap so gate on the
			// requested access and not the heap property or vmaMapMemory aborts on strict ICDs.
			const bool mappable = (allocFlags & (VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)) != 0;

			const BufferHandle handle = device->bufferSlots.Store(BufferSlot{ .buffer = raw,
				.allocation															  = allocation,
				.size																  = desc.size,
				.coherent															  = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0,
				.hostVisible														  = mappable,
				.persistentMapped													  = (allocFlags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0,
				.exportableHandleTypes												  = desc.exportableHandleTypes,
				.desc																  = detail::Recorded(desc) });
			if (!handle.IsValid())
			{
				vmaDestroyBuffer(device->allocator, raw, allocation);
				return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan buffer handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		MappedMemory VulkanMap(void * impl, BufferHandle handle, const MapDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.map");
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = ResolveBuffer(device, handle);
			if (slot == nullptr)
			{
				return FailValue<MappedMemory>(error, ErrorCode::eInvalidHandle, "map of an invalid buffer handle");
			}

			if (!slot->hostVisible)
			{
				return FailValue<MappedMemory>(error, ErrorCode::eInvalidArgument, "map of a buffer whose memory is not host visible");
			}

			std::uint64_t mapSize = desc.size;
			if (!BoundBufferRange(slot->size, desc.offset, mapSize))
			{
				return FailValue<MappedMemory>(error, ErrorCode::eInvalidArgument, "map range is outside the buffer");
			}

			void * mapped = nullptr;
			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				// Placed buffer: map its slice of the heap memory (only one mapping per heap at a time).
				if (device->device.mapMemory(slot->placedMemory, slot->placedOffset, slot->size, vk::MemoryMapFlags{}, &mapped, device->dispatch) !=
					vk::Result::eSuccess)
				{
					return FailValue<MappedMemory>(error, ErrorCode::eNativeApiError, "vkMapMemory failed");
				}
			}
			else if (slot->persistentMapped)
			{
				// VMA already holds the mapping for this allocation. Hand back its pointer instead of bumping the map count, which Unmap (and destroy) would otherwise have
				// to balance.
				VmaAllocationInfo info{};
				vmaGetAllocationInfo(device->allocator, slot->allocation, &info);
				mapped = info.pMappedData;
				if (mapped == nullptr)
				{
					return FailValue<MappedMemory>(error, ErrorCode::eNativeApiError, "persistent mapping is unexpectedly null");
				}
			}
			else if (vmaMapMemory(device->allocator, slot->allocation, &mapped) != VK_SUCCESS)
			{
				return FailValue<MappedMemory>(error, ErrorCode::eNativeApiError, "vmaMapMemory failed");
			}

			return ReturnValue(
				MappedMemory{
					.data	  = static_cast<std::uint8_t *>(mapped) + desc.offset,
					.size	  = mapSize,
					.coherent = slot->coherent,
				},
				error);
		}

		bool VulkanUnmap(void * impl, BufferHandle handle, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = ResolveBuffer(device, handle);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "unmap of an invalid buffer handle");
			}

			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				device->device.unmapMemory(slot->placedMemory, device->dispatch);
			}
			else if (!slot->persistentMapped)
			{
				// Persistent mappings stay live for the buffer's lifetime. VMA releases them on destroy.
				vmaUnmapMemory(device->allocator, slot->allocation);
			}
			return Succeed(error);
		}

		bool VulkanFlushMappedRange(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = ResolveBuffer(device, handle);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "flush of an invalid buffer handle");
			}

			std::uint64_t bounded = size;
			if (!BoundBufferRange(slot->size, offset, bounded))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "flush range is outside the buffer");
			}

			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				return Succeed(error); // placed buffers live in a host-coherent heap so no flush is needed
			}

			if (vmaFlushAllocation(device->allocator, slot->allocation, offset, bounded) != VK_SUCCESS)
			{
				return Fail(error, ErrorCode::eNativeApiError, "vmaFlushAllocation failed");
			}

			return Succeed(error);
		}

		bool VulkanInvalidateMappedRange(void * impl, BufferHandle handle, std::uint64_t offset, std::uint64_t size, Error * error) noexcept
		{
			auto * device	  = static_cast<VulkanDevice *>(impl);
			BufferSlot * slot = ResolveBuffer(device, handle);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "invalidate of an invalid buffer handle");
			}
			std::uint64_t bounded = size;
			if (!BoundBufferRange(slot->size, offset, bounded))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "invalidate range is outside the buffer");
			}

			if (slot->placedMemory != VK_NULL_HANDLE)
			{
				return Succeed(error); // placed buffers live in a host-coherent heap so no invalidate is needed
			}

			if (vmaInvalidateAllocation(device->allocator, slot->allocation, offset, bounded) != VK_SUCCESS)
			{
				return Fail(error, ErrorCode::eNativeApiError, "vmaInvalidateAllocation failed");
			}
			return Succeed(error);
		}

		namespace
		{
			// A cube sets the compatible flag, its six faces being six of desc.arrayLayers. Mutable format is what lets a view name a format other than the image's own,
			// opted into per texture because it can cost lossless compression.
			[[nodiscard]] vk::ImageCreateFlags TextureCreateFlags(const TextureDesc & desc) noexcept
			{
				vk::ImageCreateFlags flags{};
				if (desc.type == TextureType::eTexCube)
				{
					flags |= vk::ImageCreateFlagBits::eCubeCompatible;
				}
				// A plane view names that plane's single-plane format, which is a different format from the image's, so a multi-planar image needs the mutable flag
				// whatever the caller asked for. Its planes would be unreachable otherwise, which is the only way to sample one.
				if (desc.allowFormatViews || IsMultiPlanarFormat(desc.format))
				{
					flags |= vk::ImageCreateFlagBits::eMutableFormat;
				}
				return flags;
			}
		} // namespace

		/*
		 * The image an ordinary and an imported texture both name, which is everything about a texture that does not concern where its memory comes from.
		 *
		 * Split out so the import path states the shape once and never a second time. A description the two paths read differently would give an importer an image
		 * laid out unlike the one the exporter made, which no error reports and which reads as corruption at the far end.
		 */
		bool VulkanImageCreateInfo(const TextureDesc & desc, vk::ImageCreateInfo & out, Error * error) noexcept
		{
			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}

			// Refused here and not left to the driver, because on MoltenVK the image becomes an MTLTexture and Metal asserts on a chain longer than the extent holds.
			if (desc.mipLevels > detail::MaxMipLevels(desc.width, desc.height, desc.type == TextureType::eTex3D ? desc.depth : 1))
			{
				return Fail(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}

			const vk::Format format = MapFormat(desc.format);
			if (format == vk::Format::eUndefined)
			{
				return Fail(error, ErrorCode::eUnsupportedFormat, "Vulkan texture: undefined or unsupported format");
			}

			out.flags		  = TextureCreateFlags(desc);
			out.imageType	  = MapImageType(desc.type);
			out.format		  = format;
			out.extent		  = vk::Extent3D{ desc.width, desc.height, desc.depth };
			out.mipLevels	  = desc.mipLevels;
			out.arrayLayers	  = desc.arrayLayers;
			out.samples		  = MapSampleCount(desc.samples);
			out.tiling		  = vk::ImageTiling::eOptimal;
			out.usage		  = MapTextureUsage(desc.usage);
			out.sharingMode	  = vk::SharingMode::eExclusive;
			out.initialLayout = vk::ImageLayout::eUndefined;
			return Succeed(error);
		}

		// Names the image, gives it its default view and registers the slot, for an image that already has memory behind it however that memory arrived. Owns the
		// failure cleanup for both, so a caller that got this far hands the image over and does not unwind it itself.
		TextureHandle VulkanFinishTexture(VulkanDevice * device, const TextureDesc & desc, VkImage image, VmaAllocation allocation, Error * error) noexcept
		{
			NameVulkanObject(device, vk::ObjectType::eImage, std::bit_cast<std::uint64_t>(image), desc.debugName);

			/*
			 * A default view is legal only on a view-capable usage so a pure transfer texture gets none: creating one violates VUID-VkImageViewCreateInfo-image-04441
			 * and nothing binds such a texture anyway since copies work off the image.
			 *
			 * A multi-planar texture gets none either. A sampled view at the image's own format would need a VkSamplerYcbcrConversion chained onto it
			 * (VUID-VkImageViewCreateInfo-format-06415), and there is no conversion to chain: these are reached a plane at a time instead.
			 */
			constexpr vk::ImageUsageFlags viewCapableUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
															 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
															 vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment;

			const vk::Format format = MapFormat(desc.format);
			VkImageView view		= VK_NULL_HANDLE;
			if ((MapTextureUsage(desc.usage) & viewCapableUsage) && !IsMultiPlanarFormat(desc.format))
			{
				const vk::ImageAspectFlags aspect = IsDepthFormat(desc.format) ? vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eDepth }
																			   : vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor };
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image			  = image;
				viewInfo.viewType		  = MapViewType(desc.type);
				viewInfo.format			  = format;
				viewInfo.subresourceRange = vk::ImageSubresourceRange{ aspect, 0, desc.mipLevels, 0, desc.arrayLayers };

				const auto created = device->device.createImageView(viewInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					vmaDestroyImage(device->allocator, image, allocation);
					return FailValue<TextureHandle>(error, ErrorCode::eNativeApiError, "Vulkan texture creation failed");
				}

				view = created.value;
			}

			const TextureHandle handle = device->textureSlots.Store(TextureSlot{ .image = image,
				.allocation																= allocation,
				.defaultView															= view,
				.format																	= format,
				.samples																= MapSampleCount(desc.samples),
				.mipLevels																= desc.mipLevels,
				.arrayLayers															= desc.arrayLayers,
				.width																	= desc.width,
				.height																	= desc.height,
				.depth																	= desc.depth,
				.rhiFormat																= desc.format,
				.usage																	= desc.usage,
				.mutableFormat															= desc.allowFormatViews,
				.sparse																	= desc.allowSparseBinding,
				.exportableHandleTypes													= desc.exportableHandleTypes,
				.desc																	= detail::Recorded(desc) });
			if (!handle.IsValid())
			{
				if (view != VK_NULL_HANDLE)
				{
					device->device.destroyImageView(view, nullptr, device->dispatch);
				}

				vmaDestroyImage(device->allocator, image, allocation);
				return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan texture handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		// A texture is a VkImage, a VMA allocation and a default view spanning every mip and layer. The view's aspect follows the format and a cube sets the
		// compatible flag, its six faces being six of desc.arrayLayers.
		TextureHandle VulkanCreateTexture(void * impl, const TextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createTexture");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!VulkanRefuseUnexportable(device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eTexture,
					desc.format,
					kExternalQueryBufferUsage,
					"texture creation asked for an external handle type this adapter cannot export for that format",
					error))
			{
				return TextureHandle{};
			}

			vk::ImageCreateInfo imageInfo{};
			if (!VulkanImageCreateInfo(desc, imageInfo, error))
			{
				return TextureHandle{};
			}

			/*
			 * A sparse texture is a virtual tile grid with no memory behind it, filled through bindSparse.
			 *
			 * Created straight through vkCreateImage and not VMA for the same reason a sparse buffer is: there is no allocation to make. Residency and not binding
			 * alone, since a texture that can only be fully resident is not what the caps ladder means by a resident texture.
			 */
			VkImage image			 = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			if (desc.allowSparseBinding)
			{
				const SparseTier needed = desc.depth > 1 ? SparseTier::eResidentVolumes : SparseTier::eResidentTextures;
				if (device->caps.sparseTier < needed)
				{
					return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFeature, "this device cannot bind sparse memory to a texture of that shape");
				}

				imageInfo.flags |= vk::ImageCreateFlagBits::eSparseBinding | vk::ImageCreateFlagBits::eSparseResidency;
			}

			/*
			 * An exportable texture takes its handle types twice, once on the image and once on the allocation, and a dedicated allocation so that the exported memory
			 * is this texture and nothing else. A suballocated one would hand an importer the whole block with no offset to find the texture at, and every neighbour's
			 * memory with it.
			 */
			vk::ExternalMemoryImageCreateInfo externalImageInfo;
			vk::ExportMemoryAllocateInfo exportInfo;
			const bool exportable = !desc.exportableHandleTypes.Empty();
			if (exportable)
			{
				externalImageInfo.handleTypes = MapMemoryHandleTypes(desc.exportableHandleTypes);
				exportInfo.handleTypes		  = externalImageInfo.handleTypes;
				imageInfo.pNext				  = &externalImageInfo;
			}

			const VkImageCreateInfo cImageInfo	= imageInfo;
			VmaAllocationCreateFlags allocFlags = 0;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = MapMemoryUsage(desc.memory, false, allocFlags);
			allocInfo.flags = allocFlags;

			if (desc.allowSparseBinding)
			{
				const auto created = device->device.createImage(imageInfo, nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "vkCreateImage failed for a sparse texture");
				}

				image = static_cast<VkImage>(created.value);
			}
			else if (exportable)
			{
				if (vmaCreateDedicatedImage(device->allocator, &cImageInfo, &allocInfo, &exportInfo, &image, &allocation, nullptr) != VK_SUCCESS)
				{
					return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "vmaCreateDedicatedImage failed for an exportable texture");
				}
			}
			else if (vmaCreateImage(device->allocator, &cImageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eOutOfDeviceMemory, "vmaCreateImage failed");
			}

			return VulkanFinishTexture(device, desc, image, allocation, error);
		}

		// Picks a memory type for a heap from its abstract type: host-visible+coherent for the CPU heaps, device-local otherwise. Returns false when no type
		// qualifies.
		[[nodiscard]] bool FindMemoryTypeForHeap(vk::PhysicalDevice phys, const vk::detail::DispatchLoaderDynamic & dispatch, HeapType type,
			std::uint32_t & outIndex, bool & outHostVisible, bool & outCoherent) noexcept
		{
			const vk::PhysicalDeviceMemoryProperties props = phys.getMemoryProperties(dispatch);
			const bool wantHost							   = type == HeapType::eCpuUpload || type == HeapType::eCpuReadback;
			const vk::MemoryPropertyFlags required		   = wantHost ? (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
																	  : vk::MemoryPropertyFlags{ vk::MemoryPropertyFlagBits::eDeviceLocal };
			for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

		// Resolves a heap handle to its slot. Writers are serialized by the guard the RHI takes for the kind so this needs nothing.
		[[nodiscard]] HeapSlot * ResolveHeap(VulkanDevice * device, HeapHandle handle) noexcept
		{
			return device->heapSlots.Resolve(handle, kHandleAlreadyChecked);
		}

		HeapHandle VulkanCreateHeap(void * impl, const HeapDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createHeap");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!VulkanRefuseUnexportable(device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eHeap,
					Format::eUndefined,
					// A heap accepts whatever a caller later places in it, so the question is whether memory crosses at all, not what will use it.
					kExternalQueryBufferUsage,
					"heap creation asked for an external handle type this adapter cannot export",
					error))
			{
				return HeapHandle{};
			}
			if (desc.size == 0)
			{
				return FailValue<HeapHandle>(error, ErrorCode::eInvalidArgument, "heap size must be greater than zero");
			}

			std::uint32_t typeIndex = 0;
			bool hostVisible		= false;
			bool coherent			= false;
			if (!FindMemoryTypeForHeap(device->phys, device->dispatch, desc.type, typeIndex, hostVisible, coherent))
			{
				return FailValue<HeapHandle>(error, ErrorCode::eUnsupportedFeature, "no memory type matches the requested heap");
			}

			// A heap is the one memory-backed kind this backend allocates itself, so the export declaration chains straight in and never reaches VMA.
			vk::MemoryAllocateInfo allocateInfo(desc.size, typeIndex);
			vk::ExportMemoryAllocateInfo exportInfo;
			if (!desc.exportableHandleTypes.Empty())
			{
				exportInfo.handleTypes = MapMemoryHandleTypes(desc.exportableHandleTypes);
				allocateInfo.pNext	   = &exportInfo;
			}

			const auto allocated = device->device.allocateMemory(allocateInfo, nullptr, device->dispatch);
			if (allocated.result != vk::Result::eSuccess)
			{
				return FailValue<HeapHandle>(error, ErrorCode::eOutOfDeviceMemory, "Vulkan heap allocation failed");
			}

			const HeapHandle handle = device->heapSlots.Store(HeapSlot{ .memory = allocated.value,
				.size															= desc.size,
				.memoryTypeIndex												= typeIndex,
				.hostVisible													= hostVisible,
				.coherent														= coherent,
				.exportableHandleTypes											= desc.exportableHandleTypes });
			if (!handle.IsValid())
			{
				device->device.freeMemory(allocated.value, nullptr, device->dispatch);
				return FailValue<HeapHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan heap handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		BufferHandle VulkanCreatePlacedBuffer(void * impl, const PlacedBufferDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPlacedBuffer");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (desc.buffer.size == 0)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eInvalidArgument, "placed buffer size must be greater than zero");
			}

			HeapSlot heap{};
			{
				const HeapSlot * slot = ResolveHeap(device, desc.heap);
				if (slot == nullptr)
				{
					return FailValue<BufferHandle>(error, ErrorCode::eInvalidHandle, "placed buffer with an invalid heap handle");
				}
				heap = *slot;
			}

			const auto created = device->device.createBuffer(
				vk::BufferCreateInfo({}, desc.buffer.size, MapBufferUsage(desc.buffer.usage), vk::SharingMode::eExclusive), nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<BufferHandle>(error, ErrorCode::eNativeApiError, "Vulkan placed buffer creation failed");
			}

			const vk::Buffer buffer			  = created.value;
			const vk::MemoryRequirements reqs = device->device.getBufferMemoryRequirements(buffer, device->dispatch);
			// Checked in every mode, since binding to memory the buffer does not fit is undefined and not merely wrong. The range is subtracted and not added,
			// an offset a suballocator produced by underflow being free to wrap the sum back inside the heap.
			if (((reqs.memoryTypeBits & (1u << heap.memoryTypeIndex)) == 0) || (desc.offset % reqs.alignment) != 0 || desc.offset > heap.size ||
				reqs.size > heap.size - desc.offset)
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return FailValue<BufferHandle>(error, ErrorCode::eValidationFailed, "placed buffer does not fit the heap (memory type, alignment, or range)");
			}

			if (device->device.bindBufferMemory(buffer, heap.memory, desc.offset, device->dispatch) != vk::Result::eSuccess)
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return FailValue<BufferHandle>(error, ErrorCode::eNativeApiError, "Vulkan placed buffer memory binding failed");
			}

			const BufferHandle handle = device->bufferSlots.Store(BufferSlot{ .buffer = buffer,
				.allocation															  = nullptr,
				.size																  = desc.buffer.size,
				.coherent															  = heap.coherent,
				.hostVisible														  = heap.hostVisible,
				.placedMemory														  = heap.memory,
				.placedOffset														  = desc.offset,
				.desc																  = detail::Recorded(desc.buffer) });
			if (!handle.IsValid())
			{
				device->device.destroyBuffer(buffer, nullptr, device->dispatch);
				return FailValue<BufferHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan placed buffer handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		TextureHandle VulkanCreatePlacedTexture(void * impl, const PlacedTextureDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPlacedTexture");
			auto * device		  = static_cast<VulkanDevice *>(impl);
			const TextureDesc & t = desc.texture;
			if (t.width == 0 || t.height == 0 || t.depth == 0)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}
			if (t.mipLevels > detail::MaxMipLevels(t.width, t.height, t.type == TextureType::eTex3D ? t.depth : 1))
			{
				return FailValue<TextureHandle>(error, ErrorCode::eInvalidArgument, "texture asks for more mip levels than its extent can hold");
			}
			const vk::Format format = MapFormat(t.format);
			if (format == vk::Format::eUndefined)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eUnsupportedFormat, "placed texture: undefined or unsupported format");
			}

			HeapSlot heap{};
			{
				const HeapSlot * slot = ResolveHeap(device, desc.heap);
				if (slot == nullptr)
				{
					return FailValue<TextureHandle>(error, ErrorCode::eInvalidHandle, "placed texture with an invalid heap handle");
				}
				heap = *slot;
			}

			const std::uint32_t layers = t.arrayLayers;
			vk::ImageCreateInfo imageInfo{};
			imageInfo.flags			= TextureCreateFlags(t);
			imageInfo.imageType		= MapImageType(t.type);
			imageInfo.format		= format;
			imageInfo.extent		= vk::Extent3D{ t.width, t.height, t.depth };
			imageInfo.mipLevels		= t.mipLevels;
			imageInfo.arrayLayers	= layers;
			imageInfo.samples		= MapSampleCount(t.samples);
			imageInfo.tiling		= vk::ImageTiling::eOptimal;
			imageInfo.usage			= MapTextureUsage(t.usage);
			imageInfo.sharingMode	= vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;

			const auto createdImage = device->device.createImage(imageInfo, nullptr, device->dispatch);
			if (createdImage.result != vk::Result::eSuccess)
			{
				return FailValue<TextureHandle>(error, ErrorCode::eNativeApiError, "Vulkan placed texture creation failed");
			}

			const vk::Image image			  = createdImage.value;
			const vk::MemoryRequirements reqs = device->device.getImageMemoryRequirements(image, device->dispatch);
			// Same in every mode and for the same reason as the placed buffer above. The range is subtracted and not added, an offset a suballocator produced by
			// underflow being free to wrap the sum back inside the heap.
			if (((reqs.memoryTypeBits & (1u << heap.memoryTypeIndex)) == 0) || (desc.offset % reqs.alignment) != 0 || desc.offset > heap.size ||
				reqs.size > heap.size - desc.offset)
			{
				device->device.destroyImage(image, nullptr, device->dispatch);
				return FailValue<TextureHandle>(error, ErrorCode::eValidationFailed, "placed texture does not fit the heap (memory type, alignment, or range)");
			}

			if (device->device.bindImageMemory(image, heap.memory, desc.offset, device->dispatch) != vk::Result::eSuccess)
			{
				device->device.destroyImage(image, nullptr, device->dispatch);
				return FailValue<TextureHandle>(error, ErrorCode::eNativeApiError, "Vulkan placed texture memory binding failed");
			}

			const vk::ImageAspectFlags aspect =
				IsDepthFormat(t.format) ? vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eDepth } : vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor };
			// No default view over a multi-planar format, for the reason the committed path states: one at the image's own format would need a VkSamplerYcbcrConversion
			// chained onto it, and these are reached a plane at a time instead.
			VkImageView placedView = VK_NULL_HANDLE;
			if (!IsMultiPlanarFormat(t.format))
			{
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image			  = image;
				viewInfo.viewType		  = MapViewType(t.type);
				viewInfo.format			  = format;
				viewInfo.subresourceRange = vk::ImageSubresourceRange{ aspect, 0, t.mipLevels, 0, layers };

				const auto createdView = device->device.createImageView(viewInfo, nullptr, device->dispatch);
				if (createdView.result != vk::Result::eSuccess)
				{
					device->device.destroyImage(image, nullptr, device->dispatch);
					return FailValue<TextureHandle>(error, ErrorCode::eNativeApiError, "Vulkan placed texture view creation failed");
				}
				placedView = createdView.value;
			}

			const TextureHandle handle = device->textureSlots.Store(TextureSlot{ .image = image,
				.allocation																= nullptr,
				.defaultView															= placedView,
				.format																	= format,
				.samples																= MapSampleCount(t.samples),
				.mipLevels																= t.mipLevels,
				.arrayLayers															= layers,
				.width																	= t.width,
				.height																	= t.height,
				.depth																	= t.depth,
				.rhiFormat																= t.format,
				.usage																	= t.usage,
				.mutableFormat															= t.allowFormatViews,
				.desc																	= detail::Recorded(t) });
			if (!handle.IsValid())
			{
				if (placedView != VK_NULL_HANDLE)
				{
					device->device.destroyImageView(placedView, nullptr, device->dispatch);
				}
				device->device.destroyImage(image, nullptr, device->dispatch);
				return FailValue<TextureHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan placed texture handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		// Queries a throwaway unbound VkImage built from the same create info createPlacedTexture uses so size and alignment match the placement exactly. The
		// desc-only maintenance4 query is unavailable, since the device does not enable that feature.
		bool VulkanGetTextureMemoryInfo(void * impl, const TextureDesc & desc, MemoryInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getTextureMemoryInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getTextureMemoryInfo output pointer is null");
			}
			if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "texture extent must be non-zero in every dimension");
			}
			const vk::Format format = MapFormat(desc.format);
			if (format == vk::Format::eUndefined)
			{
				return Fail(error, ErrorCode::eUnsupportedFormat, "texture memory query: undefined or unsupported format");
			}

			const std::uint32_t layers = desc.arrayLayers;
			vk::ImageCreateInfo imageInfo{};
			// The same mapping creation uses. Built by hand here once, which left the query answering for an image without the mutable-format flag that a
			// view-permitting or multi-planar texture is actually made with, so the size and alignment it reported were not that texture's.
			imageInfo.flags			= TextureCreateFlags(desc);
			imageInfo.imageType		= MapImageType(desc.type);
			imageInfo.format		= format;
			imageInfo.extent		= vk::Extent3D{ desc.width, desc.height, desc.depth };
			imageInfo.mipLevels		= desc.mipLevels;
			imageInfo.arrayLayers	= layers;
			imageInfo.samples		= MapSampleCount(desc.samples);
			imageInfo.tiling		= vk::ImageTiling::eOptimal;
			imageInfo.usage			= MapTextureUsage(desc.usage);
			imageInfo.sharingMode	= vk::SharingMode::eExclusive;
			imageInfo.initialLayout = vk::ImageLayout::eUndefined;

			const auto created = device->device.createImage(imageInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Vulkan texture memory query failed");
			}

			const vk::MemoryRequirements reqs = device->device.getImageMemoryRequirements(created.value, device->dispatch);
			device->device.destroyImage(created.value, nullptr, device->dispatch);
			out->size	   = reqs.size;
			out->alignment = reqs.alignment;
			return Succeed(error);
		}

		// Memory footprint of a buffer described by a desc. Mirrors how createPlacedBuffer sizes one, on a throwaway unbound VkBuffer so the size and alignment match
		// the placement exactly.
		bool VulkanGetBufferMemoryInfo(void * impl, const BufferDesc & desc, MemoryInfo * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getBufferMemoryInfo");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "getBufferMemoryInfo output pointer is null");
			}
			if (desc.size == 0)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "buffer size must be greater than zero");
			}

			const auto created = device->device.createBuffer(
				vk::BufferCreateInfo({}, desc.size, MapBufferUsage(desc.usage), vk::SharingMode::eExclusive), nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Vulkan buffer memory query failed");
			}

			const vk::MemoryRequirements reqs = device->device.getBufferMemoryRequirements(created.value, device->dispatch);
			device->device.destroyBuffer(created.value, nullptr, device->dispatch);
			out->size	   = reqs.size;
			out->alignment = reqs.alignment;
			return Succeed(error);
		}

		// Creates a VkImageView onto a device-created texture, defaulting to the texture's own format and tracks it in the registry.
		TextureViewHandle VulkanCreateTextureView(void * impl, TextureHandle texture, const TextureViewDesc & desc, Error * error) noexcept
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
				const TextureSlot * const resolved = device->textureSlots.Resolve(texture, kHandleAlreadyChecked);
				if (resolved == nullptr)
				{
					return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidHandle, "texture view of an invalid or stale texture handle");
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

			// Component mapping is only legal where the view is sampled. Vulkan enforces it at the bind and not here, so a swizzle that reaches a storage image or an
			// attachment would come back as a validation error at descriptor write or framebuffer creation instead.
			if (!desc.swizzle.IsIdentity())
			{
				if (!device->caps.supportsTextureViewSwizzle)
				{
					return FailValue<TextureViewHandle>(error, ErrorCode::eUnsupportedFeature, "texture view swizzle is not supported by this Vulkan adapter");
				}
				if (UsageForbidsSwizzle(ResolveViewUsage(desc.usage, texUsage)))
				{
					return FailValue<TextureViewHandle>(
						error, ErrorCode::eInvalidArgument, "a swizzled texture view must be sampled only, so narrow TextureViewDesc::usage to eSampled");
				}
			}

			// A range past the end of the texture builds a view Vulkan will not have so this is asked whatever the mode.
			const TextureSubresourceRange & r = desc.range;
			if (r.baseMip >= texMips || r.mipCount > texMips - r.baseMip || r.baseLayer >= texLayers || r.layerCount > texLayers - r.baseLayer)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eInvalidArgument, "texture view subresource range is outside the source texture");
			}

			/*
			 * A view naming a plane exposes that plane's single-plane format and not the multi-planar one, which is how Vulkan reaches the texels and is legal without
			 * allowFormatViews: the aspect, not a format cast, is what selects the plane. Everything else resolves its format the ordinary way and stays under the
			 * reinterpretation rule.
			 */
			const std::uint32_t plane = PlaneIndexOf(desc.range.aspects);

			/*
			 * Sampling a multi-planar format as a whole is defined only through a conversion, so a view that names no plane needs one
			 * (VUID-VkImageViewCreateInfo-format-06415). A plane view reads raw texels and must not carry one.
			 */
			vk::SamplerYcbcrConversionInfo conversionInfo{};
			if (desc.ycbcrConversion != nullptr)
			{
				if (!device->caps.supportsSamplerYcbcrConversion)
				{
					return FailValue<TextureViewHandle>(
						error, ErrorCode::eUnsupportedFeature, "texture view Y'CbCr conversion is not supported by this Vulkan adapter");
				}
				if (plane != kNoPlane)
				{
					return FailValue<TextureViewHandle>(
						error, ErrorCode::eInvalidArgument, "a texture view naming a plane reads it raw and cannot also carry a Y'CbCr conversion");
				}
				conversionInfo.conversion = AcquireYcbcrConversion(device, *desc.ycbcrConversion);
				if (!conversionInfo.conversion)
				{
					return FailValue<TextureViewHandle>(error, ErrorCode::eNativeApiError, "Vulkan texture view Y'CbCr conversion creation failed");
				}
			}
			else if (plane == kNoPlane && IsMultiPlanarFormat(texRhiFormat))
			{
				return FailValue<TextureViewHandle>(
					error, ErrorCode::eInvalidArgument, "a texture view of a multi-planar format must either name a plane aspect or carry a Y'CbCr conversion");
			}

			vk::Format viewFormat = vk::Format::eUndefined;
			if (plane != kNoPlane)
			{
				const Format planeFormat = PlaneFormatOf(texRhiFormat, plane);
				if (planeFormat == Format::eUndefined)
				{
					return FailValue<TextureViewHandle>(
						error, ErrorCode::eInvalidArgument, "texture view names a plane the source texture's format does not have");
				}
				viewFormat = desc.format == Format::eUndefined ? MapFormat(planeFormat) : MapFormat(desc.format);
			}
			else
			{
				viewFormat = desc.format == Format::eUndefined ? textureFormat : MapFormat(desc.format);

				// Reinterpreting texels needs an image created for it (VUID-VkImageViewCreateInfo-image-01762), which the texture declares through allowFormatViews. Asked
				// whatever the mode, since otherwise this builds a view Vulkan rejects.
				if (viewFormat != textureFormat && !texMutableFormat)
				{
					return FailValue<TextureViewHandle>(
						error, ErrorCode::eInvalidArgument, "texture view names a format the source texture was not created with allowFormatViews for");
				}
			}

			// range.aspects defaults to eColor so an unspecified aspect and an explicit color one arrive identical. Color over a depth format is illegal either way
			// (VUID-VkImageViewCreateInfo-subresourceRange-09594) so take it from the format in that case.
			vk::ImageSubresourceRange subresource	= MapSubresourceRange(desc.range);
			const vk::ImageAspectFlags formatAspect = AspectForViewFormat(viewFormat);
			if (subresource.aspectMask == vk::ImageAspectFlags{ vk::ImageAspectFlagBits::eColor } && formatAspect != vk::ImageAspectFlagBits::eColor)
			{
				subresource.aspectMask = formatAspect;
			}

			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image			  = image;
			viewInfo.viewType		  = MapImageViewType(desc.type);
			viewInfo.format			  = viewFormat;
			viewInfo.components		  = MapComponentMapping(desc.swizzle);
			viewInfo.subresourceRange = subresource;
			if (desc.ycbcrConversion != nullptr)
			{
				viewInfo.pNext = &conversionInfo;
			}

			const auto created = device->device.createImageView(viewInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<TextureViewHandle>(error, ErrorCode::eNativeApiError, "Vulkan texture view creation failed");
			}

			const TextureViewHandle handle = device->textureViewSlots.Store(TextureViewSlot{
				.view	 = created.value,
				.format	 = viewFormat,
				.samples = texSamples,
			});
			if (!handle.IsValid())
			{
				device->device.destroyImageView(created.value, nullptr, device->dispatch);
				return FailValue<TextureViewHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan texture view handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		// Maps push constant ranges onto Vulkan and tracks the VkPipelineLayout.
		namespace
		{
			/*
			 * Refuses a pipeline whose shaders claim their bindings landed somewhere other than where this layout binds them.
			 *
			 * Vulkan's half of the ABI is the identity, a binding staying at the set and binding it was declared with, so a map that disagrees is a shader built against
			 * something other than this layout. Nothing here changes what is bound. It changes a silently wrong descriptor read into a refusal that names what
			 * disagreed.
			 */
			[[nodiscard]] bool BindingMapsAgree(
				VulkanDevice * device, const PipelineLayoutHandle layoutHandle, const std::span<const ShaderBinary> shaders, Error * error) noexcept
			{
				if (std::ranges::none_of(shaders,
						[](const ShaderBinary & shader) noexcept
						{
							return shader.bindingMap != nullptr;
						}))
				{
					return true;
				}

				const PipelineLayoutSlot * const layout = device->pipelineLayoutSlots.Resolve(layoutHandle, kHandleAlreadyChecked);
				if (layout == nullptr)
				{
					return Fail(error, ErrorCode::eInvalidHandle, "pipeline references an invalid pipeline layout");
				}

				detail::HostVector<DescriptorSetLayoutDesc> abiSets;
				if (!detail::TryReserve(abiSets, layout->sets.size()))
				{
					return Fail(error, ErrorCode::eOutOfHostMemory, "Vulkan shader binding map check storage allocation failed");
				}

				for (const DescriptorSetLayoutHandle setHandle : layout->sets)
				{
					const DescriptorSetLayoutSlot * const setLayout = device->descriptorSetLayoutSlots.Resolve(setHandle, kHandleAlreadyChecked);
					if (setLayout == nullptr)
					{
						return Fail(error, ErrorCode::eInvalidHandle, "a descriptor set layout this pipeline layout was built from has been destroyed");
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

					const ShaderBindingDisagreement bad = CheckShaderBindingMap(VulkanApi::id, device->caps.bindingTier, abiLayout, *shader.bindingMap);
					if (!bad.found)
					{
						continue;
					}

					if (bad.wrongAbiVersion)
					{
						return Fail(error,
							ErrorCode::eUnsupportedFormat,
							"a shader binary was built against a revision of the binding ABI this build does not implement");
					}

					if (bad.unknownToLayout)
					{
						return Fail(error, ErrorCode::eInvalidArgument, "a shader binary claims a binding the pipeline layout does not declare");
					}

					return Fail(
						error, ErrorCode::eInvalidArgument, "a shader binary put a binding at a different set and binding than this pipeline layout uses");
				}

				return true;
			}
		} // namespace

		PipelineLayoutHandle VulkanCreatePipelineLayout(void * impl, const PipelineLayoutDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPipelineLayout");
			auto * device = static_cast<VulkanDevice *>(impl);
			detail::HostVector<vk::DescriptorSetLayout> setLayouts;
			detail::HostVector<vk::PushConstantRange> ranges;
			if (!detail::TryReserve(setLayouts, desc.sets.size()) || !detail::TryReserve(ranges, desc.pushConstants.size()))
			{
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline layout storage allocation failed");
			}

			// Both were reserved for exactly what the loops below put in them, so neither append can grow.
			for (const DescriptorSetLayoutHandle & handle : desc.sets)
			{
				const vk::DescriptorSetLayout setLayout = ResolveDescriptorSetLayout(device, handle);
				if (!setLayout)
				{
					return FailValue<PipelineLayoutHandle>(error, ErrorCode::eInvalidHandle, "pipeline layout with an invalid descriptor set layout handle");
				}
				setLayouts.push_back(setLayout);
			}

			for (const PushConstantRange & pc : desc.pushConstants)
			{
				ranges.emplace_back(MapShaderStages(pc.stages), pc.offset, pc.size);
			}

			const auto created = device->device.createPipelineLayout(vk::PipelineLayoutCreateInfo({}, setLayouts, ranges), nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eNativeApiError, "Vulkan pipeline layout creation failed");
			}

			PipelineLayoutSlot slot{ .layout = created.value };
			slot.sets.assign(desc.sets.begin(), desc.sets.end());

			const PipelineLayoutHandle handle = device->pipelineLayoutSlots.Store(std::move(slot));
			if (!handle.IsValid())
			{
				device->device.destroyPipelineLayout(created.value, nullptr, device->dispatch);
				return FailValue<PipelineLayoutHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline layout handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		/*
		 * Returns a VkRenderPass matching the key, built and cached on first use, for the no-dynamic-rendering case only. One graphics subpass references the color
		 * attachments in order with depth last. Every layout field equals the attachment's recorded layout so the pass performs no implicit transitions and the RHI's
		 * explicit barriers keep ownership. A null pass is what a failure looks like, which the caller turns into an Error.
		 */
		[[nodiscard]] vk::RenderPass GetOrCreateRenderPass(
			VulkanDevice * device, detail::HostMap<RenderPassKey, vk::RenderPass, RenderPassKeyHash> & cache, const RenderPassKey & key)
		{
			if (const auto it = cache.find(key); it != cache.end())
			{
				return it->second;
			}

			detail::HostVector<vk::AttachmentDescription> attachments;
			detail::HostVector<vk::AttachmentReference> colorRefs;
			if (!detail::TryReserve(attachments, static_cast<std::size_t>(key.colorCount) + (key.hasDepth ? 1U : 0U)) ||
				!detail::TryReserve(colorRefs, key.colorCount))
			{
				return {};
			}

			// Both were reserved for exactly what this loop and the depth attachment below put in them, so neither append can grow.
			for (std::uint32_t i = 0; i < key.colorCount; ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const RenderPassAttachmentKey & a = key.colors[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				attachments.emplace_back(vk::AttachmentDescriptionFlags{},
					a.format,
					a.samples,
					a.loadOp,
					a.storeOp,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eDontCare,
					a.layout,
					a.layout);
				colorRefs.emplace_back(i, a.layout);
			}

			vk::AttachmentReference depthRef;
			if (key.hasDepth)
			{
				const bool stencil = key.depth.format == vk::Format::eD24UnormS8Uint || key.depth.format == vk::Format::eD32SfloatS8Uint;
				attachments.emplace_back(vk::AttachmentDescriptionFlags{},
					key.depth.format,
					key.depth.samples,
					key.depth.loadOp,
					key.depth.storeOp,
					stencil ? key.depth.loadOp : vk::AttachmentLoadOp::eDontCare,
					stencil ? key.depth.storeOp : vk::AttachmentStoreOp::eDontCare,
					key.depth.layout,
					key.depth.layout);
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
				return {};
			}

			// The cache is what owns these until teardown walks it, so a pass it cannot take is one nothing would ever destroy. Give it back and report the failure
			// instead of leaking it for the life of the device.
			if (!detail::TryInsertOrAssign(cache, key, created.value))
			{
				device->device.destroyRenderPass(created.value, nullptr, device->dispatch);
				return {};
			}

			return created.value;
		}

		// The render-pass key a pipeline is created against when dynamic rendering is off. Only formats and sample counts decide compatibility so ops and layouts
		// normalize to fixed values and the result matches whatever pass BeginRendering builds.
		[[nodiscard]] RenderPassKey MakePipelineRenderPassKey(const GraphicsPipelineDesc & desc) noexcept
		{
			RenderPassKey key;

			// Creation refuses a count past the array before reaching here, so this only keeps the function true on its own terms and not on its caller's.
			const std::uint32_t colorCount = std::min(desc.renderTarget.colorFormatCount, static_cast<std::uint32_t>(key.colors.size()));

			key.colorCount						  = colorCount;
			const vk::SampleCountFlagBits samples = MapSampleCount(desc.renderTarget.samples);
			for (std::uint32_t i = 0; i < colorCount; ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				key.colors[i] = RenderPassAttachmentKey{ .format = MapFormat(desc.renderTarget.colorFormats[i]),
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					.samples = samples,
					.loadOp	 = vk::AttachmentLoadOp::eLoad,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.layout	 = vk::ImageLayout::eColorAttachmentOptimal };
			}

			if (desc.renderTarget.depthStencilFormat != Format::eUndefined)
			{
				key.hasDepth = true;
				key.depth	 = RenderPassAttachmentKey{ .format = MapFormat(desc.renderTarget.depthStencilFormat),
					.samples								 = samples,
					.loadOp									 = vk::AttachmentLoadOp::eLoad,
					.storeOp								 = vk::AttachmentStoreOp::eStore,
					.layout									 = vk::ImageLayout::eDepthStencilAttachmentOptimal };
			}
			return key;
		}

		// Resolves an optional pipeline-cache handle to its VkPipelineCache so graphics and compute pipeline creation can reuse a warm cache. A null or unknown
		// handle falls back to VK_NULL_HANDLE (creation without a cache).
		[[nodiscard]] vk::PipelineCache ResolvePipelineCache(VulkanDevice * device, PipelineCacheHandle handle) noexcept
		{
			if (!handle.IsValid())
			{
				return vk::PipelineCache{};
			}
			const PipelineCacheSlot * slot = device->pipelineCacheSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->cache : vk::PipelineCache{};
		}

		// Maps GraphicsPipelineDesc onto a Vulkan pipeline. With dynamic rendering the attachment formats come from the desc through VkPipelineRenderingCreateInfo,
		// without it from a compatible cached render pass. Shader modules are destroyed right after the create.
		namespace
		{
			/*
			 * What this backend can consume, checked and not assumed. vkCreateShaderModule takes a uint32 pointer and a byte count, so a blob in another container is
			 * read as SPIR-V words and handed to the driver, which is undefined behaviour and not a refusal. The word-multiple check is the same rule from the other
			 * side: SPIR-V is a stream of 32-bit words and a size that is not a multiple of four cannot be one.
			 */
			[[nodiscard]] bool ShaderBytesUsable(const ShaderBinary & shader, Error * error) noexcept
			{
				if (shader.format != ShaderBinaryFormat::eSpirV)
				{
					return Fail(error, ErrorCode::eUnsupportedFormat, "the Vulkan backend takes SPIR-V shader binaries");
				}

				// Nothing here compiles a shader, which is what supportsShaderSource says. Reading source as SPIR-V would hand the driver text.
				if (shader.isSource)
				{
					return Fail(error, ErrorCode::eUnsupportedFormat, "the Vulkan backend has no shader compiler, so it takes compiled SPIR-V only");
				}

				if (shader.data == nullptr || shader.size == 0)
				{
					return Fail(error, ErrorCode::eInvalidArgument, "shader binary has no bytes");
				}

				if (shader.size % sizeof(std::uint32_t) != 0)
				{
					return Fail(error, ErrorCode::eUnsupportedFormat, "a SPIR-V binary is a whole number of 32-bit words");
				}

				return true;
			}
		} // namespace

		GraphicsPipelineHandle VulkanCreateGraphicsPipeline(void * impl, const GraphicsPipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createGraphicsPipeline");
			auto * device = static_cast<VulkanDevice *>(impl);
			/*
			 * A null vertexInput means primitives come from somewhere other than vertex buffers, which is what a mesh pipeline is. No backend here builds one, so it is
			 * refused by name and never lowered as an empty vertex layout that would draw nothing and report success.
			 */
			if (desc.vertexInput == nullptr)
			{
				return FailValue<GraphicsPipelineHandle>(error,
					ErrorCode::eUnsupportedFeature,
					"graphics pipeline without vertex input needs a mesh or task stage, which this backend does not have");
			}

			const VertexInputDesc & vertexInput = *desc.vertexInput;
			/*
			 * Both of these change what the rasterizer actually covers, so a backend that cannot do them refuses and never lowers the pipeline without them. Dropping
			 * either one silently produces a pipeline that creates, draws, and covers the wrong pixels.
			 */
			if (desc.raster.conservativeRasterEnable && device->caps.conservativeRasterTier == ConservativeRasterTier::eNone)
			{
				return FailValue<GraphicsPipelineHandle>(
					error, ErrorCode::eUnsupportedFeature, "conservative rasterization was requested on a device that reports none");
			}

			if (vertexInput.topology == PrimitiveTopology::ePatchList && vertexInput.patchControlPoints == 0)
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "a patch list needs a non-zero patchControlPoints");
			}

			/*
			 * Both counts index arrays of a fixed size, so a desc naming more than those hold is refused here and never read past their end.
			 *
			 * The rendering scope already refuses the same overrun on its own attachment list. Nothing refused it on this path, which left the count reading off the end
			 * of the array and the bounds-checked accessor as the only thing between a malformed desc and a bad read.
			 */
			if (desc.renderTarget.colorFormatCount > desc.renderTarget.colorFormats.size() || desc.blend.attachmentCount > desc.blend.attachments.size())
			{
				return FailValue<GraphicsPipelineHandle>(
					error, ErrorCode::eInvalidArgument, "graphics pipeline names more color attachments than a render target can hold");
			}

			// A stageless pipeline could never be bound so handing one back would defer the failure to a draw that cannot report it.
			if (desc.shaders.empty())
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidArgument, "graphics pipeline requires at least one shader stage");
			}

			detail::HostVector<vk::ShaderModule> modules;

			// The pipeline create consumes the modules and nothing needs them after it, so one guard covers every path out of here, the successful one included.
			auto moduleGuard = detail::MakeScopeGuard(
				[&]
				{
					for (const vk::ShaderModule module : modules)
					{
						if (module)
						{
							device->device.destroyShaderModule(module, nullptr, device->dispatch);
						}
					}
				});

			const vk::PipelineLayout layout = ResolvePipelineLayout(device, desc.layout);
			if (!layout)
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eInvalidHandle, "graphics pipeline references an invalid pipeline layout");
			}

			if (!BindingMapsAgree(device, desc.layout, desc.shaders, error))
			{
				return {};
			}

			detail::HostVector<vk::PipelineShaderStageCreateInfo> stages;
			detail::HostVector<vk::VertexInputBindingDescription> bindings;
			detail::HostVector<vk::VertexInputAttributeDescription> attrs;
			if (!detail::TryReserve(stages, desc.shaders.size()) || !detail::TryReserve(modules, desc.shaders.size()) ||
				!detail::TryReserve(bindings, vertexInput.bindings.size()) || !detail::TryReserve(attrs, vertexInput.attributes.size()))
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
			}

			// Each was reserved for exactly what the loops below put in it, so none of the appends can grow.
			for (const ShaderBinary & shader : desc.shaders)
			{
				if (!ShaderBytesUsable(shader, error))
				{
					return {};
				}

				const auto created = device->device.createShaderModule(
					vk::ShaderModuleCreateInfo({}, shader.size, static_cast<const std::uint32_t *>(shader.data)), nullptr, device->dispatch);
				if (created.result != vk::Result::eSuccess)
				{
					return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan shader module creation failed");
				}

				modules.push_back(created.value);
				stages.push_back(vk::PipelineShaderStageCreateInfo({}, MapShaderStageBit(shader.stage), created.value, shader.entryPoint));
			}

			for (const VertexBindingDesc & b : vertexInput.bindings)
			{
				bindings.emplace_back(b.binding, b.stride, b.perInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex);
			}

			for (const VertexAttributeDesc & a : vertexInput.attributes)
			{
				attrs.emplace_back(a.location, a.binding, MapFormat(a.format), a.offset);
			}

			const vk::PipelineVertexInputStateCreateInfo vertexInputState({}, bindings, attrs);

			const vk::PipelineInputAssemblyStateCreateInfo inputAssembly(
				{}, MapTopology(vertexInput.topology), vertexInput.primitiveRestartEnable ? VK_TRUE : VK_FALSE);

			// Read only for a patch list, and the create info is only chained in for one, since Vulkan ignores it otherwise and a stale count in the struct is easier to
			// misread than an absent struct.
			const vk::PipelineTessellationStateCreateInfo tessellation({}, vertexInput.patchControlPoints);
			const bool patches = vertexInput.topology == PrimitiveTopology::ePatchList;
			constexpr vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);

			vk::PipelineRasterizationStateCreateInfo raster;
			raster.depthClampEnable		   = desc.raster.depthClampEnable ? VK_TRUE : VK_FALSE;
			raster.rasterizerDiscardEnable = desc.raster.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
			raster.polygonMode			   = MapFillMode(desc.raster.fillMode);
			raster.cullMode				   = MapCullMode(desc.raster.cullMode);
			raster.frontFace			   = MapFrontFace(desc.raster.frontFace);
			raster.depthBiasEnable		   = desc.raster.depthBiasEnable ? VK_TRUE : VK_FALSE;
			raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
			raster.depthBiasClamp		   = desc.raster.depthBiasClamp;
			raster.depthBiasSlopeFactor	   = desc.raster.depthBiasSlopeFactor;
			raster.lineWidth			   = 1.0f;

			// Chained only when asked for, so a device without the extension never sees the struct. The gate above already refused the request on a device reporting no
			// tier, so reaching here means the extension is there.
			const vk::PipelineRasterizationConservativeStateCreateInfoEXT conservative({}, vk::ConservativeRasterizationModeEXT::eOverestimate, 0.0f);
			if (desc.raster.conservativeRasterEnable)
			{
				raster.pNext = &conservative;
			}

			vk::PipelineMultisampleStateCreateInfo multisample;
			multisample.rasterizationSamples  = MapSampleCount(desc.renderTarget.samples);
			multisample.alphaToCoverageEnable = desc.renderTarget.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
			const vk::SampleMask sampleMask	  = desc.renderTarget.sampleMask;

			if (desc.renderTarget.sampleMask != 0xffffffffu)
			{
				multisample.pSampleMask = &sampleMask;
			}

			vk::PipelineDepthStencilStateCreateInfo depthStencil;
			depthStencil.depthTestEnable	   = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.depthWriteEnable	   = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
			depthStencil.depthCompareOp		   = MapCompareOp(desc.depthStencil.depthCompareOp);
			depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.stencilTestEnable	   = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
			depthStencil.front				   = MapStencilFace(desc.depthStencil.front);
			depthStencil.back				   = MapStencilFace(desc.depthStencil.back);
			depthStencil.minDepthBounds		   = desc.depthStencil.minDepthBounds;
			depthStencil.maxDepthBounds		   = desc.depthStencil.maxDepthBounds;

			detail::HostVector<vk::PipelineColorBlendAttachmentState> blendAttachments;
			if (!detail::TryReserve(blendAttachments, desc.blend.attachmentCount))
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
			}

			for (std::uint32_t i = 0; i < desc.blend.attachmentCount; ++i)
			{
				// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ColorBlendAttachmentDesc & a = desc.blend.attachments[i];
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				vk::PipelineColorBlendAttachmentState state;
				state.blendEnable		  = a.blendEnable ? VK_TRUE : VK_FALSE;
				state.srcColorBlendFactor = MapBlendFactor(a.srcColorBlendFactor);
				state.dstColorBlendFactor = MapBlendFactor(a.dstColorBlendFactor);
				state.colorBlendOp		  = MapBlendOp(a.colorBlendOp);
				state.srcAlphaBlendFactor = MapBlendFactor(a.srcAlphaBlendFactor);
				state.dstAlphaBlendFactor = MapBlendFactor(a.dstAlphaBlendFactor);
				state.alphaBlendOp		  = MapBlendOp(a.alphaBlendOp);
				state.colorWriteMask	  = MapColorWriteMask(a.colorWriteMask);
				blendAttachments.push_back(state);
			}

			vk::PipelineColorBlendStateCreateInfo colorBlend({}, desc.blend.logicOpEnable ? VK_TRUE : VK_FALSE, vk::LogicOp::eCopy, blendAttachments);
			colorBlend.setBlendConstants(desc.blend.blendConstants);

			const detail::HostVector<vk::DynamicState> dynamics = MapDynamicStates(desc.dynamicStates);
			const vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamics);

			vk::GraphicsPipelineCreateInfo pipelineInfo({},
				stages,
				&vertexInputState,
				&inputAssembly,
				patches ? &tessellation : nullptr,
				&viewportState,
				&raster,
				&multisample,
				&depthStencil,
				&colorBlend,
				&dynamicState,
				layout);

			// colorFormats and renderingInfo must outlive the create call below so they live here even though only the dynamic-rendering branch fills them.
			detail::HostVector<vk::Format> colorFormats;
			vk::PipelineRenderingCreateInfo renderingInfo;
			if (device->dynamicRendering)
			{
				if (!detail::TryReserve(colorFormats, desc.renderTarget.colorFormatCount))
				{
					return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline storage allocation failed");
				}

				for (std::uint32_t i = 0; i < desc.renderTarget.colorFormatCount; ++i)
				{
					// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					colorFormats.push_back(MapFormat(desc.renderTarget.colorFormats[i]));
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				}
				const vk::Format depthFormat = MapFormat(desc.renderTarget.depthStencilFormat);
				renderingInfo.setColorAttachmentFormats(colorFormats);
				renderingInfo.depthAttachmentFormat	  = depthFormat;
				renderingInfo.stencilAttachmentFormat = HasStencilAspect(desc.renderTarget.depthStencilFormat) ? depthFormat : vk::Format::eUndefined;
				pipelineInfo.pNext					  = &renderingInfo;
			}
			else
			{
				const vk::RenderPass renderPass = GetOrCreateRenderPass(device, device->renderPasses, MakePipelineRenderPassKey(desc));
				if (!renderPass)
				{
					return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan render pass creation failed");
				}

				pipelineInfo.renderPass = renderPass;
				pipelineInfo.subpass	= 0;
			}

			const vk::PipelineCache pipelineCache = ResolvePipelineCache(device, desc.pipelineCache);
			const auto created					  = device->device.createGraphicsPipeline(pipelineCache, pipelineInfo, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan graphics pipeline creation failed");
			}

			// Released unless the slot map takes it, since nothing else would free the pipeline once this returns without a handle for it.
			auto pipelineGuard = detail::MakeScopeGuard(
				[&]
				{
					device->device.destroyPipeline(created.value, nullptr, device->dispatch);
				});

			const GraphicsPipelineHandle storedHandle = device->graphicsPipelineSlots.Store(GraphicsPipelineSlot{ .pipeline = created.value });
			if (!storedHandle.IsValid())
			{
				return FailValue<GraphicsPipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan graphics pipeline handle tracking failed");
			}

			pipelineGuard.Dismiss();
			return ReturnValue(storedHandle, error);
		}

		// Timeline entry. A timeline is a VkSemaphore of the timeline type, the standard primitive for CPU to GPU progress and frame pacing. Tracked in the device
		// registry with a generation validated destroy.
		TimelineHandle VulkanCreateTimeline(void * impl, const TimelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createTimeline");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!VulkanRefuseUnexportable(device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eTimeline,
					Format::eUndefined,
					kExternalQueryBufferUsage,
					"timeline creation asked for an external handle type this adapter cannot export",
					error))
			{
				return TimelineHandle{};
			}
			vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, desc.initialValue);
			vk::ExportSemaphoreCreateInfo exportInfo;
			if (!desc.exportableHandleTypes.Empty())
			{
				exportInfo.handleTypes = MapSemaphoreHandleTypes(desc.exportableHandleTypes);
				typeInfo.pNext		   = &exportInfo;
			}

			const vk::SemaphoreCreateInfo info({}, &typeInfo);
			const auto created = device->device.createSemaphore(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<TimelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan timeline creation failed");
			}

			const TimelineHandle handle =
				device->timelineSlots.Store(TimelineSlot{ .semaphore = created.value, .exportableHandleTypes = desc.exportableHandleTypes });
			if (!handle.IsValid())
			{
				device->device.destroySemaphore(created.value, nullptr, device->dispatch);
				return FailValue<TimelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan timeline handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		[[nodiscard]] vk::QueryType MapQueryType(QueryType type) noexcept
		{
			switch (type)
			{
			case QueryType::eOcclusion:			 return vk::QueryType::eOcclusion;
			case QueryType::ePipelineStatistics: return vk::QueryType::ePipelineStatistics;
			case QueryType::eTimestamp:			 break;
			}
			return vk::QueryType::eTimestamp;
		}

		[[nodiscard]] vk::QueryPipelineStatisticFlags MapPipelineStatistics(Flags<PipelineStatistic> stats) noexcept
		{
			vk::QueryPipelineStatisticFlags out{};
			if (stats.Contains(PipelineStatistic::eInputAssemblyVertices))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices;
			}

			if (stats.Contains(PipelineStatistic::eInputAssemblyPrimitives))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives;
			}

			if (stats.Contains(PipelineStatistic::eVertexShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations;
			}

			if (stats.Contains(PipelineStatistic::eClippingInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eClippingInvocations;
			}

			if (stats.Contains(PipelineStatistic::eClippingPrimitives))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eClippingPrimitives;
			}

			if (stats.Contains(PipelineStatistic::eFragmentShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations;
			}

			if (stats.Contains(PipelineStatistic::eComputeShaderInvocations))
			{
				out |= vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations;
			}

			return out;
		}

		QueryPoolHandle VulkanCreateQueryPool(void * impl, const QueryPoolDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createQueryPool");
			auto * device = static_cast<VulkanDevice *>(impl);
			vk::QueryPoolCreateInfo info({}, MapQueryType(desc.type), desc.queryCount);
			if (desc.type == QueryType::ePipelineStatistics)
			{
				info.pipelineStatistics = MapPipelineStatistics(desc.statistics);
			}

			const auto created = device->device.createQueryPool(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<QueryPoolHandle>(error, ErrorCode::eNativeApiError, "Vulkan query pool creation failed");
			}

			const QueryPoolHandle handle = device->queryPoolSlots.Store(QueryPoolSlot{ .pool = created.value, .queryCount = desc.queryCount });
			if (!handle.IsValid())
			{
				device->device.destroyQueryPool(created.value, nullptr, device->dispatch);
				return FailValue<QueryPoolHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan query pool handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		// Resolves a query pool handle to its slot under the device's validation mode (recording is lockless so no mutex). Mirrors ResolveBuffer.
		[[nodiscard]] QueryPoolSlot * ResolveQueryPool(VulkanDevice * device, QueryPoolHandle handle) noexcept
		{
			return device->queryPoolSlots.Resolve(handle, kHandleAlreadyChecked);
		}

		[[nodiscard]] vk::Filter MapFilter(Filter filter) noexcept
		{
			return filter == Filter::eNearest ? vk::Filter::eNearest : vk::Filter::eLinear;
		}

		[[nodiscard]] vk::SamplerMipmapMode MapMipmapMode(MipmapMode mode) noexcept
		{
			return mode == MipmapMode::eNearest ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear;
		}

		[[nodiscard]] vk::SamplerAddressMode MapAddressMode(AddressMode mode) noexcept
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

		[[nodiscard]] vk::BorderColor MapBorderColor(BorderColor color) noexcept
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

		SamplerHandle VulkanCreateSampler(void * impl, const SamplerDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createSampler");
			auto * device = static_cast<VulkanDevice *>(impl);

			// The conversion hangs off the sampler and does not sit in its create info, and the view this sampler reads has to name an equal one so both land on the
			// same object.
			vk::SamplerYcbcrConversionInfo conversionInfo{};
			if (desc.ycbcrConversion != nullptr)
			{
				if (!device->caps.supportsSamplerYcbcrConversion)
				{
					return FailValue<SamplerHandle>(error, ErrorCode::eUnsupportedFeature, "sampler Y'CbCr conversion is not supported by this Vulkan adapter");
				}
				/*
				 * Vulkan pins the rest of the sampler down once a conversion is attached (VUID-VkSamplerCreateInfo-addressModeU-01646 and its neighbours): edge clamping
				 * only, no anisotropy and no depth comparison. Refused and never quietly rewritten, so the sampler that comes back is the one that was asked for.
				 */
				const bool clampsToEdge =
					desc.addressU == AddressMode::eClampToEdge && desc.addressV == AddressMode::eClampToEdge && desc.addressW == AddressMode::eClampToEdge;
				if (!clampsToEdge || desc.anisotropyEnable || desc.compareEnable)
				{
					return FailValue<SamplerHandle>(error,
						ErrorCode::eInvalidArgument,
						"a sampler with a Y'CbCr conversion must clamp to edge on every axis and enable neither anisotropy nor depth comparison");
				}

				conversionInfo.conversion = AcquireYcbcrConversion(device, *desc.ycbcrConversion);
				if (!conversionInfo.conversion)
				{
					return FailValue<SamplerHandle>(error, ErrorCode::eNativeApiError, "Vulkan sampler Y'CbCr conversion creation failed");
				}
			}

			vk::SamplerCreateInfo info({},
				MapFilter(desc.magFilter),
				MapFilter(desc.minFilter),
				MapMipmapMode(desc.mipmapMode),
				MapAddressMode(desc.addressU),
				MapAddressMode(desc.addressV),
				MapAddressMode(desc.addressW),
				desc.mipLodBias,
				static_cast<vk::Bool32>(desc.anisotropyEnable),
				desc.maxAnisotropy,
				static_cast<vk::Bool32>(desc.compareEnable),
				MapCompareOp(desc.compareOp),
				desc.minLod,
				desc.maxLod,
				MapBorderColor(desc.borderColor),
				VK_FALSE);
			if (desc.ycbcrConversion != nullptr)
			{
				info.pNext = &conversionInfo;
			}

			const auto created = device->device.createSampler(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<SamplerHandle>(error, ErrorCode::eNativeApiError, "Vulkan sampler creation failed");
			}

			const SamplerHandle handle = device->samplerSlots.Store(SamplerSlot{ .sampler = created.value });
			if (!handle.IsValid())
			{
				device->device.destroySampler(created.value, nullptr, device->dispatch);
				return FailValue<SamplerHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan sampler handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		ComputePipelineHandle VulkanCreateComputePipeline(void * impl, const ComputePipelineDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createComputePipeline");

			/*
			 * Required on every backend, not only the one that reads it. SPIR-V and DXIL carry the size inside the binary so Vulkan and Direct3D 12 never look at this
			 * field, but refusing it here too is what stops a shader developed against one of them reaching Metal with the size forgotten, where the failure would be a
			 * dispatch that quietly does a fraction of the work.
			 */
			if (!desc.shader.threadgroupSize.IsStated())
			{
				return FailValue<ComputePipelineHandle>(error,
					ErrorCode::eInvalidArgument,
					"compute pipeline needs a non-zero threadgroupSize on its shader, which no backend can recover from the binary");
			}
			auto * device					= static_cast<VulkanDevice *>(impl);
			const vk::PipelineLayout layout = ResolvePipelineLayout(device, desc.layout);
			if (!layout)
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eInvalidHandle, "compute pipeline with an invalid layout handle");
			}

			const std::array<ShaderBinary, 1> stages{ desc.shader };
			if (!BindingMapsAgree(device, desc.layout, stages, error))
			{
				return {};
			}

			if (!ShaderBytesUsable(desc.shader, error))
			{
				return {};
			}

			const auto createdModule = device->device.createShaderModule(
				vk::ShaderModuleCreateInfo({}, desc.shader.size, static_cast<const std::uint32_t *>(desc.shader.data)), nullptr, device->dispatch);
			if (createdModule.result != vk::Result::eSuccess)
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan shader module creation failed");
			}

			// The module is consumed by pipeline creation and goes either way so the guard covers the success path too. One owner and not a hand destroy on each path
			// out, which would free it twice on the path where the slot store refuses.
			const auto moduleGuard = detail::MakeScopeGuard(
				[&]
				{
					device->device.destroyShaderModule(createdModule.value, nullptr, device->dispatch);
				});

			const vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, createdModule.value, desc.shader.entryPoint);
			const vk::ComputePipelineCreateInfo info({}, stage, layout);
			const vk::PipelineCache pipelineCache = ResolvePipelineCache(device, desc.pipelineCache);
			const auto created					  = device->device.createComputePipeline(pipelineCache, info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eNativeApiError, "Vulkan compute pipeline creation failed");
			}

			// Released unless the slot map takes it, which is where ownership actually moves.
			auto pipelineGuard = detail::MakeScopeGuard(
				[&]
				{
					device->device.destroyPipeline(created.value, nullptr, device->dispatch);
				});

			const ComputePipelineHandle storedHandle = device->computePipelineSlots.Store(ComputePipelineSlot{ .pipeline = created.value });
			if (!storedHandle.IsValid())
			{
				return FailValue<ComputePipelineHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan compute pipeline handle tracking failed");
			}

			pipelineGuard.Dismiss();
			return ReturnValue(storedHandle, error);
		}

		[[nodiscard]] vk::Pipeline ResolveComputePipeline(const VulkanDevice * device, ComputePipelineHandle handle) noexcept
		{
			const ComputePipelineSlot * slot = device->computePipelineSlots.Resolve(handle, kHandleAlreadyChecked);
			return slot != nullptr ? slot->pipeline : vk::Pipeline{};
		}

		PipelineCacheHandle VulkanCreatePipelineCache(void * impl, const PipelineCacheDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createPipelineCache");
			auto * device = static_cast<VulkanDevice *>(impl);
			const vk::PipelineCacheCreateInfo info({}, desc.initialSize, desc.initialData);
			const auto created = device->device.createPipelineCache(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<PipelineCacheHandle>(error, ErrorCode::eNativeApiError, "Vulkan pipeline cache creation failed");
			}

			const PipelineCacheHandle handle = device->pipelineCacheSlots.Store(PipelineCacheSlot{ .cache = created.value });
			if (!handle.IsValid())
			{
				device->device.destroyPipelineCache(created.value, nullptr, device->dispatch);
				return FailValue<PipelineCacheHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan pipeline cache handle tracking failed");
			}

			return ReturnValue(handle, error);
		}

		bool VulkanGetPipelineCacheData(void * impl, PipelineCacheHandle cache, PipelineCacheData * out, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.getPipelineCacheData");
			auto * device			 = static_cast<VulkanDevice *>(impl);
			PipelineCacheSlot * slot = device->pipelineCacheSlots.Resolve(cache, true);
			if (slot == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidHandle, "getPipelineCacheData with an invalid or stale cache handle");
			}

			auto data = device->device.getPipelineCacheData<HostAllocatorAdapter<std::uint8_t>>(slot->cache, device->dispatch);
			if (data.result != vk::Result::eSuccess)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Vulkan getPipelineCacheData failed");
			}

			// Retained in the slot so the returned view stays valid until the next query on this cache.
			slot->data = std::move(data.value);
			if (out != nullptr)
			{
				out->data = slot->data.data();
				out->size = slot->data.size();
			}

			return Succeed(error);
		}

		BinarySemaphoreHandle VulkanCreateBinarySemaphore(void * impl, const BinarySemaphoreDesc & desc, Error * error) noexcept
		{
			AZO_RHI_PROFILE_ZONE("rhi.vulkan.createBinarySemaphore");
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!VulkanRefuseUnexportable(device,
					desc.exportableHandleTypes,
					ExternalObjectKind::eBinarySemaphore,
					Format::eUndefined,
					kExternalQueryBufferUsage,
					"binary semaphore creation asked for an external handle type this adapter cannot export",
					error))
			{
				return BinarySemaphoreHandle{};
			}
			vk::SemaphoreCreateInfo info{};
			vk::ExportSemaphoreCreateInfo exportInfo;
			if (!desc.exportableHandleTypes.Empty())
			{
				exportInfo.handleTypes = MapSemaphoreHandleTypes(desc.exportableHandleTypes);
				info.pNext			   = &exportInfo;
			}

			const auto created = device->device.createSemaphore(info, nullptr, device->dispatch);
			if (created.result != vk::Result::eSuccess)
			{
				return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eNativeApiError, "Vulkan binary semaphore creation failed");
			}

			BinarySemaphoreHandle handle =
				device->binarySemaphoreSlots.Store(BinarySemaphoreSlot{ .semaphore = created.value, .exportableHandleTypes = desc.exportableHandleTypes });
			if (!handle.IsValid())
			{
				device->device.destroySemaphore(created.value, nullptr, device->dispatch);
				return FailValue<BinarySemaphoreHandle>(error, ErrorCode::eOutOfHostMemory, "Vulkan binary semaphore handle tracking failed");
			}

			handle.index |= kDeviceBinarySemaphoreBit; // tag so ResolveBinarySemaphore reads the device registry, not the swapchain
			return ReturnValue(handle, error);
		}

		bool VulkanQueryMemoryBudget(void * impl, HeapType heap, MemoryBudgetInfo * out, Error * error) noexcept
		{
			auto * device = static_cast<VulkanDevice *>(impl);
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "queryMemoryBudget output pointer is null");
			}

			// Map the abstract heap type to a Vulkan memory heap: device-local for GPU and transient heaps, the first host heap otherwise (on a unified-memory device
			// there is only one heap).
			const vk::PhysicalDeviceMemoryProperties memProps = device->phys.getMemoryProperties(device->dispatch);
			const bool wantDeviceLocal						  = heap == HeapType::eGpuLocal || heap == HeapType::eTransient;
			std::uint32_t heapIndex							  = 0;
			for (std::uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
			{
				const bool deviceLocal = static_cast<bool>(memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal);
				if (deviceLocal == wantDeviceLocal)
				{
					heapIndex = i;
					break;
				}
			}

			std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
			vmaGetHeapBudgets(device->allocator, budgets.data());
			// The loop bound is the size of what is indexed. NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const VmaBudget & budget = budgets[heapIndex];
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			out->heap						  = heap;
			out->budgetBytes				  = budget.budget;
			out->usageBytes					  = budget.usage;
			out->availableForReservationBytes = budget.budget > budget.usage ? budget.budget - budget.usage : 0;
			out->budgetIsPrecise			  = false; // VK_EXT_memory_budget is not enabled so VMA estimates.
			return Succeed(error);
		}

		// Best-effort residency hint. This device cannot honor it (VK_EXT_memory_priority is not enabled) so the hint validates the named resources and is accepted
		// as a no-op, the contract for an advisory hint.
		bool VulkanSetResidencyPriority(void * impl, std::span<const ResidencyPriorityDesc> priorities, Error * error) noexcept
		{
			auto * device = static_cast<VulkanDevice *>(impl);
			for (const ResidencyPriorityDesc & desc : priorities)
			{
				if (desc.buffer.IsValid())
				{
					if (ResolveBuffer(device, desc.buffer) == nullptr)
					{
						return Fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid buffer handle");
					}
				}

				if (desc.texture.IsValid())
				{
					if (!ResolveTexture(device, desc.texture))
					{
						return Fail(error, ErrorCode::eInvalidHandle, "setResidencyPriority with an invalid texture handle");
					}
				}
			}
			return Succeed(error);
		}

		// Samples the device and host clocks as one calibrated pair through VK_EXT_calibrated_timestamps. The host domain is QueryPerformanceCounter on Windows and a
		// monotonic clock elsewhere. Fails with eUnsupportedFeature without the extension.
		bool VulkanCalibrateTimestamp(void * impl, QueueType queueType, TimestampCalibration * out, Error * error) noexcept
		{
			if (out == nullptr)
			{
				return Fail(error, ErrorCode::eInvalidArgument, "timestamp calibration output is null");
			}
			auto * device = static_cast<VulkanDevice *>(impl);
			if (!device->caps.supportsTimestampCalibration)
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "VK_EXT_calibrated_timestamps is not available on this device");
			}

			// The same question the cap was settled from, asked through the same helper so the two cannot drift apart again.
			vk::TimeDomainEXT hostDomain = vk::TimeDomainEXT::eDevice;
			if (!VulkanCalibrationDomains(device->phys, device->dispatch, hostDomain))
			{
				return Fail(error, ErrorCode::eUnsupportedFeature, "the adapter cannot calibrate the device and host clocks together");
			}

			const std::array<vk::CalibratedTimestampInfoEXT, 2> infos{ { vk::CalibratedTimestampInfoEXT{ vk::TimeDomainEXT::eDevice },
				vk::CalibratedTimestampInfoEXT{ hostDomain } } };
			const auto calibrated = device->device.getCalibratedTimestampsEXT<HostAllocatorAdapter<std::uint64_t>>(infos, device->dispatch);
			if (calibrated.result != vk::Result::eSuccess)
			{
				return Fail(error, ErrorCode::eNativeApiError, "Vulkan timestamp calibration failed");
			}

			const std::pair<detail::HostVector<std::uint64_t>, std::uint64_t> & sampled = calibrated.value;
			if (sampled.first.size() < 2)
			{
				return Fail(error, ErrorCode::eNativeApiError, "vkGetCalibratedTimestampsEXT returned too few timestamps");
			}

			out->queueType	  = queueType;
			out->gpuTimestamp = sampled.first[0];
#ifdef _WIN32
			// QueryPerformanceCounter reports ticks so scale by the counter frequency to reach nanoseconds.
			static const double nanosPerTick = []
			{
				LARGE_INTEGER frequency{};
				return QueryPerformanceFrequency(&frequency) != 0 && frequency.QuadPart != 0 ? 1.0e9 / static_cast<double>(frequency.QuadPart) : 0.0;
			}();
			out->cpuTimestampNanoseconds = static_cast<std::uint64_t>(static_cast<double>(sampled.first[1]) * nanosPerTick);
#else
			// eClockMonotonic / eClockMonotonicRaw already report nanoseconds.
			out->cpuTimestampNanoseconds = sampled.first[1];
#endif
			out->gpuPeriodNanoseconds = device->caps.timestampPeriodNanoseconds;
			out->calibrated			  = true;
			return Succeed(error);
		}

	} // namespace vulkan

	Result<VulkanNativeDevice> GetVulkanNativeDevice(Device device)
	{
		if (device.GetGraphicsApiId() != VulkanApi::id)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeDevice called on a non-Vulkan device",
			};
		}

		auto * impl = static_cast<vulkan::VulkanDevice *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(device), vulkan::CoreDeviceBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeDevice reached something other than a Vulkan device behind the facade",
			};
		}

		return VulkanNativeDevice{ .instance = impl->instance,
			.physicalDevice					 = impl->phys,
			.device							 = impl->device,
			.graphicsQueue					 = impl->graphicsQueues.empty() ? vk::Queue{} : impl->graphicsQueues.front(),
			.graphicsQueueFamily			 = impl->graphicsFamily,
			.allocator						 = impl->allocator,
			.debugUtils						 = impl->debugUtils,
			.dispatch						 = &impl->dispatch };
	}

	Result<void> SetVulkanDeviceSurface(Device device, vk::SurfaceKHR surface)
	{
		if (device.GetGraphicsApiId() != VulkanApi::id)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "SetVulkanDeviceSurface called on a non-Vulkan device",
			};
		}

		auto * impl = static_cast<vulkan::VulkanDevice *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(device), vulkan::CoreDeviceBlock()));
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

	Result<VulkanNativeSwapchain> GetVulkanNativeSwapchain(Swapchain swapchain)
	{
		auto * impl = static_cast<vulkan::VulkanSwapchain *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(swapchain), vulkan::SwapchainBlock()));
		if (impl == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "GetVulkanNativeSwapchain called on a swapchain that is not a Vulkan one",
			};
		}

		const vulkan::SwapchainBundle & bundle = impl->bundle;
		return VulkanNativeSwapchain{ .swapchain = bundle.Swapchain,
			.colorFormat						 = bundle.ColorFormat,
			.depthFormat						 = bundle.DepthFormat,
			.extent								 = bundle.Extent,
			.images								 = bundle.Images,
			.views								 = bundle.Views,
			.depthImage							 = bundle.DepthImage,
			.depthView							 = bundle.DepthView,
			.presentMode						 = bundle.PresentMode,
			.captureCapable						 = bundle.CaptureCapable };
	}

	Result<vk::Semaphore> GetVulkanSemaphore(const Swapchain swapchain, BinarySemaphoreHandle semaphore)
	{
		const auto * impl = static_cast<vulkan::VulkanSwapchain *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(swapchain), vulkan::SwapchainBlock()));
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

	vk::CommandBuffer GetVulkanCommandBuffer(CommandList commandList)
	{
		const auto * impl =
			static_cast<vulkan::VulkanCommandList *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandList), vulkan::RenderCommandBlock()));
		return impl != nullptr ? impl->buffer : vk::CommandBuffer{};
	}

	vk::CommandPool GetVulkanCommandPool(CommandPool commandPool)
	{
		const auto * impl =
			static_cast<vulkan::VulkanCommandPool *>(detail::NativeImplOf(detail::FacadeBuilder::ImplOf(commandPool), vulkan::CommandPoolBlock()));
		return impl != nullptr ? impl->pool : vk::CommandPool{};
	}

	Result<void> RegisterVulkanBackend(GraphicsApiRegistry & registry)
	{
		BackendCreateInfo info{};
		info.info.canonicalName		   = VulkanApi::canonicalName;
		info.info.displayName		   = VulkanApi::displayName;
		info.info.apiVersionMajor	   = 1;
		info.info.apiVersionMinor	   = 3;
		info.info.supportsSurfaces	   = true;
		info.info.supportsDebugMarkers = true;
		info.createInstance			   = &vulkan::VulkanCreateInstance;
		return registry.Register<VulkanApi>(info);
	}

	// Static device form: bring up an owned instance, then a device from it and hand back the owner.
	template <>
	Result<UniqueDevice> CreateDevice<VulkanApi>(const DeviceDesc & desc)
	{
		if (const Result<void> threading = detail::CheckThreading(desc); !threading)
		{
			return threading.GetError();
		}

		Error error{};
		const InstanceDesc instanceDesc				   = InstanceDescForDevice(desc);
		HostUniquePtr<vulkan::VulkanInstance> instance = vulkan::BuildInstance(instanceDesc, &error);
		if (!instance)
		{
			return error;
		}

		// The device owns its instance here so both tear down deterministically when the owning UniqueDevice is reset, while the loader and any validation layer are
		// still loaded.
		vulkan::VulkanInstance * instanceRaw = instance.get();
		void * device						 = vulkan::MakeOwnedDevice(instanceRaw, desc, &error);
		if (device == nullptr)
		{
			return error;
		}
		static_cast<vulkan::VulkanDevice *>(device)->ownedInstance = std::move(instance);
		void * deviceImpl										   = device;
		BackendBlockSet * blocks								   = detail::ResolveDeviceBlocks(deviceImpl, desc, &error);
		if (blocks == nullptr)
		{
			return error;
		}

		return detail::FacadeBuilder::MakeUniqueDevice(deviceImpl, blocks);
	}

	namespace native
	{
		VulkanCommandListView NativeAccess<VulkanApi>::MakeCommandListView(void * commandListImpl) noexcept
		{
			return VulkanCommandListView{ .commandBuffer = static_cast<vulkan::VulkanCommandList *>(commandListImpl)->buffer };
		}
	} // namespace native

} // namespace azo::rhi

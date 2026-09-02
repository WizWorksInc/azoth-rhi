// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <bit>
#include <cstdint>
#include <memory>

namespace azo::rhi
{
	namespace
	{
		static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "VkSurfaceKHR is expected to be 64 bits wide");

		class VulkanPresentationBackend final : public PresentationBackend
		{
		public:
			bool InitInstanceLoader(SurfaceSource & source) override
			{
				native::VulkanLoaderPayload payload{};
				const SurfaceRequest request{
					.id		  = native::VulkanLoaderPayload::kId,
					.byteSize = sizeof(payload),
					.payload  = &payload,
				};

				if (!source.Provide(request) || payload.getInstanceProcAddr == nullptr)
				{
					payload.getInstanceProcAddr = native::ResolveVulkanLoader();
				}

				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a loader entry point arrives as a void pointer and this is the cast for it.
				auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(payload.getInstanceProcAddr);
				if (getInstanceProcAddr == nullptr)
				{
					return false;
				}

				// NOLINTNEXTLINE(misc-include-cleaner): VULKAN_HPP_DEFAULT_DISPATCHER is provided by Vulkan-Hpp.
				VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);
				return true;
			}

			SurfaceHandle CreateSurface(SurfaceSource & source, Device device) override
			{
				const Result<VulkanNativeDevice> native = GetVulkanNativeDevice(device);
				if (!native)
				{
					return {};
				}

				const vk::Instance instance = native.Value().instance;

				const vk::detail::DispatchLoaderDynamic & dispatch = *native.Value().dispatch;

				native::VulkanSurfacePayload payload{};
				payload.instance = static_cast<VkInstance>(instance);

				const SurfaceRequest request{
					.id		  = native::VulkanSurfacePayload::kId,
					.byteSize = sizeof(payload),
					.payload  = &payload,
				};

				if (!source.Provide(request) || payload.surface == 0)
				{
					return {};
				}

				const std::uint64_t raw = payload.surface;

				m_surface  = vk::SurfaceKHR(std::bit_cast<VkSurfaceKHR>(raw));
				m_instance = instance;

				if (const Result<void> attached = SetVulkanDeviceSurface(device, m_surface); !attached)
				{
					instance.destroySurfaceKHR(m_surface, nullptr, dispatch);
					m_surface = nullptr;
					return {};
				}

				return SurfaceHandle{ raw };
			}

		private:
			vk::Instance m_instance;
			vk::SurfaceKHR m_surface;
		};

	}

	// NOLINTNEXTLINE(misc-use-internal-linkage)
	HostUniquePtr<PresentationBackend> MakeVulkanPresentationBackend()
	{
		return HostNew<VulkanPresentationBackend>();
	}

	namespace native
	{

		void * ResolveVulkanLoader()
		{
			static const vk::detail::DynamicLoader * loader = []() -> const vk::detail::DynamicLoader *
			{
				auto * opened = new vk::detail::DynamicLoader(); // NOLINT(cppcoreguidelines-owning-memory): outlives every caller on purpose.

				if (!opened->success())
				{
					delete opened; // NOLINT(cppcoreguidelines-owning-memory)
					return nullptr;
				}

				return opened;
			}();

			if (loader == nullptr)
			{
				return nullptr;
			}

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a loader entry point leaves as a void pointer, which is what the host asked for.
			return reinterpret_cast<void *>(loader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
		}

	}

}

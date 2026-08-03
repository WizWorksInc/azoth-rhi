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
		// VkSurfaceKHR is non-dispatchable so it is 64 bits wide on every supported target and rides through SurfaceSource as a plain integer. The bit_cast
		// below depends on that and 32-bit is rejected in core/platform.hpp anyway so assert it, not assume it.
		static_assert(sizeof(VkSurfaceKHR) == sizeof(std::uint64_t), "VkSurfaceKHR is expected to be 64 bits wide");

		class VulkanPresentationBackend final : public PresentationBackend
		{
		public:
			bool InitInstanceLoader(SurfaceSource & source) override
			{
				/*
				 * Vulkan-Hpp dynamic dispatch has to be seeded from the same loader the window library brought up. Otherwise a surface made by that library
				 * and the instance this dispatches through belong to two different Vulkans, which faults inside a driver without failing here.
				 *
				 * The host is asked first and its answer wins. A host that has none gets ours, written back into the payload so it can hand the same one to
				 * its window library.
				 */
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

				// The device's own table and not the process-global one, which follows whichever device was created last and would dispatch this instance's
				// surface destroy through another instance's layer chain.
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

				// Surface teardown transfers to the RHI device so destruction order stays valid.
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

	} // namespace

	/*
	 * Reported as internal by misc-use-internal-linkage, which only sees this translation unit. presentation_backend.cpp calls it and there is no header
	 * declaring it because it is the one seam between those two files. Making it static breaks the link.
	 */
	// NOLINTNEXTLINE(misc-use-internal-linkage)
	HostUniquePtr<PresentationBackend> MakeVulkanPresentationBackend()
	{
		return HostNew<VulkanPresentationBackend>();
	}

	namespace native
	{

		/*
		 * The loader this build would use, resolved the way the Vulkan backend resolves it when nothing else has.
		 *
		 * Vulkan-Hpp's DynamicLoader carries the search and the part that matters is the part a window library usually lacks: a loader installed where the
		 * platform's dynamic linker does not look by default, which on macOS is where the SDK puts one. Held for the life of the process, since the pointers
		 * handed out have to stay callable.
		 */
		void * ResolveVulkanLoader()
		{
			static const vk::detail::DynamicLoader * loader = []() -> const vk::detail::DynamicLoader *
			{
				auto * opened = new vk::detail::DynamicLoader(); // NOLINT(cppcoreguidelines-owning-memory): outlives every caller on purpose.

				// A loader that found no library reports it here and not by throwing, Vulkan-Hpp being told there are no exceptions.
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

	} // namespace native

} // namespace azo::rhi

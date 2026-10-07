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

#include "azoth/rhi/host/presentation_backend.hpp"

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/host/surface_source.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace azo::rhi
{
	namespace
	{
		template <class Payload, void * Payload::* Member>
		class HandlePassthroughBackend final : public PresentationBackend
		{
		public:
			bool init_instance_loader(SurfaceSource & /*source*/) override
			{
				return true;
			}

			SurfaceHandle create_surface(SurfaceSource & source, Device /*device*/) override
			{
				Payload payload{};
				const SurfaceRequest request{
					.id		  = Payload::kId,
					.byteSize = sizeof(Payload),
					.payload  = &payload,
				};

				if (!source.provide(request) || payload.*Member == nullptr)
				{
					return {};
				}

				return SurfaceHandle{ std::bit_cast<std::uint64_t>(payload.*Member) };
			}
		};

	}

#ifdef AZOTH_RHI_BACKEND_VULKAN
	HostUniquePtr<PresentationBackend> make_vulkan_presentation_backend();
#else
	namespace native
	{

		void * resolve_vulkan_loader()
		{
			return nullptr;
		}

	}
#endif

	GraphicsApiId select_graphics_api(const char * requestedOverride)
	{
		// An explicit override (a --backend flag, say) wins, otherwise the env var overrides the compile-time default. NOLINTNEXTLINE(concurrency-mt-unsafe):
		const char * requested = requestedOverride != nullptr ? requestedOverride : std::getenv("AZOTH_RHI_BACKEND");
		const char * name	   = requested != nullptr ? requested : AZOTH_RHI_BACKEND_DEFAULT;

		if (std::strcmp(name, "vulkan") == 0)
		{
			return VulkanApi::kId;
		}

		if (std::strcmp(name, "d3d12") == 0)
		{
			return D3D12Api::kId;
		}

		if (std::strcmp(name, "metal") == 0)
		{
			return MetalApi::kId;
		}

		if (std::strcmp(name, "metal4") == 0)
		{
			return Metal4Api::kId;
		}

		if (std::strcmp(name, "null") == 0)
		{
			return NullApi::kId;
		}

		return VulkanApi::kId;
	}

	HostUniquePtr<PresentationBackend> make_presentation_backend(GraphicsApiId api)
	{
#ifdef AZOTH_RHI_BACKEND_VULKAN
		if (api == VulkanApi::kId)
		{
			return make_vulkan_presentation_backend();
		}
#endif

#if defined(AZOTH_RHI_BACKEND_METAL) || defined(AZOTH_RHI_BACKEND_METAL4)
		if (is_metal_family(api))
		{
			return host_new<HandlePassthroughBackend<native::MetalSurfacePayload, &native::MetalSurfacePayload::layer>>();
		}
#endif

#ifdef AZOTH_RHI_BACKEND_D3D12
		if (api == D3D12Api::kId)
		{
			return host_new<HandlePassthroughBackend<native::Win32SurfacePayload, &native::Win32SurfacePayload::window>>();
		}
#endif

		return nullptr;
	}

}

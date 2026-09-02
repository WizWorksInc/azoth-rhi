// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/host/presentation_backend.hpp"

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace azo::rhi
{
	namespace
	{
		template <class Payload, void * Payload::* Member>
		class HandlePassthroughBackend final : public PresentationBackend
		{
		public:
			bool InitInstanceLoader(SurfaceSource & ) override
			{
				return true;
			}

			SurfaceHandle CreateSurface(SurfaceSource & source, Device ) override
			{
				Payload payload{};
				const SurfaceRequest request{
					.id		  = Payload::kId,
					.byteSize = sizeof(Payload),
					.payload  = &payload,
				};

				if (!source.Provide(request) || payload.*Member == nullptr)
				{
					return {};
				}

				return SurfaceHandle{ std::bit_cast<std::uint64_t>(payload.*Member) };
			}
		};

	}

#ifdef AZOTH_RHI_BACKEND_VULKAN
	HostUniquePtr<PresentationBackend> MakeVulkanPresentationBackend();
#else
	namespace native
	{

		void * ResolveVulkanLoader()
		{
			return nullptr;
		}

	}
#endif

	GraphicsApiId SelectGraphicsApi(const char * requestedOverride)
	{
		// An explicit override (a --backend flag, say) wins, otherwise the env var overrides the compile-time default. NOLINTNEXTLINE(concurrency-mt-unsafe):
		const char * requested = requestedOverride != nullptr ? requestedOverride : std::getenv("AZOTH_RHI_BACKEND");
		const char * name	   = requested != nullptr ? requested : AZOTH_RHI_BACKEND_DEFAULT;

		if (std::strcmp(name, "vulkan") == 0)
		{
			return VulkanApi::id;
		}

		if (std::strcmp(name, "d3d12") == 0)
		{
			return D3D12Api::id;
		}

		if (std::strcmp(name, "metal") == 0)
		{
			return MetalApi::id;
		}

		if (std::strcmp(name, "metal4") == 0)
		{
			return Metal4Api::id;
		}

		if (std::strcmp(name, "null") == 0)
		{
			return NullApi::id;
		}

		return VulkanApi::id;
	}

	HostUniquePtr<PresentationBackend> MakePresentationBackend(GraphicsApiId api)
	{
#ifdef AZOTH_RHI_BACKEND_VULKAN
		if (api == VulkanApi::id)
		{
			return MakeVulkanPresentationBackend();
		}
#endif

#if defined(AZOTH_RHI_BACKEND_METAL) || defined(AZOTH_RHI_BACKEND_METAL4)
		if (IsMetalFamily(api))
		{
			return HostNew<HandlePassthroughBackend<native::MetalSurfacePayload, &native::MetalSurfacePayload::layer>>();
		}
#endif

#ifdef AZOTH_RHI_BACKEND_D3D12
		if (api == D3D12Api::id)
		{
			return HostNew<HandlePassthroughBackend<native::Win32SurfacePayload, &native::Win32SurfacePayload::window>>();
		}
#endif

		return nullptr;
	}

}

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
		/*
		 * Metal and D3D12 presentation is a handle passthrough. The host already owns the layer or the window and neither backend needs anything built on top of it,
		 * leaving no native call to make and no platform headers. Vulkan does real work, seeding the dispatcher and taking ownership of a surface it asked the host
		 * to make.
		 *
		 * Templated on the payload and not a method. The member the handle arrives in differs per API so it comes alongside.
		 */
		template <class Payload, void * Payload::* Member>
		class HandlePassthroughBackend final : public PresentationBackend
		{
		public:
			bool InitInstanceLoader(SurfaceSource & /*source*/) override
			{
				// These APIs link directly so there is no loader to bring up.
				return true;
			}

			SurfaceHandle CreateSurface(SurfaceSource & source, Device /*device*/) override
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

	} // namespace

	// Defined in presentation_vulkan.cpp, which compiles only where our Vulkan backend does. A Vulkan backend of your own owns its own surface creation, since
	// ours calls into our device internals.
#ifdef AZOTH_RHI_BACKEND_VULKAN
	HostUniquePtr<PresentationBackend> MakeVulkanPresentationBackend();
#else
	namespace native
	{

		// The header promises null on a build with no Vulkan backend and not a link error so a host can ask without first knowing the answer. The real one lives
		// beside the backend that can answer it.
		void * ResolveVulkanLoader()
		{
			return nullptr;
		}

	} // namespace native
#endif

	GraphicsApiId SelectGraphicsApi(const char * requestedOverride)
	{
		// An explicit override (a --backend flag, say) wins, otherwise the env var overrides the compile-time default. NOLINTNEXTLINE(concurrency-mt-unsafe):
		// one-time startup env read before worker threads exist.
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

		// A name we recognize is returned as asked even when this build has no backend for it so MakePresentationBackend returning null tells the caller their
		// request was unavailable. Only an unrecognized name falls back to Vulkan, which every platform reaches (MoltenVK on Apple).
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

		// One payload for both generations, a CAMetalLayer being what either backend takes and what the host builds without knowing which it will run on.
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

} // namespace azo::rhi

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

#include "FW/utility/MetalCapture.hpp"

#include "FW/utility/Log.hpp"

#ifdef AZOTH_RHI_EXAMPLES_HAVE_METAL

	#include <azoth/rhi/native/metal_native.hpp>
	#include <Foundation/Foundation.hpp>
	#include <Metal/Metal.hpp>

	#include <cstdlib>
	#include <string>

#endif

namespace rhi = azo::rhi;

namespace fw
{

#ifdef AZOTH_RHI_EXAMPLES_HAVE_METAL

	namespace
	{
		[[nodiscard]] const char * EnvOrNull(const char * name) noexcept
		{
			// NOLINTNEXTLINE(concurrency-mt-unsafe): read once at start-up, before any worker thread exists.
			const char * value = std::getenv(name);
			return value != nullptr && *value != '\0' ? value : nullptr;
		}

		/*
		 * The MTLDevice behind an RHI device, whichever Metal backend it came from.
		 *
		 * The two backends answer different accessors and neither answers for the other, so asking both and taking whichever replies is how a caller stays out
		 * of the business of knowing which it got.
		 */
		[[nodiscard]] MTL::Device * MetalDeviceOf([[maybe_unused]] rhi::Device device) noexcept
		{
	#ifdef AZOTH_RHI_EXAMPLES_HAVE_METAL3
			if (const rhi::Result<rhi::MetalNativeDevice> three = rhi::GetMetalNativeDevice(device); three)
			{
				return three.Value().device;
			}
	#endif
	#ifdef AZOTH_RHI_EXAMPLES_HAVE_METAL4
			if (const rhi::Result<rhi::Metal4NativeDevice> four = rhi::GetMetal4NativeDevice(device); four)
			{
				return four.Value().device;
			}
	#endif

			return nullptr;
		}
	} // namespace

	MetalCapture::MetalCapture(rhi::Device device)
	{
		const char * path = EnvOrNull("AZOTH_METAL_CAPTURE");
		if (path == nullptr)
		{
			return;
		}

		MTL::Device * metal = MetalDeviceOf(device);
		if (metal == nullptr)
		{
			LOG_INFO(Log(), "AZOTH_METAL_CAPTURE is set but this device is not Metal backed, so no capture was taken");
			return;
		}

		/*
		 * Metal refuses to capture at all unless the process started with this set, and it cannot be set from inside the process because the check happens
		 * before main. Saying so is the whole value of testing it: the alternative is startCapture failing with an error nobody reads.
		 */
		if (EnvOrNull("MTL_CAPTURE_ENABLED") == nullptr)
		{
			LOG_ERROR(Log(), "AZOTH_METAL_CAPTURE needs MTL_CAPTURE_ENABLED=1 in the environment, which Metal reads before main");
			return;
		}

		const char * frames = EnvOrNull("AZOTH_METAL_CAPTURE_FRAMES");
		m_frames			= frames != nullptr ? static_cast<std::uint32_t>(std::strtoul(frames, nullptr, 10)) : 1;
		if (m_frames == 0)
		{
			m_frames = 1;
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MTL::CaptureManager * manager = MTL::CaptureManager::sharedCaptureManager();
		if (!manager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument))
		{
			LOG_ERROR(Log(), "this machine cannot write a .gputrace, which usually means the Metal developer tools are not installed");
			return;
		}

		NS::SharedPtr<MTL::CaptureDescriptor> descriptor = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init());
		descriptor->setCaptureObject(metal);
		descriptor->setDestination(MTL::CaptureDestinationGPUTraceDocument);
		descriptor->setOutputURL(NS::URL::fileURLWithPath(NS::String::string(path, NS::UTF8StringEncoding)));

		NS::Error * error = nullptr;
		if (!manager->startCapture(descriptor.get(), &error))
		{
			const char * why = error != nullptr && error->localizedDescription() != nullptr ? error->localizedDescription()->utf8String() : "no diagnostic";
			LOG_ERROR(Log(), "Metal frame capture refused: {}", why);
			return;
		}

		m_capturing = true;
		LOG_INFO(Log(), "capturing {} frame(s) into {}", m_frames, path);
	}

	void MetalCapture::FramePresented()
	{
		if (!m_capturing)
		{
			return;
		}

		++m_captured;
		if (m_captured >= m_frames)
		{
			Stop();
		}
	}

	void MetalCapture::Stop()
	{
		if (!m_capturing)
		{
			return;
		}

		const NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
		MTL::CaptureManager::sharedCaptureManager()->stopCapture();
		m_capturing = false;
		LOG_INFO(Log(), "Metal frame capture written");
	}

#else

	MetalCapture::MetalCapture([[maybe_unused]] rhi::Device device) {}

	void MetalCapture::FramePresented() {}

	void MetalCapture::Stop() {}

#endif

	MetalCapture::~MetalCapture()
	{
		Stop();
	}

} // namespace fw

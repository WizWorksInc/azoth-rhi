// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include "FW/utility/Log.hpp"
#include "native/glfw_natives.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <span>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	[[nodiscard]] rhi::ClearColor FrameColor(const std::uint64_t frame)
	{
		constexpr float kThird = 2.0f * std::numbers::pi_v<float> / 3.0f;
		const float phase	   = static_cast<float>(frame % 240) / 240.0f * 2.0f * std::numbers::pi_v<float>;

		return rhi::ClearColor{
			.r = 0.5f + (0.5f * std::sin(phase)),
			.g = 0.5f + (0.5f * std::sin(phase + kThird)),
			.b = 0.5f + (0.5f * std::sin(phase + (2.0f * kThird))),
			.a = 1.0f,
		};
	}

	const char * GlfwError()
	{
		const char * description = nullptr;
		static_cast<void>(glfwGetError(&description));

		return description != nullptr ? description : "no diagnostic";
	}

	class Window final : public rhi::SurfaceSource
	{
	public:
		Window()						   = default;
		Window(const Window &)			   = delete;
		Window & operator=(const Window &) = delete;
		Window(Window &&)				   = delete;
		Window & operator=(Window &&)	   = delete;

		~Window() override
		{
			if (m_window != nullptr)
			{
				glfwDestroyWindow(m_window);
			}
			glfwTerminate();
		}

		[[nodiscard]] bool Open(const rhi::GraphicsApiId api)
		{
			glfw_native::InitVulkanLoader(rhi::native::ResolveVulkanLoader());

			if (glfwInit() != GLFW_TRUE)
			{
				LOG_ERROR(fw::Log(), "glfwInit failed: {}", GlfwError());
				return false;
			}

			if (api == rhi::VulkanApi::id && glfwVulkanSupported() != GLFW_TRUE)
			{
				LOG_INFO(fw::Log(), "this GLFW found no Vulkan loader");
				return false;
			}

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

			m_window = glfwCreateWindow(1280, 720, "AzothRHI glfw", nullptr, nullptr);
			if (m_window == nullptr)
			{
				LOG_ERROR(fw::Log(), "glfwCreateWindow failed: {}", GlfwError());
				return false;
			}

			if (rhi::IsMetalFamily(api))
			{
				m_metalLayer = glfw_native::MetalLayer(m_window);
				if (m_metalLayer == nullptr)
				{
					LOG_INFO(fw::Log(), "this GLFW gave up no Cocoa window to attach a Metal layer to");
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool PumpEvents() const
		{
			glfwPollEvents();

			if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			{
				return false;
			}

			return glfwWindowShouldClose(m_window) != GLFW_TRUE;
		}

		[[nodiscard]] rhi::Extent2D DrawableSize() const
		{
			int width  = 0;
			int height = 0;
			glfwGetFramebufferSize(m_window, &width, &height);

			return rhi::Extent2D{ .width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height) };
		}

		[[nodiscard]] bool Provide(const rhi::SurfaceRequest & request) override
		{
			if (auto * loader = rhi::SurfacePayloadOf<rhi::native::VulkanLoaderPayload>(request); loader != nullptr)
			{
				loader->getInstanceProcAddr = glfw_native::VulkanInstanceProcAddr();
				return loader->getInstanceProcAddr != nullptr;
			}

			if (auto * vulkan = rhi::SurfacePayloadOf<rhi::native::VulkanSurfacePayload>(request); vulkan != nullptr)
			{
				vulkan->surface = glfw_native::CreateVulkanSurface(m_window, vulkan->instance);
				return vulkan->surface != 0;
			}

			if (auto * metal = rhi::SurfacePayloadOf<rhi::native::MetalSurfacePayload>(request); metal != nullptr)
			{
				metal->layer = m_metalLayer;
				return metal->layer != nullptr;
			}

			if (auto * win32 = rhi::SurfacePayloadOf<rhi::native::Win32SurfacePayload>(request); win32 != nullptr)
			{
				win32->window = glfw_native::Win32WindowHandle(m_window);
				return win32->window != nullptr;
			}

			return false;
		}

	private:
		GLFWwindow * m_window = nullptr;
		void * m_metalLayer	  = nullptr;
	};

}

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const std::uint64_t frameLimit = args.size() > 1 ? std::strtoull(args[1], nullptr, 10) : 0;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };
	if (backends.IsEmpty())
	{
		LOG_INFO(fw::Log(), "this build has no backend that can present");
		return 1;
	}

	const rhi::GraphicsApiId api = backends.Preferred().front().id;

	Window window;
	if (!window.Open(api))
	{
		return 1;
	}

	const rhi::HostUniquePtr<rhi::PresentationBackend> presentation = rhi::MakePresentationBackend(api);
	if (presentation == nullptr || !presentation->InitInstanceLoader(window))
	{
		LOG_ERROR(fw::Log(), "this build cannot present through the backend it picked");
		return 1;
	}

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("sdl3").GraphicsQueue().Build(backends.Registry(), backends.PreferredApis().first(1));
	if (!device)
	{
		LOG_ERROR(fw::Log(), "failed to create a device: {}", device.GetError().message != nullptr ? device.GetError().message : "no diagnostic");
		return 1;
	}

	rhi::Device dev = device.Value().Get();
	rhi::Error error{};

	const rhi::SurfaceHandle surface = presentation->CreateSurface(window, dev);
	const rhi::Extent2D initial		 = window.DrawableSize();

	rhi::Swapchain swapchain = dev.CreateSwapchain(
		rhi::SwapchainDesc{ .surface = surface, .width = initial.width, .height = initial.height, .debugName = "present.swapchain" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "present.timeline" }, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "present.pool" }, error);
	if (surface.value == 0 || !swapchain.IsValid() || !queue.IsValid() || !timeline.IsValid() || !pool.IsValid())
	{
		LOG_ERROR(fw::Log(), "failed to set up presentation: {}", error.message != nullptr ? error.message : "no diagnostic");
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}, {} images", dev.GetGraphicsApiName(), swapchain.GetWidth(), swapchain.GetHeight(), swapchain.GetImageCount());

	std::uint64_t frame = 0;
	while (window.PumpEvents())
	{
		if (frameLimit != 0 && frame >= frameLimit)
		{
			break;
		}

		const rhi::Extent2D size = window.DrawableSize();
		if (size.width == 0 || size.height == 0)
		{
			continue;
		}

		if (size.width != swapchain.GetWidth() || size.height != swapchain.GetHeight())
		{
			static_cast<void>(queue.WaitIdle(error));
			if (!swapchain.Resize(size.width, size.height, error))
			{
				LOG_ERROR(fw::Log(), "failed to resize the swapchain");
				return 1;
			}
			LOG_INFO(fw::Log(), "resized to {}x{}", swapchain.GetWidth(), swapchain.GetHeight());
		}

		const rhi::AcquireResult acquired = swapchain.AcquireNextImage(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			static_cast<void>(queue.WaitIdle(error));
			static_cast<void>(swapchain.Resize(size.width, size.height, error));
			continue;
		}
		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		++frame;

		if (frame > 1 && !pool.Reset(rhi::RetirePoint{ .timeline = timeline, .value = frame - 1 }, error))
		{
			LOG_ERROR(fw::Log(), "failed to reset the command pool");
			return 1;
		}

		rhi::CommandList list = pool.Allocate("present.frame", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			LOG_ERROR(fw::Log(), "failed to start recording");
			return 1;
		}

		const rhi::TextureHandle backBuffer = acquired.texture;
		const std::array toAttachment{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .use = rhi::ResourceUse::eDiscard, .stages = rhi::Stage::eColorOutput },
			.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		} };
		const std::array toPresent{ rhi::TextureBarrier{
			.texture = backBuffer,
			.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.after	 = { .use = rhi::ResourceUse::ePresent },
		} };
		const std::array colors{ rhi::RenderingAttachment{
			.view		= acquired.view,
			.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = FrameColor(frame),
		} };

		const bool recorded =
			list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
			list.BeginRendering(rhi::BeginRenderingDesc{ .colors = colors, .width = swapchain.GetWidth(), .height = swapchain.GetHeight() }, error) &&
			list.EndRendering(error) && list.Barriers(rhi::BarrierBatch{ .textures = toPresent }, error) && list.End(error);
		if (!recorded)
		{
			LOG_ERROR(fw::Log(), "failed to record the frame: {}", error.message != nullptr ? error.message : "no diagnostic");
			return 1;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array present{ rhi::SwapchainSync{ .acquired = acquired.imageAvailable, .renderFinished = acquired.renderFinished } };
		const std::array retire{ rhi::TimelinePoint{ .timeline = timeline, .value = frame } };

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = retire,
			.swapchains	  = present,
			.debugName	  = "present.submit",
		};

		if (!queue.Submit(submit, error))
		{
			LOG_ERROR(fw::Log(), "failed to submit the frame");
			return 1;
		}

		static_cast<void>(swapchain.Present(queue, acquired.imageIndex, acquired.renderFinished, error));

		if (!queue.Wait(timeline, frame, kNoTimeout, error))
		{
			LOG_ERROR(fw::Log(), "failed to wait for the frame");
			return 1;
		}
	}

	static_cast<void>(queue.WaitIdle(error));
	dev.CollectGarbage(timeline, frame, error);

	LOG_INFO(fw::Log(), "{} frames presented", frame);
	return 0;
}

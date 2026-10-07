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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/frame_ring.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/imgui/renderer.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include "FW/platform/Sdl3Window.hpp"
#include "FW/utility/AssetPath.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"
#include "tracy_lifetime.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{
	constexpr std::uint64_t kNoTimeout		= std::numeric_limits<std::uint64_t>::max();
	constexpr std::uint32_t kFramesInFlight = 2;

	constexpr std::uint32_t kMaxTextures = 8;

	[[nodiscard]] bool CanDraw(const rhi::GraphicsApiId api)
	{
		return api == rhi::VulkanApi::kId || api == rhi::D3D12Api::kId || rhi::is_metal_family(api);
	}

	void ResizeToWindow(fw::platform::Sdl3Window & window, rhi::Swapchain & swapchain, rhi::Queue & queue, rhi::Error & error)
	{
		const rhi::Extent2D size = window.GetDrawableSize();

		static_cast<void>(queue.wait_idle(error));
		static_cast<void>(swapchain.resize(size.width, size.height, error));
	}

	[[nodiscard]] bool RecordFrame(
		rhi::CommandList & list,
		const rhi::Swapchain & swapchain,
		const rhi::AcquireResult & acquired,
		rhi::imgui::Renderer & renderer,
		const ImDrawData & drawData,
		const std::uint32_t slot,
		rhi::Error & error
	)
	{
		const std::array toAttachment{
			rhi::TextureBarrier{
				.texture = acquired.texture,
				.before	 = { .use = rhi::ResourceUse::eDiscard, .stages = rhi::Stage::eColorOutput },
				.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			},
		};

		const std::array toPresent{
			rhi::TextureBarrier{
				.texture = acquired.texture,
				.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.after	 = { .use = rhi::ResourceUse::ePresent },
			},
		};

		const std::array colors{
			rhi::RenderingAttachment{
				.view		= acquired.view,
				.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load		= rhi::LoadOp::eClear,
				.store		= rhi::StoreOp::eStore,
				.clearColor = rhi::ClearColor{ .r = 0.09f, .g = 0.10f, .b = 0.12f, .a = 1.0f },
			},
		};

		return list.begin(error) && renderer.UpdateTextures(list, drawData, slot, error) &&
			   list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
			   list.begin_rendering(rhi::BeginRenderingDesc{ .colors = colors, .width = swapchain.get_width(), .height = swapchain.get_height() }, error) &&
			   renderer.Record(list, drawData, slot, error) && list.end_rendering(error) && list.barriers(rhi::BarrierBatch{ .textures = toPresent }, error) &&
			   list.end(error);
	}

}

int main(int argc, char ** argv)
{
	const azo::rhi::support::TracyLifetime tracyLifetime;

	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const std::uint64_t frameLimit = args.size() > 1 ? std::strtoull(args[1], nullptr, 10) : 0;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };

	rhi::GraphicsApiId api{};
	for (const rhi::BackendInfo & backend : backends.preferred())
	{
		if (backend.supportsSurfaces && CanDraw(backend.id))
		{
			api = backend.id;
			break;
		}
	}

	if (!CanDraw(api))
	{
		LOG_ERROR(fw::Log(), "this build has no backend that can draw");
		return fw::kSkipExitCode;
	}

	fw::platform::Sdl3Window window;
	if (!window.Open(api, fw::platform::Sdl3WindowDesc{ .title = "AzothRHI imgui_overlay", .width = 1280, .height = 720 }))
	{
		return 1;
	}

	const rhi::HostUniquePtr<rhi::PresentationBackend> presentation = rhi::make_presentation_backend(api);
	if (presentation == nullptr || !presentation->init_instance_loader(window))
	{
		LOG_ERROR(fw::Log(), "this build cannot present through the backend it picked");
		return 1;
	}

	const rhi::Result<rhi::UniqueDevice> device = rhi::DeviceBuilder()
													  .debug_name("imgui_overlay")
													  .graphics_queue()
													  .validation(rhi::ValidationMode::eDeveloper)
													  .build(backends.registry(), backends.preferred_apis().first(1));
	if (!device)
	{
		return fw::ReportNoDevice(device.get_error());
	}

	rhi::Device dev = device.value().get();
	rhi::Error error{};

	rhi::Queue queue				 = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	const rhi::SurfaceHandle surface = presentation->create_surface(window, dev);
	const rhi::Extent2D initial		 = window.GetDrawableSize();

	rhi::Swapchain swapchain = dev.create_swapchain(
		rhi::SwapchainDesc{
			.surface   = surface,
			.width	   = initial.width,
			.height	   = initial.height,
			.debugName = "imgui.swapchain",
		},
		error
	);

	if (surface.value == 0 || !queue.is_valid() || !swapchain.is_valid())
	{
		fw::ReportError("failed to set up presentation", error);
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}", dev.get_graphics_api_name(), swapchain.get_width(), swapchain.get_height());

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL3_InitForOther(window.GetHandle()))
	{
		LOG_ERROR(fw::Log(), "ImGui's SDL3 backend did not start");
		return 1;
	}

	rhi::DescriptorArena arena = dev.create_descriptor_arena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= kMaxTextures,
			.maxDescriptors = kMaxTextures * 2,
			.debugName		= "imgui.arena",
		},
		error
	);

	if (!arena.is_valid())
	{
		fw::ReportError("failed to create the descriptor arena", error);
		return 1;
	}

	rhi::Result<rhi::imgui::Renderer> made = rhi::imgui::Renderer::Create(
		dev,
		rhi::imgui::RendererDesc{
			.arena			= &arena,
			.colorFormat	= swapchain.get_format(),
			.framesInFlight = kFramesInFlight,
			.debugName		= "imgui",
		}
	);

	if (!made)
	{
		fw::ReportError("failed to create the ImGui renderer", made.get_error());
		return 1;
	}

	rhi::imgui::Renderer & renderer = made.value();

	rhi::FrameRing ring = rhi::FrameRing::create(dev, queue, rhi::FrameRingDesc{ .framesInFlight = kFramesInFlight, .debugName = "imgui.ring" }, error);
	if (!ring.is_valid())
	{
		fw::ReportError("failed to create the frame ring", error);
		return 1;
	}

	std::uint64_t drawnVertices = 0;
	std::uint64_t drawnIndices	= 0;

	while (window.PumpEvents(
		[](const SDL_Event & event)
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
		}
	))
	{
		if (frameLimit != 0 && ring.frame_index() >= frameLimit)
		{
			break;
		}

		if (window.TakeResized())
		{
			ResizeToWindow(window, swapchain, queue, error);
		}

		const rhi::AcquireResult acquired = swapchain.acquire_next_image(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			ResizeToWindow(window, swapchain, queue, error);
			continue;
		}

		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		ImGui::ShowDemoWindow();
		ImGui::Render();

		const ImDrawData & drawData = *ImGui::GetDrawData();
		drawnVertices += static_cast<std::uint64_t>(drawData.TotalVtxCount);
		drawnIndices += static_cast<std::uint64_t>(drawData.TotalIdxCount);

		rhi::CommandList list = ring.begin(error);
		if (!list.is_valid() || !RecordFrame(list, swapchain, acquired, renderer, drawData, ring.slot_index(), error))
		{
			fw::ReportError("failed to record the frame", error);
			return 1;
		}

		static_cast<void>(renderer.Retire(ring.retire(), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array present{ rhi::SwapchainSync{ .acquired = acquired.imageAvailable, .renderFinished = acquired.renderFinished } };
		const std::array retire{ ring.signal() };

		if (!queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = retire, .swapchains = present, .debugName = "imgui.submit" }, error))
		{
			fw::ReportError("failed to submit the frame", error);
			return 1;
		}

		static_cast<void>(swapchain.present(queue, acquired.imageIndex, acquired.renderFinished, error));
	}

	static_cast<void>(queue.wait_idle(error));

	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	const rhi::ValidationMessageCounts validation = dev.get_validation_message_counts();

	LOG_INFO(fw::Log(), "presented {} frames, {} vertices and {} indices", ring.frame_index(), drawnVertices, drawnIndices);
	LOG_INFO(fw::Log(), "validation: {} errors, {} warnings", validation.errors, validation.warnings);

	return validation.errors == 0 ? 0 : 1;
}

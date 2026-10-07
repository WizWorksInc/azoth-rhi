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
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"

#include "FW/platform/Sdl3Window.hpp"
#include "FW/utility/AssetPath.hpp"
#include "FW/utility/Log.hpp"

#include <SDL3/SDL.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	struct Backend final
	{
		rhi::GraphicsApiId api{};
		SlangCompileTarget slang			 = SLANG_TARGET_UNKNOWN;
		const char * profile				 = "";
		rhi::ShaderBinaryFormat binaryFormat = rhi::ShaderBinaryFormat::eBackendNative;

		bool keepsEntryPointName = false;
	};

	[[nodiscard]] Slang::ComPtr<slang::IBlob> CompileStage(slang::ISession & session, slang::IModule & module, const char * entryPointName)
	{
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		if (SLANG_FAILED(module.findEntryPointByName(entryPointName, entryPoint.writeRef())))
		{
			LOG_INFO(fw::Log(), "the shader has no entry point called {}", entryPointName);
			return {};
		}

		const std::array<slang::IComponentType *, 2> parts{ &module, entryPoint.get() };

		Slang::ComPtr<slang::IBlob> diagnostics;
		Slang::ComPtr<slang::IComponentType> composed;
		Slang::ComPtr<slang::IComponentType> linked;
		Slang::ComPtr<slang::IBlob> code;

		if (SLANG_FAILED(
				session.createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composed.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			LOG_INFO(fw::Log(), "{}", diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile");
			return {};
		}

		return code;
	}

	[[nodiscard]] const Backend * TargetFor(const rhi::GraphicsApiId api)
	{
		static const std::array targets{
			Backend{ .api = rhi::VulkanApi::kId, .slang = SLANG_SPIRV, .profile = "spirv_1_5", .binaryFormat = rhi::ShaderBinaryFormat::eSpirV },
			Backend{ .api = rhi::D3D12Api::kId, .slang = SLANG_DXIL, .profile = "sm_6_0", .binaryFormat = rhi::ShaderBinaryFormat::eDxil },
			Backend{ .api			 = rhi::MetalApi::kId,
				.slang				 = SLANG_METAL_LIB,
				.profile			 = "metallib_2_4",
				.binaryFormat		 = rhi::ShaderBinaryFormat::eBackendNative,
				.keepsEntryPointName = true },
		};

		// NOLINTNEXTLINE(readability-qualified-auto): libc++ makes this array iterator a raw pointer and MSVC does not, so auto * here builds on one and not the other.

		const rhi::GraphicsApiId target = api == rhi::Metal4Api::kId ? rhi::MetalApi::kId : api;

		const auto found = std::ranges::find(targets, target, &Backend::api);
		return found != targets.end() ? &*found : nullptr;
	}

}

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	const std::uint64_t frameLimit = args.size() > 1 ? std::strtoull(args[1], nullptr, 10) : 0;

	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };

	Backend chosen{};
	for (const rhi::BackendInfo & backend : backends.preferred())
	{
		if (const Backend * target = TargetFor(backend.id); target != nullptr)
		{
			chosen = *target;
			break;
		}
	}

	if (chosen.slang == SLANG_TARGET_UNKNOWN)
	{
		LOG_INFO(fw::Log(), "this build has no backend that can draw");
		return 1;
	}

	const rhi::GraphicsApiId api = chosen.api;

	fw::platform::Sdl3Window window;
	if (!window.Open(api, fw::platform::Sdl3WindowDesc{ .title = "AzothRHI hello_triangle", .width = 1280, .height = 720 }))
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
													  .debug_name("hello_triangle")
													  .graphics_queue()
													  .require_feature(rhi::DeviceFeature::eShaderDrawParameters)
													  .build(backends.registry(), backends.preferred_apis().first(1));
	if (!device)
	{
		LOG_ERROR(fw::Log(), "failed to create a device: {}", device.get_error().message != nullptr ? device.get_error().message : "no diagnostic");
		return 1;
	}

	rhi::Device dev = device.value().get();
	rhi::Error error{};

	const rhi::SurfaceHandle surface = presentation->create_surface(window, dev);
	const rhi::Extent2D initial		 = window.GetDrawableSize();

	rhi::Swapchain swapchain = dev.create_swapchain(
		rhi::SwapchainDesc{
			.surface   = surface,
			.width	   = initial.width,
			.height	   = initial.height,
			.debugName = "triangle.swapchain",
		},
		error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "triangle.timeline" }, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "triangle.pool" }, error);
	if (surface.value == 0 || !swapchain.is_valid() || !queue.is_valid() || !timeline.is_valid() || !pool.is_valid())
	{
		LOG_ERROR(fw::Log(), "failed to set up presentation: {}", error.message != nullptr ? error.message : "no diagnostic");
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}", dev.get_graphics_api_name(), swapchain.get_width(), swapchain.get_height());

	Slang::ComPtr<slang::IGlobalSession> globalSession;
	Slang::ComPtr<slang::ISession> session;
	Slang::ComPtr<slang::IBlob> diagnostics;

	slang::TargetDesc targetDesc{};
	slang::SessionDesc sessionDesc{};

	if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
	{
		LOG_ERROR(fw::Log(), "could not start Slang");
		return 1;
	}

	targetDesc.format		= chosen.slang;
	targetDesc.profile		= globalSession->findProfile(chosen.profile);
	sessionDesc.targets		= &targetDesc;
	sessionDesc.targetCount = 1;

	if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef())))
	{
		LOG_ERROR(fw::Log(), "could not open a Slang session for {}", chosen.profile);
		return 1;
	}

	std::string shaderError;
	const std::string source = fw::util::LoadTextAsset("hello_triangle/shaders/triangle.slang", shaderError);
	if (source.empty())
	{
		LOG_ERROR(fw::Log(), "{}", shaderError);
		return 1;
	}

	slang::IModule * module = session->loadModuleFromSourceString("triangle", "triangle.slang", source.c_str(), diagnostics.writeRef());
	if (module == nullptr)
	{
		LOG_INFO(fw::Log(), "{}", diagnostics != nullptr ? static_cast<const char *>(diagnostics->getBufferPointer()) : "the shader did not compile");
		return 1;
	}

	const Slang::ComPtr<slang::IBlob> vertex   = CompileStage(*session, *module, "vertexMain");
	const Slang::ComPtr<slang::IBlob> fragment = CompileStage(*session, *module, "fragmentMain");
	if (vertex == nullptr || fragment == nullptr)
	{
		return 1;
	}

	LOG_INFO(fw::Log(), "shaders: {} and {} bytes of {}", vertex->getBufferSize(), fragment->getBufferSize(), chosen.profile);

	const std::array shaders{
		rhi::ShaderBinary{
			.stage		= rhi::ShaderStage::eVertex,
			.format		= chosen.binaryFormat,
			.data		= vertex->getBufferPointer(),
			.size		= vertex->getBufferSize(),
			.entryPoint = chosen.keepsEntryPointName ? "vertexMain" : "main",
		},
		rhi::ShaderBinary{
			.stage		= rhi::ShaderStage::eFragment,
			.format		= chosen.binaryFormat,
			.data		= fragment->getBufferPointer(),
			.size		= fragment->getBufferSize(),
			.entryPoint = chosen.keepsEntryPointName ? "fragmentMain" : "main",
		},
	};

	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.layout			 = dev.create_pipeline_layout(rhi::PipelineLayoutDesc{ .debugName = "triangle.layout" }, error);
	pipelineDesc.shaders		 = shaders;
	pipelineDesc.raster.cullMode = rhi::CullMode::eNone;

	rhi::VertexInputDesc vertexInput{};
	pipelineDesc.vertexInput = &vertexInput;

	pipelineDesc.renderTarget.colorFormats.at(0) = swapchain.get_format();
	pipelineDesc.renderTarget.colorFormatCount	 = 1;

	pipelineDesc.blend.attachmentCount = 1;

	pipelineDesc.dynamicStates = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
	pipelineDesc.debugName	   = "triangle.pipeline";

	const rhi::GraphicsPipelineHandle pipeline = dev.create_graphics_pipeline(pipelineDesc, error);
	if (!pipeline.is_valid())
	{
		LOG_ERROR(fw::Log(), "failed to create the pipeline: {}", error.message != nullptr ? error.message : "no diagnostic");
		return 1;
	}

	std::uint64_t frame = 0;
	while (window.PumpEvents())
	{
		if (frameLimit != 0 && frame >= frameLimit)
		{
			break;
		}

		const rhi::AcquireResult acquired = swapchain.acquire_next_image(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			const rhi::Extent2D size = window.GetDrawableSize();
			static_cast<void>(queue.wait_idle(error));
			static_cast<void>(swapchain.resize(size.width, size.height, error));
			continue;
		}
		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		++frame;

		if (frame > 1 && !pool.reset(rhi::RetirePoint{ .timeline = timeline, .value = frame - 1 }, error))
		{
			LOG_ERROR(fw::Log(), "failed to reset the command pool");
			return 1;
		}

		rhi::CommandList list = pool.allocate("triangle.frame", error);
		if (!list.is_valid() || !list.begin(error))
		{
			LOG_ERROR(fw::Log(), "failed to start recording");
			return 1;
		}

		const rhi::TextureHandle backBuffer = acquired.texture;
		const std::array toAttachment{
			rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = { .use = rhi::ResourceUse::eDiscard, .stages = rhi::Stage::eColorOutput },
				.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			},
		};

		const std::array toPresent{
			rhi::TextureBarrier{
				.texture = backBuffer,
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
				.clearColor = rhi::ClearColor{ .r = 0.02f, .g = 0.02f, .b = 0.06f, .a = 1.0f },
			},
		};

		const rhi::Viewport viewport{ .width = static_cast<float>(swapchain.get_width()), .height = static_cast<float>(swapchain.get_height()) };
		const rhi::Rect2D scissor{ .width = swapchain.get_width(), .height = swapchain.get_height() };

		const bool recorded =
			list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
			list.begin_rendering(rhi::BeginRenderingDesc{ .colors = colors, .width = swapchain.get_width(), .height = swapchain.get_height() }, error) &&
			list.set_graphics_pipeline(pipeline, error) && list.set_viewport(viewport, error) && list.set_scissor(scissor, error) && list.draw(3, 1, 0, 0, error) &&
			list.end_rendering(error) && list.barriers(rhi::BarrierBatch{ .textures = toPresent }, error) && list.end(error);

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
			.debugName	  = "triangle.submit",
		};

		if (!queue.submit(submit, error))
		{
			LOG_ERROR(fw::Log(), "failed to submit the frame");
			return 1;
		}

		static_cast<void>(swapchain.present(queue, acquired.imageIndex, acquired.renderFinished, error));

		if (!queue.wait(timeline, frame, kNoTimeout, error))
		{
			LOG_ERROR(fw::Log(), "failed to wait for the frame");
			return 1;
		}
	}

	static_cast<void>(queue.wait_idle(error));
	dev.collect_garbage(timeline, frame, error);

	LOG_INFO(fw::Log(), "{} frames presented", frame);
	return 0;
}

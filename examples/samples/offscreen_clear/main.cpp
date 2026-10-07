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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"
#include "tracy_lifetime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint> // NOLINT
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace rhi = azo::rhi;

namespace
{
	const char * Yes(const bool value)
	{
		return value ? "yes" : "no";
	}

} // namespace

namespace
{

	constexpr std::uint32_t kExtent = 64;

	constexpr std::uint64_t kReadbackBytes = static_cast<std::uint64_t>(kExtent) * kExtent * 4;
	constexpr std::uint64_t kNoTimeout	   = std::numeric_limits<std::uint64_t>::max();

	constexpr rhi::ClearColor kClear{ .r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f };

	std::uint8_t Quantize(const float channel)
	{
		return static_cast<std::uint8_t>(std::lround(channel * 255.0f));
	}

} // namespace

int main(int argc, char ** argv)
{
	const azo::rhi::support::TracyLifetime tracyLifetime;

	const char * requested = fw::RequestedBackend(argc, argv);

	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.honored_request())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	rhi::DeviceDesc deviceDesc{};
	deviceDesc.requireSwapchain = false;
	deviceDesc.debugName		= "offscreen_clear";

	const rhi::Result<rhi::UniqueDevice> device = backends.create_device(deviceDesc);
	if (!device)
	{
		return fw::ReportNoDevice(device.get_error());
	}

	rhi::Device dev = device.value().get();
	LOG_INFO(fw::Log(), "backend: {}", dev.get_graphics_api_name());

	rhi::Error error{};

	const rhi::TextureDesc targetDesc{
		.type	   = rhi::TextureType::eTex2D,
		.format	   = rhi::Format::eRGBA8UNorm,
		.width	   = kExtent,
		.height	   = kExtent,
		.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eColorAttachment) | rhi::TextureUsage::eCopySrc,
		.debugName = "example.colorTarget",
	};

	const rhi::TextureHandle target	  = dev.create_texture(targetDesc, error);
	const rhi::TextureViewHandle view = dev.create_texture_view(target, rhi::TextureViewDesc{ .debugName = "example.colorTargetView" }, error);

	const rhi::BufferDesc readbackDesc{
		.size	   = kReadbackBytes,
		.usage	   = rhi::BufferUsage::eCopyDst,
		.memory	   = rhi::MemoryUsage::eCpuReadback,
		.debugName = "example.readback",
	};
	const rhi::BufferHandle readback = dev.create_buffer(readbackDesc, error);

	const rhi::TimelineHandle timeline = dev.create_timeline(rhi::TimelineDesc{ .debugName = "example.timeline" }, error);
	rhi::Queue queue				   = dev.get_queue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.create_command_pool(rhi::CommandPoolDesc{ .debugName = "example.pool" }, error);
	if (!target.is_valid() || !view.is_valid() || !readback.is_valid() || !timeline.is_valid() || !queue.is_valid() || !pool.is_valid())
	{
		fw::ReportError("failed to create the render resources", error);
		return 1;
	}

	rhi::CommandList list = pool.allocate("example.clear", error);
	if (!list.is_valid() || !list.begin(error))
	{
		fw::ReportError("failed to begin recording", error);
		return 1;
	}

	const std::array toAttachment{ rhi::TextureBarrier{
		.texture = target,
		.before	 = { .use = rhi::ResourceUse::eDiscard },
		.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
	} };
	const std::array toCopySource{ rhi::TextureBarrier{
		.texture = target,
		.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
	} };

	const std::array colors{ rhi::RenderingAttachment{
		.view		= view,
		.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
		.load		= rhi::LoadOp::eClear,
		.store		= rhi::StoreOp::eStore,
		.clearColor = kClear,
	} };
	const rhi::BeginRenderingDesc rendering{
		.colors = colors,
		.width	= kExtent,
		.height = kExtent,
	};

	const std::array regions{ rhi::BufferTextureCopy{
		.subresource   = { .aspects = rhi::TextureAspect::eColor },
		.textureExtent = { .width = kExtent, .height = kExtent, .depth = 1 },
	} };

	if (!list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error))
	{
		fw::ReportError("failed to record the attachment barrier", error);
		return 1;
	}

	if (!list.begin_rendering(rendering, error))
	{
		fw::ReportError("this backend refused the rendering scope", error);
		return 1;
	}

	list.end_rendering(error);

	const bool recorded =
		list.barriers(rhi::BarrierBatch{ .textures = toCopySource }, error) && list.copy_texture_to_buffer(readback, target, regions, error) && list.end(error);
	if (!recorded)
	{
		fw::ReportError("failed to record the readback copy", error);
		return 1;
	}

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	const rhi::SubmitDesc submit{
		.commandLists = lists,
		.signals	  = signals,
		.debugName	  = "example.clearSubmit",
	};

	if (!queue.submit(submit, error) || !queue.wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("failed to submit the clear", error);
		return 1;
	}

	int status					   = 0;
	const rhi::MappedMemory mapped = dev.map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		LOG_ERROR(fw::Log(), "note: this backend exposes no mappable memory, so the pixels cannot be checked");
	}
	else
	{
		if (!mapped.coherent && !dev.invalidate_mapped_range(readback, 0, kReadbackBytes, error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return 1;
		}

		std::array<std::uint8_t, 4> texel{};
		std::memcpy(texel.data(), mapped.data, texel.size());
		static_cast<void>(dev.unmap(readback, error));

		const std::array<std::uint8_t, 4> expected{ Quantize(kClear.r), Quantize(kClear.g), Quantize(kClear.b), Quantize(kClear.a) };
		LOG_INFO(
			fw::Log(),
			"cleared to   {} {} {} {}",
			static_cast<int>(expected[0]),
			static_cast<int>(expected[1]),
			static_cast<int>(expected[2]),
			static_cast<int>(expected[3])
		);
		LOG_INFO(
			fw::Log(),
			"read back    {} {} {} {}",
			static_cast<int>(texel[0]),
			static_cast<int>(texel[1]),
			static_cast<int>(texel[2]),
			static_cast<int>(texel[3])
		);

		for (std::size_t channel = 0; channel < texel.size(); ++channel)
		{
			const int difference = static_cast<int>(texel[channel]) - static_cast<int>(expected[channel]);
			if (difference > 1 || difference < -1)
			{
				LOG_INFO(fw::Log(), "channel {} does not match the clear color", channel);
				status = 1;
			}
		}
	}

	const rhi::DestroyDesc retired{
		.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
		.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
	};
	dev.destroy(readback, retired, error);
	dev.destroy(view, retired, error);
	dev.destroy(target, retired, error);
	dev.collect_garbage(timeline, 1, error);
	dev.destroy(timeline, {}, error);

	return status;
}

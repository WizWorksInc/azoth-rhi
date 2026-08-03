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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

	// Four bytes a texel and 64 of them a row, which lands on the 256 byte row pitch D3D12 wants from a texture copy. A width that did not would need the
	// padded pitch spelled out per row.
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
	const char * requested = fw::RequestedBackend(argc, argv);

	// Every backend this build has, registered, with the requested one first. What that set is was settled when the library was compiled so this sample never
	// has to ask.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested } };
	if (requested != nullptr && !backends.HonoredRequest())
	{
		LOG_INFO(fw::Log(), "note: this build has no {} backend, using what it does have", requested);
	}

	rhi::DeviceDesc deviceDesc{};
	deviceDesc.requireSwapchain = false;
	deviceDesc.debugName		= "offscreen_clear";

	const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(deviceDesc);
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev = device.Value().Get();
	LOG_INFO(fw::Log(), "backend: {}, dynamic rendering: {}", dev.GetGraphicsApiName(), Yes(dev.GetCaps().supportsDynamicRendering));

	rhi::Error error{};

	// eRGBA8UNorm and not an sRGB format so the bytes read back are the clear color and not the clear color through a transfer function.
	const rhi::TextureDesc targetDesc{
		.type	   = rhi::TextureType::eTex2D,
		.format	   = rhi::Format::eRGBA8UNorm,
		.width	   = kExtent,
		.height	   = kExtent,
		.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eColorAttachment) | rhi::TextureUsage::eCopySrc,
		.debugName = "example.colorTarget",
	};

	const rhi::TextureHandle target	  = dev.CreateTexture(targetDesc, error);
	const rhi::TextureViewHandle view = dev.CreateTextureView(target, rhi::TextureViewDesc{ .debugName = "example.colorTargetView" }, error);

	const rhi::BufferDesc readbackDesc{
		.size	   = kReadbackBytes,
		.usage	   = rhi::BufferUsage::eCopyDst,
		.memory	   = rhi::MemoryUsage::eCpuReadback,
		.debugName = "example.readback",
	};
	const rhi::BufferHandle readback = dev.CreateBuffer(readbackDesc, error);

	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "example.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "example.pool" }, error);
	if (!target.IsValid() || !view.IsValid() || !readback.IsValid() || !timeline.IsValid() || !queue.IsValid() || !pool.IsValid())
	{
		fw::ReportError("failed to create the render resources", error);
		return 1;
	}

	rhi::CommandList list = pool.Allocate("example.clear", error);
	if (!list.IsValid() || !list.Begin(error))
	{
		fw::ReportError("failed to begin recording", error);
		return 1;
	}

	const std::array toAttachment{ rhi::TextureBarrier{
		.texture = target,
		.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
		.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
	} };
	const std::array toCopySource{ rhi::TextureBarrier{
		.texture = target,
		.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
		.after	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead, .layout = rhi::TextureLayout::eCopySrc },
	} };

	const std::array colors{ rhi::RenderingAttachment{
		.view		= view,
		.state		= { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
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

	if (!list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error))
	{
		fw::ReportError("failed to record the attachment barrier", error);
		return 1;
	}

	if (!list.BeginRendering(rendering, error))
	{
		fw::ReportError("this backend refused the rendering scope", error);
		return 1;
	}

	// Nothing is drawn. The clear is the whole pass, which is exactly what a load op is for.
	list.EndRendering(error);

	const bool recorded =
		list.Barriers(rhi::BarrierBatch{ .textures = toCopySource }, error) && list.CopyTextureToBuffer(readback, target, regions, error) && list.End(error);
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

	if (!queue.Submit(submit, error) || !queue.Wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("failed to submit the clear", error);
		return 1;
	}

	int status					   = 0;
	const rhi::MappedMemory mapped = dev.Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		// The Null backend has no host visible memory so the pass ran and there is nothing to look at.
		LOG_ERROR(fw::Log(), "note: this backend exposes no mappable memory, so the pixels cannot be checked");
	}
	else
	{
		if (!mapped.coherent && !dev.InvalidateMappedRange(readback, 0, kReadbackBytes, error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return 1;
		}

		std::array<std::uint8_t, 4> texel{};
		std::memcpy(texel.data(), mapped.data, texel.size());
		static_cast<void>(dev.Unmap(readback, error));

		const std::array<std::uint8_t, 4> expected{ Quantize(kClear.r), Quantize(kClear.g), Quantize(kClear.b), Quantize(kClear.a) };
		LOG_INFO(fw::Log(),
			"cleared to   {} {} {} {}",
			static_cast<int>(expected[0]),
			static_cast<int>(expected[1]),
			static_cast<int>(expected[2]),
			static_cast<int>(expected[3]));
		LOG_INFO(fw::Log(),
			"read back    {} {} {} {}",
			static_cast<int>(texel[0]),
			static_cast<int>(texel[1]),
			static_cast<int>(texel[2]),
			static_cast<int>(texel[3]));

		// Within one, since rounding at the edges of a unorm conversion is the backend's business.
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
	dev.Destroy(readback, retired, error);
	dev.Destroy(view, retired, error);
	dev.Destroy(target, retired, error);
	dev.CollectGarbage(timeline, 1, error);
	dev.Destroy(timeline, {}, error);

	return status;
}

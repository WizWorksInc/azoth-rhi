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

/*
 * rhi::raii, the second of the two owning tiers: the device vends owners, and failure arrives as a value.
 *
 * raii::Device::CreateBuffer hands back a Result<raii::Buffer>, either an owner or the reason there is not one, so a setup creating a dozen things is a dozen
 * lines that cannot get the test wrong.
 *
 * A command pool, an arena and a swapchain come back as plain views, the device owning those. So this sample has no Destroy call and no teardown path.
 */

#include "azoth/rhi/builders/resource_builders.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/ownership/raii.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace rhi = azo::rhi;

namespace
{
	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	constexpr std::uint32_t kWidth	= 64;
	constexpr std::uint32_t kHeight = 64;
	constexpr std::uint64_t kBytes	= static_cast<std::uint64_t>(kWidth) * kHeight * 4;

	[[nodiscard]] std::vector<std::uint8_t> MakePattern()
	{
		std::vector<std::uint8_t> pixels(kBytes);
		for (std::size_t index = 0; index < pixels.size(); ++index)
		{
			pixels[index] = static_cast<std::uint8_t>((index * 31) + (index >> 8u));
		}

		return pixels;
	}

	/*
	 * Everything the upload needs, as owners, in the order they have to die in.
	 *
	 * Members are destroyed bottom to top, so the device being first means it is destroyed last. That ordering is the one rule this tier asks for and the one
	 * the compiler will not catch: an owner holds a device view and not a reference to something counted, so a device that went first would leave each of these
	 * destroying through a device that is gone.
	 */
	struct Resources final
	{
		rhi::raii::Device device;

		rhi::raii::Buffer upload;
		rhi::raii::Buffer readback;
		rhi::raii::Texture texture;
		rhi::raii::TextureView view;
		rhi::raii::Sampler sampler;
		rhi::raii::Timeline timeline;
	};

	/*
	 * Creating the lot, with every failure arriving the same way.
	 *
	 * Each step is an assignment out of a Result and a test of that Result. No step names a handle, tests one for validity, or has anything to undo: a return
	 * between any two lines below destroys exactly what was built before it, in reverse.
	 */
	[[nodiscard]] rhi::Result<Resources> Build(const char * requested)
	{
		Resources resources;

		// The owning selection, which vends an owning device. The flat BackendSelection is still underneath and reachable through Get.
		rhi::raii::Selection selection{ rhi::BackendPreference{ .requested = requested } };

		static constexpr std::array kQueues{ rhi::QueueRequest{ .type = rhi::QueueType::eGraphics } };

		rhi::Result<rhi::raii::Device> device = selection.CreateDevice(rhi::DeviceDesc{
			.queues = kQueues,
			// No swapchain wanted, which is what makes this runnable on a machine with no display.
			.requireSwapchain = false,
			.debugName		  = "raii_handles",
		});

		if (!device)
		{
			return device.GetError();
		}

		resources.device = std::move(device.Value());

		rhi::BufferBuilder uploadDesc;
		uploadDesc.Size(kBytes).Usage(rhi::BufferUsage::eCopySrc).CpuUpload().DebugName("raii.upload");

		rhi::Result<rhi::raii::Buffer> upload = resources.device.CreateBuffer(uploadDesc.Build());
		if (!upload)
		{
			return upload.GetError();
		}

		resources.upload = std::move(upload.Value());

		rhi::BufferBuilder readbackDesc;
		readbackDesc.Size(kBytes).Usage(rhi::BufferUsage::eCopyDst).CpuReadback().DebugName("raii.readback");

		rhi::Result<rhi::raii::Buffer> readback = resources.device.CreateBuffer(readbackDesc.Build());
		if (!readback)
		{
			return readback.GetError();
		}

		resources.readback = std::move(readback.Value());

		rhi::TextureBuilder textureDesc;
		textureDesc.Format(rhi::Format::eRGBA8UNorm)
			.Extent(kWidth, kHeight)
			.Usage(rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst | rhi::TextureUsage::eCopySrc)
			.DebugName("raii.texture");

		rhi::Result<rhi::raii::Texture> texture = resources.device.CreateTexture(textureDesc.Build());
		if (!texture)
		{
			return texture.GetError();
		}

		resources.texture = std::move(texture.Value());

		// A view names the texture it looks at, which is why the texture is declared above it and destroyed after it.
		rhi::Result<rhi::raii::TextureView> view =
			resources.device.CreateTextureView(resources.texture.Get(), rhi::TextureViewDesc{ .debugName = "raii.view" });
		if (!view)
		{
			return view.GetError();
		}

		resources.view = std::move(view.Value());

		rhi::Result<rhi::raii::Sampler> sampler = resources.device.CreateSampler(rhi::SamplerDesc{ .debugName = "raii.sampler" });
		if (!sampler)
		{
			return sampler.GetError();
		}

		resources.sampler = std::move(sampler.Value());

		rhi::Result<rhi::raii::Timeline> timeline = resources.device.CreateTimeline(rhi::TimelineDesc{ .debugName = "raii.timeline" });
		if (!timeline)
		{
			return timeline.GetError();
		}

		resources.timeline = std::move(timeline.Value());

		return resources;
	}

	/*
	 * The work: pattern into the upload buffer, upload into the texture, texture back into the readback buffer.
	 *
	 * The pool and the queue below come back as views, not owners, which is the tier being honest. A command pool is reclaimed by the device and a queue was never
	 * created in the first place, so neither has a Destroy to wrap.
	 */
	enum class RoundTripOutcome : std::uint8_t
	{
		eDone,
		eNoHostMemory,
		eFailed,
	};

	[[nodiscard]] RoundTripOutcome RoundTrip(Resources & resources, const std::span<const std::uint8_t> pattern, std::vector<std::uint8_t> & out)
	{
		rhi::Device dev = resources.device.Get();
		rhi::Error error{};

		rhi::Queue queue = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);

		rhi::Result<rhi::raii::CommandPool> pool = resources.device.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "raii.pool" });
		if (!queue.IsValid() || !pool)
		{
			fw::ReportError("failed to set up the submission", queue.IsValid() ? pool.GetError() : error);
			return RoundTripOutcome::eFailed;
		}

		// Host visible memory is the one thing here a backend can genuinely not have, and the Null one does not. Owning the resources is what this sample is about
		// and that part happened either way, so this reports it without failing over it.
		const rhi::MappedMemory mapped = dev.Map(resources.upload.Get(), rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			if (error.code == rhi::ErrorCode::eUnsupportedFeature)
			{
				return RoundTripOutcome::eNoHostMemory;
			}

			fw::ReportError("failed to map the upload buffer", error);
			return RoundTripOutcome::eFailed;
		}

		std::memcpy(mapped.data, pattern.data(), pattern.size());
		if ((!mapped.coherent && !dev.FlushMappedRange(resources.upload.Get(), 0, kBytes, error)) || !dev.Unmap(resources.upload.Get(), error))
		{
			fw::ReportError("failed to flush the upload buffer", error);
			return RoundTripOutcome::eFailed;
		}

		rhi::CommandList list = pool.Value().Allocate("raii.roundTrip", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			fw::ReportError("failed to start recording", error);
			return RoundTripOutcome::eFailed;
		}

		const std::array regions{ rhi::BufferTextureCopy{ .textureExtent = { .width = kWidth, .height = kHeight } } };

		const std::array toCopyDst{
			rhi::TextureBarrier{
				.texture = resources.texture.Get(),
				.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
				.after	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eCopyDst },
			},
		};

		const std::array toCopySrc{
			rhi::TextureBarrier{
				.texture = resources.texture.Get(),
				.before	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eCopyDst },
				.after	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead, .layout = rhi::TextureLayout::eCopySrc },
			},
		};

		const bool recorded = list.Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error) &&
							  list.CopyBufferToTexture(resources.texture.Get(), resources.upload.Get(), regions, error) &&
							  list.Barriers(rhi::BarrierBatch{ .textures = toCopySrc }, error) &&
							  list.CopyTextureToBuffer(resources.readback.Get(), resources.texture.Get(), regions, error) && list.End(error);

		if (!recorded)
		{
			fw::ReportError("failed to record the round trip", error);
			return RoundTripOutcome::eFailed;
		}

		constexpr std::uint64_t kSignalValue = 1;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = resources.timeline.Get(), .value = kSignalValue } };

		if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "raii.submit" }, error) ||
			!queue.Wait(resources.timeline.Get(), kSignalValue, kNoTimeout, error))
		{
			fw::ReportError("failed to run the round trip", error);
			return RoundTripOutcome::eFailed;
		}

		const rhi::MappedMemory read = dev.Map(resources.readback.Get(), rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (read.data == nullptr)
		{
			fw::ReportError("failed to map the readback buffer", error);
			return RoundTripOutcome::eFailed;
		}

		if (!read.coherent && !dev.InvalidateMappedRange(resources.readback.Get(), 0, kBytes, error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return RoundTripOutcome::eFailed;
		}

		out.resize(kBytes);
		std::memcpy(out.data(), read.data, kBytes);
		static_cast<void>(dev.Unmap(resources.readback.Get(), error));

		return RoundTripOutcome::eDone;
	}
} // namespace

int main(int argc, char ** argv)
{
	rhi::Result<Resources> built = Build(fw::RequestedBackend(argc, argv));
	if (!built)
	{
		return fw::ReportNoDevice(built.GetError());
	}

	Resources & resources = built.Value();
	LOG_INFO(fw::Log(), "backend: {}", resources.device.Get().GetGraphicsApiName());
	LOG_INFO(fw::Log(), "built a device, two buffers, a texture, a view, a sampler and a timeline, with no handle named once");

	const std::vector<std::uint8_t> pattern = MakePattern();
	std::vector<std::uint8_t> observed;

	const RoundTripOutcome outcome = RoundTrip(resources, pattern, observed);
	if (outcome == RoundTripOutcome::eFailed)
	{
		return 1;
	}

	if (outcome == RoundTripOutcome::eNoHostMemory)
	{
		LOG_INFO(fw::Log(), "this backend exposes no mappable memory, so the round trip was skipped, but everything above was still owned and released");
	}
	else if (std::ranges::equal(observed, pattern))
	{
		LOG_INFO(fw::Log(), "{} bytes went through the texture and came back unchanged", observed.size());
	}
	else
	{
		LOG_ERROR(fw::Log(), "the readback did not match what was uploaded");
		return 1;
	}

	/*
	 * There is no teardown below this line. Returning destroys Resources, whose members go in reverse order: the timeline, the sampler, the view, the texture, the
	 * two buffers and the device last of all.
	 */
	LOG_INFO(fw::Log(), "returning, which destroys everything in reverse declaration order");

	return 0;
}

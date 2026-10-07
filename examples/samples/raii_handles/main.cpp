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

	[[nodiscard]] rhi::Result<Resources> Build(const char * requested)
	{
		Resources resources;

		rhi::raii::Selection selection{ rhi::BackendPreference{ .requested = requested } };

		static constexpr std::array kQueues{ rhi::QueueRequest{ .type = rhi::QueueType::eGraphics } };

		rhi::Result<rhi::raii::Device> device = selection.create_device(rhi::DeviceDesc{
			.queues			  = kQueues,
			.requireSwapchain = false,
			.debugName		  = "raii_handles",
		});

		if (!device)
		{
			return device.get_error();
		}

		resources.device = std::move(device.Value());

		rhi::BufferBuilder uploadDesc;
		uploadDesc.Size(kBytes).Usage(rhi::BufferUsage::eCopySrc).CpuUpload().DebugName("raii.upload");

		rhi::Result<rhi::raii::Buffer> upload = resources.device.create_buffer(uploadDesc.build());
		if (!upload)
		{
			return upload.get_error();
		}

		resources.upload = std::move(upload.Value());

		rhi::BufferBuilder readbackDesc;
		readbackDesc.Size(kBytes).Usage(rhi::BufferUsage::eCopyDst).CpuReadback().DebugName("raii.readback");

		rhi::Result<rhi::raii::Buffer> readback = resources.device.create_buffer(readbackDesc.build());
		if (!readback)
		{
			return readback.get_error();
		}

		resources.readback = std::move(readback.Value());

		rhi::TextureBuilder textureDesc;
		textureDesc.Format(rhi::Format::eRGBA8UNorm)
			.Extent(kWidth, kHeight)
			.Usage(rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst | rhi::TextureUsage::eCopySrc)
			.DebugName("raii.texture");

		rhi::Result<rhi::raii::Texture> texture = resources.device.create_texture(textureDesc.build());
		if (!texture)
		{
			return texture.get_error();
		}

		resources.texture = std::move(texture.Value());

		rhi::Result<rhi::raii::TextureView> view =
			resources.device.create_texture_view(resources.texture.Get(), rhi::TextureViewDesc{ .debugName = "raii.view" });
		if (!view)
		{
			return view.get_error();
		}

		resources.view = std::move(view.Value());

		rhi::Result<rhi::raii::Sampler> sampler = resources.device.create_sampler(rhi::SamplerDesc{ .debugName = "raii.sampler" });
		if (!sampler)
		{
			return sampler.get_error();
		}

		resources.sampler = std::move(sampler.Value());

		rhi::Result<rhi::raii::Timeline> timeline = resources.device.create_timeline(rhi::TimelineDesc{ .debugName = "raii.timeline" });
		if (!timeline)
		{
			return timeline.get_error();
		}

		resources.timeline = std::move(timeline.Value());

		return resources;
	}

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

		rhi::Queue queue = dev.get_queue(rhi::QueueType::eGraphics, 0, error);

		rhi::Result<rhi::raii::CommandPool> pool = resources.device.create_command_pool(rhi::CommandPoolDesc{ .debugName = "raii.pool" });
		if (!queue.is_valid() || !pool)
		{
			fw::ReportError("failed to set up the submission", queue.is_valid() ? pool.get_error() : error);
			return RoundTripOutcome::eFailed;
		}

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
		if ((!mapped.coherent && !dev.flush_mapped_range(resources.upload.Get(), 0, kBytes, error)) || !dev.Unmap(resources.upload.Get(), error))
		{
			fw::ReportError("failed to flush the upload buffer", error);
			return RoundTripOutcome::eFailed;
		}

		rhi::CommandList list = pool.Value().Allocate("raii.roundTrip", error);
		if (!list.is_valid() || !list.Begin(error))
		{
			fw::ReportError("failed to start recording", error);
			return RoundTripOutcome::eFailed;
		}

		const std::array regions{ rhi::BufferTextureCopy{ .textureExtent = { .width = kWidth, .height = kHeight } } };

		const std::array toCopyDst{
			rhi::TextureBarrier{
				.texture = resources.texture.Get(),
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			},
		};

		const std::array toCopySrc{
			rhi::TextureBarrier{
				.texture = resources.texture.Get(),
				.before	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			},
		};

		const bool recorded = list.barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error) &&
							  list.copy_buffer_to_texture(resources.texture.Get(), resources.upload.Get(), regions, error) &&
							  list.barriers(rhi::BarrierBatch{ .textures = toCopySrc }, error) &&
							  list.copy_texture_to_buffer(resources.readback.Get(), resources.texture.Get(), regions, error) && list.End(error);

		if (!recorded)
		{
			fw::ReportError("failed to record the round trip", error);
			return RoundTripOutcome::eFailed;
		}

		constexpr std::uint64_t kSignalValue = 1;
		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = resources.timeline.Get(), .value = kSignalValue } };

		if (!queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "raii.submit" }, error) ||
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

		if (!read.coherent && !dev.invalidate_mapped_range(resources.readback.Get(), 0, kBytes, error))
		{
			fw::ReportError("failed to invalidate the readback buffer", error);
			return RoundTripOutcome::eFailed;
		}

		out.resize(kBytes);
		std::memcpy(out.data(), read.data, kBytes);
		static_cast<void>(dev.Unmap(resources.readback.Get(), error));

		return RoundTripOutcome::eDone;
	}
}

int main(int argc, char ** argv)
{
	rhi::Result<Resources> built = Build(fw::RequestedBackend(argc, argv));
	if (!built)
	{
		return fw::ReportNoDevice(built.get_error());
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

	LOG_INFO(fw::Log(), "returning, which destroys everything in reverse declaration order");

	return 0;
}

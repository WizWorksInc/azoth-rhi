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
 * A descriptor array with no size in it, the thing a bound renderer cannot do.
 *
 * Four capabilities line up and the sample turns each on by name. BindingTier::eUnbounded declares an array without a length, eVariableDescriptorCount picks
 * the length when the set is allocated, ePartiallyBound lets unread entries stay unwritten, and eUpdateAfterBind lets a descriptor be written after the set is
 * recorded.
 *
 * That last one is worth watching: half the textures here are written after the list binding them closed.
 */

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "FW/shader/SlangCompiler.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/Sample.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	// What the set is allocated to hold. Every one of them is written and read, which is what makes the readback checkable slot by slot.
	constexpr std::uint32_t kTextures = 16;

	/*
	 * What the layout declares, which is deliberately larger than what is allocated.
	 *
	 * An unbounded binding still names an upper bound in the layout, and the allocation picks the real length below it. Keeping the two different is the point:
	 * one layout built once serves however many textures a scene turns out to have.
	 */
	constexpr std::uint32_t kMaxTextures = 1024;

	constexpr std::uint64_t kNoTimeout	 = std::numeric_limits<std::uint64_t>::max();
	constexpr std::uint64_t kOutputBytes = static_cast<std::uint64_t>(kTextures) * 4 * sizeof(float);

	// How many are written before the command list is recorded. The rest go in after it is closed, which is what eUpdateAfterBind is for.
	constexpr std::uint32_t kWrittenEarly = kTextures / 2;

	// What the shader's push constant holds.
	struct Params final
	{
		std::uint32_t count = kTextures;
	};

	/*
	 * The colour slot i is cleared to, which is also what the readback has to contain at i.
	 *
	 * Distinct per slot and away from the ends of the range, so a shader that read the wrong texture produces a wrong number, not a plausible one.
	 */
	[[nodiscard]] rhi::ClearColor ColorFor(const std::uint32_t slot)
	{
		return {
			.r = static_cast<float>(slot) / static_cast<float>(kTextures),
			.g = 1.0f - (static_cast<float>(slot) / static_cast<float>(kTextures)),
			.b = 0.25f,
			.a = 1.0f,
		};
	}

} // namespace

int main(int argc, char ** argv)
{
	const char * requested = fw::RequestedBackend(argc, argv);

	// No Null backend: it declares no binding tier worth indexing into and the readback this checks would be empty.
	rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = requested, .includeNull = false } };

	const rhi::Result<rhi::UniqueDevice> device =
		rhi::DeviceBuilder().DebugName("bindless").Headless().GraphicsQueue().Build(backends.Registry(), backends.PreferredApis());
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev				 = device.Value().Get();
	const rhi::DeviceCaps & caps = dev.GetCaps();
	LOG_INFO(fw::Log(), "backend: {}", dev.GetGraphicsApiName());

	/*
	 * Asked before anything is built, because a device below this tier does not refuse the layout, it refuses the shader that indexes it, and the failure lands
	 * somewhere much less obvious than here.
	 */
	if (caps.bindingTier < rhi::BindingTier::eUnbounded)
	{
		LOG_INFO(fw::Log(), "this device's binding tier is below unbounded, so an array without a size cannot be declared here");
		return fw::kSkipExitCode;
	}
	if (!caps.supportsUpdateAfterBind || !caps.supportsPartiallyBoundDescriptors)
	{
		LOG_INFO(fw::Log(),
			"this device is missing update after bind ({}) or partially bound descriptors ({})",
			caps.supportsUpdateAfterBind ? "yes" : "no",
			caps.supportsPartiallyBoundDescriptors ? "yes" : "no");
		return fw::kSkipExitCode;
	}

	LOG_INFO(fw::Log(), "binding tier unbounded, update after bind and partially bound descriptors all present");

	fw::shader::SlangCompiler compiler;
	std::string why;
	if (!compiler.Open(dev.GetGraphicsApiId(), why))
	{
		LOG_ERROR(fw::Log(), "{}", why);
		return fw::kSkipExitCode;
	}

	const rhi::ShaderBinary gather = compiler.Compile("bindless/shaders/gather.slang", "gatherMain", rhi::ShaderStage::eCompute, why);
	if (gather.data == nullptr)
	{
		LOG_ERROR(fw::Log(), "{}", why);
		return 1;
	}

	rhi::Error error{};

	/*
	 * The layout. The sampler and the output buffer are ordinary bindings and the texture array is not, and the array is last because a binding whose length is
	 * chosen at allocation has to be the trailing one.
	 */
	const std::array bindings{
		rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eSampler, .stages = rhi::ShaderStage::eCompute },
		rhi::DescriptorBinding{ .binding = 1, .type = rhi::DescriptorType::eStorageBuffer, .stages = rhi::ShaderStage::eCompute },
		rhi::DescriptorBinding{
			.binding = 2,
			.type	 = rhi::DescriptorType::eTextureSRV,
			.count	 = kMaxTextures,
			.stages	 = rhi::ShaderStage::eCompute,
			.flags	 = rhi::Flags<rhi::DescriptorBindingFlag>(rhi::DescriptorBindingFlag::eBindless) | rhi::DescriptorBindingFlag::ePartiallyBound |
					 rhi::DescriptorBindingFlag::eUpdateAfterBind | rhi::DescriptorBindingFlag::eVariableDescriptorCount,
		},
	};

	const rhi::DescriptorSetLayoutHandle setLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "bindless.set" }, error);

	const std::array setLayouts{ setLayout };
	const std::array pushConstants{
		rhi::PushConstantRange{ .stages = rhi::ShaderStage::eCompute, .offset = 0, .size = sizeof(Params) },
	};
	const rhi::PipelineLayoutHandle layout =
		dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushConstants, .debugName = "bindless.layout" }, error);

	const rhi::ComputePipelineHandle pipeline =
		dev.CreateComputePipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = gather, .debugName = "bindless.pipeline" }, error);

	if (!setLayout.IsValid() || !layout.IsValid() || !pipeline.IsValid())
	{
		fw::ReportError("the unbounded layout did not build a pipeline", error);
		return 1;
	}

	// One texel each, since what is being checked is which texture a thread reached and not what was in it.
	std::vector<rhi::TextureHandle> textures(kTextures);
	std::vector<rhi::TextureViewHandle> views(kTextures);
	for (std::uint32_t slot = 0; slot < kTextures; ++slot)
	{
		textures.at(slot) = dev.CreateTexture(
			rhi::TextureDesc{
				.type	   = rhi::TextureType::eTex2D,
				.format	   = rhi::Format::eRGBA8UNorm,
				.width	   = 1,
				.height	   = 1,
				// eColorAttachment because these are cleared to tell them apart, and a clear goes through a render target on two of the three backends.
				.usage	   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst | rhi::TextureUsage::eColorAttachment,
				.debugName = "bindless.texture",
			},
			error);
		views.at(slot) = dev.CreateTextureView(textures.at(slot), rhi::TextureViewDesc{ .debugName = "bindless.textureView" }, error);
		if (!textures.at(slot).IsValid() || !views.at(slot).IsValid())
		{
			fw::ReportError("a texture was refused", error);
			return 1;
		}
	}

	const rhi::SamplerHandle sampler = dev.CreateSampler(rhi::SamplerDesc{ .debugName = "bindless.sampler" }, error);

	const rhi::BufferHandle output = dev.CreateBuffer(
		rhi::BufferDesc{
			.size	   = kOutputBytes,
			.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eStorage) | rhi::BufferUsage::eCopySrc,
			.memory	   = rhi::MemoryUsage::eGpuOnly,
			.debugName = "bindless.output",
		},
		error);
	const rhi::BufferHandle readback = dev.CreateBuffer(
		rhi::BufferDesc{
			.size	   = kOutputBytes,
			.usage	   = rhi::BufferUsage::eCopyDst,
			.memory	   = rhi::MemoryUsage::eCpuReadback,
			.debugName = "bindless.readback",
		},
		error);

	if (!sampler.IsValid() || !output.IsValid() || !readback.IsValid())
	{
		fw::ReportError("the sampler or the buffers were refused", error);
		return 1;
	}

	/*
	 * The arena is sized for the array and not for the three bindings, an unbounded binding costing one descriptor per entry it is allocated with.
	 */
	rhi::DescriptorArena arena = dev.CreateDescriptorArena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= 1,
			.maxDescriptors = kTextures + 2,
			.debugName		= "bindless.arena",
		},
		error);

	// The length lands here, below the layout's upper bound, which is what eVariableDescriptorCount is for.
	const rhi::DescriptorSetHandle set = arena.Allocate(
		rhi::DescriptorSetAllocDesc{ .layout = setLayout, .variableDescriptorCount = kTextures, .debugName = "bindless.descriptors" }, error);
	if (!set.IsValid())
	{
		fw::ReportError("the variable length set was refused", error);
		return 1;
	}

	const std::array samplerWrites{ rhi::DescriptorWriteSampler{ .set = set, .binding = 0, .sampler = sampler } };
	const std::array bufferWrites{
		rhi::DescriptorWriteBuffer{ .set = set, .binding = 1, .type = rhi::DescriptorType::eStorageBuffer, .buffer = output, .range = kOutputBytes },
	};
	if (!dev.UpdateDescriptors(std::span(samplerWrites), error) || !dev.UpdateDescriptors(std::span(bufferWrites), error))
	{
		fw::ReportError("the fixed descriptors were refused", error);
		return 1;
	}

	// The first half now. The second half is deliberately left until after the command list is closed.
	std::vector<rhi::DescriptorWriteTexture> earlyWrites;
	earlyWrites.reserve(kWrittenEarly);
	for (std::uint32_t slot = 0; slot < kWrittenEarly; ++slot)
	{
		earlyWrites.push_back(rhi::DescriptorWriteTexture{
			.set		= set,
			.binding	= 2,
			.arrayIndex = slot,
			.type		= rhi::DescriptorType::eTextureSRV,
			.view		= views.at(slot),
		});
	}
	if (!dev.UpdateDescriptors(std::span(earlyWrites), error))
	{
		fw::ReportError("the first half of the array was refused", error);
		return 1;
	}

	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "bindless.timeline" }, error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	rhi::CommandPool pool			   = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "bindless.pool" }, error);
	rhi::CommandList list			   = pool.Allocate("bindless.gather", error);
	if (!timeline.IsValid() || !queue.IsValid() || !list.IsValid() || !list.Begin(error))
	{
		fw::ReportError("the submission objects were refused", error);
		return 1;
	}

	// Every texture gets its colour and then moves to where a shader can read it.
	std::vector<rhi::TextureBarrier> toCopyDst;
	std::vector<rhi::TextureBarrier> toShaderRead;
	toCopyDst.reserve(kTextures);
	toShaderRead.reserve(kTextures);
	for (std::uint32_t slot = 0; slot < kTextures; ++slot)
	{
		toCopyDst.push_back(rhi::TextureBarrier{
			.texture = textures.at(slot),
			.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
			.after	 = { .stages = rhi::PipelineStage::eClear, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eCopyDst },
		});
		toShaderRead.push_back(rhi::TextureBarrier{
			.texture = textures.at(slot),
			.before	 = { .stages = rhi::PipelineStage::eClear, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eCopyDst },
			.after	 = { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderRead, .layout = rhi::TextureLayout::eShaderReadOnly },
		});
	}

	const std::array outputToWrite{
		rhi::BufferBarrier{
			.buffer = output,
			.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
			.after	= { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
		},
	};
	const std::array outputToCopy{
		rhi::BufferBarrier{
			.buffer = output,
			.before = { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite },
			.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyRead },
		},
	};

	bool recorded = list.Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error);

	const std::array wholeTexture{ rhi::TextureSubresourceRange{ .aspects = rhi::TextureAspect::eColor } };
	for (std::uint32_t slot = 0; slot < kTextures && recorded; ++slot)
	{
		recorded = list.ClearTexture(textures.at(slot), ColorFor(slot), wholeTexture, error);
	}

	const Params params{ .count = kTextures };

	recorded = recorded && list.Barriers(rhi::BarrierBatch{ .buffers = outputToWrite, .textures = toShaderRead }, error) &&
			   list.SetComputePipeline(pipeline, error) && list.BindDescriptorSet(layout, 0, set, {}, error) &&
			   list.PushConstants(layout, rhi::ShaderStage::eCompute, 0, sizeof(params), &params, error) &&
			   list.Dispatch(kTextures / gather.threadgroupSize.x, 1, 1, error) && list.Barriers(rhi::BarrierBatch{ .buffers = outputToCopy }, error) &&
			   list.CopyBuffer(readback, 0, output, 0, kOutputBytes, error) && list.End(error);

	if (!recorded)
	{
		fw::ReportError("recording failed", error);
		return 1;
	}

	/*
	 * The second half of the array, written now: the set is already bound inside a closed command list and the descriptors it will read are only being filled
	 * in here. Without eUpdateAfterBind on the binding this is undefined behaviour, not a slower path.
	 */
	std::vector<rhi::DescriptorWriteTexture> lateWrites;
	lateWrites.reserve(kTextures - kWrittenEarly);
	for (std::uint32_t slot = kWrittenEarly; slot < kTextures; ++slot)
	{
		lateWrites.push_back(rhi::DescriptorWriteTexture{
			.set		= set,
			.binding	= 2,
			.arrayIndex = slot,
			.type		= rhi::DescriptorType::eTextureSRV,
			.view		= views.at(slot),
		});
	}
	if (!dev.UpdateDescriptors(std::span(lateWrites), error))
	{
		fw::ReportError("the second half of the array was refused after the list was recorded", error);
		return 1;
	}

	LOG_INFO(fw::Log(), "{} of {} textures were written into the set after the command list was closed", lateWrites.size(), kTextures);

	std::array<const rhi::CommandList *, 1> lists{ &list };
	const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
	if (!queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "bindless.submit" }, error) ||
		!queue.Wait(timeline, 1, kNoTimeout, error))
	{
		fw::ReportError("the dispatch did not complete", error);
		return 1;
	}

	const rhi::MappedMemory mapped = dev.Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
	if (mapped.data == nullptr)
	{
		fw::ReportError("the readback could not be mapped", error);
		return 1;
	}

	if (!mapped.coherent && !dev.InvalidateMappedRange(readback, 0, kOutputBytes, error))
	{
		fw::ReportError("the readback could not be invalidated", error);
		return 1;
	}

	std::array<float, kTextures * 4> gathered{};
	std::memcpy(gathered.data(), mapped.data, kOutputBytes);
	static_cast<void>(dev.Unmap(readback, error));

	/*
	 * Slot by slot, because the interesting failure is not an empty buffer, it is every thread reading entry zero. That produces a full buffer of one plausible
	 * colour, which only a per slot check catches.
	 */
	int status			= 0;
	std::uint32_t wrong = 0;
	for (std::uint32_t slot = 0; slot < kTextures; ++slot)
	{
		const rhi::ClearColor expected = ColorFor(slot);
		const std::size_t base		   = static_cast<std::size_t>(slot) * 4;

		// One part in 255, the textures being eight bits a channel.
		const float tolerance = 1.5f / 255.0f;
		if (std::abs(gathered.at(base) - expected.r) > tolerance || std::abs(gathered.at(base + 1) - expected.g) > tolerance)
		{
			LOG_ERROR(fw::Log(), "slot {} came back {:.3f},{:.3f} and was cleared to {:.3f},{:.3f}", slot, gathered.at(base), gathered.at(base + 1),
				expected.r, expected.g);
			wrong += 1;
		}
	}

	if (wrong != 0)
	{
		LOG_ERROR(fw::Log(), "{} of {} slots read the wrong texture", wrong, kTextures);
		status = 1;
	}
	else
	{
		LOG_INFO(fw::Log(), "all {} slots read their own texture through one binding, half of them bound after recording", kTextures);
	}

	const rhi::DestroyDesc retired{
		.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
		.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 1 },
	};
	dev.Destroy(readback, retired, error);
	dev.Destroy(output, retired, error);
	dev.Destroy(sampler, retired, error);
	for (std::uint32_t slot = 0; slot < kTextures; ++slot)
	{
		dev.Destroy(views.at(slot), retired, error);
		dev.Destroy(textures.at(slot), retired, error);
	}
	dev.CollectGarbage(timeline, 1, error);
	dev.Destroy(timeline, {}, error);

	return status;
}

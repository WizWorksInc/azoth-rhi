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

#include "azoth/rhi/utils/resampler.hpp"

#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#ifdef AZOTH_RHI_UTILS_HAVE_SPIRV
	#include "azoth/rhi/utils/resample_spirv.hpp"
#endif
#ifdef AZOTH_RHI_UTILS_HAVE_DXIL
	#include "azoth/rhi/utils/resample_dxil.hpp"
#endif
#ifdef AZOTH_RHI_UTILS_HAVE_METALLIB
	#include "azoth/rhi/utils/resample_metallib.hpp"
#endif

#include <algorithm>
#include <array>
#include <utility>

namespace azo::rhi::utils
{
	namespace
	{
		// The shader's [numthreads], which the dispatch below has to agree with and Metal has to be told separately.
		constexpr std::uint32_t kGroupSize = 8;

		constexpr std::uint32_t kSourceBinding		= 0;
		constexpr std::uint32_t kSamplerBinding		= 1;
		constexpr std::uint32_t kDestinationBinding = 2;

		// Matches Constants in resample.slang.
		struct Constants final
		{
			std::uint32_t dstWidth	 = 0;
			std::uint32_t dstHeight	 = 0;
			std::uint32_t layers	 = 1;
			std::uint32_t encodeSrgb = 0;
		};

		// The RHI's own failure helper is internal to it, so the utility carries the two lines itself without reaching past the public surface.
		bool Fail(Error & error, const ErrorCode code, const char * message) noexcept
		{
			error = Error{ .code = code, .message = message };
			return false;
		}

		[[nodiscard]] std::uint32_t GroupCount(const std::uint32_t extent) noexcept
		{
			return ((extent + kGroupSize) - 1) / kGroupSize;
		}

		[[nodiscard]] std::uint32_t MipExtent(const std::uint32_t base, const std::uint32_t mip) noexcept
		{
			return std::max(1u, base >> mip);
		}

		/*
		 * The non-sRGB format carrying the same bits.
		 *
		 * No API allows an sRGB format as a storage image, so writing one through compute means binding a view in its linear twin and doing the encode in the shader.
		 * eUndefined means there is no twin, which is what refuses a format this cannot write without writing it wrong.
		 */
		[[nodiscard]] Format StorageTwinOf(const Format format) noexcept
		{
			switch (format)
			{
			case Format::eRGBA8Srgb: return Format::eRGBA8UNorm;
			case Format::eBGRA8Srgb: return Format::eBGRA8UNorm;
			default:				 return Format::eUndefined;
			}
		}

		[[nodiscard]] bool IsSrgb(const Format format) noexcept
		{
			return StorageTwinOf(format) != Format::eUndefined;
		}

		// The compiled shader for the backend that came up. A container this build did not compile is refused, not guessed at.
		[[nodiscard]] ShaderBinary ShaderFor(const GraphicsApiId api) noexcept
		{
			ShaderBinary binary{};
			binary.stage = ShaderStage::eCompute;

			// Required on every backend, not only the one that reads it: SPIR-V and DXIL carry the size inside the binary and never look here, and a metallib does not,
			// so the RHI takes it from the caller uniformly instead of parsing two containers to find it.
			binary.threadgroupSize = { kGroupSize, kGroupSize, 1 };

#ifdef AZOTH_RHI_UTILS_HAVE_SPIRV
			if (api == VulkanApi::id)
			{
				binary.format	  = ShaderBinaryFormat::eSpirV;
				binary.data		  = shaders::kResample_spirv;
				binary.size		  = shaders::kResample_spirvSize;
				binary.entryPoint = "main";
				return binary;
			}
#endif
#ifdef AZOTH_RHI_UTILS_HAVE_DXIL
			if (api == D3D12Api::id)
			{
				binary.format	  = ShaderBinaryFormat::eDxil;
				binary.data		  = shaders::kResample_dxil;
				binary.size		  = shaders::kResample_dxilSize;
				binary.entryPoint = "main";
				return binary;
			}
#endif
#ifdef AZOTH_RHI_UTILS_HAVE_METALLIB
			if (IsMetalFamily(api))
			{
				// Metal keeps the name Slang emitted, unlike the other two which Slang renames to main.
				binary.format	  = ShaderBinaryFormat::eBackendNative;
				binary.data		  = shaders::kResample_metallib;
				binary.size		  = shaders::kResample_metallibSize;
				binary.entryPoint = "resampleMain";
				return binary;
			}
#endif
			static_cast<void>(api);
			return binary;
		}

		constexpr ResourceState kSampled{ .stages = PipelineStage::eComputeShader, .access = Access::eShaderRead, .layout = TextureLayout::eShaderReadOnly };
		constexpr ResourceState kWritten{ .stages = PipelineStage::eComputeShader, .access = Access::eShaderWrite, .layout = TextureLayout::eGeneral };

		// Where GenerateMips leaves the texture, stated once here because both paths have to end in the same place.
		constexpr ResourceState kReadable{ .stages = PipelineStage::eFragmentShader, .access = Access::eShaderRead, .layout = TextureLayout::eShaderReadOnly };
	} // namespace

	Result<Resampler> Resampler::Create(Device & device, const ResamplerDesc & desc) noexcept
	{
		if (desc.arena == nullptr || !desc.arena->IsValid())
		{
			return Error{ .code = ErrorCode::eInvalidArgument, .message = "a Resampler needs a descriptor arena to allocate its sets from" };
		}

		const ShaderBinary shader = ShaderFor(device.GetGraphicsApiId());
		if (shader.data == nullptr)
		{
			return Error{ .code = ErrorCode::eUnsupportedFeature,
				.message		= "this build of azoth::rhi-utils carries no resample shader for the backend that came up" };
		}

		Resampler resampler;
		resampler.m_device = device;
		resampler.m_arena  = desc.arena;

		Error error{};

		constexpr std::array bindings{
			DescriptorBinding{ .binding = kSourceBinding, .type = DescriptorType::eTextureSRV, .stages = ShaderStage::eCompute },
			DescriptorBinding{ .binding = kSamplerBinding, .type = DescriptorType::eSampler, .stages = ShaderStage::eCompute },
			DescriptorBinding{ .binding = kDestinationBinding, .type = DescriptorType::eTextureUAV, .stages = ShaderStage::eCompute },
		};

		resampler.m_setLayout =
			device.CreateDescriptorSetLayout(DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.utils.resample.set" }, error);
		if (!resampler.m_setLayout.IsValid())
		{
			return error;
		}

		constexpr std::array pushConstants{ PushConstantRange{ .stages = ShaderStage::eCompute, .size = sizeof(Constants) } };
		const std::array setLayouts{ resampler.m_setLayout };

		resampler.m_layout = device.CreatePipelineLayout(
			PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushConstants, .debugName = "azoth.rhi.utils.resample.layout" }, error);
		if (!resampler.m_layout.IsValid())
		{
			return error;
		}

		// Linear and clamped, which is what makes the sample a box filter for the halving case and keeps an edge texel from wrapping.
		resampler.m_sampler = device.CreateSampler(
			SamplerDesc{
				.magFilter = Filter::eLinear,
				.minFilter = Filter::eLinear,
				.addressU  = AddressMode::eClampToEdge,
				.addressV  = AddressMode::eClampToEdge,
				.addressW  = AddressMode::eClampToEdge,
				.debugName = "azoth.rhi.utils.resample.sampler",
			},
			error);
		if (!resampler.m_sampler.IsValid())
		{
			return error;
		}

		resampler.m_pipeline = device.CreateComputePipeline(
			ComputePipelineDesc{
				.layout		   = resampler.m_layout,
				.shader		   = shader,
				.pipelineCache = desc.cache,
				.debugName	   = desc.debugName != nullptr ? desc.debugName : "azoth.rhi.utils.resample.pipeline",
			},
			error);
		if (!resampler.m_pipeline.IsValid())
		{
			return error;
		}

		return resampler;
	}

	Resampler::Resampler(Resampler && other) noexcept
		: m_device(other.m_device),
		  m_arena(other.m_arena),
		  m_pipeline(other.m_pipeline),
		  m_layout(other.m_layout),
		  m_setLayout(other.m_setLayout),
		  m_sampler(other.m_sampler),
		  m_transients(std::move(other.m_transients))
	{
		other.m_device	  = Device{};
		other.m_arena	  = nullptr;
		other.m_pipeline  = {};
		other.m_layout	  = {};
		other.m_setLayout = {};
		other.m_sampler	  = {};
	}

	Resampler & Resampler::operator=(Resampler && other) noexcept
	{
		if (this != &other)
		{
			this->~Resampler();
			new (this) Resampler(std::move(other));
		}

		return *this;
	}

	Resampler::~Resampler()
	{
		if (!m_device.IsValid())
		{
			return;
		}

		Error error{};
		static_cast<void>(Retire(RetirePoint{}, error));

		static_cast<void>(m_device.Destroy(m_pipeline, {}, error));
		static_cast<void>(m_device.Destroy(m_layout, {}, error));
		static_cast<void>(m_device.Destroy(m_setLayout, {}, error));
		static_cast<void>(m_device.Destroy(m_sampler, {}, error));
	}

	bool Resampler::Retire(const RetirePoint safeAfter, Error & error) noexcept
	{
		// An unset retire point means destroy now, which is what destruction wants and what a caller that already waited wants.
		const DestroyDesc destroyDesc =
			safeAfter.timeline.IsValid() ? DestroyDesc{ .policy = DestroyPolicy::eDeferUntilSafe, .safeAfter = safeAfter } : DestroyDesc{};

		bool ok = true;
		for (const Transient & transient : m_transients)
		{
			ok = m_device.Destroy(transient.destination, destroyDesc, error) && ok;
			ok = m_device.Destroy(transient.source, destroyDesc, error) && ok;
		}

		m_transients.clear();
		return ok;
	}

	bool Resampler::GenerateMips(CommandList & list, const TextureHandle texture, Error & error) noexcept
	{
		if (!IsValid())
		{
			return Fail(error, ErrorCode::eInvalidState, "GenerateMips called on a Resampler that was never created");
		}

		TextureInfo info{};
		if (!m_device.GetTextureInfo(texture, info, error))
		{
			return false;
		}

		if (info.desc.mipLevels <= 1)
		{
			// Nothing below the top to fill. Not an error: a caller generating mips over a texture that has none asked for no work, not for the wrong work.
			return true;
		}

		/*
		 * The hardware path where the device has one and this format can take part.
		 *
		 * Both halves matter: supportsScaledBlit says the backend has a fixed function scaled blit at all, and the per format pair says this particular format can be
		 * its source and destination. Vulkan answers yes to both for an ordinary colour format and the driver's own filter runs.
		 */
		const FormatSupport support = m_device.GetFormatSupport(info.desc.format);
		const bool hardware			= m_device.GetCaps().supportsScaledBlit && support.blitSrc && support.blitDst;

		if (hardware)
		{
			if (!list.GenerateMips(texture, error))
			{
				return false;
			}
		}
		else
		{
			for (std::uint32_t mip = 1; mip < info.desc.mipLevels; ++mip)
			{
				if (!ResampleLevel(list, texture, info, mip, error))
				{
					return false;
				}
			}
		}

		/*
		 * One exit state whichever path ran, because a caller cannot see which did. A blit chain ends with every level but the last a transfer source and the last a
		 * transfer destination, while a compute chain ends with every level but the last shader readable and the last still a storage write. Handing that difference
		 * to the caller would make their barrier depend on the device they got.
		 */
		const std::uint32_t last = info.desc.mipLevels - 1;
		const ResourceState above =
			hardware ? ResourceState{ .stages = PipelineStage::eCopy, .access = Access::eCopyRead, .layout = TextureLayout::eCopySrc } : kSampled;
		const ResourceState lastState =
			hardware ? ResourceState{ .stages = PipelineStage::eCopy, .access = Access::eCopyWrite, .layout = TextureLayout::eCopyDst } : kWritten;

		const std::array exit{
			TextureBarrier{
				.texture = texture, .before = above, .after = kReadable, .range = { .baseMip = 0, .mipCount = last, .layerCount = info.desc.arrayLayers } },
			TextureBarrier{
				.texture = texture, .before = lastState, .after = kReadable, .range = { .baseMip = last, .mipCount = 1, .layerCount = info.desc.arrayLayers } },
		};

		return list.Barriers(BarrierBatch{ .textures = exit }, error);
	}

	bool Resampler::ResampleLevel(CommandList & list, const TextureHandle texture, const TextureInfo & info, const std::uint32_t dstMip, Error & error) noexcept
	{
		const bool srgb			   = IsSrgb(info.desc.format);
		const Format storageForm   = srgb ? StorageTwinOf(info.desc.format) : info.desc.format;
		const std::uint32_t layers = info.desc.arrayLayers;

		if (srgb && !info.desc.allowFormatViews)
		{
			return Fail(error,
				ErrorCode::eInvalidArgument,
				"resampling an sRGB texture through compute needs TextureDesc::allowFormatViews, no API permitting an sRGB storage image");
		}
		if (!info.desc.usage.Contains(TextureUsage::eStorage))
		{
			return Fail(error, ErrorCode::eInvalidArgument, "the compute resample path needs TextureUsage::eStorage on the texture it writes");
		}
		if (!info.desc.usage.Contains(TextureUsage::eSampled))
		{
			return Fail(error, ErrorCode::eInvalidArgument, "the compute resample path needs TextureUsage::eSampled on the texture it reads");
		}

		// The level above, read through a sampler, and this level written through a storage view. Both are array views so one shader serves every shape.
		const TextureViewHandle source = m_device.CreateTextureView(texture,
			TextureViewDesc{
				.type	   = TextureViewType::eTex2DArray,
				.range	   = { .baseMip = dstMip - 1, .mipCount = 1, .layerCount = layers },
				.usage	   = Flags<TextureUsage>(TextureUsage::eSampled),
				.debugName = "azoth.rhi.utils.resample.source",
			},
			error);

		const TextureViewHandle destination = m_device.CreateTextureView(texture,
			TextureViewDesc{
				.type	   = TextureViewType::eTex2DArray,
				.format	   = storageForm,
				.range	   = { .baseMip = dstMip, .mipCount = 1, .layerCount = layers },
				.usage	   = Flags<TextureUsage>(TextureUsage::eStorage),
				.debugName = "azoth.rhi.utils.resample.destination",
			},
			error);

		if (!source.IsValid() || !destination.IsValid())
		{
			return false;
		}

		m_transients.push_back(Transient{ .source = source, .destination = destination });

		const DescriptorSetHandle set = m_arena->Allocate(DescriptorSetAllocDesc{ .layout = m_setLayout, .debugName = "azoth.rhi.utils.resample" }, error);
		if (!set.IsValid())
		{
			return false;
		}

		const std::array sources{
			DescriptorWriteTexture{ .set = set, .binding = kSourceBinding, .type = DescriptorType::eTextureSRV, .view = source, .sampler = m_sampler },
		};
		const std::array destinations{
			DescriptorWriteTexture{ .set = set,
				.binding				 = kDestinationBinding,
				.type					 = DescriptorType::eTextureUAV,
				.view					 = destination,
				.sampler				 = m_sampler,
				.expectedLayout			 = TextureLayout::eGeneral },
		};
		const std::array samplers{ DescriptorWriteSampler{ .set = set, .binding = kSamplerBinding, .sampler = m_sampler } };

		if (!m_device.UpdateDescriptors(std::span(sources), error) || !m_device.UpdateDescriptors(std::span(destinations), error) ||
			!m_device.UpdateDescriptors(std::span(samplers), error))
		{
			return false;
		}

		/*
		 * The level being read has to be readable and the one being written writable, and they are subresources of one texture, so the two barriers name different
		 * ranges of it. The level above was written by the previous iteration, which is what makes this a chain and not a batch.
		 */
		const std::array toSampled{
			TextureBarrier{ .texture = texture,
				.before				 = dstMip == 1 ? kSampled : kWritten,
				.after				 = kSampled,
				.range				 = { .baseMip = dstMip - 1, .mipCount = 1, .layerCount = layers } },
		};
		const std::array toWritten{
			TextureBarrier{ .texture = texture, .after = kWritten, .range = { .baseMip = dstMip, .mipCount = 1, .layerCount = layers } },
		};

		const Constants constants{
			.dstWidth	= MipExtent(info.desc.width, dstMip),
			.dstHeight	= MipExtent(info.desc.height, dstMip),
			.layers		= layers,
			.encodeSrgb = srgb ? 1u : 0u,
		};

		return list.Barriers(BarrierBatch{ .textures = toSampled }, error) && list.Barriers(BarrierBatch{ .textures = toWritten }, error) &&
			   list.SetComputePipeline(m_pipeline, error) && list.BindDescriptorSet(m_layout, 0, set, {}, error) &&
			   list.PushConstants(m_layout, ShaderStage::eCompute, 0, sizeof(constants), &constants, error) &&
			   list.Dispatch(GroupCount(constants.dstWidth), GroupCount(constants.dstHeight), layers, error);
	}

	bool Resampler::Blit(CommandList & list, const TextureHandle dst, const TextureHandle src, const std::span<const TextureBlit> regions, const Filter filter,
		Error & error) noexcept
	{
		if (!IsValid())
		{
			return Fail(error, ErrorCode::eInvalidState, "Blit called on a Resampler that was never created");
		}

		TextureInfo dstInfo{};
		TextureInfo srcInfo{};
		if (!m_device.GetTextureInfo(dst, dstInfo, error) || !m_device.GetTextureInfo(src, srcInfo, error))
		{
			return false;
		}

		const FormatSupport dstSupport = m_device.GetFormatSupport(dstInfo.desc.format);
		const FormatSupport srcSupport = m_device.GetFormatSupport(srcInfo.desc.format);
		if (m_device.GetCaps().supportsScaledBlit && srcSupport.blitSrc && dstSupport.blitDst)
		{
			return list.Blit(dst, src, regions, filter, error);
		}

		// The compute path resamples whole levels, not arbitrary offset rectangles, which is what generateMips needs and what this utility was extracted to restore.
		// A region blit through compute is its own piece of work and is refused, not approximated.
		return Fail(error,
			ErrorCode::eUnsupportedFeature,
			"this device has no hardware scaled blit for these formats, and the compute path resamples whole levels, not regions");
	}
} // namespace azo::rhi::utils

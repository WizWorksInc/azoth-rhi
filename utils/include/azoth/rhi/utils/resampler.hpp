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

#pragma once

/**
 * \file
 * \brief Scaled blit and mip chain generation, above the RHI, not inside it.
 */

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace azo::rhi::utils
{
	/**
	 * \brief What a Resampler is built over.
	 */
	struct ResamplerDesc final
	{
		/**
		 * \brief The arena its descriptor sets come from, borrowed for the Resampler's lifetime.
		 *
		 * Taken, not owned because descriptor budget is caller policy, and the RHI already exposes arenas as a first class thing. Owning one here would make this the
		 * second place in the system that decides how many descriptors a frame gets.
		 *
		 * Sized for one set per destination mip level across everything this Resampler will be asked to do before it is reset.
		 */
		DescriptorArena * arena = nullptr;

		/**
		 * \brief Cache the compute pipeline is created against, or an invalid handle for none.
		 */
		PipelineCacheHandle cache{};

		const char * debugName = nullptr;
	};

	/**
	 * \brief Resamples one texture into another, by hardware blit where the device has one and by compute where it does not.
	 *
	 * Direct3D 12 has no fixed function scaled blit and the RHI refuses without answering with a compute shader under a name that means hardware elsewhere.
	 *
	 * \attention The destination needs TextureUsage::eStorage and the source needs eSampled for the compute path. An sRGB destination also needs
	 * TextureDesc::allowFormatViews, since no API permits an sRGB storage image.
	 */
	class Resampler final
	{
	public:
		[[nodiscard]] static Result<Resampler> Create(Device & device, const ResamplerDesc & desc) noexcept;

		Resampler(const Resampler &)			 = delete;
		Resampler & operator=(const Resampler &) = delete;
		Resampler(Resampler && other) noexcept;
		Resampler & operator=(Resampler && other) noexcept;
		~Resampler();

		/**
		 * \brief Fills every mip below the top from the level above it.
		 *
		 * Takes the hardware path when DeviceCaps::supportsScaledBlit and the format's own FormatSupport::blitSrc and blitDst all allow it, and dispatches the
		 * compute shader otherwise.
		 *
		 * The barrier contract differs between the two paths and is handled here: on entry the whole texture is expected in eShaderReadOnly or eGeneral, and on exit
		 * every level is left in eShaderReadOnly.
		 */
		[[nodiscard]] bool GenerateMips(CommandList & list, TextureHandle texture, Error & error) noexcept;

		/**
		 * \brief Copies regions of one texture into another, resampling where the extents differ.
		 */
		[[nodiscard]] bool Blit(CommandList & list, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter,
			Error & error) noexcept;

		/**
		 * \brief Hands back the views and descriptor sets recording created, once the work naming them has completed.
		 *
		 * A recorded resample needs its views alive until the GPU is done with them, so they accumulate until this is called. A caller that resamples once at load
		 * time can skip it and let destruction do the same thing.
		 */
		bool Retire(RetirePoint safeAfter, Error & error) noexcept;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_device.IsValid() && m_pipeline.IsValid();
		}

		// Reachable because Result<T> holds a T by value. An instance made this way is invalid until Create fills it.
		Resampler() = default;

	private:

		// Everything one recorded resample needed, kept alive until Retire or destruction.
		struct Transient final
		{
			TextureViewHandle source{};
			TextureViewHandle destination{};
		};

		[[nodiscard]] bool ResampleLevel(CommandList & list, TextureHandle texture, const TextureInfo & info, std::uint32_t dstMip, Error & error) noexcept;

		Device m_device{};
		DescriptorArena * m_arena = nullptr;
		ComputePipelineHandle m_pipeline{};
		PipelineLayoutHandle m_layout{};
		DescriptorSetLayoutHandle m_setLayout{};
		SamplerHandle m_sampler{};
		std::vector<Transient> m_transients;
	};
} // namespace azo::rhi::utils

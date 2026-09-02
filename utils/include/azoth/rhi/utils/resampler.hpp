// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

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
	struct ResamplerDesc final
	{
		DescriptorArena * arena = nullptr;

		PipelineCacheHandle cache{};

		const char * debugName = nullptr;
	};

	class Resampler final
	{
	public:
		[[nodiscard]] static Result<Resampler> Create(Device & device, const ResamplerDesc & desc) noexcept;

		Resampler(const Resampler &)			 = delete;
		Resampler & operator=(const Resampler &) = delete;
		Resampler(Resampler && other) noexcept;
		Resampler & operator=(Resampler && other) noexcept;
		~Resampler();

		[[nodiscard]] bool GenerateMips(CommandList & list, TextureHandle texture, Error & error) noexcept;

		[[nodiscard]] bool Blit(
			CommandList & list, TextureHandle dst, TextureHandle src, std::span<const TextureBlit> regions, Filter filter, Error & error) noexcept;

		bool Retire(RetirePoint safeAfter, Error & error) noexcept;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_device.IsValid() && m_pipeline.IsValid();
		}

		Resampler() = default;

	private:
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
}

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "FW/assets/ImageAsset.hpp"
#include "Scene.hpp"

#include <azoth/rhi/core/resource_handles.hpp>
#include <azoth/rhi/resources/descriptors.hpp>
#include <azoth/rhi/utils/resampler.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fw::scene
{
	using TextureId = std::int32_t;

	inline constexpr TextureId kNoTexture = -1;

	class SceneGpuImageStorage final
	{
	public:
		explicit SceneGpuImageStorage(const SceneConfig & sceneConfig);

		SceneGpuImageStorage(const SceneGpuImageStorage &)			   = delete;
		SceneGpuImageStorage & operator=(const SceneGpuImageStorage &) = delete;
		SceneGpuImageStorage(SceneGpuImageStorage &&)				   = delete;
		SceneGpuImageStorage & operator=(SceneGpuImageStorage &&)	   = delete;

		~SceneGpuImageStorage();

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_valid;
		}

		[[nodiscard]] TextureId StoreTexture(std::string name, const assets::ImageAsset & image, bool srgb = true, bool mipmapped = true);

		[[nodiscard]] TextureId StoreCubemap(std::string name, std::span<const assets::ImageAsset> faces, bool srgb = true);

		[[nodiscard]] TextureId FindTexture(std::string_view name) const;

		[[nodiscard]] std::uint32_t GetTextureCount() const noexcept
		{
			return static_cast<std::uint32_t>(m_textures.size());
		}

		[[nodiscard]] azo::rhi::TextureHandle GetTexture(TextureId id) const noexcept;

		[[nodiscard]] azo::rhi::TextureViewHandle GetTextureView(TextureId id) const noexcept;

		[[nodiscard]] azo::rhi::SamplerHandle GetSampler() const noexcept
		{
			return m_sampler;
		}

		[[nodiscard]] std::vector<azo::rhi::DescriptorWriteTexture> MakeDescriptorWrites(azo::rhi::DescriptorSetHandle set, std::uint32_t binding) const;

	private:
		struct Entry final
		{
			std::string name;
			azo::rhi::TextureHandle texture;
			azo::rhi::TextureViewHandle view;
		};

		struct CopyPlan final
		{
			std::span<const std::uint64_t> layerOffsets;
			std::uint32_t width		 = 0;
			std::uint32_t height	 = 0;
			std::uint32_t mips		 = 1;
			std::uint32_t layerCount = 1;
		};

		[[nodiscard]] TextureId Upload(std::string name, std::span<const assets::ImageAsset> layers, azo::rhi::TextureType type, bool srgb, bool mipmapped);

		[[nodiscard]] bool RecordAndSubmit(azo::rhi::TextureHandle texture, azo::rhi::BufferHandle staging, const CopyPlan & plan, azo::rhi::Error & error);

		[[nodiscard]] bool GenerateMips(azo::rhi::CommandList & list, azo::rhi::TextureHandle texture, azo::rhi::Error & error);

		static constexpr std::uint32_t kResampleSets = 32;

		SceneConfig m_config;
		azo::rhi::CommandPool m_pool;
		azo::rhi::Queue m_queue;
		azo::rhi::TimelineHandle m_timeline;
		azo::rhi::SamplerHandle m_sampler;
		azo::rhi::DescriptorArena m_resampleArena;
		azo::rhi::utils::Resampler m_resampler;
		std::uint64_t m_copyAlignment = 1;
		std::uint64_t m_uploadCount	  = 0;
		std::vector<Entry> m_textures;
		std::unordered_map<std::string, TextureId> m_byName;
		bool m_valid = false;
	};
}

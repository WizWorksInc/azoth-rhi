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
	/**
	 * \brief Where a texture sits in the storage, and what a material's texture field holds.
	 *
	 * Signed so that -1 means the material names no texture, which is what Material's map fields already default to.
	 */
	using TextureId = std::int32_t;

	inline constexpr TextureId kNoTexture = -1;

	/**
	 * \brief The textures a scene samples, uploaded on the transfer queue the scene config carries.
	 *
	 * Textures go in a flat table a material indexes by TextureId. The descriptor set is written once for the whole table, not rebound per draw. Each store
	 * records, submits and waits for its own upload, which is a load-time cost paid once per texture.
	 */
	class SceneGpuImageStorage final
	{
	public:
		explicit SceneGpuImageStorage(const SceneConfig & sceneConfig);

		SceneGpuImageStorage(const SceneGpuImageStorage &)			   = delete;
		SceneGpuImageStorage & operator=(const SceneGpuImageStorage &) = delete;
		SceneGpuImageStorage(SceneGpuImageStorage &&)				   = delete;
		SceneGpuImageStorage & operator=(SceneGpuImageStorage &&)	   = delete;

		~SceneGpuImageStorage();

		/**
		 * \brief False when the config carried no transfer queue or pool, or the sampler and timeline could not be made.
		 *
		 * A scene that never stores a texture is fine leaving this false, which is what a config with no transfer queue is saying.
		 */
		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_valid;
		}

		/**
		 * \brief Uploads one image and adds it to the table, or hands back the id a texture of the same name already has.
		 *
		 * \param srgb True for colour, which is base colour and emissive. False for numbers, which is normals, roughness, metalness and occlusion. Getting this
		 * wrong on a normal map bends every normal.
		 * \param mipmapped True to build the mip chain.
		 * \return The texture's id, or kNoTexture when the upload failed.
		 */
		[[nodiscard]] TextureId StoreTexture(std::string name, const assets::ImageAsset & image, bool srgb = true, bool mipmapped = true);

		/**
		 * \brief Uploads six faces as one cube texture.
		 *
		 * \param faces In the order every API declares them, which is +X, -X, +Y, -Y, +Z, -Z. All six have to be the same size and the same kind.
		 */
		[[nodiscard]] TextureId StoreCubemap(std::string name, std::span<const assets::ImageAsset> faces, bool srgb = true);

		/**
		 * \brief The id of a texture stored under this name, or kNoTexture when there is none.
		 */
		[[nodiscard]] TextureId FindTexture(std::string_view name) const;

		[[nodiscard]] std::uint32_t GetTextureCount() const noexcept
		{
			return static_cast<std::uint32_t>(m_textures.size());
		}

		[[nodiscard]] azo::rhi::TextureHandle GetTexture(TextureId id) const noexcept;

		[[nodiscard]] azo::rhi::TextureViewHandle GetTextureView(TextureId id) const noexcept;

		/**
		 * \brief The one sampler the whole table is read through.
		 */
		[[nodiscard]] azo::rhi::SamplerHandle GetSampler() const noexcept
		{
			return m_sampler;
		}

		/**
		 * \brief Descriptor writes covering the whole table, for one array binding a shader indexes by TextureId.
		 *
		 * \param set The set to write into.
		 * \param binding Which binding in it holds the texture array.
		 */
		[[nodiscard]] std::vector<azo::rhi::DescriptorWriteTexture> MakeDescriptorWrites(azo::rhi::DescriptorSetHandle set, std::uint32_t binding) const;

	private:
		// One entry of the table. The texture and its view are owned here and destroyed together.
		struct Entry final
		{
			std::string name;
			azo::rhi::TextureHandle texture;
			azo::rhi::TextureViewHandle view;
		};

		// Where each layer sits in the staging buffer, and the shape of the texture they are going into.
		struct CopyPlan final
		{
			std::span<const std::uint64_t> layerOffsets;
			std::uint32_t width		 = 0;
			std::uint32_t height	 = 0;
			std::uint32_t mips		 = 1;
			std::uint32_t layerCount = 1;
		};

		/**
		 * \brief Creates the texture, copies every layer in from one staging buffer and leaves the whole thing shader readable.
		 *
		 * \param layers One image per array layer, which is one for a 2D texture and six for a cube.
		 */
		[[nodiscard]] TextureId Upload(std::string name, std::span<const assets::ImageAsset> layers, azo::rhi::TextureType type, bool srgb, bool mipmapped);

		/**
		 * \brief Records the copy and the two layout transitions around it, submits, and waits for the GPU to finish.
		 *
		 * Waiting here without handing a timeline value back is what keeps the caller from having to track one, at the cost of a stall per texture. That is a
		 * load-time cost, which is the only time this runs.
		 */
		[[nodiscard]] bool RecordAndSubmit(azo::rhi::TextureHandle texture, azo::rhi::BufferHandle staging, const CopyPlan & plan, azo::rhi::Error & error);

		/**
		 * \brief Builds the mip chain, creating the resampler and its arena the first time one is asked for.
		 */
		[[nodiscard]] bool GenerateMips(azo::rhi::CommandList & list, azo::rhi::TextureHandle texture, azo::rhi::Error & error);

		static constexpr std::uint32_t kResampleSets = 32;

		SceneConfig m_config;
		azo::rhi::CommandPool m_pool;
		azo::rhi::Queue m_queue;
		azo::rhi::TimelineHandle m_timeline;
		azo::rhi::SamplerHandle m_sampler;
		azo::rhi::DescriptorArena m_resampleArena;
		azo::rhi::utils::Resampler m_resampler;
		// What this device wants a copy's buffer offset aligned to, read from its caps, not assumed.
		std::uint64_t m_copyAlignment = 1;
		std::uint64_t m_uploadCount	  = 0;
		std::vector<Entry> m_textures;
		std::unordered_map<std::string, TextureId> m_byName;
		bool m_valid = false;
	};
} // namespace fw::scene

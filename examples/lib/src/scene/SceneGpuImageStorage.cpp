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

#include "FW/scene/SceneGpuImageStorage.hpp"

#include "FW/utility/Log.hpp"

#include <azoth/rhi/commands/command.hpp>
#include <azoth/rhi/commands/copy_types.hpp>
#include <azoth/rhi/commands/sync.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>

namespace fw::scene
{
	namespace
	{
		constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

		// What a copy's buffer offset is aligned to when a device reports no requirement of its own.
		constexpr std::uint64_t kFallbackCopyAlignment = 4;

		void ReportError(const std::string_view what, const azo::rhi::Error & error)
		{
			LOG_ERROR(fw::Log(), "scene images: {}: {}", what, error.message != nullptr ? error.message : "no diagnostic");
		}

		[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}
	} // namespace

	SceneGpuImageStorage::SceneGpuImageStorage(const SceneConfig & sceneConfig)
		: m_config(sceneConfig),
		  m_pool(sceneConfig.imageTransferCmdPool),
		  m_queue(sceneConfig.imageTransferQueue)
	{
		if (!m_config.device.IsValid() || !m_pool.IsValid() || !m_queue.IsValid())
		{
			// Not an error. A scene with no textures has no reason to hand over a transfer queue, and every store below refuses without crashing.
			return;
		}

		// Asked of the device, not assumed, since a number chosen here would only be one that happened to suit today's backends.
		const std::uint64_t reported = m_config.device.GetCaps().optimalBufferCopyOffsetAlignment;
		m_copyAlignment				 = reported != 0 ? reported : kFallbackCopyAlignment;

		azo::rhi::Error error{};

		m_timeline = m_config.device.CreateTimeline(azo::rhi::TimelineDesc{ .debugName = "fw.scene.imageUpload" }, error);
		if (!m_timeline.IsValid())
		{
			ReportError("failed to create the upload timeline", error);
			return;
		}

		// One sampler for the whole table. Anisotropy is asked for only where the device reports it, since a sampler asking for what is not there is a
		// creation failure and not a silent downgrade.
		const bool anisotropy = m_config.device.GetCaps().supportsAnisotropy;

		m_sampler = m_config.device.CreateSampler(
			azo::rhi::SamplerDesc{
				.anisotropyEnable = anisotropy,
				.maxAnisotropy	  = anisotropy ? 16.0f : 1.0f,
				.debugName		  = "fw.scene.sampler",
			},
			error);

		if (!m_sampler.IsValid())
		{
			ReportError("failed to create the scene sampler", error);
			return;
		}

		m_valid = true;
	}

	SceneGpuImageStorage::~SceneGpuImageStorage()
	{
		// The resampler holds views of its own and has to go before the textures they name.
		m_resampler = {};

		for (Entry & entry : m_textures)
		{
			static_cast<void>(m_config.device.Destroy(entry.view));
			static_cast<void>(m_config.device.Destroy(entry.texture));
		}

		static_cast<void>(m_config.device.Destroy(m_sampler));
		static_cast<void>(m_config.device.Destroy(m_timeline));
	}

	TextureId SceneGpuImageStorage::StoreTexture(std::string name, const assets::ImageAsset & image, const bool srgb, const bool mipmapped)
	{
		const std::array layers{ image };
		return Upload(std::move(name), layers, azo::rhi::TextureType::eTex2D, srgb, mipmapped);
	}

	TextureId SceneGpuImageStorage::StoreCubemap(std::string name, const std::span<const assets::ImageAsset> faces, const bool srgb)
	{
		if (faces.size() != 6)
		{
			LOG_INFO(fw::Log(), "scene images: a cubemap needs six faces, {} were given", faces.size());
			return kNoTexture;
		}

		// Mips off for a cube. The resampler filters each array layer on its own, so a texel at a face edge is averaged with the opposite edge of the same
		// face instead of with the neighbouring face it actually touches, and the seam shows.
		return Upload(std::move(name), faces, azo::rhi::TextureType::eTexCube, srgb, false);
	}

	TextureId SceneGpuImageStorage::FindTexture(const std::string_view name) const
	{
		const auto found = m_byName.find(std::string{ name });
		return found != m_byName.end() ? found->second : kNoTexture;
	}

	azo::rhi::TextureHandle SceneGpuImageStorage::GetTexture(const TextureId id) const noexcept
	{
		return id >= 0 && static_cast<std::size_t>(id) < m_textures.size() ? m_textures[static_cast<std::size_t>(id)].texture : azo::rhi::TextureHandle{};
	}

	azo::rhi::TextureViewHandle SceneGpuImageStorage::GetTextureView(const TextureId id) const noexcept
	{
		return id >= 0 && static_cast<std::size_t>(id) < m_textures.size() ? m_textures[static_cast<std::size_t>(id)].view : azo::rhi::TextureViewHandle{};
	}

	std::vector<azo::rhi::DescriptorWriteTexture> SceneGpuImageStorage::MakeDescriptorWrites(
		const azo::rhi::DescriptorSetHandle set, const std::uint32_t binding) const
	{
		std::vector<azo::rhi::DescriptorWriteTexture> writes;
		writes.reserve(m_textures.size());

		for (std::uint32_t index = 0; const Entry & entry : m_textures)
		{
			writes.push_back(azo::rhi::DescriptorWriteTexture{
				.set		= set,
				.binding	= binding,
				.arrayIndex = index++,
				.type		= azo::rhi::DescriptorType::eTextureSRV,
				.view		= entry.view,
				.sampler	= m_sampler,
			});
		}

		return writes;
	}

	TextureId SceneGpuImageStorage::Upload(
		std::string name, const std::span<const assets::ImageAsset> layers, const azo::rhi::TextureType type, const bool srgb, const bool mipmapped)
	{
		if (const TextureId existing = FindTexture(name); existing != kNoTexture)
		{
			return existing;
		}

		if (!m_valid || layers.empty())
		{
			return kNoTexture;
		}

		const assets::ImageAsset & first = layers.front();
		if (!first.IsValid())
		{
			LOG_INFO(fw::Log(), "scene images: \"{}\" carries no pixels", name);
			return kNoTexture;
		}

		// Every layer feeds one subresource of one texture, so a layer of a different size or a different kind has nowhere to go.
		for (const assets::ImageAsset & layer : layers)
		{
			if (layer.width != first.width || layer.height != first.height || layer.isFloat != first.isFloat || !layer.IsValid())
			{
				LOG_INFO(fw::Log(), "scene images: the layers of \"{}\" are not all the same size and kind", name);
				return kNoTexture;
			}
		}

		const auto layerCount		  = static_cast<std::uint32_t>(layers.size());
		const std::uint32_t mips	  = mipmapped ? assets::MipCount(first.width, first.height) : 1;
		const azo::rhi::Format format = first.Format(srgb);

		azo::rhi::Error error{};

		// eCopySrc and eStorage as well as the obvious two because the resampler picks its path from what the device supports: the hardware one reads
		// each level back to blit the next, the compute one writes each level through a storage view. Which one runs is its decision, not ours.
		azo::rhi::Flags<azo::rhi::TextureUsage> usage =
			azo::rhi::Flags<azo::rhi::TextureUsage>(azo::rhi::TextureUsage::eSampled) | azo::rhi::TextureUsage::eCopyDst;
		if (mips > 1)
		{
			usage = usage | azo::rhi::TextureUsage::eCopySrc | azo::rhi::TextureUsage::eStorage;
		}

		const azo::rhi::TextureHandle texture = m_config.device.CreateTexture(
			azo::rhi::TextureDesc{
				.type		 = type,
				.format		 = format,
				.width		 = first.width,
				.height		 = first.height,
				.mipLevels	 = mips,
				.arrayLayers = layerCount,
				.usage		 = usage,
				// No API allows an sRGB storage image, so the compute path writes through a linear view of the same memory and encodes itself. Declaring
				// this is what makes that view legal.
				.allowFormatViews = mips > 1,
				.debugName		  = name.c_str(),
			},
			error);

		const azo::rhi::TextureViewHandle view = m_config.device.CreateTextureView(texture,
			azo::rhi::TextureViewDesc{
				.type	   = type == azo::rhi::TextureType::eTexCube ? azo::rhi::TextureViewType::eTexCube : azo::rhi::TextureViewType::eTex2D,
				.range	   = { .mipCount = mips, .layerCount = layerCount },
				.debugName = name.c_str(),
			},
			error);

		if (!texture.IsValid() || !view.IsValid())
		{
			ReportError("failed to create a scene texture", error);
			static_cast<void>(m_config.device.Destroy(view));
			static_cast<void>(m_config.device.Destroy(texture));
			return kNoTexture;
		}

		// One staging buffer for every layer, each starting at an offset a copy can name.
		std::vector<std::uint64_t> layerOffsets(layers.size());
		std::uint64_t stagingBytes = 0;
		for (std::size_t index = 0; index < layers.size(); ++index)
		{
			layerOffsets[index] = stagingBytes;
			stagingBytes += AlignUp(layers[index].pixels.size(), m_copyAlignment);
		}

		const azo::rhi::BufferHandle staging = m_config.device.CreateBuffer(
			azo::rhi::BufferDesc{
				.size	   = stagingBytes,
				.usage	   = azo::rhi::BufferUsage::eCopySrc,
				.memory	   = azo::rhi::MemoryUsage::eCpuUpload,
				.debugName = "fw.scene.imageStaging",
			},
			error);

		const azo::rhi::MappedMemory mapped =
			staging.IsValid() ? m_config.device.Map(staging, azo::rhi::MapDesc{ .mode = azo::rhi::MapMode::eWrite }, error) : azo::rhi::MappedMemory{};

		if (mapped.data == nullptr)
		{
			ReportError("failed to stage a scene texture", error);
			static_cast<void>(m_config.device.Destroy(staging));
			static_cast<void>(m_config.device.Destroy(view));
			static_cast<void>(m_config.device.Destroy(texture));
			return kNoTexture;
		}

		auto * bytes = static_cast<std::uint8_t *>(mapped.data);
		for (std::size_t index = 0; index < layers.size(); ++index)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			std::memcpy(bytes + layerOffsets[index], layers[index].pixels.data(), layers[index].pixels.size());
		}

		const bool staged = (mapped.coherent || m_config.device.FlushMappedRange(staging, 0, stagingBytes, error)) && m_config.device.Unmap(staging, error);
		const bool ok	  = staged && RecordAndSubmit(texture,
										  staging,
										  CopyPlan{
											  .layerOffsets = layerOffsets,
											  .width		= first.width,
											  .height		= first.height,
											  .mips			= mips,
											  .layerCount	= layerCount,
										  },
										  error);

		static_cast<void>(m_config.device.Destroy(staging));

		if (!ok)
		{
			ReportError("failed to upload a scene texture", error);
			static_cast<void>(m_config.device.Destroy(view));
			static_cast<void>(m_config.device.Destroy(texture));
			return kNoTexture;
		}

		const auto id = static_cast<TextureId>(m_textures.size());
		m_byName.emplace(name, id);
		m_textures.push_back(Entry{ .name = std::move(name), .texture = texture, .view = view });

		return id;
	}

	bool SceneGpuImageStorage::RecordAndSubmit(
		const azo::rhi::TextureHandle texture, const azo::rhi::BufferHandle staging, const CopyPlan & plan, azo::rhi::Error & error)
	{
		azo::rhi::CommandList list = m_pool.Allocate("fw.scene.imageUpload", error);
		if (!list.IsValid() || !list.Begin(error))
		{
			return false;
		}

		const azo::rhi::TextureSubresourceRange whole{ .mipCount = plan.mips, .layerCount = plan.layerCount };

		const std::array toCopyDst{
			azo::rhi::TextureBarrier{
				.texture = texture,
				.before	 = { .stages = azo::rhi::PipelineStage::eNone, .access = azo::rhi::Access::eNone, .layout = azo::rhi::TextureLayout::eUndefined },
				.after	 = { .stages = azo::rhi::PipelineStage::eCopy, .access = azo::rhi::Access::eCopyWrite, .layout = azo::rhi::TextureLayout::eCopyDst },
				.range	 = whole,
			},
		};

		std::vector<azo::rhi::BufferTextureCopy> regions;
		regions.reserve(plan.layerCount);
		for (std::uint32_t layer = 0; layer < plan.layerCount; ++layer)
		{
			regions.push_back(azo::rhi::BufferTextureCopy{
				.bufferOffset  = plan.layerOffsets[layer],
				.subresource   = { .layer = layer },
				.textureExtent = { .width = plan.width, .height = plan.height },
			});
		}

		// Mip zero is left readable either way. The resampler takes it from there and states where it leaves every level, which is also readable, so a
		// caller never has to know which of its two paths ran.
		const std::array toRead{
			azo::rhi::TextureBarrier{
				.texture = texture,
				.before	 = { .stages = azo::rhi::PipelineStage::eCopy, .access = azo::rhi::Access::eCopyWrite, .layout = azo::rhi::TextureLayout::eCopyDst },
				.after	 = { .stages = azo::rhi::PipelineStage::eFragmentShader,
					.access		   = azo::rhi::Access::eShaderRead,
					.layout		   = azo::rhi::TextureLayout::eShaderReadOnly },
				.range	 = whole,
			},
		};

		const bool recorded =
			list.Barriers(azo::rhi::BarrierBatch{ .textures = toCopyDst }, error) && list.CopyBufferToTexture(texture, staging, regions, error) &&
			list.Barriers(azo::rhi::BarrierBatch{ .textures = toRead }, error) && (plan.mips == 1 || GenerateMips(list, texture, error)) && list.End(error);

		if (!recorded)
		{
			return false;
		}

		const std::uint64_t signalValue = ++m_uploadCount;
		std::array<const azo::rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ azo::rhi::TimelinePoint{ .timeline = m_timeline, .value = signalValue } };

		if (!m_queue.Submit(azo::rhi::SubmitDesc{ .commandLists = lists, .signals = signals, .debugName = "fw.scene.imageUploadSubmit" }, error) ||
			!m_queue.Wait(m_timeline, signalValue, kNoTimeout, error))
		{
			return false;
		}

		// The wait above is what makes this retire point one the GPU has already passed, so the list, and the views and sets the resampler took, go back
		// now without piling up across every texture a scene loads.
		const azo::rhi::RetirePoint retired{ .timeline = m_timeline, .value = signalValue };
		static_cast<void>(m_pool.Reset(retired, error));

		if (m_resampler.IsValid())
		{
			static_cast<void>(m_resampler.Retire(retired, error));
			static_cast<void>(m_resampleArena.Reset(retired, error));
		}

		return true;
	}

	bool SceneGpuImageStorage::GenerateMips(azo::rhi::CommandList & list, const azo::rhi::TextureHandle texture, azo::rhi::Error & error)
	{
		// Built on the first texture that asks for mips and not in the constructor, so a scene whose textures are all single level never pays for the
		// compute pipeline the resampler compiles.
		if (!m_resampler.IsValid())
		{
			m_resampleArena = m_config.device.CreateDescriptorArena(
				azo::rhi::DescriptorArenaDesc{
					.type = azo::rhi::DescriptorArenaType::ePersistent,
					// One set per level below the top, and the arena is reset after every upload, so this only has to cover the deepest single chain.
					.maxSets		= kResampleSets,
					.maxDescriptors = kResampleSets * 3,
					.debugName		= "fw.scene.resampleArena",
				},
				error);

			if (!m_resampleArena.IsValid())
			{
				return false;
			}

			azo::rhi::Device device = m_config.device;
			azo::rhi::Result<azo::rhi::utils::Resampler> made =
				azo::rhi::utils::Resampler::Create(device, azo::rhi::utils::ResamplerDesc{ .arena = &m_resampleArena, .debugName = "fw.scene.resampler" });

			if (!made)
			{
				error = made.GetError();
				return false;
			}

			m_resampler = std::move(made.Value());
		}

		return m_resampler.GenerateMips(list, texture, error);
	}
} // namespace fw::scene

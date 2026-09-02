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
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace azo::rhi::imgui
{
	struct RendererDesc final
	{
		DescriptorArena * arena = nullptr;

		Format colorFormat = Format::eUndefined;

		std::uint32_t framesInFlight = 2;

		PipelineCacheHandle cache{};

		const char * debugName = nullptr;
	};

	class Renderer final
	{
	public:
		[[nodiscard]] static Result<Renderer> Create(Device & device, const RendererDesc & desc) noexcept;

		Renderer(const Renderer &)			   = delete;
		Renderer & operator=(const Renderer &) = delete;
		Renderer(Renderer && other) noexcept;
		Renderer & operator=(Renderer && other) noexcept;

		~Renderer();

		[[nodiscard]] bool UpdateTextures(CommandList & list, const ImDrawData & drawData, std::uint32_t frameSlot, Error & error) noexcept;

		[[nodiscard]] bool Record(CommandList & list, const ImDrawData & drawData, std::uint32_t frameSlot, Error & error) noexcept;

		[[nodiscard]] ImTextureID RegisterTexture(TextureViewHandle view, Error & error) noexcept;

		void UnregisterTexture(ImTextureID id) noexcept;

		bool Retire(RetirePoint safeAfter, Error & error) noexcept;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_pipeline.IsValid();
		}

		Renderer() = default;

	private:
		struct Texture final
		{
			TextureHandle texture;
			TextureViewHandle view;
			DescriptorSetHandle set;

			bool owned = false;
		};

		struct Staging final
		{
			BufferHandle buffer;
			std::uint8_t * data = nullptr;
			std::uint64_t size	= 0;
			std::uint64_t used	= 0;
		};

		[[nodiscard]] bool CreatePipeline(const RendererDesc & desc, Error & error) noexcept;

		[[nodiscard]] bool CreateTexture(CommandList & list, ImTextureData & data, std::uint32_t frameSlot, Error & error) noexcept;

		[[nodiscard]] bool UpdateTexture(CommandList & list, ImTextureData & data, std::uint32_t frameSlot, Error & error) noexcept;

		void DestroyTexture(ImTextureData & data) noexcept;

		[[nodiscard]] DescriptorSetHandle AllocateSet(TextureViewHandle view, Error & error) noexcept;

		[[nodiscard]] std::uint8_t * StageBytes(std::uint32_t frameSlot, std::uint64_t bytes, std::uint64_t & outOffset, Error & error) noexcept;

		[[nodiscard]] bool ReserveGeometry(std::uint32_t frameSlot, std::uint64_t vertexBytes, std::uint64_t indexBytes, Error & error) noexcept;

		void Clear() noexcept;

		void Release() noexcept;

		Device m_device{};
		DescriptorArena * m_arena = nullptr;

		std::uint64_t m_copyAlignment = 1;

		bool m_srgbTarget = false;

		SamplerHandle m_sampler{};
		DescriptorSetLayoutHandle m_setLayout{};
		PipelineLayoutHandle m_pipelineLayout{};
		GraphicsPipelineHandle m_pipeline{};

		struct Frame final
		{
			BufferHandle vertices;
			BufferHandle indices;
			std::uint64_t vertexBytes = 0;
			std::uint64_t indexBytes  = 0;
			std::uint8_t * vertexData = nullptr;
			std::uint8_t * indexData  = nullptr;
			Staging staging;
		};

		std::vector<Frame> m_frames;

		std::vector<Texture> m_registered;

		std::vector<Texture> m_pending;
	};
}

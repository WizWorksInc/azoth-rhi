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
 * \brief The renderer half of a Dear ImGui backend, drawn through the RHI.
 */

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
	/**
	 * \brief What the renderer is built against.
	 */
	struct RendererDesc final
	{
		/**
		 * \brief The arena its descriptor sets come from, borrowed for the renderer's lifetime.
		 *
		 * Taken, not owned for the same reason the resampler takes one: descriptor budget is caller policy. Size it for one set per texture ImGui will have live at
		 * once, which is one font atlas plus whatever the caller registers.
		 */
		DescriptorArena * arena = nullptr;

		/**
		 * \brief The format of the attachment the interface is drawn over, which the pipeline is compiled against.
		 */
		Format colorFormat = Format::eUndefined;

		/**
		 * \brief How many frames the caller keeps in flight, which is how many sets of geometry buffers this holds.
		 *
		 * A frame's vertices have to stay put until the GPU has drawn them, so writing the next frame's into the same buffer would overwrite what is still being
		 * read.
		 */
		std::uint32_t framesInFlight = 2;

		/**
		 * \brief Cache the pipeline is created against, or an invalid handle for none.
		 */
		PipelineCacheHandle cache{};

		const char * debugName = nullptr;
	};

	/**
	 * \brief Draws what Dear ImGui produced.
	 *
	 * ImGui builds triangles and a scissor rectangle and texture identifier per command, and leaves the rest to whoever asked. This is that rest.
	 *
	 * \par Textures
	 * ImGui asks the backend to create, update and destroy them, which UpdateTextures answers. A caller's own image goes in through RegisterTexture, and both
	 * end up as a descriptor set whose handle is the identifier.
	 */
	class Renderer final
	{
	public:
		[[nodiscard]] static Result<Renderer> Create(Device & device, const RendererDesc & desc) noexcept;

		Renderer(const Renderer &)			   = delete;
		Renderer & operator=(const Renderer &) = delete;
		Renderer(Renderer && other) noexcept;
		Renderer & operator=(Renderer && other) noexcept;

		~Renderer();

		/**
		 * \brief Honours ImGui's texture create, update and destroy requests for this frame.
		 *
		 * \attention Records copies and barriers, so call it on a list that is not inside a rendering scope.
		 *
		 * \param frameSlot Which set of staging buffers to write, below RendererDesc::framesInFlight.
		 */
		[[nodiscard]] bool UpdateTextures(CommandList & list, const ImDrawData & drawData, std::uint32_t frameSlot, Error & error) noexcept;

		/**
		 * \brief Records the draws.
		 *
		 * \attention Call inside a rendering scope, on a target of the format the renderer was created for.
		 *
		 * \param frameSlot Which set of geometry buffers to write, below RendererDesc::framesInFlight.
		 */
		[[nodiscard]] bool Record(CommandList & list, const ImDrawData & drawData, std::uint32_t frameSlot, Error & error) noexcept;

		/**
		 * \brief Gives one of the caller's own textures an identifier ImGui can carry, for ImGui::Image.
		 *
		 * Allocates a descriptor set over the view and hands back its handle as the identifier. Asking twice for the same view hands back the same one, so a caller
		 * may do this per frame without leaking sets.
		 *
		 * \return The identifier, or ImTextureID_Invalid when a set could not be allocated.
		 */
		[[nodiscard]] ImTextureID RegisterTexture(TextureViewHandle view, Error & error) noexcept;

		/**
		 * \brief Forgets a texture registered above. The view itself is the caller's and is not touched.
		 */
		void UnregisterTexture(ImTextureID id) noexcept;

		/**
		 * \brief Releases what ImGui asked to be destroyed, once the work naming it has completed.
		 *
		 * A texture ImGui drops is still being read by whatever frames are in flight, so it is held until this says otherwise. A caller that never calls it keeps
		 * every texture ImGui ever made until the renderer is destroyed.
		 */
		bool Retire(RetirePoint safeAfter, Error & error) noexcept;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_pipeline.IsValid();
		}

		// Reachable because Result<T> holds a T by value. An instance made this way is invalid until Create fills it.
		Renderer() = default;

	private:
		// One texture ImGui owns, or one the caller registered. The set is what the identifier names.
		struct Texture final
		{
			TextureHandle texture;
			TextureViewHandle view;
			DescriptorSetHandle set;

			/**
			 * \brief False for one the caller registered, whose texture and view belong to them.
			 */
			bool owned = false;
		};

		// A frame's staging, grown to whatever that frame's texture updates needed and kept at that size.
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

		// What this device wants a copy's buffer offset aligned to, read from its caps, not assumed.
		std::uint64_t m_copyAlignment = 1;

		// True when the target encodes what the shader writes, which decides whether ImGui's colours are linearized first.
		bool m_srgbTarget = false;

		SamplerHandle m_sampler{};
		DescriptorSetLayoutHandle m_setLayout{};
		PipelineLayoutHandle m_pipelineLayout{};
		GraphicsPipelineHandle m_pipeline{};

		// One buffer pair and one staging buffer per frame in flight.
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

		// Registered by the caller, keyed by the view so a repeat registration is not a second set.
		std::vector<Texture> m_registered;

		// Dropped by ImGui or unregistered, waiting for the GPU to be done with them.
		std::vector<Texture> m_pending;
	};
} // namespace azo::rhi::imgui

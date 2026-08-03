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

#include <azoth/rhi/core/enums.hpp>
#include <azoth/rhi/device/api_tags.hpp>
#include <azoth/rhi/host/surface_source.hpp>

#include <cstdint>
#include <functional>
#include <string>

// SDL's own types, forward declared so a sample including this does not also get every SDL header.
struct SDL_Window;
union SDL_Event;
using SDL_MetalView = void *;

namespace fw::platform
{
	/**
	 * \brief How a window is opened.
	 */
	struct Sdl3WindowDesc final
	{
		std::string title	 = "AzothRHI";
		std::uint32_t width	 = 1280;
		std::uint32_t height = 720;

		/**
		 * \brief Whether the user can resize it.
		 *
		 * On by default. A swapchain already reports eOutOfDate when the surface it was made for no longer matches, and every sample handles that by resizing, so a
		 * fixed window is the odd case and not the safe one.
		 */
		bool resizable = true;
	};

	/**
	 * \brief An SDL3 window, presented to the RHI as the surface it can be drawn to.
	 *
	 * In the framework and not in each sample because the three that wanted a window had the same hundred and seventy lines of it. SurfaceSource is shaped so a
	 * window can answer it with no graphics header in sight.
	 *
	 * \attention Destroying this tears down the SDL window and the RHI destroys the surface with the device, so the window has to outlive it.
	 */
	class Sdl3Window final : public azo::rhi::SurfaceSource
	{
	public:
		Sdl3Window() = default;

		Sdl3Window(const Sdl3Window &)			   = delete;
		Sdl3Window & operator=(const Sdl3Window &) = delete;
		Sdl3Window(Sdl3Window &&)				   = delete;
		Sdl3Window & operator=(Sdl3Window &&)	   = delete;

		~Sdl3Window() override;

		/**
		 * \brief Starts SDL and opens the window.
		 *
		 * \param api Which backend the window is for. SDL wants this before it makes one, since a Vulkan window and a Metal one are not the same object.
		 */
		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, const Sdl3WindowDesc & desc = {});

		/**
		 * \brief What each event is offered to before the window acts on it.
		 *
		 * An interface library wants the events too, and a window that read them and threw them away would be unusable with one. This is how they get out.
		 */
		using EventSink = std::function<void(const SDL_Event &)>;

		/**
		 * \brief Drains the event queue, offering each event to onEvent before acting on it.
		 *
		 * \return False once the user has asked to close the window, or pressed escape.
		 */
		[[nodiscard]] bool PumpEvents(const EventSink & onEvent = {});

		/**
		 * \brief Whether the window has changed size since this was last asked, clearing the flag.
		 *
		 * A sample resizes its swapchain when this says so, as well as when an acquire reports the surface out of date. Both happen: the event arrives first on some
		 * platforms and the stale acquire first on others.
		 */
		[[nodiscard]] bool TakeResized() noexcept;

		/**
		 * \brief The drawable size in pixels, which is not the window size on a display with scaling.
		 */
		[[nodiscard]] azo::rhi::Extent2D GetDrawableSize() const;

		/**
		 * \brief Width over height, for a camera's projection. One when the window has no area, so a minimized window does not divide by zero.
		 */
		[[nodiscard]] float GetAspectRatio() const;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_window != nullptr;
		}

		[[nodiscard]] SDL_Window * GetHandle() const noexcept
		{
			return m_window;
		}

		[[nodiscard]] bool Provide(const azo::rhi::SurfaceRequest & request) override;

	private:
		SDL_Window * m_window	  = nullptr;
		SDL_MetalView m_metalView = nullptr;
		bool m_resized			  = false;
	};
} // namespace fw::platform

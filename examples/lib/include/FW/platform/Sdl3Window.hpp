// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <azoth/rhi/core/enums.hpp>
#include <azoth/rhi/device/api_tags.hpp>
#include <azoth/rhi/host/surface_source.hpp>

#include <cstdint>
#include <functional>
#include <string>

struct SDL_Window;
union SDL_Event;
using SDL_MetalView = void *;

namespace fw::platform
{
	struct Sdl3WindowDesc final
	{
		std::string title	 = "AzothRHI";
		std::uint32_t width	 = 1280;
		std::uint32_t height = 720;

		bool resizable = true;
	};

	class Sdl3Window final : public azo::rhi::SurfaceSource
	{
	public:
		Sdl3Window() = default;

		Sdl3Window(const Sdl3Window &)			   = delete;
		Sdl3Window & operator=(const Sdl3Window &) = delete;
		Sdl3Window(Sdl3Window &&)				   = delete;
		Sdl3Window & operator=(Sdl3Window &&)	   = delete;

		~Sdl3Window() override;

		[[nodiscard]] bool Open(azo::rhi::GraphicsApiId api, const Sdl3WindowDesc & desc = {});

		using EventSink = std::function<void(const SDL_Event &)>;

		[[nodiscard]] bool PumpEvents(const EventSink & onEvent = {});

		[[nodiscard]] bool TakeResized() noexcept;

		[[nodiscard]] azo::rhi::Extent2D GetDrawableSize() const;

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
}

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

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fw::assets
{
	struct ImageAsset final
	{
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;

		bool isFloat = false;

		std::vector<std::uint8_t> pixels;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return width != 0 && height != 0 && !pixels.empty();
		}

		[[nodiscard]] std::uint32_t RowPitch() const noexcept
		{
			return width * 4 * (isFloat ? 4 : 1);
		}

		[[nodiscard]] azo::rhi::Format Format(bool srgb) const noexcept
		{
			if (isFloat)
			{
				return azo::rhi::Format::eRGBA32Float;
			}

			return srgb ? azo::rhi::Format::eRGBA8Srgb : azo::rhi::Format::eRGBA8UNorm;
		}
	};

	[[nodiscard]] ImageAsset DecodeImage(std::span<const std::uint8_t> encoded, bool wantFloat, std::string & error);

	[[nodiscard]] ImageAsset LoadImage(const std::filesystem::path & path, bool wantFloat, std::string & error);

	[[nodiscard]] ImageAsset SolidColorImage(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha);

	[[nodiscard]] std::uint32_t MipCount(std::uint32_t width, std::uint32_t height) noexcept;
}

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

#include "FW/assets/ImageAsset.hpp"

#include "FW/utility/AssetPath.hpp"

// STB_IMAGE_STATIC keeps every stb symbol internal to this translation unit, so the framework archive exports none of them and a sample carrying its own copy
// (deccer_cubes does) has nothing to collide with.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <memory>

namespace fw::assets
{
	namespace
	{
		constexpr int kChannels = 4;

		struct PixelDeleter final
		{
			void operator()(void * pixels) const noexcept
			{
				stbi_image_free(pixels);
			}
		};

		[[nodiscard]] const char * FailureReason()
		{
			const char * reason = stbi_failure_reason();
			return reason != nullptr ? reason : "no diagnostic";
		}
	} // namespace

	ImageAsset DecodeImage(const std::span<const std::uint8_t> encoded, const bool wantFloat, std::string & error)
	{
		if (encoded.empty())
		{
			error = "there are no bytes to decode";
			return {};
		}

		int width	 = 0;
		int height	 = 0;
		int channels = 0;

		// Both entry points are asked for four channels, so what comes back is always tightly packed RGBA and the pitch is the width times the element.
		const std::unique_ptr<void, PixelDeleter> pixels(
			wantFloat ? static_cast<void *>(stbi_loadf_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, kChannels))
					  : static_cast<void *>(stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, kChannels)));

		if (pixels == nullptr)
		{
			error = std::format("the image did not decode: {}", FailureReason());
			return {};
		}

		ImageAsset asset{
			.width	 = static_cast<std::uint32_t>(width),
			.height	 = static_cast<std::uint32_t>(height),
			.isFloat = wantFloat,
		};

		const std::size_t bytes = static_cast<std::size_t>(asset.width) * asset.height * kChannels * (wantFloat ? sizeof(float) : sizeof(std::uint8_t));
		asset.pixels.resize(bytes);
		std::memcpy(asset.pixels.data(), pixels.get(), bytes);

		return asset;
	}

	ImageAsset LoadImage(const std::filesystem::path & path, const bool wantFloat, std::string & error)
	{
		const std::filesystem::path resolved = path.is_absolute() ? path : util::AssetPath(path);
		if (resolved.empty())
		{
			error = std::format("no asset named {} was found beside the executable", path.generic_string());
			return {};
		}

		const std::vector<std::uint8_t> encoded = util::ReadFile(resolved);
		if (encoded.empty())
		{
			error = std::format("{} could not be read", resolved.generic_string());
			return {};
		}

		return DecodeImage(encoded, wantFloat, error);
	}

	ImageAsset SolidColorImage(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue, const std::uint8_t alpha)
	{
		return ImageAsset{
			.width	 = 1,
			.height	 = 1,
			.isFloat = false,
			.pixels	 = { red, green, blue, alpha },
		};
	}

	std::uint32_t MipCount(const std::uint32_t width, const std::uint32_t height) noexcept
	{
		const std::uint32_t longest = std::max(width, height);

		// A chain halves the longest side until it reaches one, so its length is the position of that side's highest set bit plus one.
		return longest == 0 ? 1 : static_cast<std::uint32_t>(std::bit_width(longest));
	}
} // namespace fw::assets

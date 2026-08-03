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

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fw::assets
{
	/**
	 * \brief A decoded image, always four channels, either eight bits or thirty two bits per channel.
	 *
	 * Four channels because that is what every backend can sample without asking about format support, and a three channel source is the one case where
	 * hardware coverage is genuinely patchy. The decoder expands to four without making each caller check.
	 */
	struct ImageAsset final
	{
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;

		/**
		 * \brief True when pixels holds floats, not bytes, which is what an HDR source decodes to.
		 */
		bool isFloat = false;

		std::vector<std::uint8_t> pixels;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return width != 0 && height != 0 && !pixels.empty();
		}

		/**
		 * \brief Bytes one row occupies.
		 */
		[[nodiscard]] std::uint32_t RowPitch() const noexcept
		{
			return width * 4 * (isFloat ? 4 : 1);
		}

		/**
		 * \brief The format this decodes to, given whether its content is colour or data.
		 *
		 * \param srgb True for a texture holding colour a shader wants in linear light, which is base colour and emissive. False for one holding numbers, which
		 * is normals, roughness, metalness and occlusion. Ignored for a float image, which is already linear.
		 */
		[[nodiscard]] azo::rhi::Format Format(bool srgb) const noexcept
		{
			if (isFloat)
			{
				return azo::rhi::Format::eRGBA32Float;
			}

			return srgb ? azo::rhi::Format::eRGBA8Srgb : azo::rhi::Format::eRGBA8UNorm;
		}
	};

	/**
	 * \brief Decodes an image already in memory, which is where a glTF keeps one that is not a file of its own.
	 *
	 * \param encoded The file's bytes, in any format stb_image reads.
	 * \param wantFloat True to decode to 32 bit floats, for an HDR source.
	 * \param error Filled with what went wrong when this returns an invalid asset.
	 */
	[[nodiscard]] ImageAsset DecodeImage(std::span<const std::uint8_t> encoded, bool wantFloat, std::string & error);

	/**
	 * \brief Reads and decodes an image file.
	 *
	 * \param path Where the file is. A relative path is resolved through fw::util::AssetPath, so a sample names its asset and not its build tree.
	 */
	[[nodiscard]] ImageAsset LoadImage(const std::filesystem::path & path, bool wantFloat, std::string & error);

	/**
	 * \brief A single pixel image, for standing in where a material names a texture the document did not carry.
	 *
	 * \param rgba The colour, as four bytes.
	 */
	[[nodiscard]] ImageAsset SolidColorImage(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha);

	/**
	 * \brief How many mip levels a texture of this size has, counting the top.
	 */
	[[nodiscard]] std::uint32_t MipCount(std::uint32_t width, std::uint32_t height) noexcept;
} // namespace fw::assets

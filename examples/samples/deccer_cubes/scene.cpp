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

#include "scene.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace deccer
{
	namespace
	{

		constexpr int kRgba = 4;

		struct PixelDeleter final
		{
			void operator()(stbi_uc * pixels) const noexcept
			{
				stbi_image_free(pixels);
			}
		};

		// The inverse transpose of the model's upper 3x3, as the three rows the shader dots a normal against. The adjugate over the determinant which is the inverse
		// transpose without a separate transpose step.
		[[nodiscard]] std::array<float, 12> NormalRows(const std::array<float, 16> & model)
		{
			const float a00 = model.at(0);
			const float a10 = model.at(1);
			const float a20 = model.at(2);
			const float a01 = model.at(4);
			const float a11 = model.at(5);
			const float a21 = model.at(6);
			const float a02 = model.at(8);
			const float a12 = model.at(9);
			const float a22 = model.at(10);

			const std::array<float, 9> cofactors{
				(a11 * a22) - (a12 * a21),
				-((a10 * a22) - (a12 * a20)),
				(a10 * a21) - (a11 * a20),
				-((a01 * a22) - (a02 * a21)),
				(a00 * a22) - (a02 * a20),
				-((a00 * a21) - (a01 * a20)),
				(a01 * a12) - (a02 * a11),
				-((a00 * a12) - (a02 * a10)),
				(a00 * a11) - (a01 * a10),
			};

			const float determinant = (a00 * cofactors.at(0)) + (a01 * cofactors.at(1)) + (a02 * cofactors.at(2));
			const float scale		= determinant != 0.0f ? 1.0f / determinant : 0.0f;

			std::array<float, 12> rows{};
			for (std::size_t row = 0; row < 3; ++row)
			{
				rows.at((row * 4) + 0) = cofactors.at((row * 3) + 0) * scale;
				rows.at((row * 4) + 1) = cofactors.at((row * 3) + 1) * scale;
				rows.at((row * 4) + 2) = cofactors.at((row * 3) + 2) * scale;
			}

			return rows;
		}

		[[nodiscard]] std::vector<std::uint8_t> ReadFile(const std::string & path)
		{
			const std::unique_ptr<std::FILE, int (*)(std::FILE *)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
			if (file == nullptr)
			{
				return {};
			}

			// Asked of the stream, not inferred from the count, so neither end of file nor a read error is followed by another read. Looping on a non-zero count instead
			// reads once more from a stream already spent, and hands back a truncated file as though it were whole.
			std::vector<std::uint8_t> bytes;
			std::array<std::uint8_t, 4096> chunk{};
			while (std::feof(file.get()) == 0 && std::ferror(file.get()) == 0)
			{
				const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get());
				bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
			}

			if (std::ferror(file.get()) != 0)
			{
				return {};
			}

			return bytes;
		}

		// A glTF image is either a slice of a buffer or a file beside the document. Both end up as RGBA8 here.
		[[nodiscard]] bool DecodeImage(
			const fastgltf::Asset & asset, const fastgltf::Image & image, const std::filesystem::path & directory, Image & out, std::string & error)
		{
			const std::string_view name = image.name.empty() ? std::string_view("unnamed") : std::string_view(image.name);

			std::vector<std::uint8_t> owned;
			std::span<const std::uint8_t> encoded;

			/*
			 * The parser was asked to load external buffers, so a URI that named a file has already become bytes and only the two in-memory forms are left. A URI
			 * arriving here is one the parser was told to leave alone, which this reads itself.
			 */
			std::visit(fastgltf::visitor{
						   [](auto &) {},
						   [&](const fastgltf::sources::Array & array)
						   {
							   encoded = std::span(reinterpret_cast<const std::uint8_t *>(array.bytes.data()), array.bytes.size());
						   },
						   [&](const fastgltf::sources::Vector & vector)
						   {
							   encoded = std::span(reinterpret_cast<const std::uint8_t *>(vector.bytes.data()), vector.bytes.size());
						   },
						   [&](const fastgltf::sources::BufferView & view)
						   {
							   const fastgltf::BufferView & bufferView = asset.bufferViews[view.bufferViewIndex];
							   const fastgltf::Buffer & buffer		   = asset.buffers[bufferView.bufferIndex];
							   std::visit(fastgltf::visitor{
											  [](auto &) {},
											  [&](const fastgltf::sources::Array & array)
											  {
												  encoded = std::span(reinterpret_cast<const std::uint8_t *>(array.bytes.data()) + bufferView.byteOffset,
													  bufferView.byteLength);
											  },
											  [&](const fastgltf::sources::Vector & vector)
											  {
												  encoded = std::span(reinterpret_cast<const std::uint8_t *>(vector.bytes.data()) + bufferView.byteOffset,
													  bufferView.byteLength);
											  },
										  },
								   buffer.data);
						   },
						   [&](const fastgltf::sources::URI & uri)
						   {
							   owned = ReadFile((directory / std::string_view(uri.uri.path())).string());
							   if (!owned.empty())
							   {
								   encoded = owned;
							   }
						   },
					   },
				image.data);

			if (encoded.empty())
			{
				error = std::format("image {} is neither in a buffer nor a file beside the document", name);
				return false;
			}

			int width	 = 0;
			int height	 = 0;
			int channels = 0;
			const std::unique_ptr<stbi_uc, PixelDeleter> pixels(
				stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, kRgba));

			if (pixels == nullptr)
			{
				error = std::format("image {} did not decode: {}", name, stbi_failure_reason());
				return false;
			}

			const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kRgba;
			out.width				= static_cast<std::uint32_t>(width);
			out.height				= static_cast<std::uint32_t>(height);
			out.pixels.assign(pixels.get(), pixels.get() + bytes); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			return true;
		}

		// The image a material samples for base colour, or nothing when it names none.
		[[nodiscard]] fastgltf::Optional<std::size_t> BaseColorImage(const fastgltf::Asset & asset, fastgltf::Optional<std::size_t> materialIndex)
		{
			if (!materialIndex.has_value())
			{
				return std::nullopt;
			}

			const fastgltf::Material & material = asset.materials[*materialIndex];
			if (!material.pbrData.baseColorTexture.has_value())
			{
				return std::nullopt;
			}

			return asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex;
		}

		[[nodiscard]] bool AppendPrimitive(const fastgltf::Asset & asset, const fastgltf::Primitive & primitive, const std::array<float, 16> & model,
			std::uint32_t textureIndex, Scene & scene, std::string & error)
		{
			const auto * position = primitive.findAttribute("POSITION");
			const auto * normal	  = primitive.findAttribute("NORMAL");
			const auto * uv		  = primitive.findAttribute("TEXCOORD_0");

			if (position == primitive.attributes.end() || normal == primitive.attributes.end() || uv == primitive.attributes.end() ||
				!primitive.indicesAccessor.has_value())
			{
				error = "a primitive is missing a position, a normal, a texture coordinate or an index buffer";
				return false;
			}

			const fastgltf::Accessor & positions = asset.accessors[position->accessorIndex];
			const fastgltf::Accessor & normals	 = asset.accessors[normal->accessorIndex];
			const fastgltf::Accessor & uvs		 = asset.accessors[uv->accessorIndex];
			const fastgltf::Accessor & indices	 = asset.accessors[*primitive.indicesAccessor];

			const fastgltf::Material & material = asset.materials[*primitive.materialIndex];

			const Draw draw{
				.model		  = model,
				.normalRows	  = NormalRows(model),
				.firstIndex	  = static_cast<std::uint32_t>(scene.indices.size()),
				.indexCount	  = static_cast<std::uint32_t>(indices.count),
				.vertexOffset = static_cast<std::int32_t>(scene.vertices.size()),
				.textureIndex = textureIndex,
				.metallic	  = material.pbrData.metallicFactor,
				.roughness	  = material.pbrData.roughnessFactor,
			};

			// Grown once and then filled per attribute, since each accessor is walked on its own and they are three views of the same vertices.
			const std::size_t base = scene.vertices.size();
			scene.vertices.resize(base + positions.count);

			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positions,
				[&](const fastgltf::math::fvec3 value, const std::size_t index)
				{
					scene.vertices[base + index].position = { value.x(), value.y(), value.z() };
				});

			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, normals,
				[&](const fastgltf::math::fvec3 value, const std::size_t index)
				{
					scene.vertices[base + index].normal = { value.x(), value.y(), value.z() };
				});

			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, uvs,
				[&](const fastgltf::math::fvec2 value, const std::size_t index)
				{
					scene.vertices[base + index].uv = { value.x(), value.y() };
				});

			scene.indices.reserve(scene.indices.size() + indices.count);
			fastgltf::iterateAccessor<std::uint32_t>(asset, indices,
				[&](const std::uint32_t index)
				{
					scene.indices.push_back(index);
				});

			scene.draws.push_back(draw);
			return true;
		}

	} // namespace

	bool Load(const char * path, Scene & scene, std::string & error)
	{
		const std::filesystem::path document(path);

		auto data = fastgltf::GltfDataBuffer::FromPath(document);
		if (data.error() != fastgltf::Error::None)
		{
			error = std::format("{} is not a glTF document this can read", path);
			return false;
		}

		// LoadExternalBuffers so a document keeping its buffers beside it arrives with the bytes already in hand, which is what lets the accessor walks below read
		// straight through without a second pass over the URIs.
		fastgltf::Parser parser;
		auto loaded = parser.loadGltf(data.get(), document.parent_path(), fastgltf::Options::LoadExternalBuffers);
		if (loaded.error() != fastgltf::Error::None)
		{
			error = std::format("{} did not parse: {}", path, fastgltf::getErrorMessage(loaded.error()));
			return false;
		}

		const fastgltf::Asset & asset = loaded.get();
		if (asset.scenes.empty())
		{
			error = std::format("{} has no scene to draw", path);
			return false;
		}

		// One image per glTF image, however many materials point at it.
		std::vector<std::size_t> seen;
		const auto imageIndex = [&](const std::size_t image) -> std::uint32_t
		{
			for (std::size_t i = 0; i < seen.size(); ++i)
			{
				if (seen.at(i) == image)
				{
					return static_cast<std::uint32_t>(i);
				}
			}

			seen.push_back(image);
			return static_cast<std::uint32_t>(seen.size() - 1);
		};

		// The default scene and not the node array, so a node the document leaves out of it stays out of the picture. fastgltf walks the parent chain and hands back
		// a world matrix, which is the part of this asset an engine is meant to get wrong.
		const std::size_t root = asset.defaultScene.value_or(0);

		bool ok = true;
		fastgltf::iterateSceneNodes(asset, root, fastgltf::math::fmat4x4(),
			[&](const fastgltf::Node & node, const fastgltf::math::fmat4x4 & world)
			{
				if (!ok || !node.meshIndex.has_value())
				{
					return;
				}

				std::array<float, 16> model{};
				for (std::size_t column = 0; column < 4; ++column)
				{
					for (std::size_t row = 0; row < 4; ++row)
					{
						model.at((column * 4) + row) = world[static_cast<int>(column)][static_cast<int>(row)];
					}
				}

				for (const fastgltf::Primitive & primitive : asset.meshes[*node.meshIndex].primitives)
				{
					if (primitive.type != fastgltf::PrimitiveType::Triangles)
					{
						error = "a primitive is not a triangle list";
						ok	  = false;
						return;
					}

					const fastgltf::Optional<std::size_t> image = BaseColorImage(asset, primitive.materialIndex);
					if (!image.has_value())
					{
						error = "a primitive has no base color texture";
						ok	  = false;
						return;
					}

					if (!AppendPrimitive(asset, primitive, model, imageIndex(*image), scene, error))
					{
						ok = false;
						return;
					}
				}
			});

		if (!ok)
		{
			return false;
		}

		scene.images.resize(seen.size());
		for (std::size_t i = 0; i < seen.size(); ++i)
		{
			if (!DecodeImage(asset, asset.images[seen.at(i)], document.parent_path(), scene.images.at(i), error))
			{
				return false;
			}
		}

		return true;
	}

} // namespace deccer

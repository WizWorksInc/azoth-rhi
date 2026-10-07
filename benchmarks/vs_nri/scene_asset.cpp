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

#include "scene_asset.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace vsnri
{

	namespace
	{

		constexpr std::uint64_t kMipAlignment = 16;

		constexpr std::uint32_t kDdsMipMapCount = 0x20000;

		constexpr std::uint32_t kDxgiBC1Typeless = 70;
		constexpr std::uint32_t kDxgiBC1UNorm	 = 71;
		constexpr std::uint32_t kDxgiBC1Srgb	 = 72;
		constexpr std::uint32_t kDxgiBC7Typeless = 97;
		constexpr std::uint32_t kDxgiBC7UNorm	 = 98;
		constexpr std::uint32_t kDxgiBC7Srgb	 = 99;

		struct PixelDeleter final
		{
			void operator()(stbi_uc * pixels) const noexcept
			{
				stbi_image_free(pixels);
			}
		};

		[[nodiscard]] std::vector<std::byte> ReadFile(const std::filesystem::path & path)
		{
			const std::unique_ptr<std::FILE, int (*)(std::FILE *)> file(std::fopen(path.string().c_str(), "rb"), &std::fclose);
			if (file == nullptr)
			{
				return {};
			}

			std::vector<std::byte> bytes;
			std::array<std::byte, 65536> chunk{};
			while (std::feof(file.get()) == 0 && std::ferror(file.get()) == 0)
			{
				const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get());
				bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
			}

			return std::ferror(file.get()) != 0 ? std::vector<std::byte>{} : bytes;
		}

		[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes, const std::size_t at)
		{
			std::uint32_t value = 0;
			std::memcpy(&value, bytes.data() + at, sizeof(value));
			return value;
		}

		[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		[[nodiscard]] bool SameFourCC(const std::span<const std::byte> bytes, const std::size_t at, const std::string_view code)
		{
			for (std::size_t i = 0; i < code.size(); ++i)
			{
				if (static_cast<char>(bytes[at + i]) != code[i])
				{
					return false;
				}
			}

			return true;
		}

		// DDS is a fixed little-endian header followed by every mip, largest first, so it is read by offset.
		[[nodiscard]] bool DecodeDds(const std::span<const std::byte> file, SceneTexture & texture, std::string & error)
		{
			constexpr std::size_t kHeaderEnd	 = 128;
			constexpr std::size_t kDx10HeaderEnd = 148;

			if (file.size() < kHeaderEnd || !SameFourCC(file, 0, "DDS "))
			{
				error = "the file is not a DDS image";
				return false;
			}

			const std::uint32_t flags	 = ReadU32(file, 8);
			const std::uint32_t height	 = ReadU32(file, 12);
			const std::uint32_t width	 = ReadU32(file, 16);
			const std::uint32_t mipCount = (flags & kDdsMipMapCount) != 0 ? std::max(ReadU32(file, 28), 1u) : 1u;

			std::size_t dataStart = kHeaderEnd;
			if (SameFourCC(file, 84, "DXT1"))
			{
				texture.format = SceneTextureFormat::eBC1;
			}
			else if (SameFourCC(file, 84, "DX10") && file.size() >= kDx10HeaderEnd)
			{
				dataStart				 = kDx10HeaderEnd;
				const std::uint32_t dxgi = ReadU32(file, 128);
				if (dxgi == kDxgiBC1Typeless || dxgi == kDxgiBC1UNorm || dxgi == kDxgiBC1Srgb)
				{
					texture.format = SceneTextureFormat::eBC1;
				}
				else if (dxgi == kDxgiBC7Typeless || dxgi == kDxgiBC7UNorm || dxgi == kDxgiBC7Srgb)
				{
					texture.format = SceneTextureFormat::eBC7;
				}
				else
				{
					error = std::format("DXGI format {} is not one this loader uploads", dxgi);
					return false;
				}
			}
			else
			{
				error = "the DDS image is neither DXT1 nor a DX10 block format";
				return false;
			}

			const std::uint32_t blockBytes = texture.format == SceneTextureFormat::eBC1 ? 8 : 16;
			texture.width				   = width;
			texture.height				   = height;

			std::uint64_t source = dataStart;
			std::uint64_t target = 0;
			for (std::uint32_t mip = 0; mip < mipCount; ++mip)
			{
				SceneMip level{};
				level.width	   = std::max(width >> mip, 1u);
				level.height   = std::max(height >> mip, 1u);
				level.rowPitch = (level.width + 3) / 4 * blockBytes;
				level.rows	   = (level.height + 3) / 4;
				level.size	   = std::uint64_t{ level.rowPitch } * level.rows;
				level.offset   = AlignUp(target, kMipAlignment);

				if (source + level.size > file.size())
				{
					error = std::format("mip {} runs past the end of the file", mip);
					return false;
				}

				texture.bytes.resize(level.offset + level.size);
				std::memcpy(texture.bytes.data() + level.offset, file.data() + source, level.size);
				texture.mips.push_back(level);

				source += level.size;
				target = level.offset + level.size;
			}

			return true;
		}

		[[nodiscard]] bool DecodeStb(const std::span<const std::byte> file, SceneTexture & texture, std::string & error)
		{
			constexpr int kRgba = 4;

			int width	 = 0;
			int height	 = 0;
			int channels = 0;
			const std::unique_ptr<stbi_uc, PixelDeleter> pixels(
				stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(file.data()), static_cast<int>(file.size()), &width, &height, &channels, kRgba)
			);
			if (pixels == nullptr)
			{
				error = stbi_failure_reason();
				return false;
			}

			SceneMip level{};
			level.width	   = static_cast<std::uint32_t>(width);
			level.height   = static_cast<std::uint32_t>(height);
			level.rowPitch = level.width * kRgba;
			level.rows	   = level.height;
			level.size	   = std::uint64_t{ level.rowPitch } * level.rows;

			texture.format = SceneTextureFormat::eRGBA8;
			texture.width  = level.width;
			texture.height = level.height;
			texture.mips.push_back(level);
			texture.bytes.resize(level.size);
			std::memcpy(texture.bytes.data(), pixels.get(), level.size);
			return true;
		}

		[[nodiscard]] SceneTexture SolidTexture(const std::array<std::uint8_t, 4> & rgba)
		{
			SceneTexture texture{};
			texture.width  = 1;
			texture.height = 1;
			texture.mips.push_back(SceneMip{ .width = 1, .height = 1, .offset = 0, .size = 4, .rowPitch = 4, .rows = 1 });
			texture.bytes.resize(4);
			std::memcpy(texture.bytes.data(), rgba.data(), rgba.size());
			return texture;
		}

		[[nodiscard]] bool LoadImage(
			const fastgltf::Image & image,
			const std::filesystem::path & documentDirectory,
			const std::filesystem::path & textureDirectory,
			SceneTexture & texture,
			std::string & error
		)
		{
			const auto * uri = std::get_if<fastgltf::sources::URI>(&image.data);
			if (uri == nullptr)
			{
				error = std::format("image {} is not a file, and only file images are loaded", image.name);
				return false;
			}

			const std::filesystem::path relative(std::string(uri->uri.path()));
			std::filesystem::path path = documentDirectory / relative;
			if (!std::filesystem::exists(path))
			{
				path = textureDirectory / relative.filename();
			}

			const std::vector<std::byte> file = ReadFile(path);
			if (file.empty())
			{
				error = std::format("{} could not be read", path.string());
				return false;
			}

			const bool dds = file.size() >= 4 && SameFourCC(file, 0, "DDS ");
			if (!(dds ? DecodeDds(file, texture, error) : DecodeStb(file, texture, error)))
			{
				error = std::format("{}: {}", path.string(), error);
				return false;
			}

			return true;
		}

		[[nodiscard]] Matrix ToMatrix(const fastgltf::math::fmat4x4 & world)
		{
			Matrix model{};
			for (std::size_t column = 0; column < 4; ++column)
			{
				for (std::size_t row = 0; row < 4; ++row)
				{
					model.at((column * 4) + row) = world[static_cast<int>(column)][static_cast<int>(row)];
				}
			}

			return model;
		}

		[[nodiscard]] bool AppendPrimitive(const fastgltf::Asset & asset, const fastgltf::Primitive & primitive, SceneAsset & scene, std::string & error)
		{
			const auto * position = primitive.findAttribute("POSITION");
			const auto * normal	  = primitive.findAttribute("NORMAL");
			const auto * uv		  = primitive.findAttribute("TEXCOORD_0");
			if (position == primitive.attributes.end() || !primitive.indicesAccessor.has_value())
			{
				error = "a primitive has no positions or no indices";
				return false;
			}

			const fastgltf::Accessor & positions = asset.accessors[position->accessorIndex];
			const fastgltf::Accessor & indices	 = asset.accessors[*primitive.indicesAccessor];

			const std::size_t base = scene.vertices.size();
			if (base + positions.count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
			{
				error = "the scene has more vertices than a signed vertex offset reaches";
				return false;
			}
			scene.vertices.resize(base + positions.count);

			std::array<float, 3> low{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			std::array<float, 3> high{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
				asset,
				positions,
				[&](const fastgltf::math::fvec3 value, const std::size_t index)
				{
					scene.vertices[base + index].position = { value.x(), value.y(), value.z() };
					for (std::size_t axis = 0; axis < 3; ++axis)
					{
						low.at(axis)  = std::min(low.at(axis), value[axis]);
						high.at(axis) = std::max(high.at(axis), value[axis]);
					}
				}
			);

			if (normal != primitive.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
					asset,
					asset.accessors[normal->accessorIndex],
					[&](const fastgltf::math::fvec3 value, const std::size_t index)
					{
						scene.vertices[base + index].normal = { value.x(), value.y(), value.z() };
					}
				);
			}

			if (uv != primitive.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
					asset,
					asset.accessors[uv->accessorIndex],
					[&](const fastgltf::math::fvec2 value, const std::size_t index)
					{
						scene.vertices[base + index].uv = { value.x(), value.y() };
					}
				);
			}

			SceneMesh mesh{};
			mesh.firstIndex	  = static_cast<std::uint32_t>(scene.indices.size());
			mesh.indexCount	  = static_cast<std::uint32_t>(indices.count);
			mesh.vertexOffset = static_cast<std::int32_t>(base);

			const std::array center{ (low[0] + high[0]) * 0.5f, (low[1] + high[1]) * 0.5f, (low[2] + high[2]) * 0.5f };
			const float radius =
				0.5f *
				std::sqrt(((high[0] - low[0]) * (high[0] - low[0])) + ((high[1] - low[1]) * (high[1] - low[1])) + ((high[2] - low[2]) * (high[2] - low[2])));
			mesh.bounds = { center[0], center[1], center[2], radius };

			scene.indices.reserve(scene.indices.size() + indices.count);
			fastgltf::iterateAccessor<std::uint32_t>(
				asset,
				indices,
				[&](const std::uint32_t index)
				{
					scene.indices.push_back(index);
				}
			);

			scene.triangles += indices.count / 3;
			scene.meshes.push_back(mesh);
			return true;
		}

	}

	bool LoadScene(const std::filesystem::path & document, const std::filesystem::path & textureDirectory, SceneAsset & scene, std::string & error)
	{
		auto data = fastgltf::GltfDataBuffer::FromPath(document);
		if (data.error() != fastgltf::Error::None)
		{
			error = std::format("{} is not a glTF document this can read", document.string());
			return false;
		}

		fastgltf::Parser parser;
		auto loaded = parser.loadGltf(data.get(), document.parent_path(), fastgltf::Options::LoadExternalBuffers);
		if (loaded.error() != fastgltf::Error::None)
		{
			error = std::format("{} did not parse: {}", document.string(), fastgltf::getErrorMessage(loaded.error()));
			return false;
		}

		const fastgltf::Asset & asset = loaded.get();
		if (asset.scenes.empty())
		{
			error = std::format("{} has no scene to draw", document.string());
			return false;
		}

		scene.textures.resize(asset.images.size());
		for (std::size_t image = 0; image < asset.images.size(); ++image)
		{
			if (!LoadImage(asset.images[image], document.parent_path(), textureDirectory, scene.textures.at(image), error))
			{
				return false;
			}
		}

		const auto white	  = static_cast<std::uint32_t>(scene.textures.size());
		const auto flatNormal = white + 1;
		scene.textures.push_back(SolidTexture({ 255, 255, 255, 255 }));
		scene.textures.push_back(SolidTexture({ 128, 128, 255, 255 }));

		const auto imageOf = [&](const std::size_t textureIndex, const std::uint32_t fallback)
		{
			const fastgltf::Optional<std::size_t> & image = asset.textures[textureIndex].imageIndex;
			return image.has_value() ? static_cast<std::uint32_t>(*image) : fallback;
		};

		for (const fastgltf::Material & source : asset.materials)
		{
			SceneMaterial material{};
			material.doubleSided = source.doubleSided;
			material.textures	 = { white, flatNormal, white };
			if (source.pbrData.baseColorTexture.has_value())
			{
				material.textures[0] = imageOf(source.pbrData.baseColorTexture->textureIndex, white);
			}
			if (source.normalTexture.has_value())
			{
				material.textures[1] = imageOf(source.normalTexture->textureIndex, flatNormal);
			}
			if (source.pbrData.metallicRoughnessTexture.has_value())
			{
				material.textures[2] = imageOf(source.pbrData.metallicRoughnessTexture->textureIndex, white);
			}
			scene.materials.push_back(material);
		}

		const auto defaultMaterial = static_cast<std::uint32_t>(scene.materials.size());
		scene.materials.push_back(SceneMaterial{ .textures = { white, flatNormal, white }, .doubleSided = false });

		// The first scene mesh each glTF mesh became, so a mesh several nodes share is stored once.
		std::vector<std::uint32_t> firstMesh(asset.meshes.size(), std::numeric_limits<std::uint32_t>::max());

		bool ok = true;
		fastgltf::iterateSceneNodes(
			asset,
			asset.defaultScene.value_or(0),
			fastgltf::math::fmat4x4(),
			[&](const fastgltf::Node & node, const fastgltf::math::fmat4x4 & world)
			{
				if (!ok || !node.meshIndex.has_value())
				{
					return;
				}

				const std::size_t meshIndex = *node.meshIndex;
				const auto & primitives		= asset.meshes[meshIndex].primitives;
				if (firstMesh.at(meshIndex) == std::numeric_limits<std::uint32_t>::max())
				{
					firstMesh.at(meshIndex) = static_cast<std::uint32_t>(scene.meshes.size());
					for (const fastgltf::Primitive & primitive : primitives)
					{
						if (primitive.type != fastgltf::PrimitiveType::Triangles)
						{
							error = "a primitive is not a triangle list";
							ok	  = false;
							return;
						}

						if (!AppendPrimitive(asset, primitive, scene, error))
						{
							ok = false;
							return;
						}
					}
				}

				const Matrix model = ToMatrix(world);
				for (std::size_t primitive = 0; primitive < primitives.size(); ++primitive)
				{
					const fastgltf::Optional<std::size_t> & material = primitives[primitive].materialIndex;
					scene.instances.push_back(
						SceneInstance{
							.mesh	  = firstMesh.at(meshIndex) + static_cast<std::uint32_t>(primitive),
							.material = material.has_value() ? static_cast<std::uint32_t>(*material) : defaultMaterial,
							.model	  = model,
						}
					);
				}
			}
		);

		if (ok && scene.instances.empty())
		{
			error = std::format("{} draws nothing", document.string());
			ok	  = false;
		}

		return ok;
	}

	std::filesystem::path DefaultSceneDataDirectory()
	{
#ifdef VSNRI_SCENE_DATA_DIR
		return std::filesystem::path(VSNRI_SCENE_DATA_DIR);
#else
		return {};
#endif
	}

}

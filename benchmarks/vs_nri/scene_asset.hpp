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

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vsnri
{

	struct SceneVertex final
	{
		std::array<float, 3> position{};
		std::array<float, 3> normal{};
		std::array<float, 2> uv{};
	};

	inline constexpr std::uint32_t kSceneVertexStride = sizeof(SceneVertex);

	static_assert(kSceneVertexStride == 32);

	enum class SceneTextureFormat : std::uint8_t
	{
		eRGBA8,
		eBC1,
		eBC7,
	};

	struct SceneMip final
	{
		std::uint32_t width	   = 0;
		std::uint32_t height   = 0;
		std::uint64_t offset   = 0;
		std::uint64_t size	   = 0;
		std::uint32_t rowPitch = 0;
		std::uint32_t rows	   = 0;
	};

	struct SceneTexture final
	{
		SceneTextureFormat format = SceneTextureFormat::eRGBA8;
		std::uint32_t width		  = 0;
		std::uint32_t height	  = 0;
		std::vector<SceneMip> mips;
		std::vector<std::byte> bytes;
	};

	struct SceneMesh final
	{
		std::uint32_t firstIndex  = 0;
		std::uint32_t indexCount  = 0;
		std::int32_t vertexOffset = 0;

		// Center and radius in the mesh's own space.
		std::array<float, 4> bounds{};
	};

	// Base color, normal and metallic-roughness, in that order.
	inline constexpr std::uint32_t kTexturesAMaterial = 3;

	struct SceneMaterial final
	{
		std::array<std::uint32_t, kTexturesAMaterial> textures{};
		bool doubleSided = false;
	};

	// Column-major, as glTF and the shaders store it.
	using Matrix = std::array<float, 16>;

	struct SceneInstance final
	{
		std::uint32_t mesh	   = 0;
		std::uint32_t material = 0;
		Matrix model{};
	};

	// Everything both arms upload, decoded once so neither library's loader is part of the comparison.
	struct SceneAsset final
	{
		std::vector<SceneVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<SceneMesh> meshes;
		std::vector<SceneMaterial> materials;
		std::vector<SceneTexture> textures;
		std::vector<SceneInstance> instances;
		std::uint64_t triangles = 0;
	};

	// Images the document names by file alone are looked for in textureDirectory, which is where NRI's sample data keeps them.
	[[nodiscard]] bool LoadScene(
		const std::filesystem::path & document,
		const std::filesystem::path & textureDirectory,
		SceneAsset & scene,
		std::string & error
	);

	[[nodiscard]] std::filesystem::path DefaultSceneDataDirectory();

}

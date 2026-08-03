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
#include <cstdint>
#include <string>
#include <vector>

namespace deccer
{

	struct Vertex final
	{
		std::array<float, 3> position{};
		std::array<float, 3> normal{};
		std::array<float, 2> uv{};
	};

	struct Draw final
	{
		// Column major, the order glTF stores a matrix in and the order the shader reads one.
		std::array<float, 16> model{};

		// The inverse transpose of the model's upper 3x3, as three rows padded to four floats each. Those non-uniform scales mean a normal transformed by the
		// model matrix comes out pointing somewhere else.
		std::array<float, 12> normalRows{};

		std::uint32_t firstIndex   = 0;
		std::uint32_t indexCount   = 0;
		std::int32_t vertexOffset  = 0;
		std::uint32_t textureIndex = 0;

		float metallic	= 0.0f;
		float roughness = 1.0f;
	};

	// Decoded RGBA8 that is ready for an upload buffer.
	struct Image final
	{
		std::uint32_t width	 = 0;
		std::uint32_t height = 0;
		std::vector<std::uint8_t> pixels;
	};

	struct Scene final
	{
		std::vector<Vertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<Draw> draws;
		std::vector<Image> images;
	};

	// Reads a .gltf or .glb
	[[nodiscard]] bool Load(const char * path, Scene & scene, std::string & error);

} // namespace deccer

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "Scene.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fw::scene
{
	enum class ComponentType : std::uint8_t
	{
		eSignedByte,
		eUnsignedByte,
		eSignedShort,
		eUnsignedShort,
		eUnsignedInt,
		eFloat,
	};

	enum class DataType : std::uint8_t
	{
		eScalar,
		eVec2,
		eVec3,
		eVec4,
		eMat2,
		eMat3,
		eMat4,
	};

	struct BufferView final
	{
		const std::uint8_t * data = nullptr;
		std::size_t byteOffset	  = 0;
		std::size_t byteLength	  = 0;

		std::size_t byteStride = 0;
	};

	struct Accessor final
	{
		BufferView bufferView{};
		std::size_t byteOffset		= 0;
		ComponentType componentType = ComponentType::eFloat;
		DataType type				= DataType::eVec3;
		std::size_t count			= 0;
	};

	struct MeshPrimitive final
	{
		std::string name;
		std::unordered_map<AttributeType, Accessor> attributes;
		Accessor indices{};
	};

	struct MeshData final
	{
		std::string name;
		std::vector<glm::vec3> positions;
		std::vector<glm::vec2> uvs;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec4> tangents;

		std::vector<std::uint32_t> indices;

		[[nodiscard]] MeshPrimitive View() const;
	};

	[[nodiscard]] std::uint32_t GetComponentSize(ComponentType componentType) noexcept;

	[[nodiscard]] std::uint32_t GetComponentCount(DataType dataType) noexcept;

	[[nodiscard]] std::uint32_t GetElementSize(const Accessor & accessor) noexcept;

	[[nodiscard]] const std::uint8_t * GetElement(const Accessor & accessor, std::size_t index) noexcept;

	[[nodiscard]] std::uint32_t ReadIndex(const Accessor & accessor, std::size_t index) noexcept;
}

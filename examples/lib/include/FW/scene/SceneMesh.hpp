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

	/**
	 * \brief A borrowed span of source bytes an accessor reads from.
	 *
	 * The naming follows glTF because that is where meshes come from, and a builtin primitive describes itself the same way so both reach the storage
	 * through one path.
	 */
	struct BufferView final
	{
		const std::uint8_t * data = nullptr;
		std::size_t byteOffset	  = 0;
		std::size_t byteLength	  = 0;

		/**
		 * \brief Distance between consecutive elements. Zero means the elements are tightly packed.
		 */
		std::size_t byteStride = 0;
	};

	/**
	 * \brief A typed element array inside a buffer view.
	 */
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

	/**
	 * \brief Mesh storage that owns its arrays, with View handing out accessors over them.
	 *
	 * A MeshPrimitive only points at bytes, so something has to keep them alive for as long as the primitive is read. Builtin primitives return this and a
	 * loader can fill it the same way, which keeps the pointers inside a MeshPrimitive tied to an object the caller can see.
	 */
	struct MeshData final
	{
		std::string name;
		std::vector<glm::vec3> positions;
		std::vector<glm::vec2> uvs;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec4> tangents;

		// Held at the widest of the two widths a draw can use. The storage narrows them on the way in when the largest one fits.
		std::vector<std::uint32_t> indices;

		/**
		 * \brief Describes these arrays as a mesh primitive.
		 *
		 * \attention The result borrows this object's storage and is invalidated by anything that reallocates one of the arrays.
		 */
		[[nodiscard]] MeshPrimitive View() const;
	};

	/**
	 * \brief Size in bytes of one component of a component type.
	 */
	[[nodiscard]] std::uint32_t GetComponentSize(ComponentType componentType) noexcept;

	/**
	 * \brief Number of components one element of a data type is made of.
	 */
	[[nodiscard]] std::uint32_t GetComponentCount(DataType dataType) noexcept;

	/**
	 * \brief Size in bytes of one element of an accessor, from the accessor's own component type and data type.
	 */
	[[nodiscard]] std::uint32_t GetElementSize(const Accessor & accessor) noexcept;

	/**
	 * \brief Address of one element of an accessor, honouring the buffer view's stride.
	 *
	 * \return Null when the accessor has no data or the element is past its end.
	 */
	[[nodiscard]] const std::uint8_t * GetElement(const Accessor & accessor, std::size_t index) noexcept;

	/**
	 * \brief Reads one index out of an index accessor, whatever integer width it was stored at.
	 */
	[[nodiscard]] std::uint32_t ReadIndex(const Accessor & accessor, std::size_t index) noexcept;
} // namespace fw::scene

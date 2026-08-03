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

#include "FW/scene/SceneMesh.hpp"

#include <cstring>

namespace fw::scene
{
	namespace
	{
		template <typename T>
		[[nodiscard]] Accessor DescribeArray(const std::vector<T> & values, const ComponentType componentType, const DataType dataType) noexcept
		{
			if (values.empty())
			{
				return {};
			}

			return Accessor{
				.bufferView =
					BufferView{
						.data		= reinterpret_cast<const std::uint8_t *>(values.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
						.byteOffset = 0,
						.byteLength = values.size() * sizeof(T),
						.byteStride = 0,
					},
				.byteOffset	   = 0,
				.componentType = componentType,
				.type		   = dataType,
				.count		   = values.size(),
			};
		}

		// Reads one integer of the given width out of bytes that carry no alignment guarantee.
		template <typename T>
		[[nodiscard]] std::uint32_t ReadUnaligned(const std::uint8_t * bytes) noexcept
		{
			T value{};
			std::memcpy(&value, bytes, sizeof(T));
			return static_cast<std::uint32_t>(value);
		}
	} // namespace

	MeshPrimitive MeshData::View() const
	{
		MeshPrimitive primitive;
		primitive.name = name;

		primitive.attributes[AttributeType::ePosition] = DescribeArray(positions, ComponentType::eFloat, DataType::eVec3);
		primitive.attributes[AttributeType::eTexCoord] = DescribeArray(uvs, ComponentType::eFloat, DataType::eVec2);
		primitive.attributes[AttributeType::eNormal]   = DescribeArray(normals, ComponentType::eFloat, DataType::eVec3);
		primitive.attributes[AttributeType::eTangent]  = DescribeArray(tangents, ComponentType::eFloat, DataType::eVec4);
		primitive.indices							   = DescribeArray(indices, ComponentType::eUnsignedInt, DataType::eScalar);

		// An array left empty describes nothing, and the storage treats a missing attribute differently from one that is present and zero length.
		std::erase_if(primitive.attributes,
			[](const auto & entry)
			{
				return entry.second.count == 0;
			});

		return primitive;
	}

	std::uint32_t GetComponentSize(const ComponentType componentType) noexcept
	{
		switch (componentType)
		{
		case ComponentType::eSignedByte:
		case ComponentType::eUnsignedByte:	return sizeof(std::uint8_t);
		case ComponentType::eSignedShort:
		case ComponentType::eUnsignedShort: return sizeof(std::uint16_t);
		case ComponentType::eUnsignedInt:	return sizeof(std::uint32_t);
		case ComponentType::eFloat:			return sizeof(float);
		}

		return 0;
	}

	std::uint32_t GetComponentCount(const DataType dataType) noexcept
	{
		switch (dataType)
		{
		case DataType::eScalar: return 1;
		case DataType::eVec2:	return 2;
		case DataType::eVec3:	return 3;
		case DataType::eVec4:
		case DataType::eMat2:	return 4;
		case DataType::eMat3:	return 9;
		case DataType::eMat4:	return 16;
		}

		return 0;
	}

	std::uint32_t GetElementSize(const Accessor & accessor) noexcept
	{
		return GetComponentSize(accessor.componentType) * GetComponentCount(accessor.type);
	}

	const std::uint8_t * GetElement(const Accessor & accessor, const std::size_t index) noexcept
	{
		if (accessor.bufferView.data == nullptr || index >= accessor.count)
		{
			return nullptr;
		}

		const std::size_t elementSize = GetElementSize(accessor);
		const std::size_t stride	  = accessor.bufferView.byteStride != 0 ? accessor.bufferView.byteStride : elementSize;
		const std::size_t offset	  = accessor.bufferView.byteOffset + accessor.byteOffset + (index * stride);

		// byteLength covers the view, not the accessor, so this catches an accessor that claims more elements than the bytes behind it hold.
		if (accessor.bufferView.byteLength != 0 && offset + elementSize > accessor.bufferView.byteLength)
		{
			return nullptr;
		}

		return accessor.bufferView.data + offset; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}

	std::uint32_t ReadIndex(const Accessor & accessor, const std::size_t index) noexcept
	{
		const std::uint8_t * element = GetElement(accessor, index);
		if (element == nullptr)
		{
			return 0;
		}

		switch (accessor.componentType)
		{
		case ComponentType::eSignedByte:	return ReadUnaligned<std::int8_t>(element);
		case ComponentType::eUnsignedByte:	return ReadUnaligned<std::uint8_t>(element);
		case ComponentType::eSignedShort:	return ReadUnaligned<std::int16_t>(element);
		case ComponentType::eUnsignedShort: return ReadUnaligned<std::uint16_t>(element);
		case ComponentType::eUnsignedInt:	return ReadUnaligned<std::uint32_t>(element);
		case ComponentType::eFloat:			return ReadUnaligned<float>(element);
		}

		return 0;
	}
} // namespace fw::scene

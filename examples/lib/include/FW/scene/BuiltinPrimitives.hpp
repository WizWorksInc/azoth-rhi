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

#include "SceneMesh.hpp"

#include <cstdint>
#include <string_view>

namespace fw::scene
{
	enum class BuiltinMeshType : std::uint8_t
	{
		eCube,
		eSphere,
		eCone,
		eCylinder,
		ePlane,

		/**
		 * \brief A grid of quads, not triangles, for a pipeline whose topology is patches.
		 */
		eTessellatedPlane,

		ePoint,
	};

	/**
	 * \brief Cache key a builtin mesh is stored under, and the name its mesh primitive carries.
	 */
	[[nodiscard]] std::string_view GetBuiltinMeshName(BuiltinMeshType builtinMeshType) noexcept;

	/**
	 * \brief Generates one builtin mesh, unit sized and centred on the origin.
	 *
	 * \param stackCount Rings along the axis of revolution, or rows for the two planes. Ignored by the cube and the point.
	 * \param sectorCount Segments around the axis of revolution, or columns for the two planes. Ignored by the cube and the point.
	 */
	[[nodiscard]] MeshData CreateBuiltinMesh(BuiltinMeshType builtinMeshType, std::uint32_t stackCount, std::uint32_t sectorCount);
} // namespace fw::scene

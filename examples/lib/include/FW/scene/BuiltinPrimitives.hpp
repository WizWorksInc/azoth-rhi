// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		eTessellatedPlane,

		ePoint,
	};

	[[nodiscard]] std::string_view GetBuiltinMeshName(BuiltinMeshType builtinMeshType) noexcept;

	[[nodiscard]] MeshData CreateBuiltinMesh(BuiltinMeshType builtinMeshType, std::uint32_t stackCount, std::uint32_t sectorCount);
}

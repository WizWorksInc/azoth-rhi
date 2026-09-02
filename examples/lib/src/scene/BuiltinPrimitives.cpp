// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "FW/scene/BuiltinPrimitives.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fw::scene
{
	namespace
	{
		constexpr float kPi = std::numbers::pi_v<float>;

		constexpr float kUnitSize = 1.0f;

		constexpr std::uint32_t kMaxSubdivision = 250;

		[[nodiscard]] std::uint32_t Sectors(const std::uint32_t requested) noexcept
		{
			return std::clamp(requested, UINT32_C(3), kMaxSubdivision);
		}

		[[nodiscard]] std::uint32_t Stacks(const std::uint32_t requested) noexcept
		{
			return std::clamp(requested, UINT32_C(1), kMaxSubdivision);
		}

		[[nodiscard]] float Angle(const std::uint32_t sector, const std::uint32_t sectorCount) noexcept
		{
			return static_cast<float>(sector) * (2.0f * kPi / static_cast<float>(sectorCount));
		}

		[[nodiscard]] float Fraction(const std::uint32_t step, const std::uint32_t stepCount) noexcept
		{
			return static_cast<float>(step) / static_cast<float>(stepCount);
		}

		void PushIndex(std::vector<std::uint32_t> & indices, const std::uint32_t index)
		{
			indices.push_back(index);
		}

		void PushQuad(std::vector<std::uint32_t> & indices, const std::uint32_t a, const std::uint32_t b, const std::uint32_t c, const std::uint32_t d)
		{
			PushIndex(indices, a);
			PushIndex(indices, b);
			PushIndex(indices, c);

			PushIndex(indices, a);
			PushIndex(indices, c);
			PushIndex(indices, d);
		}

		void PushSideGrid(std::vector<std::uint32_t> & indices, const std::uint32_t stackCount, const std::uint32_t sectorCount)
		{
			for (std::uint32_t stack = 0; stack < stackCount; ++stack)
			{
				for (std::uint32_t sector = 0; sector < sectorCount; ++sector)
				{
					const std::uint32_t upper = (stack * (sectorCount + 1)) + sector;
					const std::uint32_t lower = upper + sectorCount + 1;
					PushQuad(indices, upper, upper + 1, lower + 1, lower);
				}
			}
		}

		void PushCapFan(std::vector<std::uint32_t> & indices, const std::uint32_t center, const std::uint32_t ringStart, const std::uint32_t sectorCount,
			const bool facingUp)
		{
			for (std::uint32_t sector = 0; sector < sectorCount; ++sector)
			{
				PushIndex(indices, center);
				PushIndex(indices, ringStart + (facingUp ? sector + 1 : sector));
				PushIndex(indices, ringStart + (facingUp ? sector : sector + 1));
			}
		}

		[[nodiscard]] std::uint32_t PushCapVertices(MeshData & mesh, const float y, const float normalY, const float radius, const std::uint32_t sectorCount)
		{
			const float handedness = normalY > 0.0f ? -1.0f : 1.0f;
			const auto center	   = static_cast<std::uint32_t>(mesh.positions.size());

			mesh.positions.emplace_back(0.0f, y, 0.0f);
			mesh.uvs.emplace_back(0.5f, 0.5f);
			mesh.normals.emplace_back(0.0f, normalY, 0.0f);
			mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, handedness);

			for (std::uint32_t sector = 0; sector <= sectorCount; ++sector)
			{
				const float angle = Angle(sector, sectorCount);
				const float cosA  = std::cos(angle);
				const float sinA  = std::sin(angle);

				mesh.positions.emplace_back(radius * cosA, y, radius * sinA);
				mesh.uvs.emplace_back(0.5f + (0.5f * cosA), 0.5f + (0.5f * sinA));
				mesh.normals.emplace_back(0.0f, normalY, 0.0f);
				mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, handedness);
			}

			return center;
		}

		[[nodiscard]] MeshData CreatePoint()
		{
			MeshData mesh;
			mesh.name = GetBuiltinMeshName(BuiltinMeshType::ePoint);
			mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
			mesh.uvs.emplace_back(0.5f, 0.5f);
			mesh.normals.emplace_back(0.0f, 1.0f, 0.0f);
			mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
			mesh.indices.push_back(0);

			return mesh;
		}

		[[nodiscard]] MeshData CreateCube()
		{
			struct Face final
			{
				glm::vec3 origin;
				glm::vec3 uAxis;
				glm::vec3 vAxis;
			};

			constexpr float h = kUnitSize * 0.5f;

			constexpr std::array<Face, 6> faces{
				Face{ .origin = { -h, -h, h }, .uAxis = { 1, 0, 0 }, .vAxis = { 0, 1, 0 } },
				Face{ .origin = { h, -h, -h }, .uAxis = { -1, 0, 0 }, .vAxis = { 0, 1, 0 } },
				Face{ .origin = { -h, -h, -h }, .uAxis = { 0, 0, 1 }, .vAxis = { 0, 1, 0 } },
				Face{ .origin = { h, -h, h }, .uAxis = { 0, 0, -1 }, .vAxis = { 0, 1, 0 } },
				Face{ .origin = { -h, h, h }, .uAxis = { 1, 0, 0 }, .vAxis = { 0, 0, -1 } },
				Face{ .origin = { -h, -h, -h }, .uAxis = { 1, 0, 0 }, .vAxis = { 0, 0, 1 } },
			};

			MeshData mesh;
			mesh.name = GetBuiltinMeshName(BuiltinMeshType::eCube);
			mesh.positions.reserve(faces.size() * 4);
			mesh.uvs.reserve(faces.size() * 4);
			mesh.normals.reserve(faces.size() * 4);
			mesh.tangents.reserve(faces.size() * 4);
			mesh.indices.reserve(faces.size() * 6);

			for (const auto & [origin, uAxis, vAxis] : faces)
			{
				const glm::vec3 normal = glm::cross(uAxis, vAxis);
				const glm::vec3 u	   = uAxis * kUnitSize;
				const glm::vec3 v	   = vAxis * kUnitSize;

				const auto corner = static_cast<std::uint32_t>(mesh.positions.size());
				mesh.positions.insert(mesh.positions.end(), { origin, origin + u, origin + u + v, origin + v });
				mesh.uvs.insert(mesh.uvs.end(), { glm::vec2{ 0, 0 }, glm::vec2{ 1, 0 }, glm::vec2{ 1, 1 }, glm::vec2{ 0, 1 } });
				mesh.normals.insert(mesh.normals.end(), 4, normal);

				mesh.tangents.insert(mesh.tangents.end(), 4, glm::vec4{ uAxis, 1.0f });

				PushQuad(mesh.indices, corner, corner + 1, corner + 2, corner + 3);
			}

			return mesh;
		}

		[[nodiscard]] MeshData CreateSphere(const std::uint32_t stackCount, const std::uint32_t sectorCount)
		{
			constexpr float radius = kUnitSize * 0.5f;

			MeshData mesh;
			mesh.name = GetBuiltinMeshName(BuiltinMeshType::eSphere);

			const std::size_t vertexCount = static_cast<std::size_t>(stackCount + 1) * (sectorCount + 1);
			mesh.positions.reserve(vertexCount);
			mesh.uvs.reserve(vertexCount);
			mesh.normals.reserve(vertexCount);
			mesh.tangents.reserve(vertexCount);

			for (std::uint32_t stack = 0; stack <= stackCount; ++stack)
			{
				const float v		   = Fraction(stack, stackCount);
				const float stackAngle = (kPi * 0.5f) - (v * kPi);
				const float ring	   = std::cos(stackAngle);
				const float y		   = std::sin(stackAngle);

				for (std::uint32_t sector = 0; sector <= sectorCount; ++sector)
				{
					const float sectorAngle = Angle(sector, sectorCount);
					const float cosA		= std::cos(sectorAngle);
					const float sinA		= std::sin(sectorAngle);

					mesh.positions.emplace_back(radius * ring * cosA, radius * y, radius * ring * sinA);
					mesh.uvs.emplace_back(Fraction(sector, sectorCount), v);
					mesh.normals.emplace_back(ring * cosA, y, ring * sinA);

					mesh.tangents.emplace_back(-sinA, 0.0f, cosA, 1.0f);
				}
			}

			mesh.indices.reserve(static_cast<std::size_t>(stackCount) * sectorCount * 6);
			PushSideGrid(mesh.indices, stackCount, sectorCount);

			return mesh;
		}

		[[nodiscard]] MeshData CreateCone(const std::uint32_t stackCount, const std::uint32_t sectorCount)
		{
			constexpr float radius = kUnitSize * 0.5f;
			constexpr float apexY  = kUnitSize * 0.5f;
			constexpr float baseY  = -kUnitSize * 0.5f;

			constexpr float slope = radius / kUnitSize;

			MeshData mesh;
			mesh.name = GetBuiltinMeshName(BuiltinMeshType::eCone);

			for (std::uint32_t stack = 0; stack <= stackCount; ++stack)
			{
				const float v = Fraction(stack, stackCount);
				const float y = apexY - (v * kUnitSize);
				const float r = radius * v;

				for (std::uint32_t sector = 0; sector <= sectorCount; ++sector)
				{
					const float angle = Angle(sector, sectorCount);
					const float cosA  = std::cos(angle);
					const float sinA  = std::sin(angle);

					mesh.positions.emplace_back(r * cosA, y, r * sinA);
					mesh.uvs.emplace_back(Fraction(sector, sectorCount), v);
					mesh.normals.emplace_back(glm::normalize(glm::vec3{ cosA, slope, sinA }));
					mesh.tangents.emplace_back(-sinA, 0.0f, cosA, 1.0f);
				}
			}

			const std::uint32_t baseCenter = PushCapVertices(mesh, baseY, -1.0f, radius, sectorCount);

			mesh.indices.reserve((static_cast<std::size_t>(stackCount) * sectorCount * 6) + (static_cast<std::size_t>(sectorCount) * 3));
			PushSideGrid(mesh.indices, stackCount, sectorCount);
			PushCapFan(mesh.indices, baseCenter, baseCenter + 1, sectorCount, false);

			return mesh;
		}

		[[nodiscard]] MeshData CreateCylinder(const std::uint32_t stackCount, const std::uint32_t sectorCount)
		{
			constexpr float radius = kUnitSize * 0.5f;
			constexpr float halfH  = kUnitSize * 0.5f;

			MeshData mesh;
			mesh.name = GetBuiltinMeshName(BuiltinMeshType::eCylinder);

			for (std::uint32_t stack = 0; stack <= stackCount; ++stack)
			{
				const float v = Fraction(stack, stackCount);
				const float y = halfH - (kUnitSize * v);

				for (std::uint32_t sector = 0; sector <= sectorCount; ++sector)
				{
					const float angle = Angle(sector, sectorCount);
					const float cosA  = std::cos(angle);
					const float sinA  = std::sin(angle);

					mesh.positions.emplace_back(radius * cosA, y, radius * sinA);
					mesh.uvs.emplace_back(Fraction(sector, sectorCount), v);
					mesh.normals.emplace_back(cosA, 0.0f, sinA);
					mesh.tangents.emplace_back(-sinA, 0.0f, cosA, 1.0f);
				}
			}

			const std::uint32_t topCenter	 = PushCapVertices(mesh, halfH, 1.0f, radius, sectorCount);
			const std::uint32_t bottomCenter = PushCapVertices(mesh, -halfH, -1.0f, radius, sectorCount);

			mesh.indices.reserve((static_cast<std::size_t>(stackCount) * sectorCount * 6) + (static_cast<std::size_t>(sectorCount) * 6));
			PushSideGrid(mesh.indices, stackCount, sectorCount);
			PushCapFan(mesh.indices, topCenter, topCenter + 1, sectorCount, true);
			PushCapFan(mesh.indices, bottomCenter, bottomCenter + 1, sectorCount, false);

			return mesh;
		}

		[[nodiscard]] MeshData CreatePlaneGrid(const BuiltinMeshType type, const std::uint32_t stackCount, const std::uint32_t sectorCount)
		{
			constexpr float half = kUnitSize * 0.5f;

			MeshData mesh;
			mesh.name = GetBuiltinMeshName(type);

			const std::size_t vertexCount = static_cast<std::size_t>(stackCount + 1) * (sectorCount + 1);
			mesh.positions.reserve(vertexCount);
			mesh.uvs.reserve(vertexCount);

			for (std::uint32_t stack = 0; stack <= stackCount; ++stack)
			{
				const float v = Fraction(stack, stackCount);
				const float z = -half + (kUnitSize * v);

				for (std::uint32_t sector = 0; sector <= sectorCount; ++sector)
				{
					const float u = Fraction(sector, sectorCount);
					mesh.positions.emplace_back(-half + (kUnitSize * u), 0.0f, z);
					mesh.uvs.emplace_back(u, v);
				}
			}

			mesh.normals.assign(vertexCount, glm::vec3{ 0.0f, 1.0f, 0.0f });
			mesh.tangents.assign(vertexCount, glm::vec4{ 1.0f, 0.0f, 0.0f, -1.0f });

			const bool patches = type == BuiltinMeshType::eTessellatedPlane;
			mesh.indices.reserve(static_cast<std::size_t>(stackCount) * sectorCount * (patches ? 4 : 6));

			for (std::uint32_t stack = 0; stack < stackCount; ++stack)
			{
				for (std::uint32_t sector = 0; sector < sectorCount; ++sector)
				{
					const std::uint32_t nearRow = (stack * (sectorCount + 1)) + sector;
					const std::uint32_t farRow	= nearRow + sectorCount + 1;

					if (patches)
					{
						PushIndex(mesh.indices, nearRow);
						PushIndex(mesh.indices, farRow);
						PushIndex(mesh.indices, farRow + 1);
						PushIndex(mesh.indices, nearRow + 1);
					}
					else
					{
						PushQuad(mesh.indices, nearRow, farRow, farRow + 1, nearRow + 1);
					}
				}
			}

			return mesh;
		}
	}

	std::string_view GetBuiltinMeshName(const BuiltinMeshType builtinMeshType) noexcept
	{
		switch (builtinMeshType)
		{
		case BuiltinMeshType::eCube:			 return "builtin_cube";
		case BuiltinMeshType::eSphere:			 return "builtin_sphere";
		case BuiltinMeshType::eCone:			 return "builtin_cone";
		case BuiltinMeshType::eCylinder:		 return "builtin_cylinder";
		case BuiltinMeshType::ePlane:			 return "builtin_plane";
		case BuiltinMeshType::eTessellatedPlane: return "builtin_tessellated_plane";
		case BuiltinMeshType::ePoint:			 return "builtin_point";
		}

		return "builtin_unknown";
	}

	MeshData CreateBuiltinMesh(const BuiltinMeshType builtinMeshType, const std::uint32_t stackCount, const std::uint32_t sectorCount)
	{
		const std::uint32_t stacks	= Stacks(stackCount);
		const std::uint32_t sectors = Sectors(sectorCount);

		switch (builtinMeshType)
		{
		case BuiltinMeshType::eCube:			 return CreateCube();
		case BuiltinMeshType::eSphere:			 return CreateSphere(stacks, sectors);
		case BuiltinMeshType::eCone:			 return CreateCone(stacks, sectors);
		case BuiltinMeshType::eCylinder:		 return CreateCylinder(stacks, sectors);
		case BuiltinMeshType::ePlane:
		case BuiltinMeshType::eTessellatedPlane: return CreatePlaneGrid(builtinMeshType, stacks, sectors);
		case BuiltinMeshType::ePoint:			 return CreatePoint();
		}

		return {};
	}
}

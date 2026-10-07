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

#include "scene_asset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace vsnri
{

	namespace spirv
	{
// Regenerate with: glslangValidator -V --target-env vulkan1.0 --vn kMeshVert -o mesh_vert.h mesh.vert, and so on for each stage.
#include "shaders/mesh_frag.h"
#include "shaders/mesh_vert.h"
#include "shaders/post_frag.h"
#include "shaders/post_vert.h"
#include "shaders/scene_frag.h"
#include "shaders/scene_vert.h"
	}

	inline constexpr std::uint32_t kShadowExtent = 1024;
	inline constexpr std::uint32_t kSceneExtent	 = 512;

	inline constexpr std::uint32_t kPushConstantBytes = 64;

	inline constexpr std::uint32_t kPipelines	   = 8;
	inline constexpr std::uint32_t kMaterials	   = 64;
	inline constexpr std::uint32_t kMeshes		   = 16;
	inline constexpr std::uint32_t kMaterialStride = 256;
	inline constexpr std::uint32_t kDrawsAMaterial = 8;
	inline constexpr std::uint32_t kDrawsAMesh	   = 4;
	inline constexpr std::uint32_t kMaxDraws	   = 262'144;

	inline constexpr std::uint32_t kFramesInFlight = 2;

	inline constexpr std::uint32_t kMeshVertices = 8;
	inline constexpr std::uint32_t kMeshIndices	 = 36;
	inline constexpr std::uint32_t kVertexStride = 12;

	inline constexpr std::uint64_t kVertexBytes	  = std::uint64_t{ kMeshes } * kMeshVertices * kVertexStride;
	inline constexpr std::uint64_t kIndexBytes	  = std::uint64_t{ kMeshes } * kMeshIndices * sizeof(std::uint16_t);
	inline constexpr std::uint64_t kObjectBytes	  = std::uint64_t{ kMaxDraws } * 4 * sizeof(float);
	inline constexpr std::uint64_t kMaterialBytes = std::uint64_t{ kMaterials } * kMaterialStride;

	// Draws a frame, reaching past where the slower library's CPU frame time crosses 33.3 ms on this machine.
	inline constexpr std::array<std::uint32_t, 7> kSweepDraws{ 2'000, 5'000, 10'000, 20'000, 40'000, 80'000, 160'000 };
	inline constexpr double kSweepBudgetMilliseconds = 1000.0 / 30.0;
	inline constexpr std::array<std::uint32_t, 4> kThreadCounts{ 1, 2, 4, 8 };
	inline constexpr std::uint32_t kThreadedDraws = 20'000;

	inline constexpr std::uint32_t kShapeCommands = 8'192;
	inline constexpr std::uint32_t kChurnBatch	  = 64;

	inline constexpr std::uint32_t kChurnBufferBytes   = 64 * 1024;
	inline constexpr std::uint32_t kChurnTextureExtent = 256;

	inline constexpr std::uint64_t kTimeoutNanoseconds = 30'000'000'000;

	enum class Shape : std::uint8_t
	{
		eSetViewport,
		eSetScissor,
		ePushConstants,
		eBindDescriptorSet,
		eSetPipeline,
		eDraw,
		eDrawIndexed,
		eBarrier,
	};

	inline constexpr std::array kShapes{
		Shape::eSetViewport,
		Shape::eSetScissor,
		Shape::ePushConstants,
		Shape::eBindDescriptorSet,
		Shape::eSetPipeline,
		Shape::eDraw,
		Shape::eDrawIndexed,
		Shape::eBarrier,
	};

	[[nodiscard]] constexpr std::string_view ShapeName(const Shape shape)
	{
		switch (shape)
		{
		case Shape::eSetViewport:		return "SetViewport";
		case Shape::eSetScissor:		return "SetScissor";
		case Shape::ePushConstants:		return "PushConstants";
		case Shape::eBindDescriptorSet: return "BindDescriptorSet";
		case Shape::eSetPipeline:		return "SetPipeline";
		case Shape::eDraw:				return "Draw";
		case Shape::eDrawIndexed:		return "DrawIndexed";
		case Shape::eBarrier:			return "Barrier";
		}

		return "unknown";
	}

	// Barriers are recorded outside a rendering scope in both libraries, everything else inside one.
	[[nodiscard]] constexpr bool ShapeRendersInScope(const Shape shape)
	{
		return shape != Shape::eBarrier;
	}

	enum class Churn : std::uint8_t
	{
		eBuffer,
		eTexture,
		eDescriptorWrite,
	};

	inline constexpr std::array kChurns{ Churn::eBuffer, Churn::eTexture, Churn::eDescriptorWrite };

	[[nodiscard]] constexpr std::string_view ChurnName(const Churn churn)
	{
		switch (churn)
		{
		case Churn::eBuffer:		  return "Buffer";
		case Churn::eTexture:		  return "Texture";
		case Churn::eDescriptorWrite: return "DescriptorWrite";
		}

		return "unknown";
	}

	struct PushBlock final
	{
		std::uint32_t object   = 0;
		std::uint32_t material = 0;
		std::uint32_t pad0	   = 0;
		std::uint32_t pad1	   = 0;
		std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::array<float, 8> extra{};
	};

	static_assert(sizeof(PushBlock) == kPushConstantBytes);

	// Bit 0 turns culling on, bit 1 relaxes the depth test to less-or-equal, bit 2 turns blending on.
	[[nodiscard]] constexpr bool VariantCulls(const std::uint32_t variant)
	{
		return (variant & 1u) != 0;
	}

	[[nodiscard]] constexpr bool VariantLessEqual(const std::uint32_t variant)
	{
		return (variant & 2u) != 0;
	}

	[[nodiscard]] constexpr bool VariantBlends(const std::uint32_t variant)
	{
		return (variant & 4u) != 0;
	}

	[[nodiscard]] constexpr std::uint32_t PipelineFor(const std::uint32_t draw, const std::uint32_t draws)
	{
		return static_cast<std::uint32_t>(std::uint64_t{ draw } * kPipelines / draws);
	}

	[[nodiscard]] constexpr std::uint32_t MaterialFor(const std::uint32_t draw)
	{
		return (draw / kDrawsAMaterial) % kMaterials;
	}

	[[nodiscard]] constexpr std::uint32_t MeshFor(const std::uint32_t draw)
	{
		return (draw / kDrawsAMesh) % kMeshes;
	}

	[[nodiscard]] constexpr std::uint64_t MeshVertexOffset(const std::uint32_t mesh)
	{
		return std::uint64_t{ mesh } * kMeshVertices * kVertexStride;
	}

	[[nodiscard]] constexpr std::uint64_t MeshIndexOffset(const std::uint32_t mesh)
	{
		return std::uint64_t{ mesh } * kMeshIndices * sizeof(std::uint16_t);
	}

	// One recorder per library instantiates these, so both libraries see the same calls in the same order.
	template <class Recorder>
	void RecordShadowDraws(Recorder & recorder, const std::uint32_t draws)
	{
		PushBlock push{};
		std::uint32_t mesh = ~0u;

		for (std::uint32_t draw = 0; draw < draws; draw += 2)
		{
			if (const std::uint32_t wanted = MeshFor(draw); wanted != mesh)
			{
				mesh = wanted;
				recorder.BindMesh(mesh);
			}

			push.object = draw;
			recorder.Push(push);
			recorder.DrawMesh();
		}
	}

	template <class Recorder>
	void RecordSceneDraws(Recorder & recorder, const std::uint32_t first, const std::uint32_t count, const std::uint32_t draws)
	{
		PushBlock push{};
		std::uint32_t pipeline = ~0u;
		std::uint32_t material = ~0u;
		std::uint32_t mesh	   = ~0u;

		for (std::uint32_t draw = first; draw < first + count; ++draw)
		{
			if (const std::uint32_t wanted = PipelineFor(draw, draws); wanted != pipeline)
			{
				pipeline = wanted;
				recorder.BindPipeline(pipeline);
			}

			if (const std::uint32_t wanted = MaterialFor(draw); wanted != material)
			{
				material = wanted;
				recorder.BindMaterial(material);
			}

			if (const std::uint32_t wanted = MeshFor(draw); wanted != mesh)
			{
				mesh = wanted;
				recorder.BindMesh(mesh);
			}

			push.object	  = draw;
			push.material = material;
			recorder.Push(push);
			recorder.DrawMesh();
		}
	}

	inline void FillVertices(void * destination)
	{
		std::array<float, std::size_t{ kMeshes } * kMeshVertices * 3> vertices{};
		for (std::uint32_t mesh = 0; mesh < kMeshes; ++mesh)
		{
			const float half = 0.5f + 0.02f * static_cast<float>(mesh);
			for (std::uint32_t corner = 0; corner < kMeshVertices; ++corner)
			{
				const std::size_t at = (std::size_t{ mesh } * kMeshVertices + corner) * 3;
				vertices.at(at + 0)	 = (corner & 1u) != 0 ? half : -half;
				vertices.at(at + 1)	 = (corner & 2u) != 0 ? half : -half;
				vertices.at(at + 2)	 = (corner & 4u) != 0 ? half : -half;
			}
		}

		std::memcpy(destination, vertices.data(), sizeof(vertices));
	}

	inline void FillIndices(void * destination)
	{
		constexpr std::array<std::uint16_t, kMeshIndices> kCube{
			0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5
		};

		std::array<std::uint16_t, std::size_t{ kMeshes } * kMeshIndices> indices{};
		for (std::uint32_t mesh = 0; mesh < kMeshes; ++mesh)
		{
			for (std::uint32_t index = 0; index < kMeshIndices; ++index)
			{
				indices.at(std::size_t{ mesh } * kMeshIndices + index) = kCube.at(index);
			}
		}

		std::memcpy(destination, indices.data(), sizeof(indices));
	}

	// A fixed linear congruential sequence, so both arms place every object in the same spot.
	inline void FillObjects(void * destination)
	{
		auto * offsets	   = static_cast<float *>(destination);
		std::uint32_t seed = 0x2545f491u;
		const auto next	   = [&seed]
		{
			seed = seed * 1'664'525u + 1'013'904'223u;
			return static_cast<float>(seed >> 8u) / static_cast<float>(1u << 24u);
		};

		for (std::uint32_t object = 0; object < kMaxDraws; ++object)
		{
			offsets[std::size_t{ object } * 4 + 0] = next() * 1.8f - 0.9f;
			offsets[std::size_t{ object } * 4 + 1] = next() * 1.8f - 0.9f;
			offsets[std::size_t{ object } * 4 + 2] = next() * 0.8f + 0.1f;
			offsets[std::size_t{ object } * 4 + 3] = 0.02f;
		}
	}

	inline void FillMaterials(void * destination)
	{
		auto * bytes = static_cast<std::uint8_t *>(destination);
		std::memset(bytes, 0, kMaterialBytes);

		for (std::uint32_t material = 0; material < kMaterials; ++material)
		{
			const std::array color{ static_cast<float>(material % 4) / 3.0f, static_cast<float>(material % 7) / 6.0f, 0.5f, 1.0f };
			std::memcpy(bytes + std::size_t{ material } * kMaterialStride, color.data(), sizeof(color));
		}
	}

	inline constexpr std::array<std::uint32_t, 3> kSceneReplicas{ 1, 16, 256 };

	inline constexpr std::uint32_t kPathFrames = 240;

	inline constexpr std::uint32_t kGlobalsStride = 256;

	// Index 0 culls back faces, index 1 draws both sides.
	inline constexpr std::uint32_t kScenePipelines = 2;

	inline constexpr std::uint32_t kFrameTimestamps = 2;

	inline constexpr float kSceneSamplerMaxLod = 16.0f;

	enum class BindPolicy : std::uint8_t
	{
		ePerDraw,
		eSorted,
	};

	inline constexpr std::array kBindPolicies{ BindPolicy::ePerDraw, BindPolicy::eSorted };

	[[nodiscard]] constexpr std::string_view BindPolicyName(const BindPolicy policy)
	{
		switch (policy)
		{
		case BindPolicy::ePerDraw: return "per_draw";
		case BindPolicy::eSorted:  return "sorted";
		}

		return "unknown";
	}

	struct SceneGlobals final
	{
		Matrix viewProjection{};
		std::array<float, 4> lightDirection{};
		std::array<float, 4> cameraPosition{};
	};

	static_assert(sizeof(SceneGlobals) <= kGlobalsStride);

	struct SceneDraw final
	{
		std::uint32_t instance = 0;
		std::uint32_t mesh	   = 0;
		std::uint32_t material = 0;
		std::uint32_t pipeline = 0;
	};

	struct SceneFrameDesc final
	{
		std::span<const SceneDraw> draws;
		std::span<const Matrix> models;
		BindPolicy policy = BindPolicy::ePerDraw;
		SceneGlobals globals{};
	};

	namespace math
	{

		using Vec3 = std::array<float, 3>;

		[[nodiscard]] inline Vec3 Subtract(const Vec3 & a, const Vec3 & b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		[[nodiscard]] inline float Dot(const Vec3 & a, const Vec3 & b)
		{
			return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
		}

		[[nodiscard]] inline Vec3 Cross(const Vec3 & a, const Vec3 & b)
		{
			return { (a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0]) };
		}

		[[nodiscard]] inline Vec3 Normalize(const Vec3 & v)
		{
			const float length = std::sqrt(Dot(v, v));
			return length > 0.0f ? Vec3{ v[0] / length, v[1] / length, v[2] / length } : v;
		}

		[[nodiscard]] inline Matrix Multiply(const Matrix & a, const Matrix & b)
		{
			Matrix out{};
			for (std::size_t column = 0; column < 4; ++column)
			{
				for (std::size_t row = 0; row < 4; ++row)
				{
					float sum = 0.0f;
					for (std::size_t k = 0; k < 4; ++k)
					{
						sum += a[(k * 4) + row] * b[(column * 4) + k];
					}
					out[(column * 4) + row] = sum;
				}
			}

			return out;
		}

		[[nodiscard]] inline Matrix Translation(const Vec3 & offset)
		{
			return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, offset[0], offset[1], offset[2], 1 };
		}

		[[nodiscard]] inline Matrix LookAt(const Vec3 & eye, const Vec3 & target, const Vec3 & up)
		{
			const Vec3 forward = Normalize(Subtract(target, eye));
			const Vec3 side	   = Normalize(Cross(forward, up));
			const Vec3 upward  = Cross(side, forward);
			return {
				side[0],
				upward[0],
				-forward[0],
				0,
				side[1],
				upward[1],
				-forward[1],
				0,
				side[2],
				upward[2],
				-forward[2],
				0,
				-Dot(side, eye),
				-Dot(upward, eye),
				Dot(forward, eye),
				1,
			};
		}

		// Right-handed, depth from zero to one, y up, which both libraries flip for Vulkan in their viewport.
		[[nodiscard]] inline Matrix Perspective(const float fovY, const float aspect, const float nearPlane, const float farPlane)
		{
			const float focal = 1.0f / std::tan(fovY * 0.5f);
			const float range = farPlane / (nearPlane - farPlane);
			return { focal / aspect, 0, 0, 0, 0, focal, 0, 0, 0, 0, range, -1, 0, 0, nearPlane * range, 0 };
		}

		[[nodiscard]] inline Vec3 TransformPoint(const Matrix & m, const Vec3 & p)
		{
			return {
				(m[0] * p[0]) + (m[4] * p[1]) + (m[8] * p[2]) + m[12],
				(m[1] * p[0]) + (m[5] * p[1]) + (m[9] * p[2]) + m[13],
				(m[2] * p[0]) + (m[6] * p[1]) + (m[10] * p[2]) + m[14],
			};
		}

		[[nodiscard]] inline float MaxScale(const Matrix & m)
		{
			const float x = Dot({ m[0], m[1], m[2] }, { m[0], m[1], m[2] });
			const float y = Dot({ m[4], m[5], m[6] }, { m[4], m[5], m[6] });
			const float z = Dot({ m[8], m[9], m[10] }, { m[8], m[9], m[10] });
			return std::sqrt(std::max({ x, y, z }));
		}

	}

	// A square grid of scene copies, a camera loop around it and both draw orders, culled per frame outside the timed region.
	class SceneWorkload final
	{
	public:
		SceneWorkload(const SceneAsset & asset, const std::uint32_t replicas)
		{
			using math::Vec3;

			std::vector<std::array<float, 4>> spheres;
			Vec3 low{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			Vec3 high{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
			for (const SceneInstance & instance : asset.instances)
			{
				const SceneMesh & mesh = asset.meshes.at(instance.mesh);
				const Vec3 center	   = math::TransformPoint(instance.model, { mesh.bounds[0], mesh.bounds[1], mesh.bounds[2] });
				const float radius	   = mesh.bounds[3] * math::MaxScale(instance.model);
				spheres.push_back({ center[0], center[1], center[2], radius });
				for (std::size_t axis = 0; axis < 3; ++axis)
				{
					low[axis]  = std::min(low[axis], center[axis] - radius);
					high[axis] = std::max(high[axis], center[axis] + radius);
				}
			}

			const Vec3 middle{ (low[0] + high[0]) * 0.5f, (low[1] + high[1]) * 0.5f, (low[2] + high[2]) * 0.5f };
			const float radius = 0.5f * std::sqrt(math::Dot(math::Subtract(high, low), math::Subtract(high, low)));

			const auto side		  = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(replicas))));
			const float spacing	  = 2.2f * radius;
			const float firstCell = -0.5f * static_cast<float>(side - 1) * spacing;

			for (std::uint32_t replica = 0; replica < replicas; ++replica)
			{
				const Vec3 shift{
					firstCell + (static_cast<float>(replica % side) * spacing) - middle[0],
					0.0f,
					firstCell + (static_cast<float>(replica / side) * spacing) - middle[2],
				};
				const Matrix translation = math::Translation(shift);

				for (std::size_t source = 0; source < asset.instances.size(); ++source)
				{
					const SceneInstance & instance		= asset.instances.at(source);
					const std::array<float, 4> & sphere = spheres.at(source);

					m_perDraw.push_back(SceneDraw{
						.instance = static_cast<std::uint32_t>(m_models.size()),
						.mesh	  = instance.mesh,
						.material = instance.material,
						.pipeline = asset.materials.at(instance.material).doubleSided ? 1u : 0u,
					});
					m_models.push_back(math::Multiply(translation, instance.model));
					m_spheres.push_back({ sphere[0] + shift[0], sphere[1], sphere[2] + shift[2], sphere[3] });
				}
			}

			m_sorted = m_perDraw;
			std::ranges::stable_sort(m_sorted,
				[](const SceneDraw & a, const SceneDraw & b)
				{
					return a.pipeline != b.pipeline ? a.pipeline < b.pipeline : (a.material != b.material ? a.material < b.material : a.mesh < b.mesh);
				});

			const float gridHalf = 0.5f * static_cast<float>(side) * spacing;
			m_orbit				 = std::max(gridHalf * 0.85f, radius * 2.5f);
			m_lookRadius		 = m_orbit * 0.4f;
			m_eyeHeight			 = middle[1] + (radius * 0.8f);
			m_targetHeight		 = middle[1];
			m_near				 = radius * 0.01f;
			m_far				 = (2.0f * (m_orbit + gridHalf)) + (4.0f * radius);
		}

		[[nodiscard]] std::span<const Matrix> Models() const
		{
			return m_models;
		}

		[[nodiscard]] std::uint32_t Draws() const
		{
			return static_cast<std::uint32_t>(m_perDraw.size());
		}

		// Frame indices wrap around the loop, so any repetition replays the same path.
		void PrepareFrame(const std::uint32_t frame, const BindPolicy policy, SceneGlobals & globals, std::vector<SceneDraw> & visible) const
		{
			const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(frame % kPathFrames) / static_cast<float>(kPathFrames);
			const math::Vec3 eye{ m_orbit * std::cos(angle), m_eyeHeight, m_orbit * std::sin(angle) };
			const math::Vec3 target{ m_lookRadius * std::cos(angle + 1.1f), m_targetHeight, m_lookRadius * std::sin(angle + 1.1f) };

			const Matrix view		= math::LookAt(eye, target, { 0.0f, 1.0f, 0.0f });
			const Matrix projection = math::Perspective(std::numbers::pi_v<float> / 3.0f, 1.0f, m_near, m_far);
			globals.viewProjection	= math::Multiply(projection, view);

			const math::Vec3 light = math::Normalize({ -0.4f, -1.0f, -0.3f });
			globals.lightDirection = { light[0], light[1], light[2], 0.0f };
			globals.cameraPosition = { eye[0], eye[1], eye[2], 1.0f };

			const std::array<std::array<float, 4>, 6> planes = FrustumPlanes(globals.viewProjection);

			visible.clear();
			for (const SceneDraw & draw : policy == BindPolicy::ePerDraw ? m_perDraw : m_sorted)
			{
				const std::array<float, 4> & sphere = m_spheres.at(draw.instance);
				bool inside							= true;
				for (const std::array<float, 4> & plane : planes)
				{
					inside = inside && (plane[0] * sphere[0]) + (plane[1] * sphere[1]) + (plane[2] * sphere[2]) + plane[3] >= -sphere[3];
				}

				if (inside)
				{
					visible.push_back(draw);
				}
			}
		}

	private:
		[[nodiscard]] static std::array<std::array<float, 4>, 6> FrustumPlanes(const Matrix & m)
		{
			const auto row = [&m](const std::size_t index)
			{
				return std::array<float, 4>{ m[index], m[4 + index], m[8 + index], m[12 + index] };
			};

			const std::array<float, 4> x = row(0);
			const std::array<float, 4> y = row(1);
			const std::array<float, 4> z = row(2);
			const std::array<float, 4> w = row(3);

			std::array<std::array<float, 4>, 6> planes{};
			for (std::size_t i = 0; i < 4; ++i)
			{
				planes[0][i] = w[i] + x[i];
				planes[1][i] = w[i] - x[i];
				planes[2][i] = w[i] + y[i];
				planes[3][i] = w[i] - y[i];
				planes[4][i] = z[i];
				planes[5][i] = w[i] - z[i];
			}

			for (std::array<float, 4> & plane : planes)
			{
				const float length = std::sqrt((plane[0] * plane[0]) + (plane[1] * plane[1]) + (plane[2] * plane[2]));
				for (float & value : plane)
				{
					value /= length;
				}
			}

			return planes;
		}

		std::vector<Matrix> m_models;
		std::vector<std::array<float, 4>> m_spheres;
		std::vector<SceneDraw> m_perDraw;
		std::vector<SceneDraw> m_sorted;

		float m_orbit		 = 1.0f;
		float m_lookRadius	 = 1.0f;
		float m_eyeHeight	 = 1.0f;
		float m_targetHeight = 0.0f;
		float m_near		 = 0.01f;
		float m_far			 = 100.0f;
	};

	// Both arms record the scene through this. The per-draw order makes the calls NRI's SceneViewer makes for every instance, the sorted one skips repeated binds.
	template <class Recorder>
	void RecordSceneAssetDraws(Recorder & recorder, const SceneFrameDesc & frame)
	{
		if (frame.policy == BindPolicy::ePerDraw)
		{
			for (const SceneDraw & draw : frame.draws)
			{
				recorder.BindScenePipeline(draw.pipeline);
				recorder.BindSceneVertices();
				recorder.BindSceneMaterial(draw.material);
				recorder.PushModel(frame.models[draw.instance]);
				recorder.DrawSceneMesh(draw.mesh);
			}

			return;
		}

		std::uint32_t pipeline = ~0u;
		std::uint32_t material = ~0u;
		recorder.BindSceneVertices();
		for (const SceneDraw & draw : frame.draws)
		{
			if (draw.pipeline != pipeline)
			{
				pipeline = draw.pipeline;
				recorder.BindScenePipeline(pipeline);
			}

			if (draw.material != material)
			{
				material = draw.material;
				recorder.BindSceneMaterial(material);
			}

			recorder.PushModel(frame.models[draw.instance]);
			recorder.DrawSceneMesh(draw.mesh);
		}
	}

}

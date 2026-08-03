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

#include "FW/scene/GltfSceneLoader.hpp"

#include "FW/assets/ImageAsset.hpp"
#include "FW/scene/SceneGpuImageStorage.hpp"
#include "FW/utility/AssetPath.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <format>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fw::scene
{
	namespace
	{
		[[nodiscard]] std::string NodeName(const fastgltf::Node & node, const std::string & prefix, const std::size_t ordinal)
		{
			const std::string_view given{ node.name };
			return given.empty() ? std::format("{}node_{}", prefix, ordinal) : std::format("{}{}", prefix, given);
		}

		/*
		 * Reads a node's own transform, which glTF gives either as a matrix or as the three components separately.
		 */
		void ApplyLocalTransform(const fastgltf::Node & node, SceneObject & object)
		{
			std::visit(
				fastgltf::visitor{
					[&](const fastgltf::TRS & trs)
					{
						// glTF stores a quaternion as x, y, z, w, which is the order SetQuaternion takes.
						object.SetPosition(glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() });
						object.SetQuaternion(glm::vec4{ trs.rotation.x(), trs.rotation.y(), trs.rotation.z(), trs.rotation.w() });
						object.SetScale(glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
					},
					[&](const fastgltf::math::fmat4x4 & source)
					{
						// A composed matrix has to be taken apart again, since a scene object holds the parts and not the product. Both store columns, so this copies straight
						// across.
						glm::mat4 matrix{};
						for (int column = 0; column < 4; ++column)
						{
							for (int row = 0; row < 4; ++row)
							{
								matrix[column][row] = source[column][row];
							}
						}

						const glm::vec3 translation{ matrix[3] };
						glm::vec3 scale{ glm::length(glm::vec3{ matrix[0] }), glm::length(glm::vec3{ matrix[1] }), glm::length(glm::vec3{ matrix[2] }) };

						// A mirrored matrix has a negative determinant, which no combination of positive scales produces. Putting the sign on X is the usual convention and
						// reproduces the same matrix.
						if (glm::determinant(glm::mat3{ matrix }) < 0.0f)
						{
							scale.x = -scale.x;
						}

						glm::mat3 rotation{ matrix };
						for (int axis = 0; axis < 3; ++axis)
						{
							if (scale[axis] != 0.0f)
							{
								rotation[axis] /= scale[axis];
							}
						}

						const glm::quat orientation = glm::quat_cast(rotation);

						object.SetPosition(translation);
						object.SetQuaternion(glm::vec4{ orientation.x, orientation.y, orientation.z, orientation.w });
						object.SetScale(scale);
					},
				},
				node.transform);
		}

		/*
		 * Reads one attribute into a vector, going through fastgltf so a packed or normalized source is unpacked the same way.
		 *
		 * Empty when the primitive does not carry the attribute, which the caller reads as absent and not as a failure.
		 */
		template <typename VecT>
		void ReadAttribute(const fastgltf::Asset & asset, const fastgltf::Primitive & primitive, const std::string_view name, std::vector<VecT> & out)
		{
			const auto * attribute = primitive.findAttribute(name);
			if (attribute == primitive.attributes.end())
			{
				out.clear();
				return;
			}

			const fastgltf::Accessor & accessor = asset.accessors[attribute->accessorIndex];
			out.resize(accessor.count);
			fastgltf::copyFromAccessor<VecT>(asset, accessor, out.data());
		}

		/*
		 * Turns one primitive into mesh data the storage can take.
		 *
		 * Tangents are left out where the document has none. The storage derives them from the positions, normals and texture coordinates, which is the same thing a
		 * loader would do and one place, not two.
		 */
		[[nodiscard]] MeshData ReadPrimitive(const fastgltf::Asset & asset, const fastgltf::Primitive & primitive, std::string name)
		{
			MeshData mesh;
			mesh.name = std::move(name);

			ReadAttribute(asset, primitive, "POSITION", mesh.positions);
			ReadAttribute(asset, primitive, "NORMAL", mesh.normals);
			ReadAttribute(asset, primitive, "TEXCOORD_0", mesh.uvs);
			ReadAttribute(asset, primitive, "TANGENT", mesh.tangents);

			if (primitive.indicesAccessor.has_value())
			{
				const fastgltf::Accessor & indices = asset.accessors[*primitive.indicesAccessor];
				mesh.indices.resize(indices.count);
				fastgltf::copyFromAccessor<std::uint32_t>(asset, indices, mesh.indices.data());
			}
			else
			{
				// An unindexed primitive draws its vertices in order, so the same thing said as an index list keeps one path through the storage.
				mesh.indices.resize(mesh.positions.size());
				for (std::size_t index = 0; index < mesh.indices.size(); ++index)
				{
					mesh.indices[index] = static_cast<std::uint32_t>(index);
				}
			}

			return mesh;
		}

		/*
		 * The encoded bytes of a glTF image, whether it lives inside a buffer or in a file beside the document.
		 */
		/*
		 * The part of a buffer one view names, or nothing when the view reaches past the end of it.
		 */
		[[nodiscard]] std::span<const std::byte> Slice(const std::span<const std::byte> buffer, const fastgltf::BufferView & view)
		{
			if (view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset)
			{
				return {};
			}

			return buffer.subspan(view.byteOffset, view.byteLength);
		}

		[[nodiscard]] std::vector<std::uint8_t> ReadImageBytes(
			const fastgltf::Asset & asset, const fastgltf::Image & image, const std::filesystem::path & documentPath)
		{
			std::vector<std::uint8_t> bytes;

			const auto fromBytes = [&bytes](const std::span<const std::byte> source)
			{
				bytes.resize(source.size());
				std::memcpy(bytes.data(), source.data(), source.size());
			};

			std::visit(
				fastgltf::visitor{
					[](auto &) {},
					[&](const fastgltf::sources::Array & array)
					{
						fromBytes(std::span(array.bytes.data(), array.bytes.size()));
					},
					[&](const fastgltf::sources::Vector & vector)
					{
						fromBytes(std::span(vector.bytes.data(), vector.bytes.size()));
					},
					[&](const fastgltf::sources::BufferView & view)
					{
						const fastgltf::BufferView & bufferView = asset.bufferViews[view.bufferViewIndex];
						const fastgltf::Buffer & buffer			= asset.buffers[bufferView.bufferIndex];
						std::visit(
							fastgltf::visitor{
								[](auto &) {},
								// subspan without adding the offset to the pointer, so a view claiming a range past the end of its buffer is caught here instead of reading whatever
								// follows it.
								[&](const fastgltf::sources::Array & array)
								{
									fromBytes(Slice(std::span(array.bytes.data(), array.bytes.size()), bufferView));
								},
								[&](const fastgltf::sources::Vector & vector)
								{
									fromBytes(Slice(std::span(vector.bytes.data(), vector.bytes.size()), bufferView));
								},
							},
							buffer.data);
					},
					[&](const fastgltf::sources::URI & uri)
					{
						// fastgltf hands the URI back already decoded and relative to the document, so this only has to join the two.
						bytes = util::ReadFile(documentPath.parent_path() / std::string_view(uri.uri.path()));
					},
				},
				image.data);

			return bytes;
		}

		/*
		 * Loads every image a material points at, once each however many materials share it.
		 */
		class TextureTable final
		{
		public:
			TextureTable(SceneGpuImageStorage & storage, std::filesystem::path documentPath, GltfLoadOptions options)
				: m_storage(&storage),
				  m_documentPath(std::move(documentPath)),
				  m_options(std::move(options))
			{
			}

			/*
			 * view is the texture a material named or null where it named none. A pointer and not an optional because glTF's normal and occlusion kinds carry a scale on
			 * top of a texture info, and a pointer to the base lets all three arrive here unchanged. srgb marks a texture holding colour , not numbers, which decides
			 * whether it decodes through the sRGB transfer function.
			 */
			[[nodiscard]] TextureId Store(const fastgltf::Asset & asset, const fastgltf::TextureInfo * view, const bool srgb)
			{
				if (view == nullptr)
				{
					return kNoTexture;
				}

				const fastgltf::Optional<std::size_t> imageIndex = asset.textures[view->textureIndex].imageIndex;
				if (!imageIndex.has_value())
				{
					return kNoTexture;
				}

				const fastgltf::Image & image = asset.images[*imageIndex];

				// Keyed on the image and on how it is being read, because the same file can be a base colour map for one material and a data map for another, and those two
				// want different formats.
				const Key key{ .image = *imageIndex, .srgb = srgb };
				if (const auto cached = m_stored.find(key); cached != m_stored.end())
				{
					return cached->second;
				}

				const std::vector<std::uint8_t> encoded = ReadImageBytes(asset, image, m_documentPath);
				if (encoded.empty())
				{
					m_stored.emplace(key, kNoTexture);
					return kNoTexture;
				}

				std::string decodeError;
				const assets::ImageAsset decoded = assets::DecodeImage(encoded, false, decodeError);
				if (!decoded.IsValid())
				{
					m_stored.emplace(key, kNoTexture);
					return kNoTexture;
				}

				const std::string_view given{ image.name };
				const std::string name = std::format("{}{}#{}", m_options.namePrefix, given.empty() ? std::string_view("image") : given, m_stored.size());
				const TextureId id	   = m_storage->StoreTexture(name, decoded, srgb, m_options.mipmapTextures);

				m_stored.emplace(key, id);
				return id;
			}

			[[nodiscard]] std::uint32_t Count() const noexcept
			{
				return static_cast<std::uint32_t>(m_stored.size());
			}

		private:
			struct Key final
			{
				std::size_t image = 0;
				bool srgb		  = false;

				[[nodiscard]] bool operator==(const Key &) const noexcept = default;
			};

			struct KeyHash final
			{
				[[nodiscard]] std::size_t operator()(const Key & key) const noexcept
				{
					return std::hash<std::size_t>{}(key.image) ^ (key.srgb ? 0x9e3779b9uz : 0uz);
				}
			};

			SceneGpuImageStorage * m_storage = nullptr;
			std::filesystem::path m_documentPath;
			GltfLoadOptions m_options;
			std::unordered_map<Key, TextureId, KeyHash> m_stored;
		};

		/*
		 * Reads a glTF material into the framework's own, which carries a superset of what glTF describes.
		 */
		[[nodiscard]] Material ReadMaterial(const fastgltf::Asset & asset, const fastgltf::Optional<std::size_t> & materialIndex, TextureTable * textures)
		{
			Material material;
			if (!materialIndex.has_value())
			{
				return material;
			}

			const fastgltf::Material & source = asset.materials[*materialIndex];
			const fastgltf::PBRData & pbr	  = source.pbrData;

			material.albedoColor  = glm::vec4{ pbr.baseColorFactor.x(), pbr.baseColorFactor.y(), pbr.baseColorFactor.z(), pbr.baseColorFactor.w() };
			material.diffuseColor = material.albedoColor;
			material.metallic	  = pbr.metallicFactor;
			material.roughness	  = pbr.roughnessFactor;
			material.opacity	  = material.albedoColor.a;

			if (textures != nullptr)
			{
				material.albedoMap	= textures->Store(asset, pbr.baseColorTexture ? &*pbr.baseColorTexture : nullptr, true);
				material.diffuseMap = material.albedoMap;

				// One texture with roughness in green and metalness in blue, which is how glTF packs the pair.
				material.metallicRoughnessMap = textures->Store(asset, pbr.metallicRoughnessTexture ? &*pbr.metallicRoughnessTexture : nullptr, false);

				material.normalMap			 = textures->Store(asset, source.normalTexture ? &*source.normalTexture : nullptr, false);
				material.ambientOcclusionMap = textures->Store(asset, source.occlusionTexture ? &*source.occlusionTexture : nullptr, false);
				material.emissiveMap		 = textures->Store(asset, source.emissiveTexture ? &*source.emissiveTexture : nullptr, true);
			}

			return material;
		}

		/*
		 * Parses a document and loads what its buffers point at. Returns the asset or an error with the reason already written to error.
		 */
		[[nodiscard]] fastgltf::Expected<fastgltf::Asset> OpenDocument(const std::filesystem::path & resolved, std::string & error)
		{
			auto data = fastgltf::GltfDataBuffer::FromPath(resolved);
			if (data.error() != fastgltf::Error::None)
			{
				error = std::format("{} is not a glTF document this can read", resolved.generic_string());
				return { data.error() };
			}

			// LoadExternalBuffers so a document keeping its buffers beside it arrives with the bytes in hand, which is what the accessor reads above need.
			fastgltf::Parser parser;
			auto asset = parser.loadGltf(data.get(), resolved.parent_path(), fastgltf::Options::LoadExternalBuffers);
			if (asset.error() != fastgltf::Error::None)
			{
				error = std::format("{} did not parse: {}", resolved.generic_string(), fastgltf::getErrorMessage(asset.error()));
			}

			return asset;
		}

		/*
		 * Puts one node's mesh onto its object, adding a child for every primitive past the first. A primitive is the unit carrying one material, so a mesh with
		 * several needs one object each. The node's own object takes the first without gaining an extra child. Returns false when the scene ran out of object slots.
		 */
		[[nodiscard]] bool AttachPrimitives(Scene & scene, const fastgltf::Asset & asset, const fastgltf::Mesh & source,
			const std::shared_ptr<SceneObject> & object, TextureTable * textures, const GltfLoadOptions & options, std::uint32_t & meshCount)
		{
			const std::string_view given{ source.name };
			const std::string meshName = given.empty() ? object->GetName() : std::string(given);

			for (std::size_t index = 0; const fastgltf::Primitive & primitive : source.primitives)
			{
				const std::size_t which = index++;
				if (primitive.type != fastgltf::PrimitiveType::Triangles)
				{
					continue;
				}

				// The name doubles as the storage's cache key, so two nodes pointing at one mesh share its allocation.
				const MeshData mesh = ReadPrimitive(asset, primitive, std::format("{}{}#{}", options.namePrefix, meshName, which));
				if (mesh.positions.empty() || mesh.indices.empty())
				{
					continue;
				}

				std::shared_ptr<SceneObject> target = object;
				if (which != 0)
				{
					target = scene.CreateObject(std::format("{}#{}", object->GetName(), which));
					if (target == nullptr)
					{
						return false;
					}

					object->AddChild(target);
				}

				target->SetMesh(mesh.View());
				target->SetMaterial(ReadMaterial(asset, primitive.materialIndex, textures));
				++meshCount;
			}

			return true;
		}
	} // namespace

	GltfLoadResult LoadGltf(Scene & scene, const std::filesystem::path & path, const GltfLoadOptions & options)
	{
		GltfLoadResult result;

		if (!scene.IsValid())
		{
			result.error = "the scene's storage never came up, so there is nowhere to put a document";
			return result;
		}

		const std::filesystem::path resolved = path.is_absolute() ? path : util::AssetPath(path);
		if (resolved.empty())
		{
			result.error = std::format("no asset named {} was found beside the executable", path.generic_string());
			return result;
		}

		auto opened = OpenDocument(resolved, result.error);
		if (opened.error() != fastgltf::Error::None)
		{
			return result;
		}

		const fastgltf::Asset & document = opened.get();

		// The document's own scene, not its node array, so a node it deliberately leaves out of the scene stays out of this one too.
		if (document.scenes.empty())
		{
			result.error = std::format("{} has no scene in it", resolved.generic_string());
			return result;
		}

		const fastgltf::Scene & root = document.scenes[document.defaultScene.value_or(0)];

		SceneGpuImageStorage & images = scene.GetGpuImageStorage();

		TextureTable textures(images, resolved, options);
		TextureTable * textureTable = options.loadTextures && images.IsValid() ? &textures : nullptr;

		// Iterative, not recursive, because a document decides how deep its own tree goes and a deep one should not decide how much stack this needs. Each entry is a
		// node still to visit and the object its parent turned into.
		struct Pending final
		{
			std::size_t node = 0;
			std::shared_ptr<SceneObject> parent;
		};

		std::vector<Pending> pending;
		pending.reserve(root.nodeIndices.size());
		for (const std::size_t node : root.nodeIndices)
		{
			pending.push_back(Pending{ .node = node });
		}

		std::size_t ordinal = 0;

		while (!pending.empty())
		{
			const Pending current = std::move(pending.back());
			pending.pop_back();

			const fastgltf::Node & node = document.nodes[current.node];

			const std::shared_ptr<SceneObject> object = scene.CreateObject(NodeName(node, options.namePrefix, ordinal++));
			if (object == nullptr)
			{
				result.error = "the scene ran out of object slots part way through the document";
				return result;
			}

			ApplyLocalTransform(node, *object);

			if (!options.tag.empty())
			{
				object->SetTag(options.tag);
			}

			if (node.meshIndex.has_value() &&
				!AttachPrimitives(scene, document, document.meshes[*node.meshIndex], object, textureTable, options, result.meshCount))
			{
				result.error = "the scene ran out of object slots part way through the document";
				return result;
			}

			if (current.parent != nullptr)
			{
				current.parent->AddChild(object);
			}
			else
			{
				scene.AddRootObject(object);
				result.roots.push_back(object);
			}

			for (const std::size_t child : node.children)
			{
				pending.push_back(Pending{ .node = child, .parent = object });
			}
		}

		result.textureCount = textureTable != nullptr ? textureTable->Count() : 0;

		return result;
	}
} // namespace fw::scene

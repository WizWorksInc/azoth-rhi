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

#include "azoth/rhi/imgui/renderer.hpp"

#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#ifdef AZOTH_RHI_IMGUI_HAVE_SPIRV
	#include "azoth/rhi/imgui/imgui_fragment_spirv.hpp"
	#include "azoth/rhi/imgui/imgui_vertex_spirv.hpp"
#endif
#ifdef AZOTH_RHI_IMGUI_HAVE_DXIL
	#include "azoth/rhi/imgui/imgui_fragment_dxil.hpp"
	#include "azoth/rhi/imgui/imgui_vertex_dxil.hpp"
#endif
#ifdef AZOTH_RHI_IMGUI_HAVE_METALLIB
	#include "azoth/rhi/imgui/imgui_fragment_metallib.hpp"
	#include "azoth/rhi/imgui/imgui_vertex_metallib.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace azo::rhi::imgui
{
	namespace
	{
		constexpr std::uint32_t kTextureSet		= 0;
		constexpr std::uint32_t kImageBinding	= 0;
		constexpr std::uint32_t kSamplerBinding = 1;

		constexpr std::uint64_t kFallbackCopyAlignment = 4;

		struct Transform final
		{
			std::array<float, 2> scale{};
			std::array<float, 2> translate{};
			std::uint32_t srgbTarget = 0;
		};

		constexpr Flags<ShaderStage> kTransformStages = Flags(ShaderStage::eVertex) | ShaderStage::eFragment;

		[[nodiscard]] bool IsSrgb(const Format format) noexcept
		{
			switch (format)
			{
			case Format::eRGBA8Srgb:
			case Format::eBGRA8Srgb:
			case Format::eBC1RGBASrgb:
			case Format::eBC3Srgb:	   return true;
			default:				   return false;
			}
		}

		static_assert(sizeof(ImDrawIdx) == sizeof(std::uint16_t) || sizeof(ImDrawIdx) == sizeof(std::uint32_t),
			"Dear ImGui's index type has to be one of the two widths a draw can use");

		[[nodiscard]] ImTextureID PackSet(const DescriptorSetHandle set) noexcept
		{
			return (static_cast<ImTextureID>(set.generation) << 32u) | static_cast<ImTextureID>(set.index + 1u);
		}

		[[nodiscard]] DescriptorSetHandle UnpackSet(const ImTextureID id) noexcept
		{
			if (id == ImTextureID_Invalid)
			{
				return {};
			}

			return DescriptorSetHandle{
				.index		= static_cast<std::uint32_t>(id & 0xffffffffu) - 1u,
				.generation = static_cast<std::uint32_t>(id >> 32u),
			};
		}

		[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		[[nodiscard]] std::array<ShaderBinary, 2> ShadersFor([[maybe_unused]] const GraphicsApiId api) noexcept
		{
			std::array<ShaderBinary, 2> stages{};
			stages[0].stage = ShaderStage::eVertex;
			stages[1].stage = ShaderStage::eFragment;

#ifdef AZOTH_RHI_IMGUI_HAVE_SPIRV
			if (api == VulkanApi::kId)
			{
				stages[0].format = ShaderBinaryFormat::eSpirV;
				stages[0].data	 = shaders::kImgui_vertex_spirv;
				stages[0].size	 = shaders::kImgui_vertex_spirvSize;
				stages[1].format = ShaderBinaryFormat::eSpirV;
				stages[1].data	 = shaders::kImgui_fragment_spirv;
				stages[1].size	 = shaders::kImgui_fragment_spirvSize;

				stages[0].entryPoint = AZOTH_RHI_IMGUI_SPIRV_VERTEX_ENTRY;
				stages[1].entryPoint = AZOTH_RHI_IMGUI_SPIRV_FRAGMENT_ENTRY;
			}
#endif
#ifdef AZOTH_RHI_IMGUI_HAVE_DXIL
			if (api == D3D12Api::id)
			{
				stages[0].format	 = ShaderBinaryFormat::eDxil;
				stages[0].data		 = shaders::kImgui_vertex_dxil;
				stages[0].size		 = shaders::kImgui_vertex_dxilSize;
				stages[1].format	 = ShaderBinaryFormat::eDxil;
				stages[1].data		 = shaders::kImgui_fragment_dxil;
				stages[1].size		 = shaders::kImgui_fragment_dxilSize;
				stages[0].entryPoint = AZOTH_RHI_IMGUI_DXIL_VERTEX_ENTRY;
				stages[1].entryPoint = AZOTH_RHI_IMGUI_DXIL_FRAGMENT_ENTRY;
			}
#endif
#ifdef AZOTH_RHI_IMGUI_HAVE_METALLIB
			if (is_metal_family(api))
			{
				stages[0].format = ShaderBinaryFormat::eBackendNative;
				stages[0].data	 = shaders::kImgui_vertex_metallib;
				stages[0].size	 = shaders::kImgui_vertex_metallibSize;
				stages[1].format = ShaderBinaryFormat::eBackendNative;
				stages[1].data	 = shaders::kImgui_fragment_metallib;
				stages[1].size	 = shaders::kImgui_fragment_metallibSize;

				stages[0].entryPoint = AZOTH_RHI_IMGUI_METALLIB_VERTEX_ENTRY;
				stages[1].entryPoint = AZOTH_RHI_IMGUI_METALLIB_FRAGMENT_ENTRY;
			}
#endif

			return stages;
		}
	}

	Result<Renderer> Renderer::Create(Device & device, const RendererDesc & desc) noexcept
	{
		if (!device.is_valid())
		{
			return Error{ .code = ErrorCode::eInvalidArgument, .message = "the ImGui renderer needs a valid device" };
		}

		if (desc.arena == nullptr || !desc.arena->is_valid())
		{
			return Error{ .code = ErrorCode::eInvalidArgument, .message = "the ImGui renderer needs a descriptor arena to allocate its sets from" };
		}

		if (desc.colorFormat == Format::eUndefined)
		{
			return Error{ .code = ErrorCode::eInvalidArgument, .message = "the ImGui renderer needs the format of the attachment it draws over" };
		}

		Renderer renderer;
		renderer.m_device = device;
		renderer.m_arena  = desc.arena;
		renderer.m_frames.resize(std::max(desc.framesInFlight, 1u));

		const std::uint64_t reported = device.get_caps().optimalBufferCopyOffsetAlignment;
		renderer.m_copyAlignment	 = reported != 0 ? reported : kFallbackCopyAlignment;
		renderer.m_srgbTarget		 = IsSrgb(desc.colorFormat);

		Error error{};
		if (!renderer.CreatePipeline(desc, error))
		{
			renderer.Release();
			return error;
		}

		// NOLINTNEXTLINE(hicpp-signed-bitwise): ImGui's flag enums are signed, which is its type system and not anything decided here.
		ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

		return renderer;
	}

	Renderer::Renderer(Renderer && other) noexcept
		: m_device(other.m_device),
		  m_arena(other.m_arena),
		  m_copyAlignment(other.m_copyAlignment),
		  m_srgbTarget(other.m_srgbTarget),
		  m_sampler(other.m_sampler),
		  m_setLayout(other.m_setLayout),
		  m_pipelineLayout(other.m_pipelineLayout),
		  m_pipeline(other.m_pipeline),
		  m_frames(std::move(other.m_frames)),
		  m_registered(std::move(other.m_registered)),
		  m_pending(std::move(other.m_pending))
	{
		other.Clear();
	}

	Renderer & Renderer::operator=(Renderer && other) noexcept
	{
		if (this != &other)
		{
			Release();

			m_device		 = other.m_device;
			m_arena			 = other.m_arena;
			m_copyAlignment	 = other.m_copyAlignment;
			m_srgbTarget	 = other.m_srgbTarget;
			m_sampler		 = other.m_sampler;
			m_setLayout		 = other.m_setLayout;
			m_pipelineLayout = other.m_pipelineLayout;
			m_pipeline		 = other.m_pipeline;
			m_frames		 = std::move(other.m_frames);
			m_registered	 = std::move(other.m_registered);
			m_pending		 = std::move(other.m_pending);

			other.Clear();
		}

		return *this;
	}

	Renderer::~Renderer()
	{
		Release();
	}

	void Renderer::Clear() noexcept
	{
		m_device		 = {};
		m_arena			 = nullptr;
		m_copyAlignment	 = 1;
		m_srgbTarget	 = false;
		m_sampler		 = {};
		m_setLayout		 = {};
		m_pipelineLayout = {};
		m_pipeline		 = {};
		m_frames.clear();
		m_registered.clear();
		m_pending.clear();
	}

	void Renderer::Release() noexcept
	{
		if (!m_device.is_valid())
		{
			Clear();
			return;
		}

		for (const Texture & texture : m_registered)
		{
			if (texture.owned)
			{
				static_cast<void>(m_device.destroy(texture.view));
				static_cast<void>(m_device.destroy(texture.texture));
			}
		}

		for (const Texture & texture : m_pending)
		{
			if (texture.owned)
			{
				static_cast<void>(m_device.destroy(texture.view));
				static_cast<void>(m_device.destroy(texture.texture));
			}
		}

		for (const Frame & frame : m_frames)
		{
			if (frame.vertexData != nullptr)
			{
				static_cast<void>(m_device.unmap(frame.vertices));
			}
			if (frame.indexData != nullptr)
			{
				static_cast<void>(m_device.unmap(frame.indices));
			}
			if (frame.staging.data != nullptr)
			{
				static_cast<void>(m_device.unmap(frame.staging.buffer));
			}

			static_cast<void>(m_device.destroy(frame.vertices));
			static_cast<void>(m_device.destroy(frame.indices));
			static_cast<void>(m_device.destroy(frame.staging.buffer));
		}

		static_cast<void>(m_device.destroy(m_pipeline));
		static_cast<void>(m_device.destroy(m_pipelineLayout));
		static_cast<void>(m_device.destroy(m_setLayout));
		static_cast<void>(m_device.destroy(m_sampler));

		Clear();
	}

	bool Renderer::CreatePipeline(const RendererDesc & desc, Error & error) noexcept
	{
		const std::array<ShaderBinary, 2> stages = ShadersFor(m_device.get_graphics_api_id());
		if (stages[0].data == nullptr || stages[1].data == nullptr)
		{
			error = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "azoth::rhi-imgui was not compiled with a shader for this backend" };
			return false;
		}

		m_sampler = m_device.create_sampler(
			SamplerDesc{
				.addressU  = AddressMode::eClampToEdge,
				.addressV  = AddressMode::eClampToEdge,
				.addressW  = AddressMode::eClampToEdge,
				.debugName = desc.debugName,
			},
			error);

		const std::array bindings{
			DescriptorBinding{ .binding = kImageBinding, .type = DescriptorType::eTextureSRV, .stages = ShaderStage::eFragment },
			DescriptorBinding{ .binding = kSamplerBinding, .type = DescriptorType::eSampler, .stages = ShaderStage::eFragment },
		};

		m_setLayout = m_device.create_descriptor_set_layout(DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = desc.debugName }, error);

		const std::array sets{ m_setLayout };
		const std::array pushConstants{ PushConstantRange{ .stages = kTransformStages, .offset = 0, .size = sizeof(Transform) } };

		m_pipelineLayout =
			m_device.create_pipeline_layout(PipelineLayoutDesc{ .sets = sets, .pushConstants = pushConstants, .debugName = desc.debugName }, error);

		const std::array vertexBindings{ VertexBindingDesc{ .binding = 0, .stride = sizeof(ImDrawVert) } };

		const std::array vertexAttributes{
			VertexAttributeDesc{ .location = 0, .binding = 0, .format = Format::eRG32Float, .offset = offsetof(ImDrawVert, pos) },
			VertexAttributeDesc{ .location = 1, .binding = 0, .format = Format::eRG32Float, .offset = offsetof(ImDrawVert, uv) },
			VertexAttributeDesc{ .location = 2, .binding = 0, .format = Format::eRGBA8UNorm, .offset = offsetof(ImDrawVert, col) },
		};

		const VertexInputDesc vertexInput{ .bindings = vertexBindings, .attributes = vertexAttributes };

		BlendStateDesc blend{};
		blend.attachmentCount = 1;
		blend.attachments[0]  = ColorBlendAttachmentDesc{
			.blendEnable		 = true,
			.srcColorBlendFactor = BlendFactor::eSrcAlpha,
			.dstColorBlendFactor = BlendFactor::eOneMinusSrcAlpha,
			.colorBlendOp		 = BlendOp::eAdd,
			.srcAlphaBlendFactor = BlendFactor::eOne,
			.dstAlphaBlendFactor = BlendFactor::eOneMinusSrcAlpha,
			.alphaBlendOp		 = BlendOp::eAdd,
		};

		RenderTargetDesc renderTarget{};
		renderTarget.colorFormats[0]  = desc.colorFormat;
		renderTarget.colorFormatCount = 1;

		m_pipeline = m_device.create_graphics_pipeline(
			GraphicsPipelineDesc{
				.layout		   = m_pipelineLayout,
				.shaders	   = stages,
				.vertexInput   = &vertexInput,
				.raster		   = { .cullMode = CullMode::eNone },
				.depthStencil  = { .depthTestEnable = false, .depthWriteEnable = false },
				.blend		   = blend,
				.renderTarget  = renderTarget,
				.pipelineCache = desc.cache,
				.dynamicStates = Flags<DynamicState>(DynamicState::eViewport) | DynamicState::eScissor,
				.debugName	   = desc.debugName,
			},
			error);

		return m_sampler.is_valid() && m_setLayout.is_valid() && m_pipelineLayout.is_valid() && m_pipeline.is_valid();
	}

	DescriptorSetHandle Renderer::AllocateSet(const TextureViewHandle view, Error & error) noexcept
	{
		const DescriptorSetHandle set = m_arena->allocate(DescriptorSetAllocDesc{ .layout = m_setLayout }, error);
		if (!set.is_valid())
		{
			return {};
		}

		const std::array textureWrites{ DescriptorWriteTexture{ .set = set, .binding = kImageBinding, .type = DescriptorType::eTextureSRV, .view = view } };
		const std::array samplerWrites{ DescriptorWriteSampler{ .set = set, .binding = kSamplerBinding, .sampler = m_sampler } };

		if (!m_device.update_descriptors(std::span{ textureWrites }, error) || !m_device.update_descriptors(std::span{ samplerWrites }, error))
		{
			return {};
		}

		return set;
	}

	ImTextureID Renderer::RegisterTexture(const TextureViewHandle view, Error & error) noexcept
	{
		error = {};

		if (!view.is_valid() || !IsValid())
		{
			error = Error{ .code = ErrorCode::eInvalidArgument, .message = "RegisterTexture needs a valid view and a created renderer" };
			return ImTextureID_Invalid;
		}

		const auto found = std::ranges::find(m_registered, view, &Texture::view);
		if (found != m_registered.end())
		{
			return PackSet(found->set);
		}

		const DescriptorSetHandle set = AllocateSet(view, error);
		if (!set.is_valid())
		{
			return ImTextureID_Invalid;
		}

		m_registered.push_back(Texture{ .view = view, .set = set, .owned = false });

		return PackSet(set);
	}

	void Renderer::UnregisterTexture(const ImTextureID id) noexcept
	{
		const DescriptorSetHandle set = UnpackSet(id);

		const auto found = std::ranges::find(m_registered, set, &Texture::set);
		if (found == m_registered.end())
		{
			return;
		}

		m_pending.push_back(*found);
		m_registered.erase(found);
	}

	bool Renderer::Retire(const RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};

		if (!m_device.is_valid())
		{
			return true;
		}

		const DestroyDesc destroy{ .policy = DestroyPolicy::eDeferUntilSafe, .safeAfter = safeAfter };

		bool ok = true;
		for (const Texture & texture : m_pending)
		{
			if (texture.owned)
			{
				ok = m_device.destroy(texture.view, destroy, error) && ok;
				ok = m_device.destroy(texture.texture, destroy, error) && ok;
			}
		}

		m_pending.clear();

		return ok;
	}

	std::uint8_t * Renderer::StageBytes(const std::uint32_t frameSlot, const std::uint64_t bytes, std::uint64_t & outOffset, Error & error) noexcept
	{
		Staging & staging = m_frames[frameSlot % m_frames.size()].staging;

		const std::uint64_t offset = AlignUp(staging.used, m_copyAlignment);
		if (offset > staging.size || bytes > staging.size - offset)
		{
			error = Error{ .code = ErrorCode::eOutOfDeviceMemory, .message = "the ImGui staging buffer was sized for less than this frame asked for" };
			return nullptr;
		}

		staging.used = offset + bytes;
		outOffset	 = offset;

		return staging.data + offset; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}

	bool Renderer::UpdateTextures(CommandList & list, const ImDrawData & drawData, const std::uint32_t frameSlot, Error & error) noexcept
	{
		error = {};

		if (!IsValid())
		{
			error = Error{ .code = ErrorCode::eInvalidState, .message = "UpdateTextures on a renderer that was never created" };
			return false;
		}

		if (drawData.Textures == nullptr)
		{
			return true;
		}

		std::uint64_t required = 0;
		for (const ImTextureData * data : *drawData.Textures)
		{
			if (data->Status == ImTextureStatus_WantCreate)
			{
				required = AlignUp(required, m_copyAlignment) + (static_cast<std::uint64_t>(data->Width) * data->Height * data->BytesPerPixel);
			}
			else if (data->Status == ImTextureStatus_WantUpdates)
			{
				const ImTextureRect & box = data->UpdateRect;
				required				  = AlignUp(required, m_copyAlignment) + (static_cast<std::uint64_t>(box.w) * box.h * data->BytesPerPixel);
			}
		}

		Frame & frame	   = m_frames[frameSlot % m_frames.size()];
		frame.staging.used = 0;

		if (required > frame.staging.size)
		{
			if (frame.staging.data != nullptr)
			{
				static_cast<void>(m_device.unmap(frame.staging.buffer));
			}

			static_cast<void>(m_device.destroy(frame.staging.buffer));
			frame.staging = Staging{};

			const std::uint64_t want = required + (required / 2);

			frame.staging.buffer = m_device.create_buffer(
				BufferDesc{
					.size	   = want,
					.usage	   = BufferUsage::eCopySrc,
					.memory	   = MemoryUsage::eCpuToGpu,
					.debugName = "imgui.textureStaging",
				},
				error);

			const MappedMemory mapped =
				frame.staging.buffer.is_valid() ? m_device.map(frame.staging.buffer, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};

			if (mapped.data == nullptr)
			{
				return false;
			}

			frame.staging.data = static_cast<std::uint8_t *>(mapped.data);
			frame.staging.size = want;
		}

		for (ImTextureData * data : *drawData.Textures)
		{
			bool ok = true;
			switch (data->Status)
			{
			case ImTextureStatus_WantCreate:  ok = CreateTexture(list, *data, frameSlot, error); break;
			case ImTextureStatus_WantUpdates: ok = UpdateTexture(list, *data, frameSlot, error); break;
			case ImTextureStatus_WantDestroy: DestroyTexture(*data); break;
			case ImTextureStatus_OK:
			case ImTextureStatus_Destroyed:	  break;
			}

			if (!ok)
			{
				return false;
			}
		}

		return true;
	}

	bool Renderer::CreateTexture(CommandList & list, ImTextureData & data, const std::uint32_t frameSlot, Error & error) noexcept
	{
		const auto width  = static_cast<std::uint32_t>(data.Width);
		const auto height = static_cast<std::uint32_t>(data.Height);

		Texture texture{ .owned = true };

		texture.texture = m_device.create_texture(
			TextureDesc{
				.format	   = Format::eRGBA8UNorm,
				.width	   = width,
				.height	   = height,
				.usage	   = Flags<TextureUsage>(TextureUsage::eSampled) | TextureUsage::eCopyDst,
				.debugName = "imgui.texture",
			},
			error);

		texture.view = m_device.create_texture_view(texture.texture, TextureViewDesc{ .debugName = "imgui.textureView" }, error);
		if (!texture.texture.is_valid() || !texture.view.is_valid())
		{
			static_cast<void>(m_device.destroy(texture.view));
			static_cast<void>(m_device.destroy(texture.texture));
			return false;
		}

		texture.set = AllocateSet(texture.view, error);
		if (!texture.set.is_valid())
		{
			static_cast<void>(m_device.destroy(texture.view));
			static_cast<void>(m_device.destroy(texture.texture));
			return false;
		}

		const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;

		std::uint64_t offset  = 0;
		std::uint8_t * staged = StageBytes(frameSlot, bytes, offset, error);
		if (staged == nullptr)
		{
			static_cast<void>(m_device.destroy(texture.view));
			static_cast<void>(m_device.destroy(texture.texture));
			return false;
		}

		if (data.Format == ImTextureFormat_RGBA32)
		{
			std::memcpy(staged, data.GetPixels(), bytes);
		}
		else
		{
			const auto * source = static_cast<const std::uint8_t *>(data.GetPixels());
			for (std::uint64_t texel = 0; texel < static_cast<std::uint64_t>(width) * height; ++texel)
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
				staged[(texel * 4) + 0] = 0xff;
				staged[(texel * 4) + 1] = 0xff;
				staged[(texel * 4) + 2] = 0xff;
				staged[(texel * 4) + 3] = source[texel];
				// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			}
		}

		const std::array toCopyDst{
			TextureBarrier{
				.texture = texture.texture,
				.before	 = { .use = ResourceUse::eDiscard },
				.after	 = { .use = ResourceUse::eCopyDst, .stages = Stage::eCopy },
			},
		};

		const std::array toSample{
			TextureBarrier{
				.texture = texture.texture,
				.before	 = { .use = ResourceUse::eCopyDst, .stages = Stage::eCopy },
				.after	 = { .use = ResourceUse::eSampledRead, .stages = Stage::eFragmentShading },
			},
		};

		const std::array regions{ BufferTextureCopy{ .bufferOffset = offset, .textureExtent = { .width = width, .height = height } } };

		if (!list.barriers(BarrierBatch{ .textures = toCopyDst }, error) ||
			!list.copy_buffer_to_texture(texture.texture, m_frames[frameSlot % m_frames.size()].staging.buffer, regions, error) ||
			!list.barriers(BarrierBatch{ .textures = toSample }, error))
		{
			static_cast<void>(m_device.destroy(texture.view));
			static_cast<void>(m_device.destroy(texture.texture));
			return false;
		}

		data.SetTexID(PackSet(texture.set));
		data.SetStatus(ImTextureStatus_OK);

		m_registered.push_back(texture);

		return true;
	}

	bool Renderer::UpdateTexture(CommandList & list, ImTextureData & data, const std::uint32_t frameSlot, Error & error) noexcept
	{
		const DescriptorSetHandle set = UnpackSet(data.GetTexID());

		const auto found = std::ranges::find(m_registered, set, &Texture::set);
		if (found == m_registered.end())
		{
			data.SetStatus(ImTextureStatus_WantCreate);
			return true;
		}

		const ImTextureRect & box = data.UpdateRect;
		if (box.w == 0 || box.h == 0)
		{
			data.SetStatus(ImTextureStatus_OK);
			return true;
		}

		const std::uint64_t bytes = static_cast<std::uint64_t>(box.w) * box.h * 4;

		std::uint64_t offset  = 0;
		std::uint8_t * staged = StageBytes(frameSlot, bytes, offset, error);
		if (staged == nullptr)
		{
			return false;
		}

		for (unsigned short row = 0; row < box.h; ++row)
		{
			const auto * source	  = static_cast<const std::uint8_t *>(data.GetPixelsAt(box.x, box.y + row));
			std::uint8_t * target = staged + (static_cast<std::uint64_t>(row) * box.w * 4); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			if (data.Format == ImTextureFormat_RGBA32)
			{
				std::memcpy(target, source, static_cast<std::uint64_t>(box.w) * 4);
			}
			else
			{
				for (unsigned short column = 0; column < box.w; ++column)
				{
					// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
					target[(column * 4) + 0] = 0xff;
					target[(column * 4) + 1] = 0xff;
					target[(column * 4) + 2] = 0xff;
					target[(column * 4) + 3] = source[column];
					// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
				}
			}
		}

		const std::array toCopyDst{
			TextureBarrier{
				.texture = found->texture,
				.before	 = { .use = ResourceUse::eSampledRead, .stages = Stage::eFragmentShading },
				.after	 = { .use = ResourceUse::eCopyDst, .stages = Stage::eCopy },
			},
		};

		const std::array toSample{
			TextureBarrier{
				.texture = found->texture,
				.before	 = { .use = ResourceUse::eCopyDst, .stages = Stage::eCopy },
				.after	 = { .use = ResourceUse::eSampledRead, .stages = Stage::eFragmentShading },
			},
		};

		const std::array regions{
			BufferTextureCopy{
				.bufferOffset  = offset,
				.textureOffset = { .x = box.x, .y = box.y },
				.textureExtent = { .width = box.w, .height = box.h },
			},
		};

		if (!list.barriers(BarrierBatch{ .textures = toCopyDst }, error) ||
			!list.copy_buffer_to_texture(found->texture, m_frames[frameSlot % m_frames.size()].staging.buffer, regions, error) ||
			!list.barriers(BarrierBatch{ .textures = toSample }, error))
		{
			return false;
		}

		data.SetStatus(ImTextureStatus_OK);

		return true;
	}

	void Renderer::DestroyTexture(ImTextureData & data) noexcept
	{
		const DescriptorSetHandle set = UnpackSet(data.GetTexID());

		if (const auto found = std::ranges::find(m_registered, set, &Texture::set); found != m_registered.end())
		{
			m_pending.push_back(*found);
			m_registered.erase(found);
		}

		data.SetTexID(ImTextureID_Invalid);
		data.SetStatus(ImTextureStatus_Destroyed);
	}

	bool Renderer::ReserveGeometry(const std::uint32_t frameSlot, const std::uint64_t vertexBytes, const std::uint64_t indexBytes, Error & error) noexcept
	{
		Frame & frame = m_frames[frameSlot % m_frames.size()];
		if (frame.vertexBytes >= vertexBytes && frame.indexBytes >= indexBytes)
		{
			return true;
		}

		const std::uint64_t wantVertices = std::max(vertexBytes + (vertexBytes / 2), frame.vertexBytes);
		const std::uint64_t wantIndices	 = std::max(indexBytes + (indexBytes / 2), frame.indexBytes);

		if (frame.vertexData != nullptr)
		{
			static_cast<void>(m_device.unmap(frame.vertices));
		}
		if (frame.indexData != nullptr)
		{
			static_cast<void>(m_device.unmap(frame.indices));
		}

		static_cast<void>(m_device.destroy(frame.vertices));
		static_cast<void>(m_device.destroy(frame.indices));

		frame.vertices	 = {};
		frame.indices	 = {};
		frame.vertexData = nullptr;
		frame.indexData	 = nullptr;

		frame.vertices = m_device.create_buffer(
			BufferDesc{
				.size	   = wantVertices,
				.stride	   = sizeof(ImDrawVert),
				.usage	   = BufferUsage::eVertex,
				.memory	   = MemoryUsage::eCpuToGpu,
				.debugName = "imgui.vertices",
			},
			error);

		frame.indices = m_device.create_buffer(
			BufferDesc{
				.size	   = wantIndices,
				.stride	   = sizeof(ImDrawIdx),
				.usage	   = BufferUsage::eIndex,
				.memory	   = MemoryUsage::eCpuToGpu,
				.debugName = "imgui.indices",
			},
			error);

		const MappedMemory vertexMap = frame.vertices.is_valid() ? m_device.map(frame.vertices, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};
		const MappedMemory indexMap	 = frame.indices.is_valid() ? m_device.map(frame.indices, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};

		if (vertexMap.data == nullptr || indexMap.data == nullptr)
		{
			return false;
		}

		frame.vertexData  = static_cast<std::uint8_t *>(vertexMap.data);
		frame.indexData	  = static_cast<std::uint8_t *>(indexMap.data);
		frame.vertexBytes = wantVertices;
		frame.indexBytes  = wantIndices;

		return true;
	}

	bool Renderer::Record(CommandList & list, const ImDrawData & drawData, const std::uint32_t frameSlot, Error & error) noexcept
	{
		error = {};

		if (!IsValid())
		{
			error = Error{ .code = ErrorCode::eInvalidState, .message = "Record on a renderer that was never created" };
			return false;
		}

		const int targetWidth  = static_cast<int>(drawData.DisplaySize.x * drawData.FramebufferScale.x);
		const int targetHeight = static_cast<int>(drawData.DisplaySize.y * drawData.FramebufferScale.y);
		if (targetWidth <= 0 || targetHeight <= 0 || drawData.TotalVtxCount == 0)
		{
			return true;
		}

		const auto vertexBytes = static_cast<std::uint64_t>(drawData.TotalVtxCount) * sizeof(ImDrawVert);
		const auto indexBytes  = static_cast<std::uint64_t>(drawData.TotalIdxCount) * sizeof(ImDrawIdx);

		if (!ReserveGeometry(frameSlot, vertexBytes, indexBytes, error))
		{
			return false;
		}

		Frame & frame = m_frames[frameSlot % m_frames.size()];

		std::uint64_t vertexOffset = 0;
		std::uint64_t indexOffset  = 0;
		for (const ImDrawList * cmdList : drawData.CmdLists)
		{
			const auto listVertexBytes = static_cast<std::uint64_t>(cmdList->VtxBuffer.Size) * sizeof(ImDrawVert);
			const auto listIndexBytes  = static_cast<std::uint64_t>(cmdList->IdxBuffer.Size) * sizeof(ImDrawIdx);

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			std::memcpy(frame.vertexData + vertexOffset, cmdList->VtxBuffer.Data, listVertexBytes);
			std::memcpy(frame.indexData + indexOffset, cmdList->IdxBuffer.Data, listIndexBytes);
			// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			vertexOffset += listVertexBytes;
			indexOffset += listIndexBytes;
		}

		const float scaleX = 2.0f / drawData.DisplaySize.x;
		const float scaleY = 2.0f / drawData.DisplaySize.y;

		const Transform transform{
			.scale		= { scaleX, -scaleY },
			.translate	= { -1.0f - (drawData.DisplayPos.x * scaleX), 1.0f + (drawData.DisplayPos.y * scaleY) },
			.srgbTarget = m_srgbTarget ? 1u : 0u,
		};

		const Viewport viewport{ .width = static_cast<float>(targetWidth), .height = static_cast<float>(targetHeight) };

		bool recorded = list.set_graphics_pipeline(m_pipeline, error) && list.set_viewport(viewport, error) &&
						list.push_constants(m_pipelineLayout, kTransformStages, 0, sizeof(transform), &transform, error) &&
						list.set_vertex_buffer(0, frame.vertices, 0, error) &&
						list.set_index_buffer(frame.indices, 0, sizeof(ImDrawIdx) == sizeof(std::uint32_t), error);

		DescriptorSetHandle bound{};

		std::int32_t vertexBase = 0;
		std::uint32_t indexBase = 0;
		for (const ImDrawList * cmdList : drawData.CmdLists)
		{
			for (const ImDrawCmd & command : cmdList->CmdBuffer)
			{
				if (!recorded)
				{
					break;
				}

				if (command.UserCallback != nullptr)
				{
					continue;
				}

				const float left   = (command.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
				const float top	   = (command.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
				const float right  = (command.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
				const float bottom = (command.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y;

				const auto clipX	  = static_cast<std::int32_t>(std::clamp(left, 0.0f, static_cast<float>(targetWidth)));
				const auto clipY	  = static_cast<std::int32_t>(std::clamp(top, 0.0f, static_cast<float>(targetHeight)));
				const auto clipRight  = static_cast<std::int32_t>(std::clamp(right, 0.0f, static_cast<float>(targetWidth)));
				const auto clipBottom = static_cast<std::int32_t>(std::clamp(bottom, 0.0f, static_cast<float>(targetHeight)));

				if (clipRight <= clipX || clipBottom <= clipY)
				{
					continue;
				}

				if (const DescriptorSetHandle set = UnpackSet(command.GetTexID()); set.is_valid() && set != bound)
				{
					recorded = list.bind_descriptor_set(m_pipelineLayout, kTextureSet, set, {}, error);
					bound	 = set;
				}

				const Rect2D scissor{
					.x		= clipX,
					.y		= clipY,
					.width	= static_cast<std::uint32_t>(clipRight - clipX),
					.height = static_cast<std::uint32_t>(clipBottom - clipY),
				};

				recorded =
					recorded && list.set_scissor(scissor, error) &&
					list.draw_indexed(command.ElemCount, 1, indexBase + command.IdxOffset, vertexBase + static_cast<std::int32_t>(command.VtxOffset), 0, error);
			}

			vertexBase += cmdList->VtxBuffer.Size;
			indexBase += static_cast<std::uint32_t>(cmdList->IdxBuffer.Size);
		}

		return recorded;
	}
}

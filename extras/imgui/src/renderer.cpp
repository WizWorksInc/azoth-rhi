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

		// What a copy's buffer offset is aligned to when a device reports no requirement of its own.
		constexpr std::uint64_t kFallbackCopyAlignment = 4;

		// What both stages are pushed, matching Transform in every one of the four shader sources.
		struct Transform final
		{
			std::array<float, 2> scale{};
			std::array<float, 2> translate{};
			std::uint32_t srgbTarget = 0;
		};

		// Both stages read the block, the vertex one for the projection and the fragment one for srgbTarget, so both have to be named twice over: once by the
		// layout's range and once by the push itself. Naming one alone leaves the other reading a block nothing bound.
		constexpr Flags<ShaderStage> kTransformStages = Flags(ShaderStage::eVertex) | ShaderStage::eFragment;

		// Whether writing to this format makes the hardware encode what the shader produced.
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

		/*
		 * A descriptor set carried as ImGui's texture identifier, which is what lets a draw command bind what it names without a lookup.
		 *
		 * The index is biased by one because ImGui reserves zero for no texture, and a handle whose index and generation are both zero is otherwise a perfectly good
		 * one. Every other bit rides along untouched, an identifier being sixty four bits wide and a handle being two words.
		 */
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

		// The two stages this backend was compiled for, or a pair of empty binaries when it was not compiled for this one.
		[[nodiscard]] std::array<ShaderBinary, 2> ShadersFor([[maybe_unused]] const GraphicsApiId api) noexcept
		{
			std::array<ShaderBinary, 2> stages{};
			stages[0].stage = ShaderStage::eVertex;
			stages[1].stage = ShaderStage::eFragment;

#ifdef AZOTH_RHI_IMGUI_HAVE_SPIRV
			if (api == VulkanApi::id)
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
			if (IsMetalFamily(api))
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
	} // namespace

	Result<Renderer> Renderer::Create(Device & device, const RendererDesc & desc) noexcept
	{
		if (!device.IsValid())
		{
			return Error{ .code = ErrorCode::eInvalidArgument, .message = "the ImGui renderer needs a valid device" };
		}

		if (desc.arena == nullptr || !desc.arena->IsValid())
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

		// Asked of the device, not assumed. Zero means it has no requirement, which still needs an alignment the arithmetic below can use.
		const std::uint64_t reported = device.GetCaps().optimalBufferCopyOffsetAlignment;
		renderer.m_copyAlignment	 = reported != 0 ? reported : kFallbackCopyAlignment;
		renderer.m_srgbTarget		 = IsSrgb(desc.colorFormat);

		Error error{};
		if (!renderer.CreatePipeline(desc, error))
		{
			renderer.Release();
			return error;
		}

		/*
		 * Telling ImGui the backend answers texture requests, which is what puts it in charge of its own atlases: it creates one, asks for it to be uploaded, grows
		 * it when a font gains a glyph and asks for the changed rectangle alone. Without this flag it falls back to expecting a backend to have uploaded the whole
		 * atlas up front, which is the older contract this does not implement.
		 */
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
		if (!m_device.IsValid())
		{
			Clear();
			return;
		}

		for (const Texture & texture : m_registered)
		{
			if (texture.owned)
			{
				static_cast<void>(m_device.Destroy(texture.view));
				static_cast<void>(m_device.Destroy(texture.texture));
			}
		}

		for (const Texture & texture : m_pending)
		{
			if (texture.owned)
			{
				static_cast<void>(m_device.Destroy(texture.view));
				static_cast<void>(m_device.Destroy(texture.texture));
			}
		}

		for (const Frame & frame : m_frames)
		{
			if (frame.vertexData != nullptr)
			{
				static_cast<void>(m_device.Unmap(frame.vertices));
			}
			if (frame.indexData != nullptr)
			{
				static_cast<void>(m_device.Unmap(frame.indices));
			}
			if (frame.staging.data != nullptr)
			{
				static_cast<void>(m_device.Unmap(frame.staging.buffer));
			}

			static_cast<void>(m_device.Destroy(frame.vertices));
			static_cast<void>(m_device.Destroy(frame.indices));
			static_cast<void>(m_device.Destroy(frame.staging.buffer));
		}

		static_cast<void>(m_device.Destroy(m_pipeline));
		static_cast<void>(m_device.Destroy(m_pipelineLayout));
		static_cast<void>(m_device.Destroy(m_setLayout));
		static_cast<void>(m_device.Destroy(m_sampler));

		Clear();
	}

	bool Renderer::CreatePipeline(const RendererDesc & desc, Error & error) noexcept
	{
		const std::array<ShaderBinary, 2> stages = ShadersFor(m_device.GetGraphicsApiId());
		if (stages[0].data == nullptr || stages[1].data == nullptr)
		{
			error = Error{ .code = ErrorCode::eUnsupportedFeature, .message = "azoth::rhi-imgui was not compiled with a shader for this backend" };
			return false;
		}

		// Clamped, because a triangle sampling at the edge of a glyph would otherwise pick up whatever wrapped round from the far side of the atlas.
		m_sampler = m_device.CreateSampler(
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

		m_setLayout = m_device.CreateDescriptorSetLayout(DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = desc.debugName }, error);

		const std::array sets{ m_setLayout };
		const std::array pushConstants{ PushConstantRange{ .stages = kTransformStages, .offset = 0, .size = sizeof(Transform) } };

		m_pipelineLayout =
			m_device.CreatePipelineLayout(PipelineLayoutDesc{ .sets = sets, .pushConstants = pushConstants, .debugName = desc.debugName }, error);

		// One buffer holding all three attributes interleaved, which is what ImDrawVert is.
		const std::array vertexBindings{ VertexBindingDesc{ .binding = 0, .stride = sizeof(ImDrawVert) } };

		const std::array vertexAttributes{
			VertexAttributeDesc{ .location = 0, .binding = 0, .format = Format::eRG32Float, .offset = offsetof(ImDrawVert, pos) },
			VertexAttributeDesc{ .location = 1, .binding = 0, .format = Format::eRG32Float, .offset = offsetof(ImDrawVert, uv) },
			// Four bytes, not four floats, unpacked to the range zero to one on the way in, which is what saves ImGui three quarters of its vertex.
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
			// The destination keeps whatever coverage it already had, which is what stops an interface drawn over an opaque target punching holes in it.
			.srcAlphaBlendFactor = BlendFactor::eOne,
			.dstAlphaBlendFactor = BlendFactor::eOneMinusSrcAlpha,
			.alphaBlendOp		 = BlendOp::eAdd,
		};

		RenderTargetDesc renderTarget{};
		renderTarget.colorFormats[0]  = desc.colorFormat;
		renderTarget.colorFormatCount = 1;

		m_pipeline = m_device.CreateGraphicsPipeline(
			GraphicsPipelineDesc{
				.layout		 = m_pipelineLayout,
				.shaders	 = stages,
				.vertexInput = &vertexInput,
				// ImGui emits both windings depending on how a shape was built, so nothing is culled.
				.raster		   = { .cullMode = CullMode::eNone },
				.depthStencil  = { .depthTestEnable = false, .depthWriteEnable = false },
				.blend		   = blend,
				.renderTarget  = renderTarget,
				.pipelineCache = desc.cache,
				// The scissor changes per draw command, not per pass, which is the whole reason ImGui can clip a window to its own frame.
				.dynamicStates = Flags<DynamicState>(DynamicState::eViewport) | DynamicState::eScissor,
				.debugName	   = desc.debugName,
			},
			error);

		return m_sampler.IsValid() && m_setLayout.IsValid() && m_pipelineLayout.IsValid() && m_pipeline.IsValid();
	}

	DescriptorSetHandle Renderer::AllocateSet(const TextureViewHandle view, Error & error) noexcept
	{
		const DescriptorSetHandle set = m_arena->Allocate(DescriptorSetAllocDesc{ .layout = m_setLayout }, error);
		if (!set.IsValid())
		{
			return {};
		}

		const std::array textureWrites{ DescriptorWriteTexture{ .set = set, .binding = kImageBinding, .type = DescriptorType::eTextureSRV, .view = view } };
		const std::array samplerWrites{ DescriptorWriteSampler{ .set = set, .binding = kSamplerBinding, .sampler = m_sampler } };

		if (!m_device.UpdateDescriptors(std::span{ textureWrites }, error) || !m_device.UpdateDescriptors(std::span{ samplerWrites }, error))
		{
			return {};
		}

		return set;
	}

	ImTextureID Renderer::RegisterTexture(const TextureViewHandle view, Error & error) noexcept
	{
		error = {};

		if (!view.IsValid() || !IsValid())
		{
			error = Error{ .code = ErrorCode::eInvalidArgument, .message = "RegisterTexture needs a valid view and a created renderer" };
			return ImTextureID_Invalid;
		}

		// Asking twice for the same view hands back the same set, so a caller may do this every frame without allocating one each time.
		const auto found = std::ranges::find(m_registered, view, &Texture::view);
		if (found != m_registered.end())
		{
			return PackSet(found->set);
		}

		const DescriptorSetHandle set = AllocateSet(view, error);
		if (!set.IsValid())
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

		// Held, not dropped, because the frames still in flight may name this set.
		m_pending.push_back(*found);
		m_registered.erase(found);
	}

	bool Renderer::Retire(const RetirePoint safeAfter, Error & error) noexcept
	{
		error = {};

		if (!m_device.IsValid())
		{
			return true;
		}

		const DestroyDesc destroy{ .policy = DestroyPolicy::eDeferUntilSafe, .safeAfter = safeAfter };

		bool ok = true;
		for (const Texture & texture : m_pending)
		{
			// Only what this created. A registered view belongs to the caller and only its set was ever ours.
			if (texture.owned)
			{
				ok = m_device.Destroy(texture.view, destroy, error) && ok;
				ok = m_device.Destroy(texture.texture, destroy, error) && ok;
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

		/*
		 * The staging a frame needs is however much its texture requests add up to, which is known before any of it is written. Sized once here, not grown as
		 * it goes, because growing means a new buffer and the pointers already handed out would be into the old one.
		 */
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
				static_cast<void>(m_device.Unmap(frame.staging.buffer));
			}

			static_cast<void>(m_device.Destroy(frame.staging.buffer));
			frame.staging = Staging{};

			// Room to spare, so an atlas that gains a glyph every few frames does not reallocate every one of them.
			const std::uint64_t want = required + (required / 2);

			frame.staging.buffer = m_device.CreateBuffer(
				BufferDesc{
					.size		   = want,
					.usage		   = BufferUsage::eCopySrc,
					.memory		   = MemoryUsage::eCpuToGpu,
					.persistentMap = true,
					.debugName	   = "imgui.textureStaging",
				},
				error);

			const MappedMemory mapped =
				frame.staging.buffer.IsValid() ? m_device.Map(frame.staging.buffer, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};

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

		/*
		 * RGBA whatever ImGui rasterized, because that is the one layout every backend samples without a swizzle. A single channel atlas is expanded on the way
		 * through, not sampled as red and fixed up in the shader, which would make the shader depend on how the atlas was built.
		 */
		Texture texture{ .owned = true };

		texture.texture = m_device.CreateTexture(
			TextureDesc{
				// Unorm, not sRGB: ImGui's colours are already in whatever space the target wants, and decoding the atlas would darken the text.
				.format	   = Format::eRGBA8UNorm,
				.width	   = width,
				.height	   = height,
				.usage	   = Flags<TextureUsage>(TextureUsage::eSampled) | TextureUsage::eCopyDst,
				.debugName = "imgui.texture",
			},
			error);

		texture.view = m_device.CreateTextureView(texture.texture, TextureViewDesc{ .debugName = "imgui.textureView" }, error);
		if (!texture.texture.IsValid() || !texture.view.IsValid())
		{
			static_cast<void>(m_device.Destroy(texture.view));
			static_cast<void>(m_device.Destroy(texture.texture));
			return false;
		}

		texture.set = AllocateSet(texture.view, error);
		if (!texture.set.IsValid())
		{
			static_cast<void>(m_device.Destroy(texture.view));
			static_cast<void>(m_device.Destroy(texture.texture));
			return false;
		}

		const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4;

		std::uint64_t offset  = 0;
		std::uint8_t * staged = StageBytes(frameSlot, bytes, offset, error);
		if (staged == nullptr)
		{
			static_cast<void>(m_device.Destroy(texture.view));
			static_cast<void>(m_device.Destroy(texture.texture));
			return false;
		}

		if (data.Format == ImTextureFormat_RGBA32)
		{
			std::memcpy(staged, data.GetPixels(), bytes);
		}
		else
		{
			// One channel of coverage, spread white across the three ImGui did not store.
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
				.before	 = { .stages = PipelineStage::eNone, .access = Access::eNone, .layout = TextureLayout::eUndefined },
				.after	 = { .stages = PipelineStage::eCopy, .access = Access::eCopyWrite, .layout = TextureLayout::eCopyDst },
			},
		};

		const std::array toSample{
			TextureBarrier{
				.texture = texture.texture,
				.before	 = { .stages = PipelineStage::eCopy, .access = Access::eCopyWrite, .layout = TextureLayout::eCopyDst },
				.after	 = { .stages = PipelineStage::eFragmentShader, .access = Access::eShaderRead, .layout = TextureLayout::eShaderReadOnly },
			},
		};

		const std::array regions{ BufferTextureCopy{ .bufferOffset = offset, .textureExtent = { .width = width, .height = height } } };

		if (!list.Barriers(BarrierBatch{ .textures = toCopyDst }, error) ||
			!list.CopyBufferToTexture(texture.texture, m_frames[frameSlot % m_frames.size()].staging.buffer, regions, error) ||
			!list.Barriers(BarrierBatch{ .textures = toSample }, error))
		{
			static_cast<void>(m_device.Destroy(texture.view));
			static_cast<void>(m_device.Destroy(texture.texture));
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
			// ImGui asked to update something this never created so there is nothing to write into. Asking for a create instead gets it made before the next update.
			data.SetStatus(ImTextureStatus_WantCreate);
			return true;
		}

		/*
		 * The bounding box of everything queued, not each rectangle in Updates, which is one copy instead of a dozen for a handful of extra texels. ImGui offers both
		 * and says either is fine.
		 */
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

		// Packed tightly out of the rows the box covers, so the copy needs no source pitch of its own.
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
				.before	 = { .stages = PipelineStage::eFragmentShader, .access = Access::eShaderRead, .layout = TextureLayout::eShaderReadOnly },
				.after	 = { .stages = PipelineStage::eCopy, .access = Access::eCopyWrite, .layout = TextureLayout::eCopyDst },
			},
		};

		const std::array toSample{
			TextureBarrier{
				.texture = found->texture,
				.before	 = { .stages = PipelineStage::eCopy, .access = Access::eCopyWrite, .layout = TextureLayout::eCopyDst },
				.after	 = { .stages = PipelineStage::eFragmentShader, .access = Access::eShaderRead, .layout = TextureLayout::eShaderReadOnly },
			},
		};

		const std::array regions{
			BufferTextureCopy{
				.bufferOffset  = offset,
				.textureOffset = { .x = box.x, .y = box.y },
				.textureExtent = { .width = box.w, .height = box.h },
			},
		};

		if (!list.Barriers(BarrierBatch{ .textures = toCopyDst }, error) ||
			!list.CopyBufferToTexture(found->texture, m_frames[frameSlot % m_frames.size()].staging.buffer, regions, error) ||
			!list.Barriers(BarrierBatch{ .textures = toSample }, error))
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
			// Held, not destroyed here, because the frames still in flight name it. Retire is what lets it go.
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

		// Grown with room to spare and not to exactly what this frame asked for, so an interface that gains a row does not reallocate every frame.
		const std::uint64_t wantVertices = std::max(vertexBytes + (vertexBytes / 2), frame.vertexBytes);
		const std::uint64_t wantIndices	 = std::max(indexBytes + (indexBytes / 2), frame.indexBytes);

		if (frame.vertexData != nullptr)
		{
			static_cast<void>(m_device.Unmap(frame.vertices));
		}
		if (frame.indexData != nullptr)
		{
			static_cast<void>(m_device.Unmap(frame.indices));
		}

		static_cast<void>(m_device.Destroy(frame.vertices));
		static_cast<void>(m_device.Destroy(frame.indices));

		frame.vertices	 = {};
		frame.indices	 = {};
		frame.vertexData = nullptr;
		frame.indexData	 = nullptr;

		frame.vertices = m_device.CreateBuffer(
			BufferDesc{
				.size		   = wantVertices,
				.stride		   = sizeof(ImDrawVert),
				.usage		   = BufferUsage::eVertex,
				.memory		   = MemoryUsage::eCpuToGpu,
				.persistentMap = true,
				.debugName	   = "imgui.vertices",
			},
			error);

		frame.indices = m_device.CreateBuffer(
			BufferDesc{
				.size		   = wantIndices,
				.stride		   = sizeof(ImDrawIdx),
				.usage		   = BufferUsage::eIndex,
				.memory		   = MemoryUsage::eCpuToGpu,
				.persistentMap = true,
				.debugName	   = "imgui.indices",
			},
			error);

		const MappedMemory vertexMap = frame.vertices.IsValid() ? m_device.Map(frame.vertices, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};
		const MappedMemory indexMap	 = frame.indices.IsValid() ? m_device.Map(frame.indices, MapDesc{ .mode = MapMode::eWrite }, error) : MappedMemory{};

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

		// A minimized window, or an interface with nothing in it. Neither is a failure and neither has anything to record.
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

		// ImGui keeps its geometry in a list per window, so it goes in back to back and each list remembers where its own share started.
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

		/*
		 * ImGui works in pixels with the origin at the top left, and this turns that into clip space, with the display position folded in so a viewport that
		 * does not start at zero still lands right.
		 *
		 * The Y scale is negative, the part worth stopping on. Nearly every ImGui backend is written against Vulkan's clip space, where Y runs down. This RHI
		 * presents Y up unless SetClipSpace says otherwise, so the usual transform draws the interface upside down.
		 */
		const float scaleX = 2.0f / drawData.DisplaySize.x;
		const float scaleY = 2.0f / drawData.DisplaySize.y;

		const Transform transform{
			.scale		= { scaleX, -scaleY },
			.translate	= { -1.0f - (drawData.DisplayPos.x * scaleX), 1.0f + (drawData.DisplayPos.y * scaleY) },
			.srgbTarget = m_srgbTarget ? 1u : 0u,
		};

		const Viewport viewport{ .width = static_cast<float>(targetWidth), .height = static_cast<float>(targetHeight) };

		bool recorded = list.SetGraphicsPipeline(m_pipeline, error) && list.SetViewport(viewport, error) &&
						list.PushConstants(m_pipelineLayout, kTransformStages, 0, sizeof(transform), &transform, error) &&
						list.SetVertexBuffer(0, frame.vertices, 0, error) &&
						list.SetIndexBuffer(frame.indices, 0, sizeof(ImDrawIdx) == sizeof(std::uint32_t), error);

		// Bound only when it changes, which for an interface that is all text is once for the whole frame.
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

				// A callback is ImGui asking the caller to do something of its own here, which this backend does not run.
				if (command.UserCallback != nullptr)
				{
					continue;
				}

				/*
				 * The clip rectangle is in ImGui's own pixels, so it moves into the framebuffer's the same way the projection did, and is then clamped: a window dragged
				 * past the edge of the display produces a rectangle partly outside it, which every backend refuses.
				 */
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

				// Whatever texture the command named, which is the atlas for text and the caller's own for an image. No lookup: the identifier is the set.
				if (const DescriptorSetHandle set = UnpackSet(command.GetTexID()); set.IsValid() && set != bound)
				{
					recorded = list.BindDescriptorSet(m_pipelineLayout, kTextureSet, set, {}, error);
					bound	 = set;
				}

				const Rect2D scissor{
					.x		= clipX,
					.y		= clipY,
					.width	= static_cast<std::uint32_t>(clipRight - clipX),
					.height = static_cast<std::uint32_t>(clipBottom - clipY),
				};

				recorded =
					recorded && list.SetScissor(scissor, error) &&
					list.DrawIndexed(command.ElemCount, 1, indexBase + command.IdxOffset, vertexBase + static_cast<std::int32_t>(command.VtxOffset), 0, error);
			}

			vertexBase += cmdList->VtxBuffer.Size;
			indexBase += static_cast<std::uint32_t>(cmdList->IdxBuffer.Size);
		}

		return recorded;
	}
} // namespace azo::rhi::imgui

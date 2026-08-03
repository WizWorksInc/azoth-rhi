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

#include "ibl.hpp"

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/copy_types.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "FW/utility/AssetPath.hpp"
#include "FW/utility/Log.hpp"

// The implementation lives in scene.cpp, which is linked into this same sample.
#include <stb_image.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace rhi = azo::rhi;

namespace deccer
{
	namespace
	{

		constexpr std::uint32_t kCubeFaces = 6;
		constexpr std::uint32_t kGroupSize = 8;

		/*
		 * The cube face size, derived from the photograph, not fixed.
		 *
		 * A face covers a quarter of the way around, so a quarter of the source's width is the obvious answer and the wrong one. A face texel at u in [-1, 1]
		 * points along atan(u), so a face of N covers its middle at N/2 texels a radian where an equirectangular W carries W/2pi. Equal at the centre gives N =
		 * W/pi.
		 *
		 * Rounded to the threadgroup, capped at six faces.
		 */
		constexpr std::uint32_t kMaxEnvironmentSize = 2048;

		[[nodiscard]] constexpr std::uint32_t EnvironmentSizeFor(const std::uint32_t sourceWidth) noexcept
		{
			// 1/pi in the integer arithmetic a constant expression can do, which is within a texel of the real thing at every source size this will see.
			const std::uint32_t exact	= ((sourceWidth * 100) + 157) / 314;
			const std::uint32_t rounded = ((exact + kGroupSize - 1) / kGroupSize) * kGroupSize;
			return rounded < 256 ? 256 : (rounded > kMaxEnvironmentSize ? kMaxEnvironmentSize : rounded);
		}

		static_assert(EnvironmentSizeFor(4096) == 1304, "a 4k photograph wants 1304 per face for one texel per texel at the face centre");

		// Diffuse irradiance is low frequency by construction, the convolution having thrown away everything sharp, so a small cube holds all of it there is.
		constexpr std::uint32_t kIrradianceSize = 32;

		/*
		 * The specular chain starts at the environment's own size, because level zero of it is the environment and is what the sky is drawn from.
		 *
		 * Half of it was half the sky's resolution, which reads as a soft photograph behind sharp geometry, and the comment above the sky shader claiming level
		 * zero was the environment untouched had been wrong about that since it was written. The levels below zero are convolutions and lose nothing by
		 * halving, which they do anyway.
		 */
		constexpr std::uint32_t kSpecularMips = 5;

		// Has to match the [numthreads] on all three entry points below, since nothing checks that it does.
		constexpr Threadgroup kThreadgroup{ .x = kGroupSize, .y = kGroupSize, .z = 1 };

		constexpr rhi::Format kFormat = rhi::Format::eRGBA16Float;

		/*
		 * Slang numbers Metal's textures and samplers per kind in declaration order and the Metal backend binds a descriptor at its binding number within the
		 * matching kind. The output has to be the first texture declared and the sampler has to reach Metal at index zero. Vulkan and Direct3D 12 want the
		 * sampler as a descriptor of its own, which is binding 2.
		 */
		constexpr std::uint32_t kOutputBinding		= 0;
		constexpr std::uint32_t kEnvironmentBinding = 1;
		constexpr std::uint32_t kSamplerBinding		= 2;

		struct Constants final
		{
			std::uint32_t size = 0;
			float roughness	   = 0.0f;

			// The environment cube's face size, which the prefilter needs to know how much of it one texel covers.
			std::uint32_t sourceSize = 0;
		};

		// How many levels a cube of this size has, which is what the prefilter reads down when a sample stands for more of the sphere than one texel of the top
		// level covers.
		[[nodiscard]] constexpr std::uint32_t MipCountFor(std::uint32_t size) noexcept
		{
			std::uint32_t levels = 1;
			while (size > 1)
			{
				size /= 2;
				++levels;
			}
			return levels;
		}

		[[nodiscard]] std::uint32_t GroupCount(const std::uint32_t size)
		{
			return ((size + kGroupSize) - 1) / kGroupSize;
		}

		// A cube texture that compute writes and the shaders sample which requires one view of each shape such as an array of faces to write through and a cube
		// to read through.
		[[nodiscard]] rhi::TextureHandle CreateCube(rhi::Device dev, const std::uint32_t size, const std::uint32_t mips, const char * name, rhi::Error & error)
		{
			return dev.CreateTexture(
				rhi::TextureDesc{
					.type		 = rhi::TextureType::eTexCube,
					.format		 = kFormat,
					.width		 = size,
					.height		 = size,
					.mipLevels	 = mips,
					.arrayLayers = kCubeFaces,
					.usage		 = rhi::Flags(rhi::TextureUsage::eStorage) | rhi::TextureUsage::eSampled,
					.debugName	 = name,
				},
				error);
		}

		[[nodiscard]] rhi::TextureViewHandle CreateCubeView(
			rhi::Device dev, const rhi::TextureHandle texture, const std::uint32_t mips, const char * name, rhi::Error & error)
		{
			return dev.CreateTextureView(texture,
				rhi::TextureViewDesc{
					.type	   = rhi::TextureViewType::eTexCube,
					.range	   = { .mipCount = mips, .layerCount = kCubeFaces },
					.debugName = name,
				},
				error);
		}

		[[nodiscard]] rhi::TextureViewHandle CreateStorageView(
			rhi::Device dev, const rhi::TextureHandle texture, const std::uint32_t mip, const char * name, rhi::Error & error)
		{
			return dev.CreateTextureView(texture,
				rhi::TextureViewDesc{
					.type	   = rhi::TextureViewType::eTex2DArray,
					.range	   = { .baseMip = mip, .mipCount = 1, .layerCount = kCubeFaces },
					.debugName = name,
				},
				error);
		}

		[[nodiscard]] rhi::TextureBarrier WholeCube(
			const rhi::TextureHandle texture, const std::uint32_t mips, const rhi::ResourceState & before, const rhi::ResourceState & after)
		{
			return rhi::TextureBarrier{
				.texture = texture,
				.before	 = before,
				.after	 = after,
				.range	 = { .mipCount = mips, .layerCount = kCubeFaces },
			};
		}

		// The same, for a run of levels partway down a chain, which mip generation needs because it leaves the level it read and the levels it wrote in different states.
		[[nodiscard]] rhi::TextureBarrier CubeLevels(const rhi::TextureHandle texture,
			const std::uint32_t baseMip,
			const std::uint32_t mips,
			const rhi::ResourceState & before,
			const rhi::ResourceState & after)
		{
			return rhi::TextureBarrier{
				.texture = texture,
				.before	 = before,
				.after	 = after,
				.range	 = { .baseMip = baseMip, .mipCount = mips, .layerCount = kCubeFaces },
			};
		}

		constexpr rhi::ResourceState kUndefined{ .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined };
		constexpr rhi::ResourceState kWritten{
			.stages = rhi::PipelineStage::eComputeShader,
			.access = rhi::Access::eShaderWrite,
			.layout = rhi::TextureLayout::eGeneral,
		};

		constexpr rhi::ResourceState kSampled{
			.stages = rhi::PipelineStage::eComputeShader,
			.access = rhi::Access::eShaderRead,
			.layout = rhi::TextureLayout::eShaderReadOnly,
		};

		constexpr rhi::ResourceState kRead{
			.stages = rhi::PipelineStage::eFragmentShader,
			.access = rhi::Access::eShaderRead,
			.layout = rhi::TextureLayout::eShaderReadOnly,
		};

		constexpr rhi::ResourceState kCopyDst{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};

		constexpr rhi::ResourceState kCopySrc{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyRead,
			.layout = rhi::TextureLayout::eCopySrc,
		};

		// What an .hdr arrives as. stb decodes one to four floats a texel and offers nothing narrower.
		constexpr rhi::Format kSourceFormat = rhi::Format::eRGBA32Float;
		constexpr int kRgba					= 4;

		struct PixelDeleter final
		{
			void operator()(float * pixels) const noexcept
			{
				stbi_image_free(pixels);
			}
		};

		struct Equirect final
		{
			std::unique_ptr<float, PixelDeleter> pixels;
			std::uint32_t width	 = 0;
			std::uint32_t height = 0;

			[[nodiscard]] std::uint64_t Bytes() const
			{
				return std::uint64_t{ width } * height * kRgba * sizeof(float);
			}
		};

		// The photograph the environment cube is projected from. The build stages it beside the executable, so it is there or the sample has no environment.
		[[nodiscard]] Equirect LoadEquirect(rhi::Device dev, std::string & error)
		{
			// The projection samples between texels, and a nearest read of an equirectangular map bands where the sky should be a gradient.
			const rhi::FormatSupport support = dev.GetFormatSupport(kSourceFormat);
			if (!support.sampled || !support.copyDst || !support.linearFiltering)
			{
				error = "this device cannot sample a filtered RGBA32Float texture, which the environment is projected from";
				return {};
			}

			const std::filesystem::path path = fw::util::AssetPath("deccer_cubes/environment.hdr");
			if (path.empty())
			{
				error = "environment.hdr was not staged beside the executable";
				return {};
			}

			int width	 = 0;
			int height	 = 0;
			int channels = 0;
			std::unique_ptr<float, PixelDeleter> pixels(stbi_loadf(path.string().c_str(), &width, &height, &channels, kRgba));

			if (pixels == nullptr || width <= 0 || height <= 0)
			{
				error = std::format("environment.hdr did not decode: {}", stbi_failure_reason());
				return {};
			}

			return Equirect{ .pixels = std::move(pixels), .width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height) };
		}

		struct Source final
		{
			rhi::TextureHandle texture{};
			rhi::TextureViewHandle view{};
			rhi::BufferHandle staging{};
		};

		// Creates what the photograph lands in and fills the buffer a copy reads it from. The copy goes on the caller's list which allows us to do one submit.
		[[nodiscard]] bool StageEquirect(rhi::Device dev, const Equirect & photo, Source & out, rhi::Error & error)
		{
			out.texture = dev.CreateTexture(
				rhi::TextureDesc{
					.format	   = kSourceFormat,
					.width	   = photo.width,
					.height	   = photo.height,
					.usage	   = rhi::Flags(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst,
					.debugName = "deccer.environment.source",
				},
				error);

			out.view = dev.CreateTextureView(out.texture, rhi::TextureViewDesc{ .debugName = "deccer.environment.sourceView" }, error);

			out.staging = dev.CreateBuffer(
				rhi::BufferDesc{
					.size	   = photo.Bytes(),
					.usage	   = rhi::BufferUsage::eCopySrc,
					.memory	   = rhi::MemoryUsage::eCpuUpload,
					.debugName = "deccer.environment.sourceStaging",
				},
				error);

			const rhi::MappedMemory mapped =
				out.staging.IsValid() ? dev.Map(out.staging, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error) : rhi::MappedMemory{};

			if (!out.texture.IsValid() || !out.view.IsValid() || mapped.data == nullptr)
			{
				return false;
			}

			std::memcpy(mapped.data, photo.pixels.get(), photo.Bytes());

			return (mapped.coherent || dev.FlushMappedRange(out.staging, 0, photo.Bytes(), error)) && dev.Unmap(out.staging, error);
		}

	} // namespace

	bool BuildEnvironment(rhi::Device dev, rhi::Queue & queue, const rhi::TimelineHandle timeline, const std::uint64_t signalValue, ShaderCompiler & compiler,
		Environment & out, std::string & error)
	{
		rhi::Error rhiError{};
		const auto fail = [&](const char * what)
		{
			error = std::string(what) + ": " + (rhiError.message != nullptr ? rhiError.message : "no diagnostic");
			return false;
		};

		const Equirect photo = LoadEquirect(dev, error);
		if (photo.pixels == nullptr)
		{
			return false;
		}

		const std::uint32_t environmentSize = EnvironmentSizeFor(photo.width);
		const std::uint32_t specularSize	= environmentSize;

		const std::uint32_t environmentMips				= MipCountFor(environmentSize);
		const rhi::TextureHandle environment			= CreateCube(dev, environmentSize, environmentMips, "deccer.environment", rhiError);
		const rhi::TextureViewHandle environmentCube	= CreateCubeView(dev, environment, environmentMips, "deccer.environment.cube", rhiError);
		const rhi::TextureViewHandle environmentStorage = CreateStorageView(dev, environment, 0, "deccer.environment.storage", rhiError);

		out.irradiance								   = CreateCube(dev, kIrradianceSize, 1, "deccer.irradiance", rhiError);
		out.irradianceView							   = CreateCubeView(dev, out.irradiance, 1, "deccer.irradiance.cube", rhiError);
		const rhi::TextureViewHandle irradianceStorage = CreateStorageView(dev, out.irradiance, 0, "deccer.irradiance.storage", rhiError);

		out.specular	 = CreateCube(dev, specularSize, kSpecularMips, "deccer.specular", rhiError);
		out.specularView = CreateCubeView(dev, out.specular, kSpecularMips, "deccer.specular.cube", rhiError);
		out.specularMips = kSpecularMips;

		std::vector<rhi::TextureViewHandle> specularStorage(kSpecularMips);
		for (std::uint32_t mip = 0; mip < kSpecularMips; ++mip)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			specularStorage[mip] = CreateStorageView(dev, out.specular, mip, "deccer.specular.storage", rhiError);

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (!specularStorage[mip].IsValid())
			{
				return fail("failed to create an environment texture view");
			}
		}

		const rhi::SamplerHandle sampler = dev.CreateSampler(
			rhi::SamplerDesc{
				.addressU  = rhi::AddressMode::eClampToEdge,
				.addressV  = rhi::AddressMode::eClampToEdge,
				.addressW  = rhi::AddressMode::eClampToEdge,
				.debugName = "deccer.environment.sampler",
			},
			rhiError);

		// Longitude wraps and latitude does not, so the seam behind the camera blends without doubling the edge column back on itself.
		const rhi::SamplerHandle equirectSampler = dev.CreateSampler(
			rhi::SamplerDesc{
				.addressU  = rhi::AddressMode::eRepeat,
				.addressV  = rhi::AddressMode::eClampToEdge,
				.addressW  = rhi::AddressMode::eClampToEdge,
				.debugName = "deccer.environment.equirectSampler",
			},
			rhiError);

		if (!environment.IsValid() || !environmentCube.IsValid() || !environmentStorage.IsValid() || !out.irradiance.IsValid() ||
			!out.irradianceView.IsValid() || !irradianceStorage.IsValid() || !out.specular.IsValid() || !out.specularView.IsValid() || !sampler.IsValid() ||
			!equirectSampler.IsValid())
		{
			return fail("failed to create the environment textures");
		}

		LOG_INFO(fw::Log(), "environment from a {}x{} equirectangular photograph, cube faces of {}", photo.width, photo.height, environmentSize);

		Source source{};
		if (!StageEquirect(dev, photo, source, rhiError))
		{
			return fail("failed to stage the environment source");
		}

		/*
		 * One shape for all three passes: an output to write, a texture to read and a sampler.
		 *
		 * The projection reads the photograph where the convolutions read the cube it wrote, which is a Texture2D against a TextureCube in the shader and
		 * nothing a descriptor layout can tell apart, so they share this.
		 */
		constexpr std::array convolveBindings{
			rhi::DescriptorBinding{ .binding = kOutputBinding, .type = rhi::DescriptorType::eTextureUAV, .stages = rhi::ShaderStage::eCompute },
			rhi::DescriptorBinding{ .binding = kEnvironmentBinding, .type = rhi::DescriptorType::eTextureSRV, .stages = rhi::ShaderStage::eCompute },
			rhi::DescriptorBinding{ .binding = kSamplerBinding, .type = rhi::DescriptorType::eSampler, .stages = rhi::ShaderStage::eCompute },
		};

		const rhi::DescriptorSetLayoutHandle convolveSet =
			dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = convolveBindings, .debugName = "deccer.convolve.set" }, rhiError);

		constexpr std::array pushConstants{
			rhi::PushConstantRange{ .stages = rhi::ShaderStage::eCompute, .size = sizeof(Constants) },
		};

		const std::array convolveLayouts{ convolveSet };

		const rhi::PipelineLayoutHandle convolveLayout = dev.CreatePipelineLayout(
			rhi::PipelineLayoutDesc{ .sets = convolveLayouts, .pushConstants = pushConstants, .debugName = "deccer.convolve.layout" }, rhiError);

		if (!convolveLayout.IsValid())
		{
			return fail("failed to create the environment pipeline layout");
		}

		/*
		 * One block per pipeline, in a source of its own. A descriptor set is a Metal argument buffer and the ABI binds set 0 at buffer 1. Each pipeline's set
		 * has to be the first block its source declares, since slangc numbers blocks across the whole module and two in one file would put the second at
		 * buffer 2.
		 *
		 * They cannot share one block either. The sky pass writes the environment texture the two convolutions sample.
		 */
		const std::string prelude	   = LoadShaderSource("environment.prelude.slang", error);
		const std::string equirectBody = LoadShaderSource("environment.equirect.slang", error);
		const std::string convolveBody = LoadShaderSource("environment.convolve.slang", error);
		if (prelude.empty() || equirectBody.empty() || convolveBody.empty())
		{
			return false;
		}

		const std::string equirectSource = prelude + equirectBody;
		const std::string convolveSource = prelude + convolveBody;

		const rhi::ShaderBinary equirect =
			compiler.Compile("environment.equirect", equirectSource.c_str(), "equirectMain", rhi::ShaderStage::eCompute, error, kThreadgroup);
		const rhi::ShaderBinary irradiance =
			compiler.Compile("environment.convolve", convolveSource.c_str(), "irradianceMain", rhi::ShaderStage::eCompute, error, kThreadgroup);
		const rhi::ShaderBinary prefilter =
			compiler.Compile("environment.convolve", convolveSource.c_str(), "prefilterMain", rhi::ShaderStage::eCompute, error, kThreadgroup);

		if (equirect.data == nullptr || irradiance.data == nullptr || prefilter.data == nullptr)
		{
			return false;
		}

		const rhi::ComputePipelineHandle equirectPipeline = dev.CreateComputePipeline(
			rhi::ComputePipelineDesc{ .layout = convolveLayout, .shader = equirect, .debugName = "deccer.equirect.pipeline" }, rhiError);

		const rhi::ComputePipelineHandle irradiancePipeline = dev.CreateComputePipeline(
			rhi::ComputePipelineDesc{ .layout = convolveLayout, .shader = irradiance, .debugName = "deccer.irradiance.pipeline" }, rhiError);

		const rhi::ComputePipelineHandle prefilterPipeline = dev.CreateComputePipeline(
			rhi::ComputePipelineDesc{ .layout = convolveLayout, .shader = prefilter, .debugName = "deccer.prefilter.pipeline" }, rhiError);

		if (!equirectPipeline.IsValid() || !irradiancePipeline.IsValid() || !prefilterPipeline.IsValid())
		{
			return fail("failed to create the environment pipelines");
		}

		// One set for the projection, one for the irradiance and one per specular level, since each writes through a view of its own.
		constexpr std::uint32_t setCount = 2 + kSpecularMips;
		rhi::DescriptorArena arena		 = dev.CreateDescriptorArena(
			rhi::DescriptorArenaDesc{
				.type			= rhi::DescriptorArenaType::ePersistent,
				.maxSets		= setCount,
				.maxDescriptors = setCount * 3,
				.debugName		= "deccer.environment.arena",
			},
			rhiError);

		if (!arena.IsValid())
		{
			return fail("failed to create the environment descriptor arena");
		}

		// Writes the output view, and with it the sampler Metal reads at that same index. Vulkan and Direct3D 12 take their sampler from the standalone write
		// that follows.
		const auto writeSet = [&](const rhi::DescriptorSetHandle set,
								  const rhi::TextureViewHandle output,
								  const rhi::TextureViewHandle input,
								  const rhi::SamplerHandle setSampler)
		{
			const std::array outputs{
				rhi::DescriptorWriteTexture{
					.set			= set,
					.binding		= kOutputBinding,
					.type			= rhi::DescriptorType::eTextureUAV,
					.view			= output,
					.sampler		= setSampler,
					.expectedLayout = rhi::TextureLayout::eGeneral,
				},
			};

			const std::array sources{
				rhi::DescriptorWriteTexture{ .set = set, .binding = kEnvironmentBinding, .view = input, .sampler = setSampler },
			};

			const std::array samplers{ rhi::DescriptorWriteSampler{ .set = set, .binding = kSamplerBinding, .sampler = setSampler } };

			return dev.UpdateDescriptors(std::span(outputs), rhiError) && dev.UpdateDescriptors(std::span(sources), rhiError) &&
				   dev.UpdateDescriptors(std::span(samplers), rhiError);
		};

		const rhi::DescriptorSetHandle equirectDescriptors =
			arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = convolveSet, .debugName = "deccer.equirect" }, rhiError);

		const rhi::DescriptorSetHandle irradianceDescriptors =
			arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = convolveSet, .debugName = "deccer.irradiance" }, rhiError);

		if (!equirectDescriptors.IsValid() || !irradianceDescriptors.IsValid() ||
			!writeSet(equirectDescriptors, environmentStorage, source.view, equirectSampler) ||
			!writeSet(irradianceDescriptors, irradianceStorage, environmentCube, sampler))
		{
			return fail("failed to write the environment descriptors");
		}

		std::vector<rhi::DescriptorSetHandle> specularDescriptors(kSpecularMips);
		for (std::uint32_t mip = 0; mip < kSpecularMips; ++mip)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			specularDescriptors[mip] = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = convolveSet, .debugName = "deccer.specular" }, rhiError);

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (!specularDescriptors[mip].IsValid() || !writeSet(specularDescriptors[mip], specularStorage[mip], environmentCube, sampler))
			{
				return fail("failed to write the environment descriptors");
			}
		}

		rhi::CommandPool pool = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "deccer.environment.pool" }, rhiError);
		rhi::CommandList list = pool.Allocate("deccer.environment", rhiError);

		if (!pool.IsValid() || !list.IsValid() || !list.Begin(rhiError))
		{
			return fail("failed to start the environment build");
		}

		const std::array toWritten{
			WholeCube(environment, 1, kUndefined, kWritten),
			WholeCube(out.irradiance, 1, kUndefined, kWritten),
			WholeCube(out.specular, kSpecularMips, kUndefined, kWritten),
		};

		/*
		 * Mip generation reads level zero and writes every level under it, so the two halves of the chain arrive from different places and leave in different
		 * states. Naming only level zero, and the compute stage that wrote it and not the copy stage filling the rest, is a barrier that orders nothing.
		 *
		 * Metal 3 and Vulkan cover for that. Metal 4 does what it is told and reads the unwritten levels as black, which shows up as specular too dark.
		 */
		const std::array toMipSource{
			CubeLevels(environment, 0, 1, kWritten, kCopySrc),
			CubeLevels(environment, 1, environmentMips - 1, kUndefined, kCopyDst),
		};

		// The whole chain, now that every level of it exists, for the convolutions to read down.
		const std::array toSampled{
			CubeLevels(environment, 0, 1, kCopySrc, kSampled),
			CubeLevels(environment, 1, environmentMips - 1, kCopyDst, kSampled),
		};

		const std::array toRead{
			WholeCube(out.irradiance, 1, kWritten, kRead),
			WholeCube(out.specular, kSpecularMips, kWritten, kRead),
		};

		const Constants equirectConstants{ .size = environmentSize, .sourceSize = environmentSize };
		const Constants irradianceConstants{ .size = kIrradianceSize, .sourceSize = environmentSize };

		// The photograph goes up on the same list the passes run on, so the whole environment build is still one submit and one wait.
		const std::array sourceToCopyDst{
			rhi::TextureBarrier{ .texture = source.texture, .before = kUndefined, .after = kCopyDst },
		};

		const std::array sourceToSampled{
			rhi::TextureBarrier{ .texture = source.texture, .before = kCopyDst, .after = kSampled },
		};

		const std::array regions{
			rhi::BufferTextureCopy{ .textureExtent = { .width = photo.width, .height = photo.height } },
		};

		bool recorded = list.Barriers(rhi::BarrierBatch{ .textures = sourceToCopyDst }, rhiError) &&
						list.CopyBufferToTexture(source.texture, source.staging, regions, rhiError) &&
						list.Barriers(rhi::BarrierBatch{ .textures = sourceToSampled }, rhiError) &&
						list.Barriers(rhi::BarrierBatch{ .textures = toWritten }, rhiError) && list.SetComputePipeline(equirectPipeline, rhiError) &&
						list.BindDescriptorSet(convolveLayout, 0, equirectDescriptors, {}, rhiError) &&
						list.PushConstants(convolveLayout, rhi::ShaderStage::eCompute, 0, sizeof(equirectConstants), &equirectConstants, rhiError) &&
						list.Dispatch(GroupCount(environmentSize), GroupCount(environmentSize), kCubeFaces, rhiError) &&
						list.Barriers(rhi::BarrierBatch{ .textures = toMipSource }, rhiError) &&

						/*
						 * The chain the prefilter reads down. Level zero is what the projection above wrote and the rest are halvings of it, which is what lets
						 * 128 samples of a wide lobe come back smooth instead of hitting or missing the sun.
						 */
						list.GenerateMips(environment, rhiError) && list.Barriers(rhi::BarrierBatch{ .textures = toSampled }, rhiError) &&

						list.SetComputePipeline(irradiancePipeline, rhiError) &&
						list.BindDescriptorSet(convolveLayout, 0, irradianceDescriptors, {}, rhiError) &&
						list.PushConstants(convolveLayout, rhi::ShaderStage::eCompute, 0, sizeof(irradianceConstants), &irradianceConstants, rhiError) &&
						list.Dispatch(GroupCount(kIrradianceSize), GroupCount(kIrradianceSize), kCubeFaces, rhiError) &&
						list.SetComputePipeline(prefilterPipeline, rhiError);

		// One level per roughness, from a mirror at the top to fully rough at the bottom. The levels are separate subresources fed from a texture nothing here
		// writes, so they need nothing between them.
		for (std::uint32_t mip = 0; recorded && mip < kSpecularMips; ++mip)
		{
			const std::uint32_t size = specularSize >> mip;
			const Constants constants{
				.size		= size,
				.roughness	= static_cast<float>(mip) / static_cast<float>(kSpecularMips - 1),
				.sourceSize = environmentSize,
			};

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): the loop bound is the vector's size.
			recorded = list.BindDescriptorSet(convolveLayout, 0, specularDescriptors[mip], {}, rhiError) &&
					   list.PushConstants(convolveLayout, rhi::ShaderStage::eCompute, 0, sizeof(constants), &constants, rhiError) &&
					   list.Dispatch(GroupCount(size), GroupCount(size), kCubeFaces, rhiError);
		}

		recorded = recorded && list.Barriers(rhi::BarrierBatch{ .textures = toRead }, rhiError) && list.End(rhiError);
		if (!recorded)
		{
			return fail("failed to record the environment build");
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = signalValue } };
		const rhi::SubmitDesc submit{ .commandLists = lists, .signals = signals, .debugName = "deccer.environmentSubmit" };
		constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
		if (!queue.Submit(submit, rhiError) || !queue.Wait(timeline, signalValue, kNoTimeout, rhiError))
		{
			return fail("failed to submit the environment build");
		}

		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = signalValue },
		};
		dev.Destroy(environmentStorage, retired, rhiError);
		dev.Destroy(environmentCube, retired, rhiError);
		dev.Destroy(environment, retired, rhiError);

		dev.Destroy(source.staging, retired, rhiError);
		dev.Destroy(source.view, retired, rhiError);
		dev.Destroy(source.texture, retired, rhiError);

		return true;
	}

} // namespace deccer

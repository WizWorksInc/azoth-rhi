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

#include "azoth/rhi/builders/device_builder.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/frame_ring.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/host/presentation_backend.hpp"
#include "azoth/rhi/native/surface_payloads.hpp"
#include "azoth/rhi/present/swapchain.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"
#include "azoth/rhi/utils/resampler.hpp"

#include "FW/assets/ImageAsset.hpp"
#include "FW/platform/Sdl3Window.hpp"
#include "FW/scene/Camera.hpp"
#include "FW/utility/AssetPath.hpp"
#include "FW/utility/Log.hpp"
#include "FW/utility/MetalCapture.hpp"
#include "FW/utility/Sample.hpp"
#include "ibl.hpp"
#include "scene.hpp"
#include "shaders.hpp"

#include <SDL3/SDL.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace rhi = azo::rhi;

namespace
{

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	constexpr glm::vec3 kEyePosition{ 10.10f, -1.79f, 9.88f };
	constexpr glm::vec3 kLookAt{ -10.54f, 1.92f, -3.68f };
	constexpr float kFieldOfView = 28.79f;
	constexpr float kNearPlane	 = 0.05f;
	constexpr float kFarPlane	 = 200.0f;

	// What the build stages under assets/ beside the executable, taken as the scene when no other one is named on the command line.
	constexpr auto kSceneFile = "deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf";

	constexpr auto kDepthFormat = rhi::Format::eD32Float;

	// What a copy's buffer offset is aligned to when a device reports no requirement of its own. The real number comes from its caps.
	constexpr std::uint64_t kFallbackCopyAlignment = 4;

	[[nodiscard]] std::uint64_t CopyAlignmentOf(const rhi::Device & dev)
	{
		const std::uint64_t reported = dev.GetCaps().optimalBufferCopyOffsetAlignment;
		return reported != 0 ? reported : kFallbackCopyAlignment;
	}

	constexpr std::uint32_t kFrameSet	 = 0;
	constexpr std::uint32_t kMaterialSet = 1;

	// Zero because a ParameterBlock numbers its members from zero. It was 1 while the shader declared bindings by hand.
	constexpr std::uint32_t kFrameBinding	   = 0;
	constexpr std::uint32_t kBaseColorBinding  = 0;
	constexpr std::uint32_t kIrradianceBinding = 1;
	constexpr std::uint32_t kSpecularBinding   = 2;
	constexpr std::uint32_t kSamplerBinding	   = 3;

	[[nodiscard]] constexpr std::uint64_t AlignUp(const std::uint64_t value, const std::uint64_t alignment)
	{
		return ((value + alignment) - 1) & ~(alignment - 1);
	}

	// What the scene turned into once it was on the device.
	struct GpuScene final
	{
		rhi::BufferHandle vertices{};
		rhi::BufferHandle indices{};
		std::vector<rhi::TextureHandle> textures;
		std::vector<rhi::TextureViewHandle> views;
	};

	// Everything the file holds, in one staging buffer and one submission: the geometry into device memory and each image into mip zero of its texture, whose
	// remaining levels are then blitted out of it.
	[[nodiscard]] bool Upload(
		rhi::Device dev, rhi::Queue & queue, const rhi::TimelineHandle timeline, const std::uint64_t signalValue, const deccer::Scene & scene, GpuScene & gpu)
	{
		rhi::Error error{};

		const std::uint64_t copyAlignment = CopyAlignmentOf(dev);
		const std::uint64_t vertexBytes	  = scene.vertices.size() * sizeof(deccer::Vertex);
		const std::uint64_t indexBytes	  = scene.indices.size() * sizeof(std::uint32_t);

		std::vector<std::uint64_t> imageOffsets(scene.images.size());
		std::uint64_t stagingBytes = AlignUp(vertexBytes, copyAlignment) + AlignUp(indexBytes, copyAlignment);
		for (std::size_t i = 0; i < scene.images.size(); ++i)
		{
			imageOffsets[i] = stagingBytes;
			stagingBytes += AlignUp(scene.images[i].pixels.size(), copyAlignment);
		}

		const rhi::BufferHandle staging = dev.CreateBuffer(
			rhi::BufferDesc{
				.size	   = stagingBytes,
				.usage	   = rhi::BufferUsage::eCopySrc,
				.memory	   = rhi::MemoryUsage::eCpuUpload,
				.debugName = "deccer.staging",
			},
			error);

		gpu.vertices = dev.CreateBuffer(
			rhi::BufferDesc{
				.size	   = vertexBytes,
				.stride	   = sizeof(deccer::Vertex),
				.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eVertex) | rhi::BufferUsage::eCopyDst,
				.debugName = "deccer.vertices",
			},
			error);

		gpu.indices = dev.CreateBuffer(
			rhi::BufferDesc{
				.size	   = indexBytes,
				.stride	   = sizeof(std::uint32_t),
				.usage	   = rhi::Flags<rhi::BufferUsage>(rhi::BufferUsage::eIndex) | rhi::BufferUsage::eCopyDst,
				.debugName = "deccer.indices",
			},
			error);

		if (!staging.IsValid() || !gpu.vertices.IsValid() || !gpu.indices.IsValid())
		{
			fw::ReportError("failed to create the geometry buffers", error);
			return false;
		}

		const rhi::MappedMemory mapped = dev.Map(staging, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			fw::ReportError("failed to map the staging buffer", error);
			return false;
		}

		auto * bytes = static_cast<std::uint8_t *>(mapped.data);
		std::memcpy(bytes, scene.vertices.data(), vertexBytes);
		std::memcpy(bytes + AlignUp(vertexBytes, copyAlignment), scene.indices.data(), indexBytes);
		for (std::size_t i = 0; i < scene.images.size(); ++i)
		{
			std::memcpy(bytes + imageOffsets[i], scene.images[i].pixels.data(), scene.images[i].pixels.size());
		}

		if ((!mapped.coherent && !dev.FlushMappedRange(staging, 0, stagingBytes, error)) || !dev.Unmap(staging, error))
		{
			fw::ReportError("failed to flush the staging buffer", error);
			return false;
		}

		for (const deccer::Image & image : scene.images)
		{
			const std::uint32_t mips = fw::assets::MipCount(image.width, image.height);

			const rhi::TextureHandle texture = dev.CreateTexture(
				rhi::TextureDesc{
					// sRGB so a sample decodes on the way in, which is what leaves the shading arithmetic in linear light.
					.format	   = rhi::Format::eRGBA8Srgb,
					.width	   = image.width,
					.height	   = image.height,
					.mipLevels = mips,
					// eCopySrc as well as eCopyDst because the hardware path reads each level back to blit the next one out of it, and eStorage because the compute path the
					// Resampler falls back to writes each level through a storage view instead.
					.usage = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst | rhi::TextureUsage::eCopySrc |
							 rhi::TextureUsage::eStorage,
					// No API allows an sRGB storage image, so the compute path writes through a view in the linear twin and encodes itself. Declaring this is what makes that
					// view legal.
					.allowFormatViews = true,
					.debugName		  = "deccer.texture",
				},
				error);

			const rhi::TextureViewHandle view = dev.CreateTextureView(texture,
				rhi::TextureViewDesc{
					.range	   = { .mipCount = mips },
					.debugName = "deccer.textureView",
				},
				error);

			if (!texture.IsValid() || !view.IsValid())
			{
				fw::ReportError("failed to create a texture", error);
				return false;
			}

			gpu.textures.push_back(texture);
			gpu.views.push_back(view);
		}

		rhi::CommandPool pool = dev.CreateCommandPool(rhi::CommandPoolDesc{ .debugName = "deccer.uploadPool" }, error);
		rhi::CommandList list = pool.Allocate("deccer.upload", error);
		if (!pool.IsValid() || !list.IsValid() || !list.Begin(error))
		{
			fw::ReportError("failed to start the upload", error);
			return false;
		}

		/*
		 * One descriptor set per level the compute path writes, which is every level below the top of every texture.
		 *
		 * Sized even where the hardware path will run and use none: which path the Resampler takes is its decision and depends on the device, so a caller that sized
		 * this from the answer would be making the branch it just delegated.
		 */
		std::uint32_t resampleSets = 0;
		for (const deccer::Image & image : scene.images)
		{
			resampleSets += fw::assets::MipCount(image.width, image.height) - 1;
		}

		rhi::DescriptorArena resampleArena = dev.CreateDescriptorArena(
			rhi::DescriptorArenaDesc{
				.type			= rhi::DescriptorArenaType::ePersistent,
				.maxSets		= resampleSets,
				.maxDescriptors = resampleSets * 3,
				.debugName		= "deccer.resample.arena",
			},
			error);

		if (!resampleArena.IsValid())
		{
			fw::ReportError("failed to create the resampler descriptor arena", error);
			return false;
		}

		rhi::Result<rhi::utils::Resampler> resamplerResult =
			rhi::utils::Resampler::Create(dev, rhi::utils::ResamplerDesc{ .arena = &resampleArena, .debugName = "deccer.resampler" });
		if (!resamplerResult)
		{
			error = resamplerResult.GetError();
			fw::ReportError("failed to create the resampler", error);
			return false;
		}

		rhi::utils::Resampler & resampler = resamplerResult.Value();

		std::vector<rhi::BufferBarrier> toCopy;
		std::vector<rhi::TextureBarrier> toCopyDst;
		std::vector<rhi::BufferBarrier> toRead;
		std::vector<rhi::TextureBarrier> toSample;
		for (const rhi::BufferHandle buffer : { gpu.vertices, gpu.indices })
		{
			toCopy.push_back(rhi::BufferBarrier{
				.buffer = buffer,
				.before = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone },
				.after	= { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite },
			});
			toRead.push_back(rhi::BufferBarrier{
				.buffer = buffer,
				.before = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite },
				.after	= { .stages = rhi::PipelineStage::eVertexInput, .access = rhi::Flags<rhi::Access>(rhi::Access::eVertexRead) | rhi::Access::eIndexRead },
			});
		}

		for (std::size_t i = 0; i < gpu.textures.size(); ++i)
		{
			const rhi::TextureSubresourceRange whole{ .mipCount = fw::assets::MipCount(scene.images[i].width, scene.images[i].height) };
			toCopyDst.push_back(rhi::TextureBarrier{
				.texture = gpu.textures[i],
				.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
				.after	 = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eCopyDst },
				.range	 = whole,
			});
			// No transition for the textures here. The Resampler states where it leaves them, which is fragment readable, and it has to because the caller cannot see
			// whether it took the hardware path or the compute one.
		}

		bool recorded = list.Barriers(rhi::BarrierBatch{ .buffers = toCopy, .textures = toCopyDst }, error) &&
						list.CopyBuffer(gpu.vertices, 0, staging, 0, vertexBytes, error) &&
						list.CopyBuffer(gpu.indices, 0, staging, AlignUp(vertexBytes, copyAlignment), indexBytes, error);

		for (std::size_t i = 0; recorded && i < gpu.textures.size(); ++i)
		{
			const std::array regions{
				rhi::BufferTextureCopy{
					.bufferOffset  = imageOffsets[i],
					.textureExtent = { .width = scene.images[i].width, .height = scene.images[i].height },
				},
			};

			recorded = list.CopyBufferToTexture(gpu.textures[i], staging, regions, error) && resampler.GenerateMips(list, gpu.textures[i], error);
		}

		recorded = recorded && list.Barriers(rhi::BarrierBatch{ .buffers = toRead, .textures = toSample }, error) && list.End(error);
		if (!recorded)
		{
			fw::ReportError("failed to record the upload", error);
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = signalValue } };
		const rhi::SubmitDesc submit{ .commandLists = lists, .signals = signals, .debugName = "deccer.uploadSubmit" };
		if (!queue.Submit(submit, error) || !queue.Wait(timeline, signalValue, kNoTimeout, error))
		{
			fw::ReportError("failed to submit the upload", error);
			return false;
		}

		dev.Destroy(staging, {}, error);
		return true;
	}

	// The camera, which changes only when the window does.
	[[nodiscard]] bool WriteFrame(
		rhi::Device dev, const rhi::BufferHandle buffer, const fw::scene::PerspectiveCamera & camera, const std::uint32_t specularMips)
	{
		const float tangentY = std::tan(glm::radians(camera.GetFieldOfView()) * 0.5f);

		std::array<float, 36> block{};

		// The shader reads a row major matrix and glm stores columns, so it goes in transposed.
		const glm::mat4 viewProjection = glm::transpose(camera.GetViewProjection());
		std::memcpy(block.data(), glm::value_ptr(viewProjection), sizeof(viewProjection));

		// The camera's own basis, sent along so the sky can turn a screen corner into a direction without inverting anything.
		const auto place = [&block](const std::size_t at, const glm::vec3 & value)
		{
			std::memcpy(block.data() + at, glm::value_ptr(value), sizeof(value));
		};

		place(16, camera.GetPosition());
		place(20, camera.GetRight());
		block[23] = tangentY * camera.GetAspectRatio();
		place(24, camera.GetUp());
		block[27] = tangentY;
		place(28, camera.GetForward());
		block[32] = static_cast<float>(specularMips);

		rhi::Error error{};
		const rhi::MappedMemory mapped = dev.Map(buffer, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (mapped.data == nullptr)
		{
			fw::ReportError("failed to map the frame buffer", error);
			return false;
		}

		std::memcpy(mapped.data, block.data(), sizeof(block));
		if ((!mapped.coherent && !dev.FlushMappedRange(buffer, 0, sizeof(block), error)) || !dev.Unmap(buffer, error))
		{
			fw::ReportError("failed to flush the frame buffer", error);
			return false;
		}

		return true;
	}

} // namespace

int main(int argc, char ** argv)
{
	const std::span<char * const> args(argv, static_cast<std::size_t>(argc));
	// The build stages the scene under assets/ beside the executable, so the sample runs from any directory without a path compiled into it.
	const std::string bundled	   = fw::util::AssetPath(kSceneFile).string();
	const char * gltfPath		   = args.size() > 1 ? args[1] : bundled.c_str();
	const std::uint64_t frameLimit = args.size() > 2 ? std::strtoull(args[2], nullptr, 10) : 0;

	deccer::Scene scene;
	std::string sceneError;
	if (!deccer::Load(gltfPath, scene, sceneError))
	{
		LOG_ERROR(fw::Log(), "{}", sceneError);
		return 1;
	}

	LOG_INFO(fw::Log(), "{}: {} draws, {} vertices, {} textures", gltfPath, scene.draws.size(), scene.vertices.size(), scene.images.size());

	rhi::BackendSelection backends{ rhi::BackendPreference{ .includeNull = false } };

	rhi::GraphicsApiId api{};
	for (const rhi::BackendInfo & backend : backends.Preferred())
	{
		if (deccer::CanCompileFor(backend.id))
		{
			api = backend.id;
			break;
		}
	}

	if (!deccer::CanCompileFor(api))
	{
		LOG_INFO(fw::Log(), "this build has no backend that can draw");
		return 1;
	}

	fw::platform::Sdl3Window window;
	if (!window.Open(api, fw::platform::Sdl3WindowDesc{ .title = "AzothRHI deccer_cubes", .width = 1646, .height = 1049 }))
	{
		return 1;
	}

	const rhi::HostUniquePtr<rhi::PresentationBackend> presentation = rhi::MakePresentationBackend(api);
	if (presentation == nullptr || !presentation->InitInstanceLoader(window))
	{
		LOG_ERROR(fw::Log(), "this build cannot present through the backend it picked");
		return 1;
	}

	const rhi::Result<rhi::UniqueDevice> device =
		// The sky's vertex shader builds its corners from SV_VertexID, which Slang lowers against the base vertex and so emits the DrawParameters capability for. A
		// device that did not enable the matching feature would be running a shader declaring a capability it does not have.
		rhi::DeviceBuilder()
			.DebugName("deccer_cubes")
			.GraphicsQueue()
			.RequireFeature(rhi::DeviceFeature::eShaderDrawParameters)
			.Build(backends.Registry(), backends.PreferredApis().first(1));
	if (!device)
	{
		return fw::ReportNoDevice(device.GetError());
	}

	rhi::Device dev = device.Value().Get();
	rhi::Error error{};

	const rhi::SurfaceHandle surface = presentation->CreateSurface(window, dev);
	const rhi::Extent2D initial		 = window.GetDrawableSize();

	rhi::Swapchain swapchain = dev.CreateSwapchain(
		rhi::SwapchainDesc{
			.surface   = surface,
			.width	   = initial.width,
			.height	   = initial.height,
			.debugName = "deccer.swapchain",
		},
		error);
	rhi::Queue queue				   = dev.GetQueue(rhi::QueueType::eGraphics, 0, error);
	const rhi::TimelineHandle timeline = dev.CreateTimeline(rhi::TimelineDesc{ .debugName = "deccer.timeline" }, error);
	if (surface.value == 0 || !swapchain.IsValid() || !queue.IsValid() || !timeline.IsValid())
	{
		fw::ReportError("failed to set up presentation", error);
		return 1;
	}

	LOG_INFO(fw::Log(), "{} at {}x{}", dev.GetGraphicsApiName(), swapchain.GetWidth(), swapchain.GetHeight());

	deccer::ShaderCompiler compiler;
	std::string shaderError;
	if (!compiler.Open(api, shaderError))
	{
		LOG_ERROR(fw::Log(), "{}", shaderError);
		return 1;
	}

	// Two submissions before a frame is drawn, in order: the lighting, then the geometry and its textures.
	deccer::Environment environment;
	if (!deccer::BuildEnvironment(dev, queue, timeline, 1, compiler, environment, shaderError))
	{
		LOG_ERROR(fw::Log(), "{}", shaderError);
		return 1;
	}

	GpuScene gpu;
	if (!Upload(dev, queue, timeline, 2, scene, gpu))
	{
		return 1;
	}

	/*
	 * The frame loop's own pools and timeline, separate from the setup timeline above because the two order different things. That one sequenced the environment
	 * build against the upload and this one counts frames.
	 *
	 * Two frames in flight, so recording the next one overlaps executing this one. Everything the loop writes per frame is indexed by SlotIndex, which here is
	 * only the depth targets below.
	 */
	rhi::FrameRing ring = rhi::FrameRing::Create(dev, queue, rhi::FrameRingDesc{ .framesInFlight = 2, .debugName = "deccer.frame" }, error);
	if (!ring.IsValid())
	{
		fw::ReportError("failed to create the frame ring", error);
		return 1;
	}

	const rhi::BufferHandle frameBuffer = dev.CreateBuffer(
		rhi::BufferDesc{
			.size	   = sizeof(float) * 36,
			.usage	   = rhi::BufferUsage::eUniform,
			.memory	   = rhi::MemoryUsage::eCpuToGpu,
			.debugName = "deccer.frame",
		},
		error);

	// One sampler covers everything: the cube lookups do not care what happens outside a face and the base colours want repeating.
	const rhi::SamplerHandle sampler = dev.CreateSampler(rhi::SamplerDesc{ .debugName = "deccer.sampler" }, error);
	if (!frameBuffer.IsValid() || !sampler.IsValid())
	{
		fw::ReportError("failed to create the frame buffer or the sampler", error);
		return 1;
	}

	const std::array frameBindings{
		rhi::DescriptorBinding{ .binding = kFrameBinding,
			.type						 = rhi::DescriptorType::eUniformBuffer,
			.stages						 = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment },
	};
	const std::array materialBindings{
		rhi::DescriptorBinding{ .binding = kBaseColorBinding, .type = rhi::DescriptorType::eTextureSRV, .stages = rhi::ShaderStage::eFragment },
		rhi::DescriptorBinding{ .binding = kIrradianceBinding, .type = rhi::DescriptorType::eTextureSRV, .stages = rhi::ShaderStage::eFragment },
		rhi::DescriptorBinding{ .binding = kSpecularBinding, .type = rhi::DescriptorType::eTextureSRV, .stages = rhi::ShaderStage::eFragment },
		rhi::DescriptorBinding{ .binding = kSamplerBinding, .type = rhi::DescriptorType::eSampler, .stages = rhi::ShaderStage::eFragment },
	};

	const auto materialCount = static_cast<std::uint32_t>(gpu.views.size());
	const rhi::DescriptorSetLayoutHandle frameSetLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = frameBindings, .debugName = "deccer.frameSet" }, error);
	const rhi::DescriptorSetLayoutHandle materialSetLayout =
		dev.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = materialBindings, .debugName = "deccer.materialSet" }, error);
	rhi::DescriptorArena arena = dev.CreateDescriptorArena(
		rhi::DescriptorArenaDesc{
			.type			= rhi::DescriptorArenaType::ePersistent,
			.maxSets		= materialCount + 1,
			.maxDescriptors = (materialCount * static_cast<std::uint32_t>(materialBindings.size())) + 1,
			.debugName		= "deccer.arena",
		},
		error);
	if (!frameSetLayout.IsValid() || !materialSetLayout.IsValid() || !arena.IsValid())
	{
		fw::ReportError("failed to create the descriptor layouts", error);
		return 1;
	}

	const rhi::DescriptorSetHandle frameSet = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = frameSetLayout, .debugName = "deccer.frame" }, error);
	const std::array frameWrites{
		rhi::DescriptorWriteBuffer{ .set = frameSet, .binding = kFrameBinding, .type = rhi::DescriptorType::eUniformBuffer, .buffer = frameBuffer },
	};
	if (!frameSet.IsValid() || !dev.UpdateDescriptors(std::span(frameWrites), error))
	{
		fw::ReportError("failed to write the frame descriptor set", error);
		return 1;
	}

	// One set per texture in the file. The two environment cubes are the same in every one of them, since the lighting does not vary by material.
	std::vector<rhi::DescriptorSetHandle> materialSets(gpu.views.size());
	for (std::size_t i = 0; i < gpu.views.size(); ++i)
	{
		materialSets[i] = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = materialSetLayout, .debugName = "deccer.material" }, error);
		if (!materialSets[i].IsValid())
		{
			fw::ReportError("failed to allocate a descriptor set", error);
			return 1;
		}

		const std::array textures{
			rhi::DescriptorWriteTexture{ .set = materialSets[i], .binding = kBaseColorBinding, .view = gpu.views[i], .sampler = sampler },
			rhi::DescriptorWriteTexture{ .set = materialSets[i], .binding = kIrradianceBinding, .view = environment.irradianceView },
			rhi::DescriptorWriteTexture{ .set = materialSets[i], .binding = kSpecularBinding, .view = environment.specularView },
		};
		const std::array samplers{ rhi::DescriptorWriteSampler{ .set = materialSets[i], .binding = kSamplerBinding, .sampler = sampler } };

		if (!dev.UpdateDescriptors(std::span(textures), error) || !dev.UpdateDescriptors(std::span(samplers), error))
		{
			fw::ReportError("failed to write a descriptor set", error);
			return 1;
		}
	}

	const std::string sceneSource = deccer::LoadShaderSource("scene.slang", shaderError);
	if (sceneSource.empty())
	{
		LOG_ERROR(fw::Log(), "{}", shaderError);
		return 1;
	}

	const rhi::ShaderBinary vertex		= compiler.Compile("deccer", sceneSource.c_str(), "vertexMain", rhi::ShaderStage::eVertex, shaderError);
	const rhi::ShaderBinary fragment	= compiler.Compile("deccer", sceneSource.c_str(), "fragmentMain", rhi::ShaderStage::eFragment, shaderError);
	const rhi::ShaderBinary skyVertex	= compiler.Compile("deccer", sceneSource.c_str(), "skyVertexMain", rhi::ShaderStage::eVertex, shaderError);
	const rhi::ShaderBinary skyFragment = compiler.Compile("deccer", sceneSource.c_str(), "skyFragmentMain", rhi::ShaderStage::eFragment, shaderError);
	if (vertex.data == nullptr || fragment.data == nullptr || skyVertex.data == nullptr || skyFragment.data == nullptr)
	{
		LOG_ERROR(fw::Log(), "{}", shaderError);
		return 1;
	}

	const std::array shaders{ vertex, fragment };
	const std::array skyShaders{ skyVertex, skyFragment };

	const std::array vertexBindings{ rhi::VertexBindingDesc{ .binding = 0, .stride = sizeof(deccer::Vertex) } };
	const std::array vertexAttributes{
		rhi::VertexAttributeDesc{ .location = 0, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = offsetof(deccer::Vertex, position) },
		rhi::VertexAttributeDesc{ .location = 1, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = offsetof(deccer::Vertex, normal) },
		rhi::VertexAttributeDesc{ .location = 2, .binding = 0, .format = rhi::Format::eRG32Float, .offset = offsetof(deccer::Vertex, uv) },
	};

	// The model matrix, the three normal rows, the two material factors and the padding that carries the struct to its alignment, which is what changes from one
	// cube to the next. Matches DrawConstants in the shader exactly, since a shorter push is a block the shader reads past the end of.
	constexpr std::uint32_t kPushConstantBytes = sizeof(float) * 32;
	// Both stages, because the fragment shader reads metallic and roughness out of the same block the vertex shader takes its matrices from. Declaring only the
	// vertex stage leaves the fragment stage reading a range the layout never granted it, which is undefined, not empty.
	constexpr rhi::Flags<rhi::ShaderStage> kPushConstantStages = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment;

	const std::array pushConstants{ rhi::PushConstantRange{ .stages = kPushConstantStages, .size = kPushConstantBytes } };
	const std::array setLayouts{ frameSetLayout, materialSetLayout };

	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.layout =
		dev.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = setLayouts, .pushConstants = pushConstants, .debugName = "deccer.layout" }, error);
	pipelineDesc.shaders = shaders;

	rhi::VertexInputDesc vertexInput{};
	vertexInput.bindings	 = vertexBindings;
	vertexInput.attributes	 = vertexAttributes;
	pipelineDesc.vertexInput = &vertexInput;

	// Every material in the file is double sided and the bars are thin enough that it shows.
	pipelineDesc.raster.cullMode = rhi::CullMode::eNone;

	pipelineDesc.depthStencil.depthTestEnable  = true;
	pipelineDesc.depthStencil.depthWriteEnable = true;
	pipelineDesc.depthStencil.depthCompareOp   = rhi::CompareOp::eLess;

	pipelineDesc.renderTarget.colorFormats.at(0) = swapchain.GetFormat();
	pipelineDesc.renderTarget.colorFormatCount	 = 1;
	pipelineDesc.renderTarget.depthStencilFormat = kDepthFormat;
	pipelineDesc.blend.attachmentCount			 = 1;
	pipelineDesc.dynamicStates					 = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
	pipelineDesc.debugName						 = "deccer.pipeline";

	/*
	 * The sky shares the layout, so it needs no descriptors of its own, and differs only in what it does not do: no vertex buffer, since the three vertices come
	 * from the vertex id, and no depth state at all, since it is drawn first and everything else is meant to land on top of it.
	 */
	rhi::GraphicsPipelineDesc skyDesc = pipelineDesc;
	skyDesc.shaders					  = skyShaders;

	// Still a vertex pipeline, just one with nothing bound: the vertex shader builds its three positions from the vertex id. A null vertexInput would mean no
	// vertex stage at all, which is a different thing and one no backend here builds.
	rhi::VertexInputDesc skyInput{};
	skyDesc.vertexInput					  = &skyInput;
	skyDesc.depthStencil.depthTestEnable  = false;
	skyDesc.depthStencil.depthWriteEnable = false;
	skyDesc.debugName					  = "deccer.sky.pipeline";

	const rhi::GraphicsPipelineHandle pipeline	  = dev.CreateGraphicsPipeline(pipelineDesc, error);
	const rhi::GraphicsPipelineHandle skyPipeline = dev.CreateGraphicsPipeline(skyDesc, error);
	if (!pipelineDesc.layout.IsValid() || !pipeline.IsValid() || !skyPipeline.IsValid())
	{
		fw::ReportError("failed to create the pipelines", error);
		return 1;
	}

	/*
	 * One depth target per frame in flight, sized with the swapchain and rebuilt with it when the window changes.
	 *
	 * Per slot and not one shared: the loop clears depth at the top of every frame, so a single target would be cleared by the next frame while the previous one
	 * was still reading it. That reads as flicker or as geometry punched through, and no validation layer calls it because both frames declare the barriers they
	 * actually perform.
	 */
	struct DepthTarget final
	{
		rhi::TextureHandle texture{};
		rhi::TextureViewHandle view{};
	};

	std::array<DepthTarget, rhi::kMaxFramesInFlight> depths{};
	std::uint32_t depthWidth  = 0;
	std::uint32_t depthHeight = 0;

	// The camera the frame block is built from. Fixed where it stands, so only the aspect ratio moves, and only when the window does.
	fw::scene::PerspectiveCamera camera;
	camera.SetFieldOfView(kFieldOfView);
	camera.SetNearPlane(kNearPlane);
	camera.SetFarPlane(kFarPlane);
	camera.LookAt(kEyePosition, kLookAt);

	const auto resize = [&]()
	{
		depthWidth	= swapchain.GetWidth();
		depthHeight = swapchain.GetHeight();

		for (std::uint32_t slot = 0; slot < ring.FramesInFlight(); ++slot)
		{
			DepthTarget & target = depths.at(slot);
			if (target.texture.IsValid())
			{
				const rhi::DestroyDesc retired{
					.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
					.safeAfter = ring.Retire(),
				};
				dev.Destroy(target.view, retired, error);
				dev.Destroy(target.texture, retired, error);
			}

			target.texture = dev.CreateTexture(
				rhi::TextureDesc{
					.format	   = kDepthFormat,
					.width	   = depthWidth,
					.height	   = depthHeight,
					.usage	   = rhi::TextureUsage::eDepthStencilAttachment,
					.debugName = "deccer.depth",
				},
				error);
			target.view = dev.CreateTextureView(target.texture,
				rhi::TextureViewDesc{
					.format	   = kDepthFormat,
					.range	   = { .aspects = rhi::TextureAspect::eDepth },
					.debugName = "deccer.depthView",
				},
				error);

			if (!target.texture.IsValid() || !target.view.IsValid())
			{
				return false;
			}
		}

		camera.SetAspectRatio(static_cast<float>(depthWidth) / static_cast<float>(depthHeight));

		return WriteFrame(dev, frameBuffer, camera, environment.specularMips);
	};

	if (!resize())
	{
		fw::ReportError("failed to create the depth buffer", error);
		return 1;
	}

	// Writes a .gputrace when AZOTH_METAL_CAPTURE names one, and does nothing otherwise. Here and not inside the loop, so the capture covers the first
	// frames, which is where a bad one usually is.
	fw::MetalCapture capture(dev);

	while (window.PumpEvents())
	{
		if (frameLimit != 0 && ring.FrameIndex() >= frameLimit)
		{
			break;
		}

		if (window.TakeResized())
		{
			const rhi::Extent2D size = window.GetDrawableSize();
			static_cast<void>(queue.WaitIdle(error));
			static_cast<void>(swapchain.Resize(size.width, size.height, error));
			if (!resize())
			{
				fw::ReportError("failed to resize the depth buffer", error);
				return 1;
			}
		}

		const rhi::AcquireResult acquired = swapchain.AcquireNextImage(kNoTimeout, error);
		if (acquired.status == rhi::SwapchainStatus::eOutOfDate)
		{
			const rhi::Extent2D size = window.GetDrawableSize();
			static_cast<void>(queue.WaitIdle(error));
			static_cast<void>(swapchain.Resize(size.width, size.height, error));
			if (!resize())
			{
				fw::ReportError("failed to resize the depth buffer", error);
				return 1;
			}
			continue;
		}
		if (acquired.status != rhi::SwapchainStatus::eOk && acquired.status != rhi::SwapchainStatus::eSuboptimal)
		{
			LOG_ERROR(fw::Log(), "failed to acquire a back buffer");
			return 1;
		}

		// Waits for the frame this slot last carried, recycles its pool and hands back the list to record into.
		rhi::CommandList list = ring.Begin(error);
		if (!list.IsValid() || !list.Begin(error))
		{
			fw::ReportError("failed to start recording", error);
			return 1;
		}

		// This frame's own depth target, which is what lets the next one start recording before this one has finished with it.
		const DepthTarget & depth = depths.at(ring.SlotIndex());

		const rhi::TextureHandle backBuffer = acquired.texture;
		const std::array toAttachment{
			rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
				.after	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
			},
			// Cleared every frame, so what it held before is of no interest and it starts undefined each time.
			rhi::TextureBarrier{
				.texture = depth.texture,
				.before	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined },
				.after	 = { .stages = rhi::PipelineStage::eEarlyFragmentTests,
					.access		   = rhi::Access::eDepthStencilWrite,
					.layout		   = rhi::TextureLayout::eDepthStencilAttachment },
				.range	 = { .aspects = rhi::TextureAspect::eDepth },
			},
		};

		const std::array toPresent{
			rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
				.after	 = { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::ePresent },
			},
		};

		const std::array colors{
			rhi::RenderingAttachment{
				.view  = acquired.view,
				.state = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment },
				// The sky covers every pixel before anything else draws, so clearing first would be work thrown away.
				.load  = rhi::LoadOp::eDontCare,
				.store = rhi::StoreOp::eStore,
			},
		};

		const rhi::RenderingAttachment depthAttachment{
			.view			   = depth.view,
			.state			   = { .stages = rhi::PipelineStage::eEarlyFragmentTests,
				.access		   = rhi::Access::eDepthStencilWrite,
				.layout		   = rhi::TextureLayout::eDepthStencilAttachment },
			.load			   = rhi::LoadOp::eClear,
			.store			   = rhi::StoreOp::eDontCare,
			.clearDepthStencil = { .depth = 1.0f },
		};

		const rhi::Viewport viewport{ .width = static_cast<float>(swapchain.GetWidth()), .height = static_cast<float>(swapchain.GetHeight()) };
		const rhi::Rect2D scissor{ .width = swapchain.GetWidth(), .height = swapchain.GetHeight() };

		// The frame set is the same all the way through, so it is bound once and only the material set moves between draws.
		bool recorded = list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error) &&
						list.BeginRendering(
							rhi::BeginRenderingDesc{
								.colors		  = colors,
								.depthStencil = &depthAttachment,
								.width		  = swapchain.GetWidth(),
								.height		  = swapchain.GetHeight(),
							},
							error) &&
						list.SetViewport(viewport, error) && list.SetScissor(scissor, error) &&
						list.BindDescriptorSet(pipelineDesc.layout, kFrameSet, frameSet, {}, error) &&
						list.BindDescriptorSet(pipelineDesc.layout, kMaterialSet, materialSets[0], {}, error) && list.SetGraphicsPipeline(skyPipeline, error) &&
						list.Draw(3, 1, 0, 0, error) && list.SetGraphicsPipeline(pipeline, error) && list.SetVertexBuffer(0, gpu.vertices, 0, error) &&
						list.SetIndexBuffer(gpu.indices, 0, true, error);

		for (const deccer::Draw & draw : scene.draws)
		{
			if (!recorded)
			{
				break;
			}

			std::array<float, 32> constants{};
			// The loader hands the model back column major, and the shader reads rows.
			const glm::mat4 model = glm::transpose(glm::make_mat4(draw.model.data()));
			std::memcpy(constants.data(), glm::value_ptr(model), sizeof(model));
			std::ranges::copy(draw.normalRows, constants.begin() + 16);
			constants[28] = draw.metallic;
			constants[29] = draw.roughness;

			recorded = list.PushConstants(pipelineDesc.layout, kPushConstantStages, 0, kPushConstantBytes, constants.data(), error) &&
					   list.BindDescriptorSet(pipelineDesc.layout, kMaterialSet, materialSets[draw.textureIndex], {}, error) &&
					   list.DrawIndexed(draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0, error);
		}

		recorded = recorded && list.EndRendering(error) && list.Barriers(rhi::BarrierBatch{ .textures = toPresent }, error) && list.End(error);
		if (!recorded)
		{
			fw::ReportError("failed to record the frame", error);
			return 1;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array retire{ ring.Signal() };

		// Both ends of the presentation edge came back from the acquire, so nothing here looks either of them up by index.
		const std::array present{ rhi::SwapchainSync{ .acquired = acquired.imageAvailable, .renderFinished = acquired.renderFinished } };

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = retire,
			.swapchains	  = present,
			.debugName	  = "deccer.submit",
		};

		if (!queue.Submit(submit, error))
		{
			LOG_ERROR(fw::Log(), "failed to submit the frame");
			return 1;
		}

		static_cast<void>(swapchain.Present(queue, acquired.imageIndex, acquired.renderFinished, error));
		capture.FramePresented();

		// The wait that used to sit here belongs to the ring now, which takes it at the top of the next frame instead.
	}

	static_cast<void>(queue.WaitIdle(error));
	dev.CollectGarbage(ring.Timeline(), ring.FrameIndex(), error);

	LOG_INFO(fw::Log(), "{} frames presented", ring.FrameIndex());
	return 0;
}

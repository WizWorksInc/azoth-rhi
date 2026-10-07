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

#ifndef VULKAN_HPP_NO_EXCEPTIONS
	#define VULKAN_HPP_NO_EXCEPTIONS
#endif
#undef VULKAN_HPP_ASSERT_ON_RESULT
#define VULKAN_HPP_ASSERT_ON_RESULT(expression)

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/core/version.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/host/allocator.hpp"
#include "azoth/rhi/native/vulkan_config.hpp"
#include "azoth/rhi/native/vulkan_native.hpp"
#include "azoth/rhi/resources/descriptors.hpp"
#include "azoth/rhi/resources/pipeline.hpp"
#include "azoth/rhi/resources/query.hpp"
#include "azoth/rhi/resources/resources.hpp"
#include "azoth/rhi/resources/texture_view.hpp"

#include "harness.hpp"
#include "raw_vulkan.hpp"
#include "scene.hpp"

#ifdef TRACY_ENABLE
	#include "azoth/rhi/host/profiler.hpp"
	#include "azoth/rhi/host/tracy_profiler.hpp"
#endif

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <print>
#include <span>
#include <utility>
#include <vector>

namespace rhi = azo::rhi;

namespace vsnri
{

	namespace
	{

		constexpr std::uint32_t kMaxThreads = kThreadCounts.back();

		constexpr rhi::Format kColorFormat = rhi::Format::eRGBA8UNorm;
		constexpr rhi::Format kDepthFormat = rhi::Format::eD32Float;

		constexpr rhi::Flags<rhi::ShaderStage> kGraphicsStages = rhi::Flags<rhi::ShaderStage>(rhi::ShaderStage::eVertex) | rhi::ShaderStage::eFragment;

		constexpr rhi::ResourceState kNothing{ .use = rhi::ResourceUse::eDiscard, .stages = rhi::Stage::eNone };
		constexpr rhi::ResourceState kSampled{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };
		constexpr rhi::ResourceState kColorTarget{ .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput };
		constexpr rhi::ResourceState kDepthTarget{ .use = rhi::ResourceUse::eDepthStencilTarget, .stages = rhi::Stage::eDepthStencil };

		// Every attachment is cleared before it is drawn to, so its old contents are discarded after whatever last read or wrote it.
		constexpr rhi::ResourceState kDiscardColor{
			.use	= rhi::ResourceUse::eDiscard,
			.stages = rhi::Flags<rhi::Stage>(rhi::Stage::eFragmentShading) | rhi::Stage::eColorOutput,
		};
		constexpr rhi::ResourceState kDiscardDepth{
			.use	= rhi::ResourceUse::eDiscard,
			.stages = rhi::Flags<rhi::Stage>(rhi::Stage::eFragmentShading) | rhi::Stage::eDepthStencil,
		};

		constexpr rhi::Flags<rhi::TextureAspect> kColorAspect = rhi::TextureAspect::eColor;
		constexpr rhi::Flags<rhi::TextureAspect> kDepthAspect = rhi::TextureAspect::eDepth;

		constexpr rhi::ResourceState kCopyTarget{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState kVertexInput{ .use = rhi::ResourceUse::eVertexBuffer, .stages = rhi::Stage::eVertexWork };
		constexpr rhi::ResourceState kIndexInput{ .use = rhi::ResourceUse::eIndexBuffer, .stages = rhi::Stage::eVertexWork };

		[[nodiscard]] rhi::Format SceneFormat(const SceneTextureFormat format)
		{
			switch (format)
			{
			case SceneTextureFormat::eRGBA8: return rhi::Format::eRGBA8UNorm;
			case SceneTextureFormat::eBC1:	 return rhi::Format::eBC1RGBAUNorm;
			case SceneTextureFormat::eBC7:	 return rhi::Format::eBC7UNorm;
			}

			return rhi::Format::eUndefined;
		}

		// The same counting allocator the NRI arm hands its device, behind the seam this library routes its host memory through.
		class CountingAllocator final : public rhi::HostAllocator
		{
		public:
			[[nodiscard]] void * Allocate(const std::size_t size, const std::size_t alignment) override
			{
				return CountedAllocate(size, alignment);
			}

			void Free(void * memory, std::size_t, std::size_t) override
			{
				CountedFree(memory);
			}
		};

		void Report(const char * what, const rhi::Error & error)
		{
			std::println(
				stderr, "azoth arm: {} failed: {} (code {})", what, error.message != nullptr ? error.message : "no message", static_cast<int>(error.code));
		}

		void ReportValidation(const rhi::ValidationMessageSeverity severity, const char * message, void *) noexcept
		{
			std::println(stderr, "azoth validation {}: {}", severity == rhi::ValidationMessageSeverity::eError ? "error" : "warning", message);
		}

		[[nodiscard]] rhi::TextureBarrier Transition(
			const rhi::TextureHandle texture, const rhi::ResourceState & before, const rhi::ResourceState & after, const rhi::Flags<rhi::TextureAspect> aspects)
		{
			return rhi::TextureBarrier{ .texture = texture, .before = before, .after = after, .range = { .aspects = aspects } };
		}

	}

	class AzothArm final : public Arm
	{
	public:
		AzothArm()							   = default;
		AzothArm(const AzothArm &)			   = delete;
		AzothArm & operator=(const AzothArm &) = delete;
		AzothArm(AzothArm &&)				   = delete;
		AzothArm & operator=(AzothArm &&)	   = delete;

		~AzothArm() override
		{
			if (!m_device.IsValid())
			{
				return;
			}

			// MoltenVK finishes a submission's queries in a completion block that can run after the timeline signals.
			static_cast<void>(Drain());
			static_cast<void>(m_queue.WaitIdle());
			raw::Release();

			for (const rhi::GraphicsPipelineHandle pipeline : m_assetPipelines)
			{
				m_device.Destroy(pipeline);
			}
			m_device.Destroy(m_assetLayout);
			for (const rhi::DescriptorSetLayoutHandle layout : m_assetSetLayouts)
			{
				m_device.Destroy(layout);
			}
			m_device.Destroy(m_assetSampler);
			for (const rhi::TextureViewHandle view : m_sceneTextureViews)
			{
				m_device.Destroy(view);
			}
			for (const rhi::TextureHandle texture : m_sceneTextures)
			{
				m_device.Destroy(texture);
			}
			m_device.Destroy(m_globals);
			m_device.Destroy(m_sceneIndices);
			m_device.Destroy(m_sceneVertices);
			m_device.Destroy(m_timestampReadback);
			m_device.Destroy(m_timestamps);

			m_device.Destroy(m_postPipeline);
			m_device.Destroy(m_shadowPipeline);
			for (const rhi::GraphicsPipelineHandle pipeline : m_scenePipelines)
			{
				m_device.Destroy(pipeline);
			}

			m_device.Destroy(m_postLayout);
			m_device.Destroy(m_sceneLayout);
			for (const rhi::DescriptorSetLayoutHandle layout : m_setLayouts)
			{
				m_device.Destroy(layout);
			}

			m_device.Destroy(m_sampler);
			m_device.Destroy(m_postView);
			m_device.Destroy(m_sceneDepthView);
			m_device.Destroy(m_sceneColorTexture);
			m_device.Destroy(m_sceneColorView);
			m_device.Destroy(m_shadowTexture);
			m_device.Destroy(m_shadowView);

			m_device.Destroy(m_post);
			m_device.Destroy(m_sceneDepth);
			m_device.Destroy(m_sceneColor);
			m_device.Destroy(m_shadow);

			m_device.Destroy(m_materials);
			m_device.Destroy(m_objects);
			m_device.Destroy(m_indices);
			m_device.Destroy(m_vertices);

			m_device.Destroy(m_timeline);
		}

		[[nodiscard]] bool Initialize(const HarnessOptions & options)
		{
			VSNRI_ZONE("azoth/Initialize");

			// NRI takes the newest version the driver offers, so both libraries run against the same MoltenVK feature set.
			constexpr rhi::ApiVersion kVulkan14{ .major = 1, .minor = 4 };

			rhi::native::VulkanInstanceConfig instance{};
			instance.minimumInstanceVersion = kVulkan14;
			const std::array instanceConfigs{ rhi::InstanceConfigEntry{ .api = rhi::VulkanApi::id, .config = &instance } };

			rhi::native::VulkanDeviceConfig vulkan{};
			vulkan.deviceVersion	 = kVulkan14;
			vulkan.renderingLowering = rhi::native::VulkanRenderingLowering::eDynamicRendering;
			const std::array configs{ rhi::DeviceConfigEntry{ .api = rhi::VulkanApi::id, .config = &vulkan } };

			rhi::DeviceDesc desc{};
			desc.validation		   = rhi::ValidationMode::eOff;
			desc.enableDebugNames  = false;
			desc.enableDebugLabels = false;
			desc.requireSwapchain  = false;
			desc.backendConfigs	   = configs;
			desc.instanceConfigs   = instanceConfigs;
			if (options.enableGraphicsApiValidation)
			{
				desc.nativeValidation.apiValidation = rhi::NativeValidationPolicy::eEnabled;
				desc.nativeValidation.onMessage		= ReportValidation;
			}

			rhi::Result<rhi::UniqueDevice> created = rhi::CreateDevice<rhi::VulkanApi>(desc);
			if (!created)
			{
				Report("CreateDevice<VulkanApi>", created.GetError());
				return false;
			}

			m_owner	 = std::move(created).Value();
			m_device = m_owner.Get();

			rhi::Error error{};
			m_queue	   = m_device.GetQueue(rhi::QueueType::eGraphics, 0, error);
			m_timeline = m_device.CreateTimeline(rhi::TimelineDesc{}, error);
			if (!m_queue.IsValid() || !m_timeline.IsValid())
			{
				Report("GetQueue or CreateTimeline", error);
				return false;
			}

			return CreatePools() && CreateBuffers() && CreateTextures() && CreateLayouts() && CreateDescriptors() && CreatePipelines() && CreateTimestamps() &&
				   PrepareLayouts() && PrepareRaw();
		}

		[[nodiscard]] const Identity & Describe() const override
		{
			return m_identity;
		}

		[[nodiscard]] bool RecordShape(const Shape shape, const std::uint32_t commands, const bool rawFirst, ShapeTiming & timing) override
		{
			VSNRI_ZONE("azoth/RecordShape");

			rhi::Error error{};
			if (!m_shapePool.Reset(rhi::RetirePoint{}, error))
			{
				Report("CommandPool::Reset", error);
				return false;
			}

			rhi::CommandList list = m_shapePool.Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error))
			{
				Report("CommandList::Begin", error);
				return false;
			}

			const bool inScope = ShapeRendersInScope(shape);
			if (inScope)
			{
				Recorder recorder{ *this, list };
				recorder.ok = BeginScene(list, rhi::LoadOp::eClear, rhi::StoreOp::eDontCare, true);
				recorder.BindPipeline(0);
				recorder.BindMaterial(0);
				recorder.BindMesh(0);
				recorder.Push(PushBlock{});
				if (!recorder.ok)
				{
					std::println(stderr, "azoth arm: the shape pass could not bind what its commands need");
					return false;
				}
			}
			else
			{
				const std::array barriers{ Transition(m_sceneColor, kDiscardColor, kColorTarget, kColorAspect) };
				if (!list.Barriers(rhi::BarrierBatch{ .textures = barriers }, error))
				{
					Report("CommandList::Barriers", error);
					return false;
				}
			}

			const std::array touched{ rhi::NativeTouchedTexture{
				.texture	= m_sceneColor,
				.access		= rhi::NativeMutationAccess::eReadWrite,
				.range		= { .aspects = kColorAspect },
				.finalState = kColorTarget,
			} };
			const rhi::NativeMutationDesc mutation{ .textures = touched };
			const auto recordRaw = [&](const rhi::native::VulkanCommandListView & view)
			{
				timing.rawNs = raw::RecordShape(shape, static_cast<VkCommandBuffer>(view.commandBuffer), commands);
			};

			if (rawFirst && !list.ModifyNative<rhi::VulkanApi>(mutation, recordRaw, error))
			{
				Report("CommandList::ModifyNative", error);
				return false;
			}

			std::uint64_t accepted = 0;
			timing.libraryNs	   = RecordLibraryShape(list, shape, commands, accepted);

			if (!rawFirst && !list.ModifyNative<rhi::VulkanApi>(mutation, recordRaw, error))
			{
				Report("CommandList::ModifyNative", error);
				return false;
			}

			if (accepted != commands)
			{
				std::println(stderr, "azoth arm: {} of {} recorded commands were accepted", accepted, commands);
				return false;
			}

			if (inScope && !list.EndRendering(error))
			{
				Report("CommandList::EndRendering", error);
				return false;
			}

			if (!list.End(error))
			{
				Report("CommandList::End", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool Frame(const std::uint32_t draws, FrameTiming & timing) override
		{
			VSNRI_ZONE("azoth/Frame");

			const std::uint32_t index = m_frame % kFramesInFlight;
			Slot & slot				  = WaitForSlot(timing);

			const std::uint64_t recordStarted = Now();
			rhi::Error error{};
			if (!slot.pools.front().Reset(rhi::RetirePoint{ .timeline = m_timeline, .value = slot.fenceValue }, error))
			{
				Report("CommandPool::Reset", error);
				return false;
			}

			rhi::CommandList & list = slot.lists.front();
			list					= slot.pools.front().Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error))
			{
				Report("CommandList::Begin", error);
				return false;
			}

			bool recorded = OpenTimestamps(list, index);
			recorded	  = RecordFrame(list, draws) && recorded;
			recorded	  = CloseTimestamps(list, index) && recorded;
			if (!recorded)
			{
				std::println(stderr, "azoth arm: a frame command was refused");
				return false;
			}

			if (!list.End(error))
			{
				Report("CommandList::End", error);
				return false;
			}
			timing.recordNs = Now() - recordStarted;

			const std::uint64_t submitStarted = Now();
			if (!Submit(slot, 1, slot.fenceValue))
			{
				return false;
			}
			timing.submitNs = Now() - submitStarted;

			slot.timed = true;
			++m_frame;
			VSNRI_FRAME_MARK;
			return true;
		}

		[[nodiscard]] bool PrepareScene(const SceneAsset & scene) override
		{
			VSNRI_ZONE("azoth/PrepareScene");

			m_sceneMeshes = scene.meshes;
			return CreateSceneResources(scene) && UploadScene(scene) && CreateAssetLayout() && CreateAssetDescriptors(scene) && CreateAssetPipelines();
		}

		[[nodiscard]] bool SceneFrame(const SceneFrameDesc & frame, FrameTiming & timing) override
		{
			VSNRI_ZONE("azoth/SceneFrame");

			const std::uint32_t index = m_frame % kFramesInFlight;
			Slot & slot				  = WaitForSlot(timing);

			rhi::Error error{};
			const rhi::MappedMemory globals = m_device.Map(m_globals,
				rhi::MapDesc{ .mode = rhi::MapMode::eWrite, .offset = std::uint64_t{ index } * kGlobalsStride, .size = sizeof(SceneGlobals) },
				error);
			if (globals.data == nullptr)
			{
				Report("Map(globals)", error);
				return false;
			}
			std::memcpy(globals.data, &frame.globals, sizeof(SceneGlobals));
			if (!m_device.Unmap(m_globals, error))
			{
				Report("Unmap(globals)", error);
				return false;
			}

			const std::uint64_t recordStarted = Now();
			if (!slot.pools.front().Reset(rhi::RetirePoint{ .timeline = m_timeline, .value = slot.fenceValue }, error))
			{
				Report("CommandPool::Reset", error);
				return false;
			}

			rhi::CommandList & list = slot.lists.front();
			list					= slot.pools.front().Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error))
			{
				Report("CommandList::Begin", error);
				return false;
			}

			bool recorded = OpenTimestamps(list, index);
			recorded	  = RecordAssetFrame(list, frame, index) && recorded;
			recorded	  = CloseTimestamps(list, index) && recorded;
			if (!recorded)
			{
				std::println(stderr, "azoth arm: a scene frame command was refused");
				return false;
			}

			if (!list.End(error))
			{
				Report("CommandList::End", error);
				return false;
			}
			timing.recordNs = Now() - recordStarted;

			const std::uint64_t submitStarted = Now();
			if (!Submit(slot, 1, slot.fenceValue))
			{
				return false;
			}
			timing.submitNs = Now() - submitStarted;

			slot.timed = true;
			++m_frame;
			VSNRI_FRAME_MARK;
			return true;
		}

		[[nodiscard]] bool ThreadedFrame(const std::uint32_t threads, const std::uint32_t draws, FrameTiming & timing) override
		{
			VSNRI_ZONE("azoth/ThreadedFrame");

			if (threads == 0 || threads > kMaxThreads)
			{
				std::println(stderr, "azoth arm: {} threads is outside the 1 to {} this arm records with", threads, kMaxThreads);
				return false;
			}

			Slot & slot = WaitForSlot(timing);

			ChunkJob job{ this, &slot, threads, draws };
			const std::function<void(std::uint32_t)> task = [&job](const std::uint32_t worker)
			{
				if (!job.arm->RecordChunk(*job.slot, worker, job.threads, job.draws))
				{
					job.failed.store(true, std::memory_order_relaxed);
				}
			};

			const std::uint64_t recordStarted = Now();
			m_workers.Run(threads, task);
			timing.recordNs = Now() - recordStarted;

			if (job.failed.load(std::memory_order_relaxed))
			{
				return false;
			}

			const std::uint64_t submitStarted = Now();
			if (!Submit(slot, threads, slot.fenceValue))
			{
				return false;
			}
			timing.submitNs = Now() - submitStarted;

			++m_frame;
			VSNRI_FRAME_MARK;
			return true;
		}

		[[nodiscard]] bool ChurnBatch(const Churn churn, const std::uint32_t count, std::uint64_t & elapsedNs) override
		{
			VSNRI_ZONE("azoth/ChurnBatch");

			if (count > kChurnBatch)
			{
				std::println(stderr, "azoth arm: a churn batch of {} is larger than the {} this arm holds", count, kChurnBatch);
				return false;
			}

			switch (churn)
			{
			case Churn::eBuffer:		  return ChurnBuffers(count, elapsedNs);
			case Churn::eTexture:		  return ChurnTextures(count, elapsedNs);
			case Churn::eDescriptorWrite: return ChurnDescriptorWrites(count, elapsedNs);
			}

			return false;
		}

		[[nodiscard]] bool SubmitRoundTrip(std::uint64_t & elapsedNs) override
		{
			VSNRI_ZONE("azoth/SubmitRoundTrip");

			const std::uint64_t started = Now();
			rhi::Error error{};
			if (!m_oneShot.pools.front().Reset(rhi::RetirePoint{ .timeline = m_timeline, .value = m_oneShot.fenceValue }, error))
			{
				Report("CommandPool::Reset", error);
				return false;
			}

			rhi::CommandList & list = m_oneShot.lists.front();
			list					= m_oneShot.pools.front().Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error) || !list.End(error))
			{
				Report("CommandList::Begin or End", error);
				return false;
			}

			if (!Submit(m_oneShot, 1, m_oneShot.fenceValue) || !m_queue.Wait(m_timeline, m_oneShot.fenceValue, kTimeoutNanoseconds, error))
			{
				Report("Queue::Wait", error);
				return false;
			}

			elapsedNs = Now() - started;
			return true;
		}

		[[nodiscard]] bool Drain() override
		{
			if (!m_queue.IsValid())
			{
				return true;
			}

			rhi::Error error{};
			if (!m_queue.Wait(m_timeline, m_fenceValue, kTimeoutNanoseconds, error))
			{
				Report("Queue::Wait", error);
				return false;
			}

			for (Slot & slot : m_slots)
			{
				slot.timed = false;
			}

			return true;
		}

	private:
		struct Slot final
		{
			std::array<rhi::CommandPool, kMaxThreads> pools{};
			std::array<rhi::CommandList, kMaxThreads> lists{};
			std::uint64_t fenceValue = 0;

			// Its last submission wrote timestamps that belong to the run in progress.
			bool timed = false;
		};

		struct ChunkJob final
		{
			AzothArm * arm		  = nullptr;
			Slot * slot			  = nullptr;
			std::uint32_t threads = 0;
			std::uint32_t draws	  = 0;
			std::atomic<bool> failed{ false };
		};

		// The scene templates call these, so this library sees exactly the calls the NRI arm sees.
		struct Recorder final
		{
			const AzothArm & arm;
			rhi::CommandList & list;
			bool ok = true;

			void BindPipeline(const std::uint32_t pipeline)
			{
				ok = list.SetGraphicsPipeline(arm.m_scenePipelines.at(pipeline)) && ok;
			}

			void BindMaterial(const std::uint32_t material)
			{
				ok = list.BindDescriptorSet(arm.m_sceneLayout, 1, arm.m_materialSets.at(material)) && ok;
			}

			void BindMesh(const std::uint32_t mesh)
			{
				ok = list.SetVertexBuffer(0, arm.m_vertices, MeshVertexOffset(mesh)) && ok;
				ok = list.SetIndexBuffer(arm.m_indices, MeshIndexOffset(mesh), false) && ok;
			}

			void Push(const PushBlock & push)
			{
				ok = list.PushConstants(arm.m_sceneLayout, kGraphicsStages, 0, kPushConstantBytes, &push) && ok;
			}

			void DrawMesh()
			{
				ok = list.DrawIndexed(kMeshIndices, 1, 0, 0, 0) && ok;
			}

			void BindScenePipeline(const std::uint32_t pipeline)
			{
				ok = list.SetGraphicsPipeline(arm.m_assetPipelines.at(pipeline)) && ok;
			}

			void BindSceneVertices()
			{
				ok = list.SetVertexBuffer(0, arm.m_sceneVertices, 0) && ok;
			}

			void BindSceneMaterial(const std::uint32_t material)
			{
				ok = list.BindDescriptorSet(arm.m_assetLayout, 1, arm.m_assetMaterialSets.at(material)) && ok;
			}

			void PushModel(const Matrix & model)
			{
				ok = list.PushConstants(arm.m_assetLayout, rhi::ShaderStage::eVertex, 0, sizeof(Matrix), model.data()) && ok;
			}

			void DrawSceneMesh(const std::uint32_t mesh)
			{
				const SceneMesh & range = arm.m_sceneMeshes.at(mesh);
				ok						= list.DrawIndexed(range.indexCount, 1, range.firstIndex, range.vertexOffset, 0) && ok;
			}
		};

		[[nodiscard]] bool CreatePool(rhi::CommandPool & pool)
		{
			rhi::Error error{};
			pool = m_device.CreateCommandPool(rhi::CommandPoolDesc{}, error);
			if (!pool.IsValid())
			{
				Report("CreateCommandPool", error);
			}

			return pool.IsValid();
		}

		[[nodiscard]] bool CreatePools()
		{
			for (Slot & slot : m_slots)
			{
				for (rhi::CommandPool & pool : slot.pools)
				{
					if (!CreatePool(pool))
					{
						return false;
					}
				}
			}

			return CreatePool(m_shapePool) && CreatePool(m_oneShot.pools.front());
		}

		[[nodiscard]] bool CreateUploadBuffer(
			const std::uint64_t size, const rhi::Flags<rhi::BufferUsage> usage, void (*fill)(void *), rhi::BufferHandle & buffer)
		{
			rhi::Error error{};
			buffer = m_device.CreateBuffer(rhi::BufferDesc{ .size = size, .usage = usage, .memory = rhi::MemoryUsage::eCpuUpload }, error);
			if (!buffer.IsValid())
			{
				Report("CreateBuffer", error);
				return false;
			}

			const rhi::MappedMemory mapped = m_device.Map(buffer, rhi::MapDesc{}, error);
			if (mapped.data == nullptr)
			{
				Report("Map", error);
				return false;
			}

			fill(mapped.data);
			return m_device.Unmap(buffer, error) || (Report("Unmap", error), false);
		}

		[[nodiscard]] bool CreateBuffers()
		{
			return CreateUploadBuffer(kVertexBytes, rhi::BufferUsage::eVertex, FillVertices, m_vertices) &&
				   CreateUploadBuffer(kIndexBytes, rhi::BufferUsage::eIndex, FillIndices, m_indices) &&
				   CreateUploadBuffer(kObjectBytes, rhi::BufferUsage::eStorage, FillObjects, m_objects) &&
				   CreateUploadBuffer(kMaterialBytes, rhi::BufferUsage::eUniform, FillMaterials, m_materials);
		}

		[[nodiscard]] bool CreateTexture(
			const rhi::Format format, const rhi::Flags<rhi::TextureUsage> usage, const std::uint32_t extent, rhi::TextureHandle & texture)
		{
			rhi::Error error{};
			texture = m_device.CreateTexture(
				rhi::TextureDesc{ .type = rhi::TextureType::eTex2D, .format = format, .width = extent, .height = extent, .usage = usage }, error);
			if (!texture.IsValid())
			{
				Report("CreateTexture", error);
			}

			return texture.IsValid();
		}

		[[nodiscard]] bool CreateView(const rhi::TextureHandle texture, const rhi::Format format, const rhi::Flags<rhi::TextureAspect> aspects,
			const rhi::Flags<rhi::TextureUsage> usage, rhi::TextureViewHandle & view)
		{
			rhi::Error error{};
			view = m_device.CreateTextureView(texture,
				rhi::TextureViewDesc{ .type = rhi::TextureViewType::eTex2D, .format = format, .range = { .aspects = aspects }, .usage = usage },
				error);
			if (!view.IsValid())
			{
				Report("CreateTextureView", error);
			}

			return view.IsValid();
		}

		[[nodiscard]] bool CreateTextures()
		{
			using Usage = rhi::TextureUsage;

			return CreateTexture(kDepthFormat, rhi::Flags<Usage>(Usage::eDepthStencilAttachment) | Usage::eSampled, kShadowExtent, m_shadow) &&
				   CreateTexture(kColorFormat, rhi::Flags<Usage>(Usage::eColorAttachment) | Usage::eSampled, kSceneExtent, m_sceneColor) &&
				   CreateTexture(kDepthFormat, Usage::eDepthStencilAttachment, kSceneExtent, m_sceneDepth) &&
				   CreateTexture(kColorFormat, Usage::eColorAttachment, kSceneExtent, m_post) &&
				   CreateView(m_shadow, kDepthFormat, kDepthAspect, Usage::eDepthStencilAttachment, m_shadowView) &&
				   CreateView(m_shadow, kDepthFormat, kDepthAspect, Usage::eSampled, m_shadowTexture) &&
				   CreateView(m_sceneColor, kColorFormat, kColorAspect, Usage::eColorAttachment, m_sceneColorView) &&
				   CreateView(m_sceneColor, kColorFormat, kColorAspect, Usage::eSampled, m_sceneColorTexture) &&
				   CreateView(m_sceneDepth, kDepthFormat, kDepthAspect, Usage::eDepthStencilAttachment, m_sceneDepthView) &&
				   CreateView(m_post, kColorFormat, kColorAspect, Usage::eColorAttachment, m_postView);
		}

		[[nodiscard]] bool CreateLayouts()
		{
			using Type = rhi::DescriptorType;

			const std::array objects{ rhi::DescriptorBinding{ .binding = 0, .type = Type::eStorageBuffer, .count = 1, .stages = kGraphicsStages } };
			const std::array material{ rhi::DescriptorBinding{ .binding = 0, .type = Type::eUniformBuffer, .count = 1, .stages = kGraphicsStages } };
			const std::array shadow{
				rhi::DescriptorBinding{ .binding = 0, .type = Type::eTextureSRV, .count = 1, .stages = kGraphicsStages },
				rhi::DescriptorBinding{ .binding = 1, .type = Type::eSampler, .count = 1, .stages = kGraphicsStages },
			};
			const std::array post{
				rhi::DescriptorBinding{ .binding = 0, .type = Type::eTextureSRV, .count = 1, .stages = rhi::ShaderStage::eFragment },
				rhi::DescriptorBinding{ .binding = 1, .type = Type::eSampler, .count = 1, .stages = rhi::ShaderStage::eFragment },
			};

			const std::array<std::span<const rhi::DescriptorBinding>, 4> bindings{ objects, material, shadow, post };

			rhi::Error error{};
			for (std::size_t set = 0; set < bindings.size(); ++set)
			{
				m_setLayouts.at(set) = m_device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings.at(set) }, error);
				if (!m_setLayouts.at(set).IsValid())
				{
					Report("CreateDescriptorSetLayout", error);
					return false;
				}
			}

			const std::array sceneSets{ m_setLayouts.at(0), m_setLayouts.at(1), m_setLayouts.at(2) };
			const std::array push{ rhi::PushConstantRange{ .stages = kGraphicsStages, .offset = 0, .size = kPushConstantBytes } };
			m_sceneLayout = m_device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = sceneSets, .pushConstants = push }, error);

			const std::array postSets{ m_setLayouts.at(3) };
			m_postLayout = m_device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = postSets }, error);

			if (!m_sceneLayout.IsValid() || !m_postLayout.IsValid())
			{
				Report("CreatePipelineLayout", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreateDescriptors()
		{
			rhi::Error error{};
			m_sampler = m_device.CreateSampler(rhi::SamplerDesc{ .magFilter = rhi::Filter::eLinear,
												   .minFilter				= rhi::Filter::eLinear,
												   .mipmapMode				= rhi::MipmapMode::eNearest,
												   .addressU				= rhi::AddressMode::eClampToEdge,
												   .addressV				= rhi::AddressMode::eClampToEdge,
												   .addressW				= rhi::AddressMode::eClampToEdge,
												   .maxLod					= 0.0f },
				error);
			if (!m_sampler.IsValid())
			{
				Report("CreateSampler", error);
				return false;
			}

			m_arena = m_device.CreateDescriptorArena(
				rhi::DescriptorArenaDesc{ .type = rhi::DescriptorArenaType::ePersistent, .maxSets = kMaterials + 4, .maxDescriptors = kMaterials + 8 }, error);
			if (!m_arena.IsValid())
			{
				Report("CreateDescriptorArena", error);
				return false;
			}

			const auto allocate = [this, &error](const rhi::DescriptorSetLayoutHandle layout, rhi::DescriptorSetHandle & set)
			{
				set = m_arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = layout }, error);
				if (!set.IsValid())
				{
					Report("DescriptorArena::Allocate", error);
				}

				return set.IsValid();
			};

			bool allocated = allocate(m_setLayouts.at(0), m_objectsSet) && allocate(m_setLayouts.at(1), m_churnSet) &&
							 allocate(m_setLayouts.at(2), m_shadowSet) && allocate(m_setLayouts.at(3), m_postSet);
			for (rhi::DescriptorSetHandle & set : m_materialSets)
			{
				allocated = allocated && allocate(m_setLayouts.at(1), set);
			}
			if (!allocated)
			{
				return false;
			}

			std::array<rhi::DescriptorWriteBuffer, kMaterials + 2> buffers{};
			buffers.at(0) = rhi::DescriptorWriteBuffer{ .set = m_objectsSet, .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .buffer = m_objects };
			buffers.at(1) = rhi::DescriptorWriteBuffer{
				.set = m_churnSet, .binding = 0, .type = rhi::DescriptorType::eUniformBuffer, .buffer = m_materials, .range = kMaterialStride
			};
			for (std::uint32_t material = 0; material < kMaterials; ++material)
			{
				buffers.at(material + 2) = rhi::DescriptorWriteBuffer{ .set = m_materialSets.at(material),
					.binding												= 0,
					.type													= rhi::DescriptorType::eUniformBuffer,
					.buffer													= m_materials,
					.offset													= std::uint64_t{ material } * kMaterialStride,
					.range													= kMaterialStride };
			}

			const std::array textures{
				rhi::DescriptorWriteTexture{ .set = m_shadowSet, .binding = 0, .type = rhi::DescriptorType::eTextureSRV, .view = m_shadowTexture },
				rhi::DescriptorWriteTexture{ .set = m_postSet, .binding = 0, .type = rhi::DescriptorType::eTextureSRV, .view = m_sceneColorTexture },
			};
			const std::array samplers{
				rhi::DescriptorWriteSampler{ .set = m_shadowSet, .binding = 1, .sampler = m_sampler },
				rhi::DescriptorWriteSampler{ .set = m_postSet, .binding = 1, .sampler = m_sampler },
			};

			if (!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteBuffer>(buffers), error) ||
				!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteTexture>(textures), error) ||
				!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteSampler>(samplers), error))
			{
				Report("UpdateDescriptors", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreatePipelines()
		{
			const std::array bindings{ rhi::VertexBindingDesc{ .binding = 0, .stride = kVertexStride, .perInstance = false } };
			const std::array attributes{ rhi::VertexAttributeDesc{ .location = 0, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = 0 } };
			const rhi::VertexInputDesc vertexInput{ .bindings = bindings, .attributes = attributes, .topology = rhi::PrimitiveTopology::eTriangleList };
			const rhi::VertexInputDesc noVertices{};

			const std::array meshShaders{
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eVertex,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kMeshVert,
					.size				  = sizeof(spirv::kMeshVert),
					.entryPoint			  = "main" },
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eFragment,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kMeshFrag,
					.size				  = sizeof(spirv::kMeshFrag),
					.entryPoint			  = "main" },
			};
			const std::array postShaders{
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eVertex,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kPostVert,
					.size				  = sizeof(spirv::kPostVert),
					.entryPoint			  = "main" },
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eFragment,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kPostFrag,
					.size				  = sizeof(spirv::kPostFrag),
					.entryPoint			  = "main" },
			};

			const auto base = [](const rhi::PipelineLayoutHandle layout)
			{
				rhi::GraphicsPipelineDesc desc{};
				desc.layout			  = layout;
				desc.raster.cullMode  = rhi::CullMode::eNone;
				desc.raster.frontFace = rhi::FrontFace::eCounterClockwise;
				desc.dynamicStates	  = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
				return desc;
			};

			rhi::Error error{};
			for (std::uint32_t variant = 0; variant < kPipelines; ++variant)
			{
				rhi::GraphicsPipelineDesc desc					 = base(m_sceneLayout);
				desc.shaders									 = meshShaders;
				desc.vertexInput								 = &vertexInput;
				desc.raster.cullMode							 = VariantCulls(variant) ? rhi::CullMode::eBack : rhi::CullMode::eNone;
				desc.depthStencil.depthTestEnable				 = true;
				desc.depthStencil.depthWriteEnable				 = true;
				desc.depthStencil.depthCompareOp				 = VariantLessEqual(variant) ? rhi::CompareOp::eLessOrEqual : rhi::CompareOp::eLess;
				desc.blend.attachmentCount						 = 1;
				desc.blend.attachments.at(0).blendEnable		 = VariantBlends(variant);
				desc.blend.attachments.at(0).srcColorBlendFactor = rhi::BlendFactor::eSrcAlpha;
				desc.blend.attachments.at(0).dstColorBlendFactor = rhi::BlendFactor::eOneMinusSrcAlpha;
				desc.blend.attachments.at(0).colorBlendOp		 = rhi::BlendOp::eAdd;
				desc.blend.attachments.at(0).srcAlphaBlendFactor = rhi::BlendFactor::eOne;
				desc.blend.attachments.at(0).dstAlphaBlendFactor = rhi::BlendFactor::eZero;
				desc.blend.attachments.at(0).alphaBlendOp		 = rhi::BlendOp::eAdd;
				desc.renderTarget.colorFormats.at(0)			 = kColorFormat;
				desc.renderTarget.colorFormatCount				 = 1;
				desc.renderTarget.depthStencilFormat			 = kDepthFormat;

				m_scenePipelines.at(variant) = m_device.CreateGraphicsPipeline(desc, error);
				if (!m_scenePipelines.at(variant).IsValid())
				{
					Report("CreateGraphicsPipeline(scene)", error);
					return false;
				}
			}

			rhi::GraphicsPipelineDesc shadow	   = base(m_sceneLayout);
			shadow.shaders						   = std::span(meshShaders).first(1);
			shadow.vertexInput					   = &vertexInput;
			shadow.depthStencil.depthTestEnable	   = true;
			shadow.depthStencil.depthWriteEnable   = true;
			shadow.depthStencil.depthCompareOp	   = rhi::CompareOp::eLess;
			shadow.renderTarget.depthStencilFormat = kDepthFormat;
			m_shadowPipeline					   = m_device.CreateGraphicsPipeline(shadow, error);
			if (!m_shadowPipeline.IsValid())
			{
				Report("CreateGraphicsPipeline(shadow)", error);
				return false;
			}

			rhi::GraphicsPipelineDesc post		 = base(m_postLayout);
			post.shaders						 = postShaders;
			post.vertexInput					 = &noVertices;
			post.blend.attachmentCount			 = 1;
			post.renderTarget.colorFormats.at(0) = kColorFormat;
			post.renderTarget.colorFormatCount	 = 1;
			m_postPipeline						 = m_device.CreateGraphicsPipeline(post, error);
			if (!m_postPipeline.IsValid())
			{
				Report("CreateGraphicsPipeline(post)", error);
				return false;
			}

			return true;
		}

		// The threaded frame samples the shadow map without drawing it first, so it has to start in a readable layout.
		[[nodiscard]] bool PrepareLayouts()
		{
			rhi::Error error{};
			rhi::CommandList & list = m_oneShot.lists.front();
			list					= m_oneShot.pools.front().Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error))
			{
				Report("CommandList::Begin", error);
				return false;
			}

			const std::array barriers{
				Transition(m_shadow, kNothing, kSampled, kDepthAspect),
				Transition(m_sceneColor, kNothing, kSampled, kColorAspect),
			};
			if (!list.Barriers(rhi::BarrierBatch{ .textures = barriers }, error) || !list.End(error))
			{
				Report("CommandList::Barriers", error);
				return false;
			}

			if (!Submit(m_oneShot, 1, m_oneShot.fenceValue) || !m_queue.Wait(m_timeline, m_oneShot.fenceValue, kTimeoutNanoseconds, error))
			{
				Report("Queue::Wait", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool PrepareRaw()
		{
			const rhi::Result<rhi::VulkanNativeDevice> native = rhi::GetVulkanNativeDevice(m_device);
			if (!native)
			{
				Report("GetVulkanNativeDevice", native.GetError());
				return false;
			}

			rhi::Error error{};
			rhi::NativeTexture<rhi::VulkanApi> image{};
			rhi::NativeBuffer<rhi::VulkanApi> materials{};
			if (!m_device.GetNativeTexture<rhi::VulkanApi>(m_sceneColor, image, error) ||
				!m_device.GetNativeBuffer<rhi::VulkanApi>(m_materials, materials, error))
			{
				Report("GetNativeTexture or GetNativeBuffer", error);
				return false;
			}

			const rhi::VulkanNativeDevice & device = native.Value();

			raw::Handles handles{};
			handles.instance			= static_cast<VkInstance>(device.instance);
			handles.physicalDevice		= static_cast<VkPhysicalDevice>(device.physicalDevice);
			handles.device				= static_cast<VkDevice>(device.device);
			handles.getInstanceProcAddr = std::bit_cast<void *>(device.dispatch->vkGetInstanceProcAddr);
			handles.getDeviceProcAddr	= std::bit_cast<void *>(device.dispatch->vkGetDeviceProcAddr);
			handles.barrierImage		= std::bit_cast<std::uint64_t>(static_cast<VkImage>(image.image));
			handles.materialBuffer		= std::bit_cast<std::uint64_t>(static_cast<VkBuffer>(materials.buffer));

			m_identity.library		  = "Azoth RHI";
			m_identity.libraryVersion = rhi::kVersionString;

			return raw::Prepare(handles) && raw::Identify(handles, m_identity);
		}

		[[nodiscard]] bool Barrier(rhi::CommandList & list, const std::span<const rhi::TextureBarrier> textures) const
		{
			return list.Barriers(rhi::BarrierBatch{ .textures = textures });
		}

		[[nodiscard]] bool SetTarget(rhi::CommandList & list, const std::uint32_t extent) const
		{
			const bool viewport = list.SetViewport(rhi::Viewport{ .width = static_cast<float>(extent), .height = static_cast<float>(extent) });
			return list.SetScissor(rhi::Rect2D{ .width = extent, .height = extent }) && viewport;
		}

		// Opens the scene pass with the per-pass bindings every recording path shares.
		[[nodiscard]] bool BeginScene(rhi::CommandList & list, const rhi::LoadOp load, const rhi::StoreOp depthStore, const bool discard) const
		{
			bool ok = true;
			if (discard)
			{
				const std::array barriers{
					Transition(m_sceneColor, kDiscardColor, kColorTarget, kColorAspect),
					Transition(m_sceneDepth, kDiscardDepth, kDepthTarget, kDepthAspect),
				};
				ok = Barrier(list, barriers);
			}
			else
			{
				const std::array barriers{
					Transition(m_sceneColor, kColorTarget, kColorTarget, kColorAspect),
					Transition(m_sceneDepth, kDepthTarget, kDepthTarget, kDepthAspect),
				};
				ok = Barrier(list, barriers);
			}

			const std::array colors{ rhi::RenderingAttachment{
				.view		= m_sceneColorView,
				.state		= kColorTarget,
				.load		= load,
				.store		= rhi::StoreOp::eStore,
				.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
			} };
			const rhi::RenderingAttachment depth{
				.view			   = m_sceneDepthView,
				.state			   = kDepthTarget,
				.load			   = load,
				.store			   = depthStore,
				.clearDepthStencil = { .depth = 1.0f },
			};

			ok = list.BeginRendering(rhi::BeginRenderingDesc{ .colors = colors, .depthStencil = &depth, .width = kSceneExtent, .height = kSceneExtent }) && ok;
			ok = SetTarget(list, kSceneExtent) && ok;
			ok = list.BindDescriptorSet(m_sceneLayout, 0, m_objectsSet) && ok;
			return list.BindDescriptorSet(m_sceneLayout, 2, m_shadowSet) && ok;
		}

		[[nodiscard]] bool RecordFrame(rhi::CommandList & list, const std::uint32_t draws) const
		{
			Recorder recorder{ *this, list };

			const std::array opening{
				Transition(m_shadow, kDiscardDepth, kDepthTarget, kDepthAspect),
				Transition(m_post, kDiscardColor, kColorTarget, kColorAspect),
			};
			bool ok = Barrier(list, opening);

			const rhi::RenderingAttachment shadowDepth{
				.view			   = m_shadowView,
				.state			   = kDepthTarget,
				.load			   = rhi::LoadOp::eClear,
				.store			   = rhi::StoreOp::eStore,
				.clearDepthStencil = { .depth = 1.0f },
			};
			ok = list.BeginRendering(rhi::BeginRenderingDesc{ .depthStencil = &shadowDepth, .width = kShadowExtent, .height = kShadowExtent }) && ok;
			ok = SetTarget(list, kShadowExtent) && ok;
			ok = list.SetGraphicsPipeline(m_shadowPipeline) && ok;
			ok = list.BindDescriptorSet(m_sceneLayout, 0, m_objectsSet) && ok;
			RecordShadowDraws(recorder, draws);
			ok = list.EndRendering() && ok;

			const std::array shadowRead{ Transition(m_shadow, kDepthTarget, kSampled, kDepthAspect) };
			ok = Barrier(list, shadowRead) && ok;

			ok = BeginScene(list, rhi::LoadOp::eClear, rhi::StoreOp::eDontCare, true) && ok;
			RecordSceneDraws(recorder, 0, draws, draws);
			ok = list.EndRendering() && ok;

			return RecordPost(list) && ok && recorder.ok;
		}

		// The scene pass the way NRI's SceneViewer records it, then the same post pass as the synthetic frame.
		[[nodiscard]] bool RecordAssetFrame(rhi::CommandList & list, const SceneFrameDesc & frame, const std::uint32_t index) const
		{
			const std::array opening{
				Transition(m_sceneColor, kDiscardColor, kColorTarget, kColorAspect),
				Transition(m_sceneDepth, kDiscardDepth, kDepthTarget, kDepthAspect),
				Transition(m_post, kDiscardColor, kColorTarget, kColorAspect),
			};
			bool ok = Barrier(list, opening);

			const std::array colors{ rhi::RenderingAttachment{
				.view		= m_sceneColorView,
				.state		= kColorTarget,
				.load		= rhi::LoadOp::eClear,
				.store		= rhi::StoreOp::eStore,
				.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
			} };
			const rhi::RenderingAttachment depth{
				.view			   = m_sceneDepthView,
				.state			   = kDepthTarget,
				.load			   = rhi::LoadOp::eClear,
				.store			   = rhi::StoreOp::eDontCare,
				.clearDepthStencil = { .depth = 1.0f },
			};

			ok = list.BeginRendering(rhi::BeginRenderingDesc{ .colors = colors, .depthStencil = &depth, .width = kSceneExtent, .height = kSceneExtent }) && ok;
			ok = SetTarget(list, kSceneExtent) && ok;
			ok = list.BindDescriptorSet(m_assetLayout, 0, m_globalSets.at(index)) && ok;
			ok = list.SetIndexBuffer(m_sceneIndices, 0, true) && ok;

			Recorder recorder{ *this, list };
			RecordSceneAssetDraws(recorder, frame);
			ok = list.EndRendering() && ok;

			return RecordPost(list) && ok && recorder.ok;
		}

		[[nodiscard]] bool RecordPost(rhi::CommandList & list) const
		{
			const std::array sceneRead{ Transition(m_sceneColor, kColorTarget, kSampled, kColorAspect) };
			bool ok = Barrier(list, sceneRead);

			const std::array postColor{ rhi::RenderingAttachment{
				.view		= m_postView,
				.state		= kColorTarget,
				.load		= rhi::LoadOp::eClear,
				.store		= rhi::StoreOp::eStore,
				.clearColor = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
			} };
			ok = list.BeginRendering(rhi::BeginRenderingDesc{ .colors = postColor, .width = kSceneExtent, .height = kSceneExtent }) && ok;
			ok = SetTarget(list, kSceneExtent) && ok;
			ok = list.SetGraphicsPipeline(m_postPipeline) && ok;
			ok = list.BindDescriptorSet(m_postLayout, 0, m_postSet) && ok;
			ok = list.Draw(3, 1, 0, 0) && ok;
			return list.EndRendering() && ok;
		}

		// Each worker records its share of the scene into its own command list, and the first one clears.
		[[nodiscard]] bool RecordChunk(Slot & slot, const std::uint32_t worker, const std::uint32_t threads, const std::uint32_t draws) const
		{
			VSNRI_ZONE("azoth/RecordChunk");

			const auto first = static_cast<std::uint32_t>(std::uint64_t{ draws } * worker / threads);
			const auto last	 = static_cast<std::uint32_t>(std::uint64_t{ draws } * (worker + 1) / threads);

			rhi::Error error{};
			rhi::CommandPool & pool = slot.pools.at(worker);
			if (!pool.Reset(rhi::RetirePoint{ .timeline = m_timeline, .value = slot.fenceValue }, error))
			{
				Report("CommandPool::Reset", error);
				return false;
			}

			rhi::CommandList & list = slot.lists.at(worker);
			list					= pool.Allocate(nullptr, error);
			if (!list.IsValid() || !list.Begin(error))
			{
				Report("CommandList::Begin", error);
				return false;
			}

			const bool opens = worker == 0;
			Recorder recorder{ *this, list };
			recorder.ok = BeginScene(list, opens ? rhi::LoadOp::eClear : rhi::LoadOp::eLoad, rhi::StoreOp::eStore, opens);
			RecordSceneDraws(recorder, first, last - first, draws);
			recorder.ok = list.EndRendering() && recorder.ok;

			if (!recorder.ok || !list.End(error))
			{
				Report("recording a chunk", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] std::uint64_t RecordLibraryShape(rhi::CommandList & list, const Shape shape, const std::uint32_t commands, std::uint64_t & accepted) const
		{
			constexpr auto kExtent = static_cast<float>(kSceneExtent);
			constexpr auto kHalf   = kSceneExtent / 2;
			const std::array viewports{
				rhi::Viewport{ .width = kExtent, .height = kExtent },
				rhi::Viewport{ .width = kExtent / 2.0f, .height = kExtent / 2.0f },
			};
			const std::array scissors{
				rhi::Rect2D{ .width = kSceneExtent, .height = kSceneExtent },
				rhi::Rect2D{ .width = kHalf, .height = kHalf },
			};

			const std::array<rhi::TextureBarrier, 2> textures{
				Transition(m_sceneColor, kColorTarget, kSampled, kColorAspect),
				Transition(m_sceneColor, kSampled, kColorTarget, kColorAspect),
			};
			const std::array<rhi::BarrierBatch, 2> barriers{
				rhi::BarrierBatch{ .textures = std::span(textures).subspan(0, 1) },
				rhi::BarrierBatch{ .textures = std::span(textures).subspan(1, 1) },
			};

			PushBlock push{};

			const std::uint64_t started = Now();
			switch (shape)
			{
			case Shape::eSetViewport:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.SetViewport(viewports.at(index & 1u)));
				}
				break;

			case Shape::eSetScissor:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.SetScissor(scissors.at(index & 1u)));
				}
				break;

			case Shape::ePushConstants:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					push.object = index;
					accepted += static_cast<std::uint64_t>(list.PushConstants(m_sceneLayout, kGraphicsStages, 0, kPushConstantBytes, &push));
				}
				break;

			case Shape::eBindDescriptorSet:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.BindDescriptorSet(m_sceneLayout, 1, m_materialSets.at(index & 1u)));
				}
				break;

			case Shape::eSetPipeline:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.SetGraphicsPipeline(m_scenePipelines.at(index & 1u)));
				}
				break;

			case Shape::eDraw:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.Draw(3, 1, 0, 0));
				}
				break;

			case Shape::eDrawIndexed:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.DrawIndexed(3, 1, 0, 0, 0));
				}
				break;

			case Shape::eBarrier:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					accepted += static_cast<std::uint64_t>(list.Barriers(barriers.at(index & 1u)));
				}
				break;
			}

			return Now() - started;
		}

		[[nodiscard]] Slot & WaitForSlot(FrameTiming & timing)
		{
			Slot & slot = m_slots.at(m_frame % kFramesInFlight);

			const std::uint64_t started = Now();
			rhi::Error error{};
			if (!m_queue.Wait(m_timeline, slot.fenceValue, kTimeoutNanoseconds, error))
			{
				Report("Queue::Wait", error);
			}
			timing.waitNs = Now() - started;

			if (slot.timed)
			{
				ReadTimestamps(m_frame % kFramesInFlight, timing);
				slot.timed = false;
			}

			return slot;
		}

		[[nodiscard]] static std::uint32_t FirstTimestamp(const std::uint32_t index)
		{
			return index * kFrameTimestamps;
		}

		[[nodiscard]] bool OpenTimestamps(rhi::CommandList & list, const std::uint32_t index) const
		{
			const bool reset = list.ResetQueryPool(m_timestamps, FirstTimestamp(index), kFrameTimestamps);
			return list.WriteTimestamp(m_timestamps, FirstTimestamp(index), rhi::Stage::eAllCommands) && reset;
		}

		[[nodiscard]] bool CloseTimestamps(rhi::CommandList & list, const std::uint32_t index) const
		{
			const bool written = list.WriteTimestamp(m_timestamps, FirstTimestamp(index) + 1, rhi::Stage::eAllCommands);
			return list.ResolveQueryData(
					   m_timestamps, FirstTimestamp(index), kFrameTimestamps, m_timestampReadback, FirstTimestamp(index) * sizeof(std::uint64_t)) &&
				   written;
		}

		void ReadTimestamps(const std::uint32_t index, FrameTiming & timing)
		{
			std::array<std::uint64_t, kFrameTimestamps> stamps{};

			rhi::Error error{};
			const rhi::MappedMemory mapped = m_device.Map(m_timestampReadback,
				rhi::MapDesc{ .mode = rhi::MapMode::eRead, .offset = FirstTimestamp(index) * sizeof(std::uint64_t), .size = sizeof(stamps) },
				error);
			if (mapped.data == nullptr)
			{
				Report("Map(timestamps)", error);
				return;
			}

			std::memcpy(stamps.data(), mapped.data, sizeof(stamps));
			if (!m_device.Unmap(m_timestampReadback, error))
			{
				Report("Unmap(timestamps)", error);
				return;
			}

			timing.gpuValid = stamps[1] >= stamps[0];
			timing.gpuNs	= static_cast<std::uint64_t>(static_cast<double>(stamps[1] - stamps[0]) * m_device.GetCaps().timestampPeriodNanoseconds);
		}

		[[nodiscard]] bool CreateTimestamps()
		{
			rhi::Error error{};
			m_timestamps =
				m_device.CreateQueryPool(rhi::QueryPoolDesc{ .type = rhi::QueryType::eTimestamp, .queryCount = kFramesInFlight * kFrameTimestamps }, error);
			if (!m_timestamps.IsValid())
			{
				Report("CreateQueryPool", error);
				return false;
			}

			m_timestampReadback = m_device.CreateBuffer(rhi::BufferDesc{ .size = std::uint64_t{ kFramesInFlight } * kFrameTimestamps * sizeof(std::uint64_t),
															.usage			   = rhi::BufferUsage::eCopyDst,
															.memory			   = rhi::MemoryUsage::eCpuReadback },
				error);
			if (!m_timestampReadback.IsValid())
			{
				Report("CreateBuffer(readback)", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreateSceneResources(const SceneAsset & scene)
		{
			using Usage = rhi::BufferUsage;

			rhi::Error error{};
			m_sceneVertices = m_device.CreateBuffer(
				rhi::BufferDesc{ .size = scene.vertices.size() * sizeof(SceneVertex), .usage = rhi::Flags<Usage>(Usage::eVertex) | Usage::eCopyDst }, error);
			m_sceneIndices = m_device.CreateBuffer(
				rhi::BufferDesc{ .size = scene.indices.size() * sizeof(std::uint32_t), .usage = rhi::Flags<Usage>(Usage::eIndex) | Usage::eCopyDst }, error);
			m_globals = m_device.CreateBuffer(
				rhi::BufferDesc{ .size = std::uint64_t{ kGlobalsStride } * kFramesInFlight, .usage = Usage::eUniform, .memory = rhi::MemoryUsage::eCpuUpload },
				error);
			if (!m_sceneVertices.IsValid() || !m_sceneIndices.IsValid() || !m_globals.IsValid())
			{
				Report("CreateBuffer(scene)", error);
				return false;
			}

			m_sceneTextures.assign(scene.textures.size(), rhi::TextureHandle{});
			m_sceneTextureViews.assign(scene.textures.size(), rhi::TextureViewHandle{});
			for (std::size_t index = 0; index < scene.textures.size(); ++index)
			{
				const SceneTexture & source = scene.textures.at(index);
				const rhi::Format format	= SceneFormat(source.format);
				const auto mips				= static_cast<std::uint32_t>(source.mips.size());

				m_sceneTextures.at(index) =
					m_device.CreateTexture(rhi::TextureDesc{ .type = rhi::TextureType::eTex2D,
											   .format			   = format,
											   .width			   = source.width,
											   .height			   = source.height,
											   .mipLevels		   = mips,
											   .usage			   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eSampled) | rhi::TextureUsage::eCopyDst },
						error);
				if (!m_sceneTextures.at(index).IsValid())
				{
					Report("CreateTexture(scene)", error);
					return false;
				}

				m_sceneTextureViews.at(index) = m_device.CreateTextureView(m_sceneTextures.at(index),
					rhi::TextureViewDesc{ .type = rhi::TextureViewType::eTex2D,
						.format					= format,
						.range					= { .aspects = kColorAspect, .mipCount = mips },
						.usage					= rhi::TextureUsage::eSampled },
					error);
				if (!m_sceneTextureViews.at(index).IsValid())
				{
					Report("CreateTextureView(scene)", error);
					return false;
				}
			}

			return true;
		}

		// One staging buffer and one submission, the shape NRI's upload helper takes.
		[[nodiscard]] bool UploadScene(const SceneAsset & scene)
		{
			const std::uint64_t vertexBytes = scene.vertices.size() * sizeof(SceneVertex);
			const std::uint64_t indexBytes	= scene.indices.size() * sizeof(std::uint32_t);

			std::vector<std::uint64_t> textureOffsets;
			textureOffsets.reserve(scene.textures.size());
			std::uint64_t stagingBytes = vertexBytes + indexBytes;
			for (const SceneTexture & texture : scene.textures)
			{
				stagingBytes = (stagingBytes + 15) / 16 * 16;
				textureOffsets.push_back(stagingBytes);
				stagingBytes += texture.bytes.size();
			}

			rhi::Error error{};
			const rhi::BufferHandle staging = m_device.CreateBuffer(
				rhi::BufferDesc{ .size = stagingBytes, .usage = rhi::BufferUsage::eCopySrc, .memory = rhi::MemoryUsage::eCpuUpload }, error);
			if (!staging.IsValid())
			{
				Report("CreateBuffer(staging)", error);
				return false;
			}

			const rhi::MappedMemory mapped = m_device.Map(staging, rhi::MapDesc{}, error);
			if (mapped.data == nullptr)
			{
				Report("Map(staging)", error);
				m_device.Destroy(staging);
				return false;
			}

			auto * bytes = static_cast<std::byte *>(mapped.data);
			std::memcpy(bytes, scene.vertices.data(), vertexBytes);
			std::memcpy(bytes + vertexBytes, scene.indices.data(), indexBytes);
			for (std::size_t index = 0; index < scene.textures.size(); ++index)
			{
				std::memcpy(bytes + textureOffsets.at(index), scene.textures.at(index).bytes.data(), scene.textures.at(index).bytes.size());
			}
			if (!m_device.Unmap(staging, error))
			{
				Report("Unmap(staging)", error);
				m_device.Destroy(staging);
				return false;
			}

			rhi::CommandList & list = m_oneShot.lists.front();
			list					= m_oneShot.pools.front().Allocate(nullptr, error);
			bool ok					= list.IsValid() && list.Begin(error);

			std::vector<rhi::TextureBarrier> toCopy;
			std::vector<rhi::TextureBarrier> toSampled;
			for (std::size_t index = 0; index < scene.textures.size(); ++index)
			{
				const rhi::TextureSubresourceRange range{ .aspects = kColorAspect,
					.mipCount									   = static_cast<std::uint32_t>(scene.textures.at(index).mips.size()) };
				toCopy.push_back(rhi::TextureBarrier{ .texture = m_sceneTextures.at(index), .before = kNothing, .after = kCopyTarget, .range = range });
				toSampled.push_back(rhi::TextureBarrier{ .texture = m_sceneTextures.at(index), .before = kCopyTarget, .after = kSampled, .range = range });
			}
			ok = ok && list.Barriers(rhi::BarrierBatch{ .textures = toCopy }, error);

			ok = ok && list.CopyBuffer(m_sceneVertices, 0, staging, 0, vertexBytes, error);
			ok = ok && list.CopyBuffer(m_sceneIndices, 0, staging, vertexBytes, indexBytes, error);

			std::vector<rhi::BufferTextureCopy> regions;
			for (std::size_t index = 0; index < scene.textures.size() && ok; ++index)
			{
				const SceneTexture & texture = scene.textures.at(index);
				regions.clear();
				for (std::uint32_t mip = 0; mip < texture.mips.size(); ++mip)
				{
					const SceneMip & level = texture.mips.at(mip);
					regions.push_back(rhi::BufferTextureCopy{ .bufferOffset = textureOffsets.at(index) + level.offset,
						.subresource										= { .aspects = kColorAspect, .mip = mip },
						.textureExtent										= { .width = level.width, .height = level.height, .depth = 1 } });
				}
				ok = list.CopyBufferToTexture(m_sceneTextures.at(index), staging, regions, error);
			}

			const std::array buffers{
				rhi::BufferBarrier{ .buffer = m_sceneVertices, .before = kCopyTarget, .after = kVertexInput },
				rhi::BufferBarrier{ .buffer = m_sceneIndices, .before = kCopyTarget, .after = kIndexInput },
			};
			ok = ok && list.Barriers(rhi::BarrierBatch{ .buffers = buffers, .textures = toSampled }, error);
			ok = ok && list.End(error);

			ok = ok && Submit(m_oneShot, 1, m_oneShot.fenceValue) && m_queue.Wait(m_timeline, m_oneShot.fenceValue, kTimeoutNanoseconds, error);
			if (!ok)
			{
				Report("uploading the scene", error);
			}

			m_device.Destroy(staging);
			return ok;
		}

		[[nodiscard]] bool CreateAssetLayout()
		{
			using Type = rhi::DescriptorType;

			const std::array globals{
				rhi::DescriptorBinding{ .binding = 0, .type = Type::eUniformBuffer, .count = 1, .stages = kGraphicsStages },
				rhi::DescriptorBinding{ .binding = 1, .type = Type::eSampler, .count = 1, .stages = rhi::ShaderStage::eFragment },
			};
			const std::array material{
				rhi::DescriptorBinding{ .binding = 0, .type = Type::eTextureSRV, .count = kTexturesAMaterial, .stages = rhi::ShaderStage::eFragment },
			};
			const std::array<std::span<const rhi::DescriptorBinding>, 2> bindings{ globals, material };

			rhi::Error error{};
			for (std::size_t set = 0; set < bindings.size(); ++set)
			{
				m_assetSetLayouts.at(set) = m_device.CreateDescriptorSetLayout(rhi::DescriptorSetLayoutDesc{ .bindings = bindings.at(set) }, error);
				if (!m_assetSetLayouts.at(set).IsValid())
				{
					Report("CreateDescriptorSetLayout(asset)", error);
					return false;
				}
			}

			const std::array push{ rhi::PushConstantRange{ .stages = rhi::ShaderStage::eVertex, .offset = 0, .size = sizeof(Matrix) } };
			m_assetLayout = m_device.CreatePipelineLayout(rhi::PipelineLayoutDesc{ .sets = m_assetSetLayouts, .pushConstants = push }, error);
			if (!m_assetLayout.IsValid())
			{
				Report("CreatePipelineLayout(asset)", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreateAssetDescriptors(const SceneAsset & scene)
		{
			rhi::Error error{};
			m_assetSampler = m_device.CreateSampler(rhi::SamplerDesc{ .magFilter = rhi::Filter::eLinear,
														.minFilter				 = rhi::Filter::eLinear,
														.mipmapMode				 = rhi::MipmapMode::eLinear,
														.addressU				 = rhi::AddressMode::eRepeat,
														.addressV				 = rhi::AddressMode::eRepeat,
														.addressW				 = rhi::AddressMode::eRepeat,
														.maxLod					 = kSceneSamplerMaxLod },
				error);
			if (!m_assetSampler.IsValid())
			{
				Report("CreateSampler(asset)", error);
				return false;
			}

			const auto materials = static_cast<std::uint32_t>(scene.materials.size());
			m_assetArena		 = m_device.CreateDescriptorArena(rhi::DescriptorArenaDesc{ .type = rhi::DescriptorArenaType::ePersistent,
																	  .maxSets					  = materials + kFramesInFlight,
																	  .maxDescriptors			  = (materials * kTexturesAMaterial) + (kFramesInFlight * 2) },
				error);
			if (!m_assetArena.IsValid())
			{
				Report("CreateDescriptorArena(asset)", error);
				return false;
			}

			const auto allocate = [this, &error](const rhi::DescriptorSetLayoutHandle layout, rhi::DescriptorSetHandle & set)
			{
				set = m_assetArena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = layout }, error);
				if (!set.IsValid())
				{
					Report("DescriptorArena::Allocate(asset)", error);
				}

				return set.IsValid();
			};

			m_assetMaterialSets.assign(materials, rhi::DescriptorSetHandle{});
			bool allocated = true;
			for (rhi::DescriptorSetHandle & set : m_globalSets)
			{
				allocated = allocated && allocate(m_assetSetLayouts.at(0), set);
			}
			for (rhi::DescriptorSetHandle & set : m_assetMaterialSets)
			{
				allocated = allocated && allocate(m_assetSetLayouts.at(1), set);
			}
			if (!allocated)
			{
				return false;
			}

			std::vector<rhi::DescriptorWriteBuffer> buffers;
			std::vector<rhi::DescriptorWriteSampler> samplers;
			for (std::uint32_t index = 0; index < kFramesInFlight; ++index)
			{
				buffers.push_back(rhi::DescriptorWriteBuffer{ .set = m_globalSets.at(index),
					.binding									   = 0,
					.type										   = rhi::DescriptorType::eUniformBuffer,
					.buffer										   = m_globals,
					.offset										   = std::uint64_t{ index } * kGlobalsStride,
					.range										   = kGlobalsStride });
				samplers.push_back(rhi::DescriptorWriteSampler{ .set = m_globalSets.at(index), .binding = 1, .sampler = m_assetSampler });
			}

			std::vector<rhi::DescriptorWriteTexture> textures;
			for (std::uint32_t material = 0; material < materials; ++material)
			{
				for (std::uint32_t slot = 0; slot < kTexturesAMaterial; ++slot)
				{
					textures.push_back(rhi::DescriptorWriteTexture{ .set = m_assetMaterialSets.at(material),
						.binding										 = 0,
						.arrayIndex										 = slot,
						.type											 = rhi::DescriptorType::eTextureSRV,
						.view											 = m_sceneTextureViews.at(scene.materials.at(material).textures.at(slot)) });
				}
			}

			if (!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteBuffer>(buffers), error) ||
				!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteSampler>(samplers), error) ||
				!m_device.UpdateDescriptors(std::span<const rhi::DescriptorWriteTexture>(textures), error))
			{
				Report("UpdateDescriptors(asset)", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool CreateAssetPipelines()
		{
			const std::array bindings{ rhi::VertexBindingDesc{ .binding = 0, .stride = kSceneVertexStride, .perInstance = false } };
			const std::array attributes{
				rhi::VertexAttributeDesc{
					.location = 0, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = static_cast<std::uint32_t>(offsetof(SceneVertex, position)) },
				rhi::VertexAttributeDesc{
					.location = 1, .binding = 0, .format = rhi::Format::eRGB32Float, .offset = static_cast<std::uint32_t>(offsetof(SceneVertex, normal)) },
				rhi::VertexAttributeDesc{
					.location = 2, .binding = 0, .format = rhi::Format::eRG32Float, .offset = static_cast<std::uint32_t>(offsetof(SceneVertex, uv)) },
			};
			const rhi::VertexInputDesc vertexInput{ .bindings = bindings, .attributes = attributes, .topology = rhi::PrimitiveTopology::eTriangleList };

			const std::array shaders{
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eVertex,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kSceneVert,
					.size				  = sizeof(spirv::kSceneVert),
					.entryPoint			  = "main" },
				rhi::ShaderBinary{ .stage = rhi::ShaderStage::eFragment,
					.format				  = rhi::ShaderBinaryFormat::eSpirV,
					.data				  = spirv::kSceneFrag,
					.size				  = sizeof(spirv::kSceneFrag),
					.entryPoint			  = "main" },
			};

			rhi::Error error{};
			for (std::uint32_t pipeline = 0; pipeline < kScenePipelines; ++pipeline)
			{
				rhi::GraphicsPipelineDesc desc{};
				desc.layout							 = m_assetLayout;
				desc.shaders						 = shaders;
				desc.vertexInput					 = &vertexInput;
				desc.raster.cullMode				 = pipeline == 0 ? rhi::CullMode::eBack : rhi::CullMode::eNone;
				desc.raster.frontFace				 = rhi::FrontFace::eCounterClockwise;
				desc.dynamicStates					 = rhi::Flags<rhi::DynamicState>(rhi::DynamicState::eViewport) | rhi::DynamicState::eScissor;
				desc.depthStencil.depthTestEnable	 = true;
				desc.depthStencil.depthWriteEnable	 = true;
				desc.depthStencil.depthCompareOp	 = rhi::CompareOp::eLess;
				desc.blend.attachmentCount			 = 1;
				desc.renderTarget.colorFormats.at(0) = kColorFormat;
				desc.renderTarget.colorFormatCount	 = 1;
				desc.renderTarget.depthStencilFormat = kDepthFormat;

				m_assetPipelines.at(pipeline) = m_device.CreateGraphicsPipeline(desc, error);
				if (!m_assetPipelines.at(pipeline).IsValid())
				{
					Report("CreateGraphicsPipeline(asset)", error);
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool Submit(Slot & slot, const std::uint32_t lists, std::uint64_t & signaled)
		{
			std::array<const rhi::CommandList *, kMaxThreads> pointers{};
			for (std::uint32_t list = 0; list < lists; ++list)
			{
				pointers.at(list) = &slot.lists.at(list);
			}

			++m_fenceValue;
			const std::array signals{ rhi::TimelinePoint{ .timeline = m_timeline, .value = m_fenceValue } };
			const rhi::SubmitDesc submit{ .commandLists = std::span(pointers).first(lists), .signals = signals };

			signaled = m_fenceValue;
			rhi::Error error{};
			if (!m_queue.Submit(submit, error))
			{
				Report("Queue::Submit", error);
				return false;
			}

			return true;
		}

		[[nodiscard]] bool ChurnBuffers(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			std::array<rhi::BufferHandle, kChurnBatch> buffers{};
			const rhi::BufferDesc desc{ .size = kChurnBufferBytes, .usage = rhi::BufferUsage::eStorage };

			bool created				= true;
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count && created; ++index)
			{
				buffers.at(index) = m_device.CreateBuffer(desc);
				created			  = buffers.at(index).IsValid();
			}
			for (const rhi::BufferHandle buffer : buffers)
			{
				if (buffer.IsValid())
				{
					m_device.Destroy(buffer);
				}
			}
			elapsedNs = Now() - started;

			if (!created)
			{
				std::println(stderr, "azoth arm: CreateBuffer failed during churn");
			}
			return created;
		}

		[[nodiscard]] bool ChurnTextures(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			std::array<rhi::TextureHandle, kChurnBatch> textures{};
			std::array<rhi::TextureViewHandle, kChurnBatch> views{};

			bool created				= true;
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count && created; ++index)
			{
				created = CreateTexture(kColorFormat, rhi::TextureUsage::eSampled, kChurnTextureExtent, textures.at(index)) &&
						  CreateView(textures.at(index), kColorFormat, kColorAspect, rhi::TextureUsage::eSampled, views.at(index));
			}
			for (std::uint32_t index = 0; index < kChurnBatch; ++index)
			{
				if (views.at(index).IsValid())
				{
					m_device.Destroy(views.at(index));
				}
				if (textures.at(index).IsValid())
				{
					m_device.Destroy(textures.at(index));
				}
			}
			elapsedNs = Now() - started;

			return created;
		}

		[[nodiscard]] bool ChurnDescriptorWrites(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			bool written				= true;
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count; ++index)
			{
				const rhi::DescriptorWriteBuffer write{ .set = m_churnSet,
					.binding								 = 0,
					.type									 = rhi::DescriptorType::eUniformBuffer,
					.buffer									 = m_materials,
					.offset									 = std::uint64_t{ index & 1u } * kMaterialStride,
					.range									 = kMaterialStride };
				written = m_device.UpdateDescriptors(std::span(&write, 1)) && written;
			}
			elapsedNs = Now() - started;

			if (!written)
			{
				std::println(stderr, "azoth arm: UpdateDescriptors failed during churn");
			}
			return written;
		}

		rhi::UniqueDevice m_owner;
		rhi::Device m_device;
		rhi::Queue m_queue;
		rhi::TimelineHandle m_timeline{};

		std::uint64_t m_fenceValue = 0;
		std::uint32_t m_frame	   = 0;

		std::array<Slot, kFramesInFlight> m_slots{};
		rhi::CommandPool m_shapePool;
		Slot m_oneShot{};

		rhi::BufferHandle m_vertices{};
		rhi::BufferHandle m_indices{};
		rhi::BufferHandle m_objects{};
		rhi::BufferHandle m_materials{};

		rhi::TextureHandle m_shadow{};
		rhi::TextureHandle m_sceneColor{};
		rhi::TextureHandle m_sceneDepth{};
		rhi::TextureHandle m_post{};

		rhi::TextureViewHandle m_shadowView{};
		rhi::TextureViewHandle m_shadowTexture{};
		rhi::TextureViewHandle m_sceneColorView{};
		rhi::TextureViewHandle m_sceneColorTexture{};
		rhi::TextureViewHandle m_sceneDepthView{};
		rhi::TextureViewHandle m_postView{};
		rhi::SamplerHandle m_sampler{};

		std::array<rhi::DescriptorSetLayoutHandle, 4> m_setLayouts{};
		rhi::PipelineLayoutHandle m_sceneLayout{};
		rhi::PipelineLayoutHandle m_postLayout{};
		rhi::DescriptorArena m_arena;

		rhi::DescriptorSetHandle m_objectsSet{};
		std::array<rhi::DescriptorSetHandle, kMaterials> m_materialSets{};
		rhi::DescriptorSetHandle m_churnSet{};
		rhi::DescriptorSetHandle m_shadowSet{};
		rhi::DescriptorSetHandle m_postSet{};

		std::array<rhi::GraphicsPipelineHandle, kPipelines> m_scenePipelines{};
		rhi::GraphicsPipelineHandle m_shadowPipeline{};
		rhi::GraphicsPipelineHandle m_postPipeline{};

		rhi::QueryPoolHandle m_timestamps{};
		rhi::BufferHandle m_timestampReadback{};

		std::vector<SceneMesh> m_sceneMeshes;
		rhi::BufferHandle m_sceneVertices{};
		rhi::BufferHandle m_sceneIndices{};
		rhi::BufferHandle m_globals{};
		std::vector<rhi::TextureHandle> m_sceneTextures;
		std::vector<rhi::TextureViewHandle> m_sceneTextureViews;
		rhi::SamplerHandle m_assetSampler{};
		std::array<rhi::DescriptorSetLayoutHandle, 2> m_assetSetLayouts{};
		rhi::PipelineLayoutHandle m_assetLayout{};
		rhi::DescriptorArena m_assetArena;
		std::array<rhi::DescriptorSetHandle, kFramesInFlight> m_globalSets{};
		std::vector<rhi::DescriptorSetHandle> m_assetMaterialSets;
		std::array<rhi::GraphicsPipelineHandle, kScenePipelines> m_assetPipelines{};

		Identity m_identity;
		WorkerPool m_workers{ kMaxThreads };
	};

}

int main(int argc, char ** argv)
{
	vsnri::RaiseThreadPriority();

	const vsnri::HarnessOptions options = vsnri::ReadHarnessOptions(argc, argv);

#ifdef TRACY_ENABLE
	static rhi::TracyProfiler profiler;
	rhi::SetProfiler(&profiler);
#endif
	vsnri::WaitForProfiler(options);

	// Stays installed until exit, because the backend's static owner frees through it during static destruction.
	static vsnri::CountingAllocator allocator;
	rhi::SetHostAllocator(&allocator);

	vsnri::AzothArm arm;
	if (!arm.Initialize(options))
	{
		std::println(stderr, "azoth arm: the device could not be set up, see the diagnostic above");
		return 1;
	}

	return vsnri::RunBenchmarks(argc, argv, arm);
}

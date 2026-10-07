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

#include "harness.hpp"
#include "raw_vulkan.hpp"
#include "scene.hpp"

// clang-format off
#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
// clang-format on

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <print>
#include <span>
#include <vector>

namespace vsnri
{

	namespace
	{

		constexpr std::uint32_t kMaxThreads = kThreadCounts.back();

		constexpr nri::Format kColorFormat = nri::Format::RGBA8_UNORM;
		constexpr nri::Format kDepthFormat = nri::Format::D32_SFLOAT;

		constexpr nri::StageBits kGraphicsStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

		constexpr nri::AccessLayoutStage kNothing{ nri::AccessBits::NONE, nri::Layout::UNDEFINED, nri::StageBits::NONE };
		constexpr nri::AccessLayoutStage kSampled{ nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::FRAGMENT_SHADER };
		constexpr nri::AccessLayoutStage kColorTarget{
			nri::AccessBits::COLOR_ATTACHMENT_WRITE, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT
		};
		constexpr nri::AccessLayoutStage kDepthTarget{
			nri::AccessBits::DEPTH_STENCIL_ATTACHMENT, nri::Layout::DEPTH_STENCIL_ATTACHMENT, nri::StageBits::DEPTH_STENCIL_ATTACHMENT
		};

		// Every attachment is cleared before it is drawn to, so its old contents are discarded after whatever last read or wrote it.
		constexpr nri::AccessLayoutStage kDiscardColor{
			nri::AccessBits::COLOR_ATTACHMENT_WRITE, nri::Layout::UNDEFINED, nri::StageBits::FRAGMENT_SHADER | nri::StageBits::COLOR_ATTACHMENT
		};
		constexpr nri::AccessLayoutStage kDiscardDepth{
			nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE, nri::Layout::UNDEFINED, nri::StageBits::FRAGMENT_SHADER | nri::StageBits::DEPTH_STENCIL_ATTACHMENT
		};

		constexpr nri::AccessStage kVertexInput{ nri::AccessBits::VERTEX_BUFFER, nri::StageBits::VERTEX_SHADER };
		constexpr nri::AccessStage kIndexInput{ nri::AccessBits::INDEX_BUFFER, nri::StageBits::INDEX_INPUT };

		[[nodiscard]] nri::Format SceneFormat(const SceneTextureFormat format)
		{
			switch (format)
			{
			case SceneTextureFormat::eRGBA8: return nri::Format::RGBA8_UNORM;
			case SceneTextureFormat::eBC1:	 return nri::Format::BC1_RGBA_UNORM;
			case SceneTextureFormat::eBC7:	 return nri::Format::BC7_RGBA_UNORM;
			}

			return nri::Format::UNKNOWN;
		}

		void * NRI_CALL NriAllocate(void *, const std::size_t size, const std::size_t alignment)
		{
			return CountedAllocate(size, alignment);
		}

		void * NRI_CALL NriReallocate(void *, void * memory, const std::size_t size, const std::size_t alignment)
		{
			return CountedReallocate(memory, size, alignment);
		}

		void NRI_CALL NriFree(void *, void * memory)
		{
			CountedFree(memory);
		}

		void NRI_CALL ReportMessage(const nri::Message type, const char * file, const std::uint32_t line, const char * message, void *)
		{
			const char * kind = type == nri::Message::ERROR ? "error" : (type == nri::Message::WARNING ? "warning" : "info");
			std::println(stderr, "nri {}: {} ({}:{})", kind, message, file, line);
		}

		[[nodiscard]] bool Succeeded(const nri::Result result, const char * what)
		{
			if (result != nri::Result::SUCCESS)
			{
				std::println(stderr, "nri arm: {} failed with result {}", what, static_cast<int>(result));
			}

			return result == nri::Result::SUCCESS;
		}

		template <class Object>
		void Release(void(NRI_CALL * destroy)(Object *), Object *& object)
		{
			if (object != nullptr)
			{
				destroy(object);
				object = nullptr;
			}
		}

		[[nodiscard]] nri::TextureBarrierDesc Transition(nri::Texture * texture, const nri::AccessLayoutStage & before, const nri::AccessLayoutStage & after)
		{
			nri::TextureBarrierDesc barrier{};
			barrier.texture = texture;
			barrier.before	= before;
			barrier.after	= after;
			barrier.planes	= nri::PlaneBits::ALL;
			return barrier;
		}

	}

	class NriArm final : public Arm
	{
	public:
		NriArm()						   = default;
		NriArm(const NriArm &)			   = delete;
		NriArm & operator=(const NriArm &) = delete;
		NriArm(NriArm &&)				   = delete;
		NriArm & operator=(NriArm &&)	   = delete;

		~NriArm() override
		{
			if (m_device == nullptr)
			{
				return;
			}

			// MoltenVK finishes a submission's queries in a completion block that can run after the fence signals.
			static_cast<void>(Drain());
			static_cast<void>(m_core.QueueWaitIdle(m_queue));
			raw::Release();

			for (nri::Pipeline *& pipeline : m_assetPipelines)
			{
				Release(m_core.DestroyPipeline, pipeline);
			}
			Release(m_core.DestroyDescriptorPool, m_assetPool);
			Release(m_core.DestroyPipelineLayout, m_assetLayout);
			for (nri::Descriptor *& view : m_globalsViews)
			{
				Release(m_core.DestroyDescriptor, view);
			}
			Release(m_core.DestroyDescriptor, m_assetSampler);
			for (nri::Descriptor *& view : m_sceneTextureViews)
			{
				Release(m_core.DestroyDescriptor, view);
			}
			for (nri::Texture *& texture : m_sceneTextures)
			{
				Release(m_core.DestroyTexture, texture);
			}
			Release(m_core.DestroyBuffer, m_globals);
			Release(m_core.DestroyBuffer, m_sceneIndices);
			Release(m_core.DestroyBuffer, m_sceneVertices);
			Release(m_core.DestroyBuffer, m_timestampReadback);
			Release(m_core.DestroyQueryPool, m_timestamps);

			Release(m_core.DestroyPipeline, m_postPipeline);
			Release(m_core.DestroyPipeline, m_shadowPipeline);
			for (nri::Pipeline *& pipeline : m_scenePipelines)
			{
				Release(m_core.DestroyPipeline, pipeline);
			}

			Release(m_core.DestroyDescriptorPool, m_pool);
			Release(m_core.DestroyPipelineLayout, m_postLayout);
			Release(m_core.DestroyPipelineLayout, m_sceneLayout);

			for (nri::Descriptor *& view : m_materialViews)
			{
				Release(m_core.DestroyDescriptor, view);
			}
			Release(m_core.DestroyDescriptor, m_objectsView);
			Release(m_core.DestroyDescriptor, m_sampler);
			Release(m_core.DestroyDescriptor, m_postAttachment);
			Release(m_core.DestroyDescriptor, m_sceneDepthAttachment);
			Release(m_core.DestroyDescriptor, m_sceneColorTexture);
			Release(m_core.DestroyDescriptor, m_sceneColorAttachment);
			Release(m_core.DestroyDescriptor, m_shadowTexture);
			Release(m_core.DestroyDescriptor, m_shadowAttachment);

			Release(m_core.DestroyTexture, m_post);
			Release(m_core.DestroyTexture, m_sceneDepth);
			Release(m_core.DestroyTexture, m_sceneColor);
			Release(m_core.DestroyTexture, m_shadow);

			Release(m_core.DestroyBuffer, m_materials);
			Release(m_core.DestroyBuffer, m_objects);
			Release(m_core.DestroyBuffer, m_indices);
			Release(m_core.DestroyBuffer, m_vertices);

			Release(m_core.DestroyCommandBuffer, m_oneShotBuffer);
			Release(m_core.DestroyCommandAllocator, m_oneShotAllocator);
			Release(m_core.DestroyCommandBuffer, m_shapeBuffer);
			Release(m_core.DestroyCommandAllocator, m_shapeAllocator);
			for (Slot & slot : m_slots)
			{
				for (std::uint32_t worker = 0; worker < kMaxThreads; ++worker)
				{
					Release(m_core.DestroyCommandBuffer, slot.buffers.at(worker));
					Release(m_core.DestroyCommandAllocator, slot.allocators.at(worker));
				}
			}

			Release(m_core.DestroyFence, m_fence);
			nriDestroyDevice(m_device);
		}

		[[nodiscard]] bool Initialize(const HarnessOptions & options)
		{
			VSNRI_ZONE("nri/Initialize");

			nri::DeviceCreationDesc desc{};
			desc.graphicsAPI				 = nri::GraphicsAPI::VK;
			desc.robustness					 = nri::Robustness::OFF;
			desc.callbackInterface			 = nri::CallbackInterface{ ReportMessage, nullptr, nullptr };
			desc.allocationCallbacks		 = nri::AllocationCallbacks{ NriAllocate, NriReallocate, NriFree, nullptr, true };
			desc.enableGraphicsAPIValidation = options.enableGraphicsApiValidation;

			if (!Succeeded(nriCreateDevice(desc, m_device), "nriCreateDevice") ||
				!Succeeded(nriGetInterface(*m_device, NRI_INTERFACE(nri::CoreInterface), &m_core), "nriGetInterface(CoreInterface)") ||
				!Succeeded(nriGetInterface(*m_device, NRI_INTERFACE(nri::WrapperVKInterface), &m_wrapper), "nriGetInterface(WrapperVKInterface)") ||
				!Succeeded(nriGetInterface(*m_device, NRI_INTERFACE(nri::HelperInterface), &m_helper), "nriGetInterface(HelperInterface)") ||
				!Succeeded(m_core.GetQueue(*m_device, nri::QueueType::GRAPHICS, 0, m_queue), "GetQueue") ||
				!Succeeded(m_core.CreateFence(*m_device, 0, m_fence), "CreateFence"))
			{
				return false;
			}

			return CreateCommandBuffers() && CreateBuffers() && CreateTextures() && CreateLayouts() && CreateDescriptors() && CreatePipelines() &&
				   CreateTimestamps() && PrepareLayouts() && PrepareRaw();
		}

		[[nodiscard]] const Identity & Describe() const override
		{
			return m_identity;
		}

		[[nodiscard]] bool RecordShape(const Shape shape, const std::uint32_t commands, const bool rawFirst, ShapeTiming & timing) override
		{
			VSNRI_ZONE("nri/RecordShape");

			m_core.ResetCommandAllocator(*m_shapeAllocator);
			if (!Succeeded(m_core.BeginCommandBuffer(*m_shapeBuffer, nullptr), "BeginCommandBuffer"))
			{
				return false;
			}

			nri::CommandBuffer & commandBuffer = *m_shapeBuffer;
			const bool inScope				   = ShapeRendersInScope(shape);
			if (inScope)
			{
				BeginScene(commandBuffer, nri::LoadOp::CLEAR, nri::StoreOp::DISCARD, true);

				Recorder recorder{ *this, commandBuffer };
				recorder.BindPipeline(0);
				recorder.BindMaterial(0);
				recorder.BindMesh(0);
				recorder.Push(PushBlock{});
			}
			else
			{
				const std::array barriers{ Transition(m_sceneColor, kDiscardColor, kColorTarget) };
				Barrier(commandBuffer, barriers);
			}

			void * native = m_core.GetCommandBufferNativeObject(m_shapeBuffer);
			if (rawFirst)
			{
				timing.rawNs = raw::RecordShape(shape, native, commands);
			}

			timing.libraryNs = RecordLibraryShape(commandBuffer, shape, commands);

			if (!rawFirst)
			{
				timing.rawNs = raw::RecordShape(shape, native, commands);
			}

			if (inScope)
			{
				m_core.CmdEndRendering(commandBuffer);
			}

			return Succeeded(m_core.EndCommandBuffer(commandBuffer), "EndCommandBuffer");
		}

		[[nodiscard]] bool Frame(const std::uint32_t draws, FrameTiming & timing) override
		{
			VSNRI_ZONE("nri/Frame");

			const std::uint32_t index = m_frame % kFramesInFlight;
			Slot & slot				  = WaitForSlot(timing);

			const std::uint64_t recordStarted  = Now();
			nri::CommandBuffer & commandBuffer = *slot.buffers.front();
			m_core.ResetCommandAllocator(*slot.allocators.front());
			if (!Succeeded(m_core.BeginCommandBuffer(commandBuffer, nullptr), "BeginCommandBuffer"))
			{
				return false;
			}

			OpenTimestamps(commandBuffer, index);
			RecordFrame(commandBuffer, draws);
			CloseTimestamps(commandBuffer, index);

			if (!Succeeded(m_core.EndCommandBuffer(commandBuffer), "EndCommandBuffer"))
			{
				return false;
			}
			timing.recordNs = Now() - recordStarted;

			const std::uint64_t submitStarted = Now();
			if (!Submit(std::span(slot.buffers.data(), 1), slot.fenceValue))
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
			VSNRI_ZONE("nri/PrepareScene");

			m_sceneMeshes = scene.meshes;
			return CreateSceneResources(scene) && UploadScene(scene) && CreateAssetLayout() && CreateAssetDescriptors(scene) && CreateAssetPipelines();
		}

		[[nodiscard]] bool SceneFrame(const SceneFrameDesc & frame, FrameTiming & timing) override
		{
			VSNRI_ZONE("nri/SceneFrame");

			const std::uint32_t index = m_frame % kFramesInFlight;
			Slot & slot				  = WaitForSlot(timing);

			void * globals = m_core.MapBuffer(*m_globals, std::uint64_t{ index } * kGlobalsStride, sizeof(SceneGlobals));
			if (globals == nullptr)
			{
				std::println(stderr, "nri arm: MapBuffer returned nothing for the scene globals");
				return false;
			}
			std::memcpy(globals, &frame.globals, sizeof(SceneGlobals));
			m_core.UnmapBuffer(*m_globals);

			const std::uint64_t recordStarted  = Now();
			nri::CommandBuffer & commandBuffer = *slot.buffers.front();
			m_core.ResetCommandAllocator(*slot.allocators.front());
			if (!Succeeded(m_core.BeginCommandBuffer(commandBuffer, nullptr), "BeginCommandBuffer"))
			{
				return false;
			}

			OpenTimestamps(commandBuffer, index);
			RecordAssetFrame(commandBuffer, frame, index);
			CloseTimestamps(commandBuffer, index);

			if (!Succeeded(m_core.EndCommandBuffer(commandBuffer), "EndCommandBuffer"))
			{
				return false;
			}
			timing.recordNs = Now() - recordStarted;

			const std::uint64_t submitStarted = Now();
			if (!Submit(std::span(slot.buffers.data(), 1), slot.fenceValue))
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
			VSNRI_ZONE("nri/ThreadedFrame");

			if (threads == 0 || threads > kMaxThreads)
			{
				std::println(stderr, "nri arm: {} threads is outside the 1 to {} this arm records with", threads, kMaxThreads);
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
			if (!Submit(std::span(slot.buffers.data(), threads), slot.fenceValue))
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
			VSNRI_ZONE("nri/ChurnBatch");

			if (count > kChurnBatch)
			{
				std::println(stderr, "nri arm: a churn batch of {} is larger than the {} this arm holds", count, kChurnBatch);
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
			VSNRI_ZONE("nri/SubmitRoundTrip");

			const std::uint64_t started = Now();
			m_core.ResetCommandAllocator(*m_oneShotAllocator);
			if (!Succeeded(m_core.BeginCommandBuffer(*m_oneShotBuffer, nullptr), "BeginCommandBuffer") ||
				!Succeeded(m_core.EndCommandBuffer(*m_oneShotBuffer), "EndCommandBuffer"))
			{
				return false;
			}

			std::uint64_t signaled = 0;
			if (!Submit(std::span(&m_oneShotBuffer, 1), signaled))
			{
				return false;
			}

			m_core.Wait(*m_fence, signaled);
			elapsedNs = Now() - started;
			return true;
		}

		[[nodiscard]] bool Drain() override
		{
			if (m_fence != nullptr)
			{
				m_core.Wait(*m_fence, m_fenceValue);
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
			std::array<nri::CommandAllocator *, kMaxThreads> allocators{};
			std::array<nri::CommandBuffer *, kMaxThreads> buffers{};
			std::uint64_t fenceValue = 0;

			// Its last submission wrote timestamps that belong to the run in progress.
			bool timed = false;
		};

		struct ChunkJob final
		{
			NriArm * arm		  = nullptr;
			Slot * slot			  = nullptr;
			std::uint32_t threads = 0;
			std::uint32_t draws	  = 0;
			std::atomic<bool> failed{ false };
		};

		// The scene templates call these, so NRI sees exactly the calls the Azoth arm sees.
		struct Recorder final
		{
			const NriArm & arm;
			nri::CommandBuffer & commandBuffer;

			void BindPipeline(const std::uint32_t pipeline) const
			{
				arm.m_core.CmdSetPipeline(commandBuffer, *arm.m_scenePipelines.at(pipeline));
			}

			void BindMaterial(const std::uint32_t material) const
			{
				arm.BindSet(commandBuffer, 1, arm.m_materialSets.at(material));
			}

			void BindMesh(const std::uint32_t mesh) const
			{
				const nri::VertexBufferDesc vertices{ arm.m_vertices, MeshVertexOffset(mesh), kVertexStride };
				arm.m_core.CmdSetVertexBuffers(commandBuffer, 0, &vertices, 1);
				arm.m_core.CmdSetIndexBuffer(commandBuffer, *arm.m_indices, MeshIndexOffset(mesh), nri::IndexType::UINT16);
			}

			void Push(const PushBlock & push) const
			{
				const nri::SetRootConstantsDesc constants{ 0, &push, kPushConstantBytes, 0, nri::BindPoint::INHERIT };
				arm.m_core.CmdSetRootConstants(commandBuffer, constants);
			}

			void DrawMesh() const
			{
				const nri::DrawIndexedDesc draw{ kMeshIndices, 1, 0, 0, 0 };
				arm.m_core.CmdDrawIndexed(commandBuffer, draw);
			}

			void BindScenePipeline(const std::uint32_t pipeline) const
			{
				arm.m_core.CmdSetPipeline(commandBuffer, *arm.m_assetPipelines.at(pipeline));
			}

			void BindSceneVertices() const
			{
				const nri::VertexBufferDesc vertices{ arm.m_sceneVertices, 0, kSceneVertexStride };
				arm.m_core.CmdSetVertexBuffers(commandBuffer, 0, &vertices, 1);
			}

			void BindSceneMaterial(const std::uint32_t material) const
			{
				arm.BindSet(commandBuffer, 1, arm.m_assetMaterialSets.at(material));
			}

			void PushModel(const Matrix & model) const
			{
				const nri::SetRootConstantsDesc constants{ 0, model.data(), sizeof(Matrix), 0, nri::BindPoint::INHERIT };
				arm.m_core.CmdSetRootConstants(commandBuffer, constants);
			}

			void DrawSceneMesh(const std::uint32_t mesh) const
			{
				const SceneMesh & range = arm.m_sceneMeshes.at(mesh);
				arm.m_core.CmdDrawIndexed(commandBuffer, nri::DrawIndexedDesc{ range.indexCount, 1, range.firstIndex, range.vertexOffset, 0 });
			}
		};

		[[nodiscard]] bool CreateCommandBuffers()
		{
			const auto create = [this](nri::CommandAllocator *& allocator, nri::CommandBuffer *& buffer)
			{
				return Succeeded(m_core.CreateCommandAllocator(*m_queue, allocator), "CreateCommandAllocator") &&
					   Succeeded(m_core.CreateCommandBuffer(*allocator, buffer), "CreateCommandBuffer");
			};

			for (Slot & slot : m_slots)
			{
				for (std::uint32_t worker = 0; worker < kMaxThreads; ++worker)
				{
					if (!create(slot.allocators.at(worker), slot.buffers.at(worker)))
					{
						return false;
					}
				}
			}

			return create(m_shapeAllocator, m_shapeBuffer) && create(m_oneShotAllocator, m_oneShotBuffer);
		}

		[[nodiscard]] bool CreateUploadBuffer(const nri::BufferDesc & desc, void (*fill)(void *), nri::Buffer *& buffer)
		{
			if (!Succeeded(m_core.CreatePlacedBuffer(*m_device, nullptr, static_cast<std::uint64_t>(nri::MemoryLocation::HOST_UPLOAD), desc, buffer),
					"CreatePlacedBuffer"))
			{
				return false;
			}

			void * mapped = m_core.MapBuffer(*buffer, 0, desc.size);
			if (mapped == nullptr)
			{
				std::println(stderr, "nri arm: MapBuffer returned nothing");
				return false;
			}

			fill(mapped);
			m_core.UnmapBuffer(*buffer);
			return true;
		}

		[[nodiscard]] bool CreateBuffers()
		{
			return CreateUploadBuffer(nri::BufferDesc{ kVertexBytes, 0, nri::BufferUsageBits::VERTEX_BUFFER }, FillVertices, m_vertices) &&
				   CreateUploadBuffer(nri::BufferDesc{ kIndexBytes, 0, nri::BufferUsageBits::INDEX_BUFFER }, FillIndices, m_indices) &&
				   CreateUploadBuffer(nri::BufferDesc{ kObjectBytes, 4 * sizeof(float), nri::BufferUsageBits::SHADER_RESOURCE }, FillObjects, m_objects) &&
				   CreateUploadBuffer(nri::BufferDesc{ kMaterialBytes, 0, nri::BufferUsageBits::CONSTANT_BUFFER }, FillMaterials, m_materials);
		}

		[[nodiscard]] bool CreateTexture(const nri::Format format, const nri::TextureUsageBits usage, const std::uint32_t extent, nri::Texture *& texture)
		{
			nri::TextureDesc desc{};
			desc.type	   = nri::TextureType::TEXTURE_2D;
			desc.usage	   = usage;
			desc.format	   = format;
			desc.width	   = static_cast<nri::Dim_t>(extent);
			desc.height	   = static_cast<nri::Dim_t>(extent);
			desc.depth	   = 1;
			desc.mipNum	   = 1;
			desc.layerNum  = 1;
			desc.sampleNum = 1;

			return Succeeded(
				m_core.CreatePlacedTexture(*m_device, nullptr, static_cast<std::uint64_t>(nri::MemoryLocation::DEVICE), desc, texture), "CreatePlacedTexture");
		}

		[[nodiscard]] bool CreateView(nri::Texture * texture, const nri::TextureView type, const nri::Format format, nri::Descriptor *& view)
		{
			nri::TextureViewDesc desc{};
			desc.texture = texture;
			desc.type	 = type;
			desc.format	 = format;
			desc.planes	 = nri::PlaneBits::ALL;
			return Succeeded(m_core.CreateTextureView(desc, view), "CreateTextureView");
		}

		[[nodiscard]] bool CreateTextures()
		{
			using Usage = nri::TextureUsageBits;

			return CreateTexture(kDepthFormat, Usage::DEPTH_STENCIL_ATTACHMENT | Usage::SHADER_RESOURCE, kShadowExtent, m_shadow) &&
				   CreateTexture(kColorFormat, Usage::COLOR_ATTACHMENT | Usage::SHADER_RESOURCE, kSceneExtent, m_sceneColor) &&
				   CreateTexture(kDepthFormat, Usage::DEPTH_STENCIL_ATTACHMENT, kSceneExtent, m_sceneDepth) &&
				   CreateTexture(kColorFormat, Usage::COLOR_ATTACHMENT, kSceneExtent, m_post) &&
				   CreateView(m_shadow, nri::TextureView::DEPTH_STENCIL_ATTACHMENT, kDepthFormat, m_shadowAttachment) &&
				   CreateView(m_shadow, nri::TextureView::TEXTURE, kDepthFormat, m_shadowTexture) &&
				   CreateView(m_sceneColor, nri::TextureView::COLOR_ATTACHMENT, kColorFormat, m_sceneColorAttachment) &&
				   CreateView(m_sceneColor, nri::TextureView::TEXTURE, kColorFormat, m_sceneColorTexture) &&
				   CreateView(m_sceneDepth, nri::TextureView::DEPTH_STENCIL_ATTACHMENT, kDepthFormat, m_sceneDepthAttachment) &&
				   CreateView(m_post, nri::TextureView::COLOR_ATTACHMENT, kColorFormat, m_postAttachment);
		}

		[[nodiscard]] bool CreateLayouts()
		{
			using Type = nri::DescriptorType;
			using Bits = nri::DescriptorRangeBits;

			const std::array objectRanges{ nri::DescriptorRangeDesc{ 0, 1, Type::STRUCTURED_BUFFER, kGraphicsStages, Bits::NONE } };
			const std::array materialRanges{ nri::DescriptorRangeDesc{ 0, 1, Type::CONSTANT_BUFFER, kGraphicsStages, Bits::NONE } };
			const std::array shadowRanges{
				nri::DescriptorRangeDesc{ 0, 1, Type::TEXTURE, kGraphicsStages, Bits::NONE },
				nri::DescriptorRangeDesc{ 1, 1, Type::SAMPLER, kGraphicsStages, Bits::NONE },
			};
			const std::array sceneSets{
				nri::DescriptorSetDesc{ 0, objectRanges.data(), static_cast<std::uint32_t>(objectRanges.size()), nri::DescriptorSetBits::NONE },
				nri::DescriptorSetDesc{ 1, materialRanges.data(), static_cast<std::uint32_t>(materialRanges.size()), nri::DescriptorSetBits::NONE },
				nri::DescriptorSetDesc{ 2, shadowRanges.data(), static_cast<std::uint32_t>(shadowRanges.size()), nri::DescriptorSetBits::NONE },
			};
			const nri::RootConstantDesc push{ 0, kPushConstantBytes, kGraphicsStages };

			nri::PipelineLayoutDesc scene{};
			scene.rootRegisterSpace = static_cast<std::uint32_t>(sceneSets.size());
			scene.rootConstants		= &push;
			scene.rootConstantNum	= 1;
			scene.descriptorSets	= sceneSets.data();
			scene.descriptorSetNum	= static_cast<std::uint32_t>(sceneSets.size());
			scene.shaderStages		= kGraphicsStages;

			const std::array postRanges{
				nri::DescriptorRangeDesc{ 0, 1, Type::TEXTURE, nri::StageBits::FRAGMENT_SHADER, Bits::NONE },
				nri::DescriptorRangeDesc{ 1, 1, Type::SAMPLER, nri::StageBits::FRAGMENT_SHADER, Bits::NONE },
			};
			const nri::DescriptorSetDesc postSet{ 0, postRanges.data(), static_cast<std::uint32_t>(postRanges.size()), nri::DescriptorSetBits::NONE };

			nri::PipelineLayoutDesc post{};
			post.rootRegisterSpace = 1;
			post.descriptorSets	   = &postSet;
			post.descriptorSetNum  = 1;
			post.shaderStages	   = kGraphicsStages;

			return Succeeded(m_core.CreatePipelineLayout(*m_device, scene, m_sceneLayout), "CreatePipelineLayout(scene)") &&
				   Succeeded(m_core.CreatePipelineLayout(*m_device, post, m_postLayout), "CreatePipelineLayout(post)");
		}

		[[nodiscard]] bool CreateDescriptors()
		{
			nri::SamplerDesc sampler{};
			sampler.filters		 = nri::Filters{ nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::NEAREST, nri::FilterOp::AVERAGE };
			sampler.anisotropy	 = 1;
			sampler.addressModes = nri::AddressModes{ nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE };
			sampler.compareOp	 = nri::CompareOp::NONE;
			if (!Succeeded(m_core.CreateSampler(*m_device, sampler, m_sampler), "CreateSampler"))
			{
				return false;
			}

			const nri::BufferViewDesc objects{ m_objects, nri::BufferView::STRUCTURED_BUFFER, 0, nri::WHOLE_SIZE, nri::Format::UNKNOWN, 4 * sizeof(float) };
			if (!Succeeded(m_core.CreateBufferView(objects, m_objectsView), "CreateBufferView(objects)"))
			{
				return false;
			}

			for (std::uint32_t material = 0; material < kMaterials; ++material)
			{
				const nri::BufferViewDesc view{
					m_materials, nri::BufferView::CONSTANT_BUFFER, std::uint64_t{ material } * kMaterialStride, kMaterialStride, nri::Format::UNKNOWN, 0
				};
				if (!Succeeded(m_core.CreateBufferView(view, m_materialViews.at(material)), "CreateBufferView(material)"))
				{
					return false;
				}
			}

			nri::DescriptorPoolDesc pool{};
			pool.descriptorSetMaxNum	= kMaterials + 4;
			pool.samplerMaxNum			= 2;
			pool.constantBufferMaxNum	= kMaterials + 1;
			pool.textureMaxNum			= 2;
			pool.structuredBufferMaxNum = 1;
			if (!Succeeded(m_core.CreateDescriptorPool(*m_device, pool, m_pool), "CreateDescriptorPool") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_pool, *m_sceneLayout, 0, &m_objectsSet, 1, 0), "AllocateDescriptorSets(objects)") ||
				!Succeeded(
					m_core.AllocateDescriptorSets(*m_pool, *m_sceneLayout, 1, m_materialSets.data(), kMaterials, 0), "AllocateDescriptorSets(materials)") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_pool, *m_sceneLayout, 1, &m_churnSet, 1, 0), "AllocateDescriptorSets(churn)") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_pool, *m_sceneLayout, 2, &m_shadowSet, 1, 0), "AllocateDescriptorSets(shadow)") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_pool, *m_postLayout, 0, &m_postSet, 1, 0), "AllocateDescriptorSets(post)"))
			{
				return false;
			}

			const auto write = [this](nri::DescriptorSet * set, const std::uint32_t range, const nri::Descriptor * descriptor)
			{
				const nri::UpdateDescriptorRangeDesc update{ set, range, 0, &descriptor, 1 };
				m_core.UpdateDescriptorRanges(&update, 1);
			};

			write(m_objectsSet, 0, m_objectsView);
			for (std::uint32_t material = 0; material < kMaterials; ++material)
			{
				write(m_materialSets.at(material), 0, m_materialViews.at(material));
			}
			write(m_churnSet, 0, m_materialViews.front());
			write(m_shadowSet, 0, m_shadowTexture);
			write(m_shadowSet, 1, m_sampler);
			write(m_postSet, 0, m_sceneColorTexture);
			write(m_postSet, 1, m_sampler);
			return true;
		}

		[[nodiscard]] bool CreatePipelines()
		{
			nri::VertexAttributeDesc attribute{};
			attribute.d3d		  = nri::VertexAttributeD3D{ "POSITION", 0 };
			attribute.vk		  = nri::VertexAttributeVK{ 0 };
			attribute.format	  = nri::Format::RGB32_SFLOAT;
			attribute.streamIndex = 0;
			const nri::VertexStreamDesc stream{ 0, nri::VertexStreamStepRate::PER_VERTEX, static_cast<std::uint16_t>(kVertexStride) };
			const nri::VertexInputDesc vertexInput{ &attribute, 1, &stream, 1 };

			const std::array meshShaders{
				nri::ShaderDesc{ nri::StageBits::VERTEX_SHADER, spirv::kMeshVert, sizeof(spirv::kMeshVert), "main" },
				nri::ShaderDesc{ nri::StageBits::FRAGMENT_SHADER, spirv::kMeshFrag, sizeof(spirv::kMeshFrag), "main" },
			};
			const std::array postShaders{
				nri::ShaderDesc{ nri::StageBits::VERTEX_SHADER, spirv::kPostVert, sizeof(spirv::kPostVert), "main" },
				nri::ShaderDesc{ nri::StageBits::FRAGMENT_SHADER, spirv::kPostFrag, sizeof(spirv::kPostFrag), "main" },
			};

			const auto base = [](const nri::PipelineLayout * layout)
			{
				nri::GraphicsPipelineDesc desc{};
				desc.pipelineLayout						 = layout;
				desc.inputAssembly.topology				 = nri::Topology::TRIANGLE_LIST;
				desc.rasterization.fillMode				 = nri::FillMode::SOLID;
				desc.rasterization.cullMode				 = nri::CullMode::NONE;
				desc.rasterization.frontCounterClockwise = true;
				desc.robustness							 = nri::Robustness::DEFAULT;
				return desc;
			};

			for (std::uint32_t variant = 0; variant < kPipelines; ++variant)
			{
				nri::ColorAttachmentDesc color{};
				color.format		 = kColorFormat;
				color.colorBlend	 = nri::BlendDesc{ nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
				color.alphaBlend	 = nri::BlendDesc{ nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
				color.colorWriteMask = nri::ColorWriteBits::RGBA;
				color.blendEnabled	 = VariantBlends(variant);

				nri::GraphicsPipelineDesc desc		 = base(m_sceneLayout);
				desc.vertexInput					 = &vertexInput;
				desc.rasterization.cullMode			 = VariantCulls(variant) ? nri::CullMode::BACK : nri::CullMode::NONE;
				desc.outputMerger.colors			 = &color;
				desc.outputMerger.colorNum			 = 1;
				desc.outputMerger.depth.compareOp	 = VariantLessEqual(variant) ? nri::CompareOp::LESS_EQUAL : nri::CompareOp::LESS;
				desc.outputMerger.depth.write		 = true;
				desc.outputMerger.depthStencilFormat = kDepthFormat;
				desc.shaders						 = meshShaders.data();
				desc.shaderNum						 = static_cast<std::uint32_t>(meshShaders.size());
				if (!Succeeded(m_core.CreateGraphicsPipeline(*m_device, desc, m_scenePipelines.at(variant)), "CreateGraphicsPipeline(scene)"))
				{
					return false;
				}
			}

			nri::GraphicsPipelineDesc shadow	   = base(m_sceneLayout);
			shadow.vertexInput					   = &vertexInput;
			shadow.outputMerger.depth.compareOp	   = nri::CompareOp::LESS;
			shadow.outputMerger.depth.write		   = true;
			shadow.outputMerger.depthStencilFormat = kDepthFormat;
			shadow.shaders						   = meshShaders.data();
			shadow.shaderNum					   = 1;
			if (!Succeeded(m_core.CreateGraphicsPipeline(*m_device, shadow, m_shadowPipeline), "CreateGraphicsPipeline(shadow)"))
			{
				return false;
			}

			nri::ColorAttachmentDesc postColor{};
			postColor.format		 = kColorFormat;
			postColor.colorWriteMask = nri::ColorWriteBits::RGBA;

			nri::GraphicsPipelineDesc post	  = base(m_postLayout);
			post.outputMerger.colors		  = &postColor;
			post.outputMerger.colorNum		  = 1;
			post.outputMerger.depth.compareOp = nri::CompareOp::NONE;
			post.shaders					  = postShaders.data();
			post.shaderNum					  = static_cast<std::uint32_t>(postShaders.size());
			return Succeeded(m_core.CreateGraphicsPipeline(*m_device, post, m_postPipeline), "CreateGraphicsPipeline(post)");
		}

		// The threaded frame samples the shadow map without drawing it first, so it has to start in a readable layout.
		[[nodiscard]] bool PrepareLayouts()
		{
			m_core.ResetCommandAllocator(*m_oneShotAllocator);
			if (!Succeeded(m_core.BeginCommandBuffer(*m_oneShotBuffer, nullptr), "BeginCommandBuffer"))
			{
				return false;
			}

			const std::array barriers{
				Transition(m_shadow, kNothing, kSampled),
				Transition(m_sceneColor, kNothing, kSampled),
			};
			Barrier(*m_oneShotBuffer, barriers);

			std::uint64_t signaled = 0;
			if (!Succeeded(m_core.EndCommandBuffer(*m_oneShotBuffer), "EndCommandBuffer") || !Submit(std::span(&m_oneShotBuffer, 1), signaled))
			{
				return false;
			}

			m_core.Wait(*m_fence, signaled);
			return true;
		}

		[[nodiscard]] bool PrepareRaw()
		{
			raw::Handles handles{};
			handles.instance			= m_wrapper.GetInstanceVK(*m_device);
			handles.physicalDevice		= m_wrapper.GetPhysicalDeviceVK(*m_device);
			handles.device				= m_core.GetDeviceNativeObject(m_device);
			handles.getInstanceProcAddr = m_wrapper.GetInstanceProcAddrVK(*m_device);
			handles.getDeviceProcAddr	= m_wrapper.GetDeviceProcAddrVK(*m_device);
			handles.barrierImage		= m_core.GetTextureNativeObject(m_sceneColor);
			handles.materialBuffer		= m_core.GetBufferNativeObject(m_materials);

			m_identity.library		  = "NRI";
			m_identity.libraryVersion = std::format("{} ({})", NRI_VERSION, NRI_VERSION_DATE);

			return raw::Prepare(handles) && raw::Identify(handles, m_identity);
		}

		void Barrier(nri::CommandBuffer & commandBuffer, const std::span<const nri::TextureBarrierDesc> textures) const
		{
			nri::BarrierDesc barrier{};
			barrier.textures   = textures.data();
			barrier.textureNum = static_cast<std::uint32_t>(textures.size());
			m_core.CmdBarrier(commandBuffer, barrier);
		}

		void BindSet(nri::CommandBuffer & commandBuffer, const std::uint32_t set, const nri::DescriptorSet * descriptorSet) const
		{
			const nri::SetDescriptorSetDesc desc{ set, descriptorSet, nri::BindPoint::INHERIT };
			m_core.CmdSetDescriptorSet(commandBuffer, desc);
		}

		void SetTarget(nri::CommandBuffer & commandBuffer, const std::uint32_t extent) const
		{
			const nri::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(extent), static_cast<float>(extent), 0.0f, 1.0f, false };
			const nri::Rect scissor{ 0, 0, static_cast<nri::Dim_t>(extent), static_cast<nri::Dim_t>(extent) };
			m_core.CmdSetViewports(commandBuffer, &viewport, 1);
			m_core.CmdSetScissors(commandBuffer, &scissor, 1);
		}

		// Opens the scene pass with the per-pass bindings every recording path shares.
		void BeginScene(nri::CommandBuffer & commandBuffer, const nri::LoadOp load, const nri::StoreOp depthStore, const bool discard) const
		{
			if (discard)
			{
				const std::array barriers{
					Transition(m_sceneColor, kDiscardColor, kColorTarget),
					Transition(m_sceneDepth, kDiscardDepth, kDepthTarget),
				};
				Barrier(commandBuffer, barriers);
			}
			else
			{
				const std::array barriers{
					Transition(m_sceneColor, kColorTarget, kColorTarget),
					Transition(m_sceneDepth, kDepthTarget, kDepthTarget),
				};
				Barrier(commandBuffer, barriers);
			}

			nri::AttachmentDesc color{};
			color.descriptor		 = m_sceneColorAttachment;
			color.clearValue.color.f = nri::Color32f{ 0.0f, 0.0f, 0.0f, 1.0f };
			color.loadOp			 = load;
			color.storeOp			 = nri::StoreOp::STORE;

			nri::AttachmentDesc depth{};
			depth.descriptor			  = m_sceneDepthAttachment;
			depth.clearValue.depthStencil = nri::DepthStencil{ 1.0f, 0 };
			depth.loadOp				  = load;
			depth.storeOp				  = depthStore;

			nri::RenderingDesc rendering{};
			rendering.colors   = &color;
			rendering.colorNum = 1;
			rendering.depth	   = depth;
			m_core.CmdBeginRendering(commandBuffer, rendering);

			SetTarget(commandBuffer, kSceneExtent);
			m_core.CmdSetPipelineLayout(commandBuffer, nri::BindPoint::GRAPHICS, *m_sceneLayout);
			BindSet(commandBuffer, 0, m_objectsSet);
			BindSet(commandBuffer, 2, m_shadowSet);
		}

		void RecordFrame(nri::CommandBuffer & commandBuffer, const std::uint32_t draws) const
		{
			Recorder recorder{ *this, commandBuffer };

			const std::array opening{
				Transition(m_shadow, kDiscardDepth, kDepthTarget),
				Transition(m_post, kDiscardColor, kColorTarget),
			};
			Barrier(commandBuffer, opening);

			nri::AttachmentDesc shadowDepth{};
			shadowDepth.descriptor				= m_shadowAttachment;
			shadowDepth.clearValue.depthStencil = nri::DepthStencil{ 1.0f, 0 };
			shadowDepth.loadOp					= nri::LoadOp::CLEAR;
			shadowDepth.storeOp					= nri::StoreOp::STORE;

			nri::RenderingDesc shadowPass{};
			shadowPass.depth = shadowDepth;
			m_core.CmdBeginRendering(commandBuffer, shadowPass);
			SetTarget(commandBuffer, kShadowExtent);
			m_core.CmdSetPipelineLayout(commandBuffer, nri::BindPoint::GRAPHICS, *m_sceneLayout);
			m_core.CmdSetPipeline(commandBuffer, *m_shadowPipeline);
			BindSet(commandBuffer, 0, m_objectsSet);
			RecordShadowDraws(recorder, draws);
			m_core.CmdEndRendering(commandBuffer);

			const std::array shadowRead{ Transition(m_shadow, kDepthTarget, kSampled) };
			Barrier(commandBuffer, shadowRead);

			BeginScene(commandBuffer, nri::LoadOp::CLEAR, nri::StoreOp::DISCARD, true);
			RecordSceneDraws(recorder, 0, draws, draws);
			m_core.CmdEndRendering(commandBuffer);

			RecordPost(commandBuffer);
		}

		// The scene pass the way NRI's SceneViewer records it, then the same post pass as the synthetic frame.
		void RecordAssetFrame(nri::CommandBuffer & commandBuffer, const SceneFrameDesc & frame, const std::uint32_t index) const
		{
			const std::array opening{
				Transition(m_sceneColor, kDiscardColor, kColorTarget),
				Transition(m_sceneDepth, kDiscardDepth, kDepthTarget),
				Transition(m_post, kDiscardColor, kColorTarget),
			};
			Barrier(commandBuffer, opening);

			nri::AttachmentDesc color{};
			color.descriptor		 = m_sceneColorAttachment;
			color.clearValue.color.f = nri::Color32f{ 0.0f, 0.0f, 0.0f, 1.0f };
			color.loadOp			 = nri::LoadOp::CLEAR;
			color.storeOp			 = nri::StoreOp::STORE;

			nri::AttachmentDesc depth{};
			depth.descriptor			  = m_sceneDepthAttachment;
			depth.clearValue.depthStencil = nri::DepthStencil{ 1.0f, 0 };
			depth.loadOp				  = nri::LoadOp::CLEAR;
			depth.storeOp				  = nri::StoreOp::DISCARD;

			nri::RenderingDesc rendering{};
			rendering.colors   = &color;
			rendering.colorNum = 1;
			rendering.depth	   = depth;
			m_core.CmdBeginRendering(commandBuffer, rendering);

			SetTarget(commandBuffer, kSceneExtent);
			m_core.CmdSetPipelineLayout(commandBuffer, nri::BindPoint::GRAPHICS, *m_assetLayout);
			BindSet(commandBuffer, 0, m_globalSets.at(index));
			m_core.CmdSetIndexBuffer(commandBuffer, *m_sceneIndices, 0, nri::IndexType::UINT32);

			Recorder recorder{ *this, commandBuffer };
			RecordSceneAssetDraws(recorder, frame);
			m_core.CmdEndRendering(commandBuffer);

			RecordPost(commandBuffer);
		}

		void RecordPost(nri::CommandBuffer & commandBuffer) const
		{
			const std::array sceneRead{ Transition(m_sceneColor, kColorTarget, kSampled) };
			Barrier(commandBuffer, sceneRead);

			nri::AttachmentDesc postColor{};
			postColor.descriptor		 = m_postAttachment;
			postColor.clearValue.color.f = nri::Color32f{ 0.0f, 0.0f, 0.0f, 1.0f };
			postColor.loadOp			 = nri::LoadOp::CLEAR;
			postColor.storeOp			 = nri::StoreOp::STORE;

			nri::RenderingDesc postPass{};
			postPass.colors	  = &postColor;
			postPass.colorNum = 1;
			m_core.CmdBeginRendering(commandBuffer, postPass);
			SetTarget(commandBuffer, kSceneExtent);
			m_core.CmdSetPipelineLayout(commandBuffer, nri::BindPoint::GRAPHICS, *m_postLayout);
			m_core.CmdSetPipeline(commandBuffer, *m_postPipeline);
			BindSet(commandBuffer, 0, m_postSet);
			m_core.CmdDraw(commandBuffer, nri::DrawDesc{ 3, 1, 0, 0 });
			m_core.CmdEndRendering(commandBuffer);
		}

		// Each worker records its share of the scene into its own command buffer, and the first one clears.
		[[nodiscard]] bool RecordChunk(Slot & slot, const std::uint32_t worker, const std::uint32_t threads, const std::uint32_t draws) const
		{
			VSNRI_ZONE("nri/RecordChunk");

			const auto first = static_cast<std::uint32_t>(std::uint64_t{ draws } * worker / threads);
			const auto last	 = static_cast<std::uint32_t>(std::uint64_t{ draws } * (worker + 1) / threads);

			nri::CommandBuffer & commandBuffer = *slot.buffers.at(worker);
			m_core.ResetCommandAllocator(*slot.allocators.at(worker));
			if (!Succeeded(m_core.BeginCommandBuffer(commandBuffer, nullptr), "BeginCommandBuffer"))
			{
				return false;
			}

			const bool opens = worker == 0;
			BeginScene(commandBuffer, opens ? nri::LoadOp::CLEAR : nri::LoadOp::LOAD, nri::StoreOp::STORE, opens);

			Recorder recorder{ *this, commandBuffer };
			RecordSceneDraws(recorder, first, last - first, draws);
			m_core.CmdEndRendering(commandBuffer);

			return Succeeded(m_core.EndCommandBuffer(commandBuffer), "EndCommandBuffer");
		}

		[[nodiscard]] std::uint64_t RecordLibraryShape(nri::CommandBuffer & commandBuffer, const Shape shape, const std::uint32_t commands) const
		{
			constexpr auto kExtent = static_cast<float>(kSceneExtent);
			constexpr auto kHalf   = static_cast<nri::Dim_t>(kSceneExtent / 2);
			const std::array viewports{
				nri::Viewport{ 0.0f, 0.0f, kExtent, kExtent, 0.0f, 1.0f, false },
				nri::Viewport{ 0.0f, 0.0f, kExtent / 2.0f, kExtent / 2.0f, 0.0f, 1.0f, false },
			};
			const std::array scissors{
				nri::Rect{ 0, 0, static_cast<nri::Dim_t>(kSceneExtent), static_cast<nri::Dim_t>(kSceneExtent) },
				nri::Rect{ 0, 0, kHalf, kHalf },
			};

			std::array<nri::TextureBarrierDesc, 2> textures{
				Transition(m_sceneColor, kColorTarget, kSampled),
				Transition(m_sceneColor, kSampled, kColorTarget),
			};
			std::array<nri::BarrierDesc, 2> barriers{};
			for (std::size_t direction = 0; direction < barriers.size(); ++direction)
			{
				barriers.at(direction).textures	  = &textures.at(direction);
				barriers.at(direction).textureNum = 1;
			}

			PushBlock push{};

			const std::uint64_t started = Now();
			switch (shape)
			{
			case Shape::eSetViewport:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdSetViewports(commandBuffer, &viewports.at(index & 1u), 1);
				}
				break;

			case Shape::eSetScissor:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdSetScissors(commandBuffer, &scissors.at(index & 1u), 1);
				}
				break;

			case Shape::ePushConstants:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					push.object = index;
					const nri::SetRootConstantsDesc constants{ 0, &push, kPushConstantBytes, 0, nri::BindPoint::INHERIT };
					m_core.CmdSetRootConstants(commandBuffer, constants);
				}
				break;

			case Shape::eBindDescriptorSet:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					BindSet(commandBuffer, 1, m_materialSets.at(index & 1u));
				}
				break;

			case Shape::eSetPipeline:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdSetPipeline(commandBuffer, *m_scenePipelines.at(index & 1u));
				}
				break;

			case Shape::eDraw:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdDraw(commandBuffer, nri::DrawDesc{ 3, 1, 0, 0 });
				}
				break;

			case Shape::eDrawIndexed:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdDrawIndexed(commandBuffer, nri::DrawIndexedDesc{ 3, 1, 0, 0, 0 });
				}
				break;

			case Shape::eBarrier:
				for (std::uint32_t index = 0; index < commands; ++index)
				{
					m_core.CmdBarrier(commandBuffer, barriers.at(index & 1u));
				}
				break;
			}

			return Now() - started;
		}

		[[nodiscard]] Slot & WaitForSlot(FrameTiming & timing)
		{
			Slot & slot = m_slots.at(m_frame % kFramesInFlight);

			const std::uint64_t started = Now();
			m_core.Wait(*m_fence, slot.fenceValue);
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

		void OpenTimestamps(nri::CommandBuffer & commandBuffer, const std::uint32_t index) const
		{
			m_core.CmdResetQueries(commandBuffer, *m_timestamps, FirstTimestamp(index), kFrameTimestamps);
			m_core.CmdEndQuery(commandBuffer, *m_timestamps, FirstTimestamp(index));
		}

		void CloseTimestamps(nri::CommandBuffer & commandBuffer, const std::uint32_t index) const
		{
			m_core.CmdEndQuery(commandBuffer, *m_timestamps, FirstTimestamp(index) + 1);
			m_core.CmdCopyQueries(
				commandBuffer, *m_timestamps, FirstTimestamp(index), kFrameTimestamps, *m_timestampReadback, FirstTimestamp(index) * sizeof(std::uint64_t));
		}

		void ReadTimestamps(const std::uint32_t index, FrameTiming & timing) const
		{
			std::array<std::uint64_t, kFrameTimestamps> stamps{};
			const void * mapped = m_core.MapBuffer(*m_timestampReadback, FirstTimestamp(index) * sizeof(std::uint64_t), sizeof(stamps));
			if (mapped == nullptr)
			{
				return;
			}

			std::memcpy(stamps.data(), mapped, sizeof(stamps));
			m_core.UnmapBuffer(*m_timestampReadback);

			timing.gpuValid = stamps[1] >= stamps[0];
			timing.gpuNs	= static_cast<std::uint64_t>(static_cast<double>(stamps[1] - stamps[0]) * m_timestampPeriodNs);
		}

		[[nodiscard]] bool CreateTimestamps()
		{
			const nri::QueryPoolDesc pool{ nri::QueryType::TIMESTAMP, kFramesInFlight * kFrameTimestamps };
			const nri::BufferDesc readback{ std::uint64_t{ kFramesInFlight } * kFrameTimestamps * sizeof(std::uint64_t), 0, nri::BufferUsageBits::NONE };
			if (!Succeeded(m_core.CreateQueryPool(*m_device, pool, m_timestamps), "CreateQueryPool") ||
				!Succeeded(m_core.CreatePlacedBuffer(
							   *m_device, nullptr, static_cast<std::uint64_t>(nri::MemoryLocation::HOST_READBACK), readback, m_timestampReadback),
					"CreatePlacedBuffer(readback)"))
			{
				return false;
			}

			m_timestampPeriodNs = 1e9 / static_cast<double>(m_core.GetDeviceDesc(*m_device).other.timestampFrequencyHz);
			return true;
		}

		[[nodiscard]] bool CreateSceneResources(const SceneAsset & scene)
		{
			const auto placed = [this](const nri::MemoryLocation location, const nri::BufferDesc & desc, nri::Buffer *& buffer)
			{
				return Succeeded(
					m_core.CreatePlacedBuffer(*m_device, nullptr, static_cast<std::uint64_t>(location), desc, buffer), "CreatePlacedBuffer(scene)");
			};

			const nri::BufferDesc vertices{ scene.vertices.size() * sizeof(SceneVertex), 0, nri::BufferUsageBits::VERTEX_BUFFER };
			const nri::BufferDesc indices{ scene.indices.size() * sizeof(std::uint32_t), 0, nri::BufferUsageBits::INDEX_BUFFER };
			const nri::BufferDesc globals{ std::uint64_t{ kGlobalsStride } * kFramesInFlight, 0, nri::BufferUsageBits::CONSTANT_BUFFER };
			if (!placed(nri::MemoryLocation::DEVICE, vertices, m_sceneVertices) || !placed(nri::MemoryLocation::DEVICE, indices, m_sceneIndices) ||
				!placed(nri::MemoryLocation::HOST_UPLOAD, globals, m_globals))
			{
				return false;
			}

			m_sceneTextures.assign(scene.textures.size(), nullptr);
			m_sceneTextureViews.assign(scene.textures.size(), nullptr);
			for (std::size_t index = 0; index < scene.textures.size(); ++index)
			{
				const SceneTexture & source = scene.textures.at(index);

				nri::TextureDesc desc{};
				desc.type	   = nri::TextureType::TEXTURE_2D;
				desc.usage	   = nri::TextureUsageBits::SHADER_RESOURCE;
				desc.format	   = SceneFormat(source.format);
				desc.width	   = static_cast<nri::Dim_t>(source.width);
				desc.height	   = static_cast<nri::Dim_t>(source.height);
				desc.depth	   = 1;
				desc.mipNum	   = static_cast<nri::Dim_t>(source.mips.size());
				desc.layerNum  = 1;
				desc.sampleNum = 1;

				if (!Succeeded(m_core.CreatePlacedTexture(
								   *m_device, nullptr, static_cast<std::uint64_t>(nri::MemoryLocation::DEVICE), desc, m_sceneTextures.at(index)),
						"CreatePlacedTexture(scene)") ||
					!CreateView(m_sceneTextures.at(index), nri::TextureView::TEXTURE, desc.format, m_sceneTextureViews.at(index)))
				{
					return false;
				}
			}

			return true;
		}

		// Through NRI's own upload helper, the path its samples take.
		[[nodiscard]] bool UploadScene(const SceneAsset & scene)
		{
			std::size_t mips = 0;
			for (const SceneTexture & texture : scene.textures)
			{
				mips += texture.mips.size();
			}

			std::vector<nri::TextureSubresourceUploadDesc> subresources;
			subresources.reserve(mips);
			std::vector<nri::TextureUploadDesc> textures;
			textures.reserve(scene.textures.size());
			for (std::size_t index = 0; index < scene.textures.size(); ++index)
			{
				const SceneTexture & texture = scene.textures.at(index);
				const std::size_t first		 = subresources.size();
				for (const SceneMip & mip : texture.mips)
				{
					subresources.push_back(nri::TextureSubresourceUploadDesc{ &texture.bytes.at(mip.offset), 1, mip.rowPitch, mip.rowPitch * mip.rows });
				}

				textures.push_back(nri::TextureUploadDesc{ &subresources.at(first), m_sceneTextures.at(index), kSampled, nri::PlaneBits::ALL });
			}

			const std::array buffers{
				nri::BufferUploadDesc{ scene.vertices.data(), m_sceneVertices, kVertexInput },
				nri::BufferUploadDesc{ scene.indices.data(), m_sceneIndices, kIndexInput },
			};

			return Succeeded(
				m_helper.UploadData(
					*m_queue, textures.data(), static_cast<std::uint32_t>(textures.size()), buffers.data(), static_cast<std::uint32_t>(buffers.size())),
				"UploadData");
		}

		[[nodiscard]] bool CreateAssetLayout()
		{
			using Type = nri::DescriptorType;
			using Bits = nri::DescriptorRangeBits;

			const std::array globalRanges{
				nri::DescriptorRangeDesc{ 0, 1, Type::CONSTANT_BUFFER, kGraphicsStages, Bits::NONE },
				nri::DescriptorRangeDesc{ 1, 1, Type::SAMPLER, nri::StageBits::FRAGMENT_SHADER, Bits::NONE },
			};
			const std::array materialRanges{
				nri::DescriptorRangeDesc{ 0, kTexturesAMaterial, Type::TEXTURE, nri::StageBits::FRAGMENT_SHADER, Bits::ARRAY },
			};
			const std::array sets{
				nri::DescriptorSetDesc{ 0, globalRanges.data(), static_cast<std::uint32_t>(globalRanges.size()), nri::DescriptorSetBits::NONE },
				nri::DescriptorSetDesc{ 1, materialRanges.data(), static_cast<std::uint32_t>(materialRanges.size()), nri::DescriptorSetBits::NONE },
			};
			const nri::RootConstantDesc model{ 0, sizeof(Matrix), nri::StageBits::VERTEX_SHADER };

			nri::PipelineLayoutDesc desc{};
			desc.rootRegisterSpace = static_cast<std::uint32_t>(sets.size());
			desc.rootConstants	   = &model;
			desc.rootConstantNum   = 1;
			desc.descriptorSets	   = sets.data();
			desc.descriptorSetNum  = static_cast<std::uint32_t>(sets.size());
			desc.shaderStages	   = kGraphicsStages;
			return Succeeded(m_core.CreatePipelineLayout(*m_device, desc, m_assetLayout), "CreatePipelineLayout(asset)");
		}

		[[nodiscard]] bool CreateAssetDescriptors(const SceneAsset & scene)
		{
			nri::SamplerDesc sampler{};
			sampler.filters		 = nri::Filters{ nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::LINEAR, nri::FilterOp::AVERAGE };
			sampler.anisotropy	 = 1;
			sampler.mipMax		 = kSceneSamplerMaxLod;
			sampler.addressModes = nri::AddressModes{ nri::AddressMode::REPEAT, nri::AddressMode::REPEAT, nri::AddressMode::REPEAT };
			sampler.compareOp	 = nri::CompareOp::NONE;
			if (!Succeeded(m_core.CreateSampler(*m_device, sampler, m_assetSampler), "CreateSampler(asset)"))
			{
				return false;
			}

			for (std::uint32_t index = 0; index < kFramesInFlight; ++index)
			{
				const nri::BufferViewDesc view{
					m_globals, nri::BufferView::CONSTANT_BUFFER, std::uint64_t{ index } * kGlobalsStride, kGlobalsStride, nri::Format::UNKNOWN, 0
				};
				if (!Succeeded(m_core.CreateBufferView(view, m_globalsViews.at(index)), "CreateBufferView(globals)"))
				{
					return false;
				}
			}

			const auto materials = static_cast<std::uint32_t>(scene.materials.size());

			nri::DescriptorPoolDesc pool{};
			pool.descriptorSetMaxNum  = materials + kFramesInFlight;
			pool.samplerMaxNum		  = kFramesInFlight;
			pool.constantBufferMaxNum = kFramesInFlight;
			pool.textureMaxNum		  = materials * kTexturesAMaterial;

			m_assetMaterialSets.assign(materials, nullptr);
			if (!Succeeded(m_core.CreateDescriptorPool(*m_device, pool, m_assetPool), "CreateDescriptorPool(asset)") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_assetPool, *m_assetLayout, 0, m_globalSets.data(), kFramesInFlight, 0),
					"AllocateDescriptorSets(globals)") ||
				!Succeeded(m_core.AllocateDescriptorSets(*m_assetPool, *m_assetLayout, 1, m_assetMaterialSets.data(), materials, 0),
					"AllocateDescriptorSets(asset materials)"))
			{
				return false;
			}

			for (std::uint32_t index = 0; index < kFramesInFlight; ++index)
			{
				const nri::Descriptor * view	= m_globalsViews.at(index);
				const nri::Descriptor * sampled = m_assetSampler;
				const std::array updates{
					nri::UpdateDescriptorRangeDesc{ m_globalSets.at(index), 0, 0, &view, 1 },
					nri::UpdateDescriptorRangeDesc{ m_globalSets.at(index), 1, 0, &sampled, 1 },
				};
				m_core.UpdateDescriptorRanges(updates.data(), static_cast<std::uint32_t>(updates.size()));
			}

			for (std::uint32_t material = 0; material < materials; ++material)
			{
				std::array<const nri::Descriptor *, kTexturesAMaterial> views{};
				for (std::uint32_t slot = 0; slot < kTexturesAMaterial; ++slot)
				{
					views.at(slot) = m_sceneTextureViews.at(scene.materials.at(material).textures.at(slot));
				}

				const nri::UpdateDescriptorRangeDesc update{ m_assetMaterialSets.at(material), 0, 0, views.data(), kTexturesAMaterial };
				m_core.UpdateDescriptorRanges(&update, 1);
			}

			return true;
		}

		[[nodiscard]] bool CreateAssetPipelines()
		{
			const std::array attributes{
				nri::VertexAttributeDesc{ nri::VertexAttributeD3D{ "POSITION", 0 },
					nri::VertexAttributeVK{ 0 },
					static_cast<std::uint32_t>(offsetof(SceneVertex, position)),
					nri::Format::RGB32_SFLOAT,
					0 },
				nri::VertexAttributeDesc{ nri::VertexAttributeD3D{ "NORMAL", 0 },
					nri::VertexAttributeVK{ 1 },
					static_cast<std::uint32_t>(offsetof(SceneVertex, normal)),
					nri::Format::RGB32_SFLOAT,
					0 },
				nri::VertexAttributeDesc{ nri::VertexAttributeD3D{ "TEXCOORD", 0 },
					nri::VertexAttributeVK{ 2 },
					static_cast<std::uint32_t>(offsetof(SceneVertex, uv)),
					nri::Format::RG32_SFLOAT,
					0 },
			};
			const nri::VertexStreamDesc stream{ 0, nri::VertexStreamStepRate::PER_VERTEX, static_cast<std::uint16_t>(kSceneVertexStride) };
			const nri::VertexInputDesc vertexInput{ attributes.data(), static_cast<std::uint8_t>(attributes.size()), &stream, 1 };

			const std::array shaders{
				nri::ShaderDesc{ nri::StageBits::VERTEX_SHADER, spirv::kSceneVert, sizeof(spirv::kSceneVert), "main" },
				nri::ShaderDesc{ nri::StageBits::FRAGMENT_SHADER, spirv::kSceneFrag, sizeof(spirv::kSceneFrag), "main" },
			};

			nri::ColorAttachmentDesc color{};
			color.format		 = kColorFormat;
			color.colorWriteMask = nri::ColorWriteBits::RGBA;

			for (std::uint32_t pipeline = 0; pipeline < kScenePipelines; ++pipeline)
			{
				nri::GraphicsPipelineDesc desc{};
				desc.pipelineLayout						 = m_assetLayout;
				desc.vertexInput						 = &vertexInput;
				desc.inputAssembly.topology				 = nri::Topology::TRIANGLE_LIST;
				desc.rasterization.fillMode				 = nri::FillMode::SOLID;
				desc.rasterization.cullMode				 = pipeline == 0 ? nri::CullMode::BACK : nri::CullMode::NONE;
				desc.rasterization.frontCounterClockwise = true;
				desc.outputMerger.colors				 = &color;
				desc.outputMerger.colorNum				 = 1;
				desc.outputMerger.depth.compareOp		 = nri::CompareOp::LESS;
				desc.outputMerger.depth.write			 = true;
				desc.outputMerger.depthStencilFormat	 = kDepthFormat;
				desc.shaders							 = shaders.data();
				desc.shaderNum							 = static_cast<std::uint32_t>(shaders.size());
				desc.robustness							 = nri::Robustness::DEFAULT;
				if (!Succeeded(m_core.CreateGraphicsPipeline(*m_device, desc, m_assetPipelines.at(pipeline)), "CreateGraphicsPipeline(asset)"))
				{
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool Submit(const std::span<nri::CommandBuffer * const> buffers, std::uint64_t & signaled)
		{
			const nri::FenceSubmitDesc signal{ m_fence, ++m_fenceValue, nri::StageBits::ALL };

			nri::QueueSubmitDesc submit{};
			submit.commandBuffers	= buffers.data();
			submit.commandBufferNum = static_cast<std::uint32_t>(buffers.size());
			submit.signalFences		= &signal;
			submit.signalFenceNum	= 1;

			signaled = m_fenceValue;
			return Succeeded(m_core.QueueSubmit(*m_queue, submit), "QueueSubmit");
		}

		[[nodiscard]] bool ChurnBuffers(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			std::array<nri::Buffer *, kChurnBatch> buffers{};
			const nri::BufferDesc desc{ kChurnBufferBytes, 0, nri::BufferUsageBits::SHADER_RESOURCE };

			bool created				= true;
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count && created; ++index)
			{
				created =
					Succeeded(m_core.CreatePlacedBuffer(*m_device, nullptr, static_cast<std::uint64_t>(nri::MemoryLocation::DEVICE), desc, buffers.at(index)),
						"CreatePlacedBuffer(churn)");
			}
			for (nri::Buffer *& buffer : buffers)
			{
				Release(m_core.DestroyBuffer, buffer);
			}
			elapsedNs = Now() - started;

			return created;
		}

		[[nodiscard]] bool ChurnTextures(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			std::array<nri::Texture *, kChurnBatch> textures{};
			std::array<nri::Descriptor *, kChurnBatch> views{};

			bool created				= true;
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count && created; ++index)
			{
				created = CreateTexture(kColorFormat, nri::TextureUsageBits::SHADER_RESOURCE, kChurnTextureExtent, textures.at(index)) &&
						  CreateView(textures.at(index), nri::TextureView::TEXTURE, kColorFormat, views.at(index));
			}
			for (std::uint32_t index = 0; index < kChurnBatch; ++index)
			{
				Release(m_core.DestroyDescriptor, views.at(index));
				Release(m_core.DestroyTexture, textures.at(index));
			}
			elapsedNs = Now() - started;

			return created;
		}

		[[nodiscard]] bool ChurnDescriptorWrites(const std::uint32_t count, std::uint64_t & elapsedNs)
		{
			const std::uint64_t started = Now();
			for (std::uint32_t index = 0; index < count; ++index)
			{
				const nri::Descriptor * view = m_materialViews.at(index & 1u);
				const nri::UpdateDescriptorRangeDesc update{ m_churnSet, 0, 0, &view, 1 };
				m_core.UpdateDescriptorRanges(&update, 1);
			}
			elapsedNs = Now() - started;

			return true;
		}

		nri::CoreInterface m_core{};
		nri::WrapperVKInterface m_wrapper{};
		nri::Device * m_device = nullptr;
		nri::Queue * m_queue   = nullptr;
		nri::Fence * m_fence   = nullptr;

		std::uint64_t m_fenceValue = 0;
		std::uint32_t m_frame	   = 0;

		std::array<Slot, kFramesInFlight> m_slots{};
		nri::CommandAllocator * m_shapeAllocator   = nullptr;
		nri::CommandBuffer * m_shapeBuffer		   = nullptr;
		nri::CommandAllocator * m_oneShotAllocator = nullptr;
		nri::CommandBuffer * m_oneShotBuffer	   = nullptr;

		nri::Buffer * m_vertices  = nullptr;
		nri::Buffer * m_indices	  = nullptr;
		nri::Buffer * m_objects	  = nullptr;
		nri::Buffer * m_materials = nullptr;

		nri::Texture * m_shadow		= nullptr;
		nri::Texture * m_sceneColor = nullptr;
		nri::Texture * m_sceneDepth = nullptr;
		nri::Texture * m_post		= nullptr;

		nri::Descriptor * m_shadowAttachment	 = nullptr;
		nri::Descriptor * m_shadowTexture		 = nullptr;
		nri::Descriptor * m_sceneColorAttachment = nullptr;
		nri::Descriptor * m_sceneColorTexture	 = nullptr;
		nri::Descriptor * m_sceneDepthAttachment = nullptr;
		nri::Descriptor * m_postAttachment		 = nullptr;
		nri::Descriptor * m_sampler				 = nullptr;
		nri::Descriptor * m_objectsView			 = nullptr;
		std::array<nri::Descriptor *, kMaterials> m_materialViews{};

		nri::PipelineLayout * m_sceneLayout = nullptr;
		nri::PipelineLayout * m_postLayout	= nullptr;
		nri::DescriptorPool * m_pool		= nullptr;

		nri::DescriptorSet * m_objectsSet = nullptr;
		std::array<nri::DescriptorSet *, kMaterials> m_materialSets{};
		nri::DescriptorSet * m_churnSet	 = nullptr;
		nri::DescriptorSet * m_shadowSet = nullptr;
		nri::DescriptorSet * m_postSet	 = nullptr;

		std::array<nri::Pipeline *, kPipelines> m_scenePipelines{};
		nri::Pipeline * m_shadowPipeline = nullptr;
		nri::Pipeline * m_postPipeline	 = nullptr;

		nri::HelperInterface m_helper{};
		nri::QueryPool * m_timestamps	  = nullptr;
		nri::Buffer * m_timestampReadback = nullptr;
		double m_timestampPeriodNs		  = 1.0;

		std::vector<SceneMesh> m_sceneMeshes;
		nri::Buffer * m_sceneVertices = nullptr;
		nri::Buffer * m_sceneIndices  = nullptr;
		nri::Buffer * m_globals		  = nullptr;
		std::vector<nri::Texture *> m_sceneTextures;
		std::vector<nri::Descriptor *> m_sceneTextureViews;
		nri::Descriptor * m_assetSampler = nullptr;
		std::array<nri::Descriptor *, kFramesInFlight> m_globalsViews{};
		nri::PipelineLayout * m_assetLayout = nullptr;
		nri::DescriptorPool * m_assetPool	= nullptr;
		std::array<nri::DescriptorSet *, kFramesInFlight> m_globalSets{};
		std::vector<nri::DescriptorSet *> m_assetMaterialSets;
		std::array<nri::Pipeline *, kScenePipelines> m_assetPipelines{};

		Identity m_identity;
		WorkerPool m_workers{ kMaxThreads };
	};

}

int main(int argc, char ** argv)
{
	vsnri::RaiseThreadPriority();

	const vsnri::HarnessOptions options = vsnri::ReadHarnessOptions(argc, argv);
	vsnri::WaitForProfiler(options);

	vsnri::NriArm arm;
	if (!arm.Initialize(options))
	{
		std::println(stderr, "nri arm: the device could not be set up, see the diagnostic above");
		return 1;
	}

	return vsnri::RunBenchmarks(argc, argv, arm);
}

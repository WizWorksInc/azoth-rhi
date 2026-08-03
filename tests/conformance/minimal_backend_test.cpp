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

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/render.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/present/swapchain.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "fixtures/minimal_backend.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

namespace rhi	  = azo::rhi;
namespace test	  = azo::rhi::test;
namespace minimal = azo::rhi::test::minimal;

namespace
{

	[[nodiscard]] rhi::BackendSelection Selection(rhi::Result<void> (*registerInto)(rhi::GraphicsApiRegistry &), const char * name)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = name, .includeAvailable = false } };
		EXPECT_TRUE(test::Ok(backends.Add(rhi::BackendEntry{
			.id			   = rhi::MakeGraphicsApiId("azoth.rhi.test.minimal"),
			.canonicalName = "azoth.rhi.test.minimal",
			.displayName   = "Minimal fixture",
			.Register	   = registerInto,
		})));
		return backends;
	}

	TEST(MinimalBackend, TheRequiredBlocksComeToSeventySevenEntries)
	{
		// A ratchet: this is what an out-of-tree backend must fill and it is not allowed to grow. A later capability goes in a new block.
		EXPECT_EQ(minimal::HeadlessEntryCount(), 77u);
		EXPECT_LE(minimal::HeadlessEntryCount(), 80u) << "the required set grew past the ratchet";

		EXPECT_EQ(minimal::PresentingEntryCount(), 91u);
	}

	TEST(MinimalBackend, gate_MinimalBackendFixture)
	{
		rhi::BackendSelection backends = Selection(&minimal::RegisterHeadless, "minimal");

		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		rhi::CommandPool pool = device.CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("minimal.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		EXPECT_TRUE(list.SetViewport(rhi::Viewport{ .width = 64.0f, .height = 64.0f }, error));
		EXPECT_TRUE(list.Draw(3, 1, 0, 0, error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const rhi::TimelineHandle timeline = device.CreateTimeline(rhi::TimelineDesc{}, error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
		EXPECT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error));
		EXPECT_TRUE(test::Ok(queue.Wait(timeline, 1, std::numeric_limits<std::uint64_t>::max(), error), error));
	}

	TEST(MinimalBackend, EveryDeclinedCapabilityReportsItselfRatherThanFailingSomewhereElse)
	{
		rhi::BackendSelection backends			   = Selection(&minimal::RegisterHeadless, "minimal");
		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		static_cast<void>(device.CreateSwapchain(rhi::SwapchainDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "declining PresentApi did not read as a declined capability";

		static_cast<void>(device.CreateHeap(rhi::HeapDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "PlacedMemoryApi";

		static_cast<void>(device.CreateQueryPool(rhi::QueryPoolDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "QueryApi";

		static_cast<void>(device.CreatePipelineCache(rhi::PipelineCacheDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "PipelineCacheApi";

		static_cast<void>(device.CreateRayTracingPipeline(rhi::RayTracingPipelineDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "RayTracingApi";

		rhi::MemoryBudgetInfo budget{};
		EXPECT_FALSE(device.QueryMemoryBudget(rhi::HeapType::eGpuLocal, budget, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "ResidencyApi";

		rhi::CommandPool pool = device.CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("minimal.declined", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		EXPECT_FALSE(list.AliasBarriers({}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "AliasingCommandApi";

		EXPECT_FALSE(list.DrawIndirect(rhi::BufferHandle{}, 0, 1, 0, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "IndirectApi";

		EXPECT_FALSE(list.BeginQuery(rhi::QueryPoolHandle{}, 0, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "QueryCommandApi";

		EXPECT_FALSE(list.TraceRays(rhi::ShaderBindingTableDesc{}, 1, 1, 1, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "RayTracingCommandApi";
	}

	TEST(MinimalBackend, gate_PresentingFixture)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.Add(rhi::BackendEntry{
			.id			   = rhi::MakeGraphicsApiId("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.CreateSwapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.IsValid(), error)) << "publishing PresentApi did not make swapchains reachable";

		EXPECT_EQ(swapchain.GetWidth(), 64u);
		EXPECT_EQ(swapchain.GetImageCount(), 2u);

		const rhi::AcquireResult acquired = swapchain.AcquireNextImage(std::numeric_limits<std::uint64_t>::max(), error);
		ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

		rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		const rhi::PresentResult presented = swapchain.Present(queue, acquired.imageIndex, rhi::BinarySemaphoreHandle{}, error);
		EXPECT_EQ(presented.status, rhi::SwapchainStatus::eOk) << "the eight block fixture did not present";
	}

	TEST(MinimalBackend, gate_AFrameNotAPresent)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.Add(rhi::BackendEntry{
			.id			   = rhi::MakeGraphicsApiId("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.CreateSwapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.IsValid(), error));

		rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		rhi::CommandPool pool = device.CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		for (int frame = 0; frame < 2; ++frame)
		{
			SCOPED_TRACE(frame == 0 ? "first frame" : "after a resize");

			const rhi::AcquireResult acquired = swapchain.AcquireNextImage(std::numeric_limits<std::uint64_t>::max(), error);
			ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

			const rhi::TextureHandle backBuffer = swapchain.GetBackBuffer(acquired.imageIndex);
			const rhi::TextureViewHandle view	= swapchain.GetBackBufferView(acquired.imageIndex);
			ASSERT_TRUE(backBuffer.IsValid()) << "the swapchain handed out no back buffer to render into";
			ASSERT_TRUE(view.IsValid()) << "the swapchain handed out no view of its back buffer";

			rhi::CommandList list = pool.Allocate("azoth.rhi.test.frame", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error));
			ASSERT_TRUE(test::Ok(list.Begin(error), error));

			const std::array<rhi::TextureBarrier, 1> toAttachment{ rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = {},
				.after =
					{
						.stages = rhi::PipelineStage::eColorOutput,
						.access = rhi::Access::eColorWrite,
						.layout = rhi::TextureLayout::eColorAttachment,
					},
				.range = test::samples::WholeColorRange(),
			} };
			EXPECT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .textures = toAttachment }, error), error))
				<< "barriering the acquired back buffer was refused";

			const std::array<rhi::RenderingAttachment, 1> colors{ rhi::RenderingAttachment{
				.view  = view,
				.state = {
					.stages = rhi::PipelineStage::eColorOutput,
					.access = rhi::Access::eColorWrite,
					.layout = rhi::TextureLayout::eColorAttachment,
				},
				.load  = rhi::LoadOp::eClear,
				.store = rhi::StoreOp::eStore,
			} };
			EXPECT_TRUE(test::Ok(list.BeginRendering(rhi::BeginRenderingDesc{ .colors = colors, .width = 64, .height = 64 }, error), error))
				<< "opening a rendering scope against the back buffer's view was refused";
			EXPECT_TRUE(test::Ok(list.EndRendering(error), error));

			const std::array<rhi::TextureBarrier, 1> toPresent{ rhi::TextureBarrier{
				.texture = backBuffer,
				.before =
					{
						.stages = rhi::PipelineStage::eColorOutput,
						.access = rhi::Access::eColorWrite,
						.layout = rhi::TextureLayout::eColorAttachment,
					},
				.after = {
					.stages = rhi::PipelineStage::eNone,
					.access = rhi::Access::eNone,
					.layout = rhi::TextureLayout::ePresent,
				},
				.range = test::samples::WholeColorRange(),
			} };
			EXPECT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .textures = toPresent }, error), error));

			EXPECT_TRUE(test::Ok(list.End(error), error));

			const rhi::PresentResult presented = swapchain.Present(queue, acquired.imageIndex, rhi::BinarySemaphoreHandle{}, error);
			EXPECT_EQ(presented.status, rhi::SwapchainStatus::eOk) << "the frame recorded but did not present";

			if (frame == 0)
			{
				ASSERT_TRUE(test::Ok(swapchain.Resize(128, 128, error), error)) << "the swapchain refused to resize between frames";
				EXPECT_EQ(swapchain.GetWidth(), 128u);
			}
		}
	}

	TEST(MinimalBackend, gate_HandleVendingRegisters)
	{
		if constexpr (!test::kValidatesHandles)
		{
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;
		}

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.Add(rhi::BackendEntry{
			.id			   = rhi::MakeGraphicsApiId("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.CreateSwapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.IsValid(), error));

		const rhi::AcquireResult acquired = swapchain.AcquireNextImage(std::numeric_limits<std::uint64_t>::max(), error);
		ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

		const rhi::TextureHandle backBuffer = swapchain.GetBackBuffer(acquired.imageIndex);
		ASSERT_TRUE(backBuffer.IsValid()) << "the presenting fixture vended no back buffer to barrier";

		rhi::CommandPool pool = device.CreateCommandPool(rhi::CommandPoolDesc{}, error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.backBuffer", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		const std::array intoColorTarget{
			rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = {},
				.after	 = { .layout = rhi::TextureLayout::eColorAttachment },
			},
		};

		EXPECT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .textures = intoColorTarget }, error), error))
			<< "a barrier naming a back buffer this swapchain vended was refused";

		const rhi::TextureViewHandle view = swapchain.GetBackBufferView(acquired.imageIndex);
		ASSERT_TRUE(view.IsValid());

		const std::array attachments{ rhi::RenderingAttachment{ .view = view } };
		EXPECT_TRUE(test::Ok(list.BeginRendering(rhi::BeginRenderingDesc{ .colors = attachments, .width = 64, .height = 64 }, error), error))
			<< "a rendering scope naming a back buffer view was refused";

		static_cast<void>(list.EndRendering(error));
		static_cast<void>(list.End(error));
	}

	TEST(MinimalBackend, EveryCategoricalCapabilityComesOffTheBlocksRatherThanAField)
	{
		rhi::BackendSelection headless			  = Selection(&minimal::RegisterHeadless, "minimal");
		const rhi::Result<rhi::UniqueDevice> bare = headless.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(bare));

		const rhi::DeviceCaps & declined = bare.Value().Get().GetCaps();

		EXPECT_FALSE(declined.supportsSurfaces) << "PresentApi";
		EXPECT_FALSE(declined.supportsPlacedResources) << "PlacedMemoryApi";
		EXPECT_FALSE(declined.supportsPipelineCache) << "PipelineCacheApi";
		EXPECT_FALSE(declined.supportsMemoryBudget) << "ResidencyApi";
		EXPECT_FALSE(declined.supportsResourceAdoption) << "AdoptionApi";
		EXPECT_FALSE(declined.supportsRayTracing) << "RayTracingApi and RayTracingCommandApi";
		EXPECT_FALSE(declined.supportsTimestampQueries) << "QueryApi and QueryCommandApi";
		EXPECT_FALSE(declined.supportsMultiDrawIndirect) << "IndirectApi";
		EXPECT_FALSE(declined.supportsIndirectCount) << "IndirectCountApi";
		EXPECT_EQ(declined.sparseTier, rhi::SparseTier::eNone) << "SparseApi";

		rhi::BackendSelection presenting{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(presenting.Add(rhi::BackendEntry{
			.id			   = rhi::MakeGraphicsApiId("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> withSurfaces = presenting.CreateDevice(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(withSurfaces));

		const rhi::DeviceCaps & provided = withSurfaces.Value().Get().GetCaps();
		EXPECT_TRUE(provided.supportsSurfaces) << "publishing PresentApi did not turn the answer true";

		EXPECT_FALSE(provided.supportsPlacedResources);
		EXPECT_FALSE(provided.supportsRayTracing);
		EXPECT_EQ(provided.sparseTier, rhi::SparseTier::eNone);
	}

	TEST(MinimalBackend, ABundledBackendReportsOnlyWhatItPublished)
	{
		for (const rhi::BackendEntry & entry : rhi::AvailableBackends())
		{
			rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
			ASSERT_TRUE(test::Ok(backends.Add(entry))) << entry.canonicalName;

			const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
			if (!owner)
			{
				continue; // No driver on this machine for that one.
			}

			rhi::Device device				 = owner.Value().Get();
			const rhi::DeviceCaps & reported = device.GetCaps();
			rhi::Error error{};

			if (reported.sparseTier > rhi::SparseTier::eNone)
			{
				rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics, 0, error);
				ASSERT_TRUE(test::Ok(queue.IsValid(), error));
				static_cast<void>(queue.BindSparse(rhi::SparseBindDesc{}, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature)
					<< entry.canonicalName << " claims sparse binding and then declines the block behind it";
			}

			if (reported.supportsPipelineCache)
			{
				static_cast<void>(device.CreatePipelineCache(rhi::PipelineCacheDesc{}, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature) << entry.canonicalName << " claims a pipeline cache it declined";
			}

			if (reported.supportsMemoryBudget)
			{
				rhi::MemoryBudgetInfo budget{};
				static_cast<void>(device.QueryMemoryBudget(rhi::HeapType::eGpuLocal, budget, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature) << entry.canonicalName << " claims a memory budget it declined";
			}
		}
	}

	TEST(MinimalBackend, ReleasesEverythingItAllocated)
	{
		const std::size_t before = minimal::LiveObjectCount();

		{
			rhi::BackendSelection backends			   = Selection(&minimal::RegisterHeadless, "minimal");
			const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
			ASSERT_TRUE(test::Ok(owner));
		}

		EXPECT_EQ(minimal::LiveObjectCount(), before) << "tearing the device down left objects behind";
	}

	TEST(MinimalBackend, gate_ValidationReachesStrangers)
	{
		if constexpr (!test::kValidatesHandles)
		{
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;
		}

		rhi::BackendSelection backends			   = Selection(&minimal::RegisterHeadless, "minimal");
		const rhi::Result<rhi::UniqueDevice> owner = backends.CreateDevice(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.Value().Get();
		rhi::Error error{};

		const rhi::BufferHandle never{};
		EXPECT_FALSE(device.Destroy(never, {}, error)) << "a handle this device never handed out was accepted";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);

		const rhi::BufferHandle live = device.CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(live.IsValid(), error));
		ASSERT_TRUE(test::Ok(device.Destroy(live, {}, error), error));

		error = {};
		EXPECT_FALSE(device.Destroy(live, {}, error)) << "a handle destroyed twice was accepted the second time";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);

		error							 = {};
		const rhi::TextureHandle texture = device.CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));
		ASSERT_TRUE(test::Ok(device.Destroy(texture, {}, error), error));

		error								  = {};
		const rhi::TextureViewHandle fromDead = device.CreateTextureView(texture, rhi::TextureViewDesc{}, error);
		EXPECT_FALSE(fromDead.IsValid()) << "a view was made over a texture that had already gone back";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

} // namespace

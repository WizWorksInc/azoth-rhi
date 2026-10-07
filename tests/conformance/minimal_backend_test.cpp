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
		EXPECT_TRUE(test::Ok(backends.add(rhi::BackendEntry{
			.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimal"),
			.canonicalName = "azoth.rhi.test.minimal",
			.displayName   = "Minimal fixture",
			.Register	   = registerInto,
		})));
		return backends;
	}

	TEST(MinimalBackend, TheRequiredBlocksComeToSeventySixEntries)
	{
		EXPECT_EQ(minimal::HeadlessEntryCount(), 76u);
		EXPECT_LE(minimal::HeadlessEntryCount(), 80u) << "the required set grew past the ratchet";

		EXPECT_EQ(minimal::PresentingEntryCount(), 90u);
	}

	TEST(MinimalBackend, gate_MinimalBackendFixture)
	{
		rhi::BackendSelection backends = Selection(&minimal::RegisterHeadless, "minimal");

		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		rhi::CommandPool pool = device.create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("minimal.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		EXPECT_TRUE(list.set_viewport(rhi::Viewport{ .width = 64.0f, .height = 64.0f }, error));
		EXPECT_TRUE(list.draw(3, 1, 0, 0, error));
		ASSERT_TRUE(test::Ok(list.end(error), error));

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle timeline = device.create_timeline(rhi::TimelineDesc{}, error);
		ASSERT_TRUE(test::Ok(timeline.is_valid(), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = lists, .signals = signals }, error), error));
		EXPECT_TRUE(test::Ok(queue.wait(timeline, 1, std::numeric_limits<std::uint64_t>::max(), error), error));
	}

	TEST(MinimalBackend, EveryDeclinedCapabilityReportsItselfRatherThanFailingSomewhereElse)
	{
		rhi::BackendSelection backends			   = Selection(&minimal::RegisterHeadless, "minimal");
		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		static_cast<void>(device.create_swapchain(rhi::SwapchainDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "declining PresentApi did not read as a declined capability";

		static_cast<void>(device.create_heap(rhi::HeapDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "PlacedMemoryApi";

		static_cast<void>(device.create_query_pool(rhi::QueryPoolDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "QueryApi";

		static_cast<void>(device.create_pipeline_cache(rhi::PipelineCacheDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "PipelineCacheApi";

		static_cast<void>(device.create_ray_tracing_pipeline(rhi::RayTracingPipelineDesc{}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "RayTracingApi";

		rhi::MemoryBudgetInfo budget{};
		EXPECT_FALSE(device.query_memory_budget(rhi::HeapType::eGpuLocal, budget, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "ResidencyApi";

		rhi::CommandPool pool = device.create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("minimal.declined", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		EXPECT_FALSE(list.alias_barriers({}, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "AliasingCommandApi";

		EXPECT_FALSE(list.draw_indirect(rhi::BufferHandle{}, 0, 1, 0, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "IndirectApi";

		EXPECT_FALSE(list.begin_query(rhi::QueryPoolHandle{}, 0, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "QueryCommandApi";

		EXPECT_FALSE(list.trace_rays(rhi::ShaderBindingTableDesc{}, 1, 1, 1, error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature) << "RayTracingCommandApi";
	}

	TEST(MinimalBackend, gate_PresentingFixture)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.add(rhi::BackendEntry{
			.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.create_swapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.is_valid(), error)) << "publishing PresentApi did not make swapchains reachable";

		EXPECT_EQ(swapchain.get_width(), 64u);
		EXPECT_EQ(swapchain.get_image_count(), 2u);

		const rhi::AcquireResult acquired = swapchain.acquire_next_image(std::numeric_limits<std::uint64_t>::max(), error);
		ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::PresentResult presented = swapchain.present(queue, acquired.imageIndex, rhi::BinarySemaphoreHandle{}, error);
		EXPECT_EQ(presented.status, rhi::SwapchainStatus::eOk) << "the eight block fixture did not present";
	}

	TEST(MinimalBackend, gate_AFrameNotAPresent)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.add(rhi::BackendEntry{
			.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.create_swapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.is_valid(), error));

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = device.create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		for (int frame = 0; frame < 2; ++frame)
		{
			SCOPED_TRACE(frame == 0 ? "first frame" : "after a resize");

			const rhi::AcquireResult acquired = swapchain.acquire_next_image(std::numeric_limits<std::uint64_t>::max(), error);
			ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

			const rhi::TextureHandle backBuffer = swapchain.get_back_buffer(acquired.imageIndex);
			const rhi::TextureViewHandle view	= swapchain.get_back_buffer_view(acquired.imageIndex);
			ASSERT_TRUE(backBuffer.is_valid()) << "the swapchain handed out no back buffer to render into";
			ASSERT_TRUE(view.is_valid()) << "the swapchain handed out no view of its back buffer";

			rhi::CommandList list = pool.allocate("azoth.rhi.test.frame", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			ASSERT_TRUE(test::Ok(list.begin(error), error));

			const std::array<rhi::TextureBarrier, 1> toAttachment{ rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = {},
				.after	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.range	 = test::samples::WholeColorRange(),
			} };
			EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .textures = toAttachment }, error), error))
				<< "barriering the acquired back buffer was refused";

			const std::array<rhi::RenderingAttachment, 1> colors{ rhi::RenderingAttachment{
				.view  = view,
				.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load  = rhi::LoadOp::eClear,
				.store = rhi::StoreOp::eStore,
			} };
			EXPECT_TRUE(test::Ok(list.begin_rendering(rhi::BeginRenderingDesc{ .colors = colors, .width = 64, .height = 64 }, error), error))
				<< "opening a rendering scope against the back buffer's view was refused";
			EXPECT_TRUE(test::Ok(list.end_rendering(error), error));

			const std::array<rhi::TextureBarrier, 1> toPresent{ rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.after	 = { .use = rhi::ResourceUse::ePresent },
				.range	 = test::samples::WholeColorRange(),
			} };
			EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .textures = toPresent }, error), error));

			EXPECT_TRUE(test::Ok(list.end(error), error));

			const rhi::PresentResult presented = swapchain.present(queue, acquired.imageIndex, rhi::BinarySemaphoreHandle{}, error);
			EXPECT_EQ(presented.status, rhi::SwapchainStatus::eOk) << "the frame recorded but did not present";

			if (frame == 0)
			{
				ASSERT_TRUE(test::Ok(swapchain.resize(128, 128, error), error)) << "the swapchain refused to resize between frames";
				EXPECT_EQ(swapchain.get_width(), 128u);
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
		ASSERT_TRUE(test::Ok(backends.add(rhi::BackendEntry{
			.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		rhi::Swapchain swapchain = device.create_swapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		ASSERT_TRUE(test::Ok(swapchain.is_valid(), error));

		const rhi::AcquireResult acquired = swapchain.acquire_next_image(std::numeric_limits<std::uint64_t>::max(), error);
		ASSERT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

		const rhi::TextureHandle backBuffer = swapchain.get_back_buffer(acquired.imageIndex);
		ASSERT_TRUE(backBuffer.is_valid()) << "the presenting fixture vended no back buffer to barrier";

		rhi::CommandPool pool = device.create_command_pool(rhi::CommandPoolDesc{}, error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.backBuffer", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		const std::array intoColorTarget{
			rhi::TextureBarrier{
				.texture = backBuffer,
				.before	 = {},
				.after	 = { .use = rhi::ResourceUse::eColorTarget },
			},
		};

		EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .textures = intoColorTarget }, error), error))
			<< "a barrier naming a back buffer this swapchain vended was refused";

		const rhi::TextureViewHandle view = swapchain.get_back_buffer_view(acquired.imageIndex);
		ASSERT_TRUE(view.is_valid());

		const std::array attachments{ rhi::RenderingAttachment{ .view = view } };
		EXPECT_TRUE(test::Ok(list.begin_rendering(rhi::BeginRenderingDesc{ .colors = attachments, .width = 64, .height = 64 }, error), error))
			<< "a rendering scope naming a back buffer view was refused";

		static_cast<void>(list.end_rendering(error));
		static_cast<void>(list.end(error));
	}

	TEST(MinimalBackend, gate_ABackBufferViewCarriesTheSwapchainFormatIntoTheAttachmentCheck)
	{
		const auto openAScope = [](const rhi::ValidationMode validation) noexcept
		{
			rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "minimalPresenting", .includeAvailable = false } };
			EXPECT_TRUE(test::Ok(backends.add(rhi::BackendEntry{
				.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimalPresenting"),
				.canonicalName = "azoth.rhi.test.minimalPresenting",
				.displayName   = "Minimal presenting fixture",
				.Register	   = &minimal::RegisterPresenting,
			})));

			const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .validation = validation });
			EXPECT_TRUE(test::Ok(owner));
			if (!owner)
			{
				return false;
			}

			rhi::Device device = owner.value().get();
			rhi::Error error{};

			rhi::Swapchain swapchain = device.create_swapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
			EXPECT_TRUE(test::Ok(swapchain.is_valid(), error));

			const rhi::AcquireResult acquired = swapchain.acquire_next_image(std::numeric_limits<std::uint64_t>::max(), error);
			EXPECT_EQ(acquired.status, rhi::SwapchainStatus::eOk);

			const rhi::TextureViewHandle view = swapchain.get_back_buffer_view(acquired.imageIndex);
			EXPECT_TRUE(view.is_valid());

			rhi::CommandPool pool = device.create_command_pool(rhi::CommandPoolDesc{}, error);
			EXPECT_TRUE(test::Ok(pool.is_valid(), error));
			rhi::CommandList list = pool.allocate("azoth.rhi.test.backBufferFormat", error);
			EXPECT_TRUE(test::Ok(list.is_valid(), error));
			EXPECT_TRUE(test::Ok(list.begin(error), error));

			const std::array attachments{ rhi::RenderingAttachment{ .view = view } };
			const bool opened = list.begin_rendering(rhi::BeginRenderingDesc{ .colors = attachments, .width = 64, .height = 64 }, error);
			if (opened)
			{
				static_cast<void>(list.end_rendering(error));
			}

			static_cast<void>(list.end(error));
			return opened;
		};

		EXPECT_TRUE(openAScope(rhi::ValidationMode::eReleaseLight))
			<< "the fixture itself refused a rendering scope, so the developer-mode refusal below would prove nothing";

		EXPECT_FALSE(openAScope(rhi::ValidationMode::eDeveloper))
			<< "the fixture advertises no colour attachment support at all, so the format the swapchain vended never reached the check";
	}

	TEST(MinimalBackend, EveryCategoricalCapabilityComesOffTheBlocksRatherThanAField)
	{
		rhi::BackendSelection headless			  = Selection(&minimal::RegisterHeadless, "minimal");
		const rhi::Result<rhi::UniqueDevice> bare = headless.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(bare));

		const rhi::DeviceCaps & declined = bare.value().get().get_caps();

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
		ASSERT_TRUE(test::Ok(presenting.add(rhi::BackendEntry{
			.id			   = rhi::make_graphics_api_id("azoth.rhi.test.minimalPresenting"),
			.canonicalName = "azoth.rhi.test.minimalPresenting",
			.displayName   = "Minimal presenting fixture",
			.Register	   = &minimal::RegisterPresenting,
		})));

		const rhi::Result<rhi::UniqueDevice> withSurfaces = presenting.create_device(rhi::DeviceDesc{});
		ASSERT_TRUE(test::Ok(withSurfaces));

		const rhi::DeviceCaps & provided = withSurfaces.value().get().get_caps();
		EXPECT_TRUE(provided.supportsSurfaces) << "publishing PresentApi did not turn the answer true";

		EXPECT_FALSE(provided.supportsPlacedResources);
		EXPECT_FALSE(provided.supportsRayTracing);
		EXPECT_EQ(provided.sparseTier, rhi::SparseTier::eNone);
	}

	TEST(MinimalBackend, ABundledBackendReportsOnlyWhatItPublished)
	{
		for (const rhi::BackendEntry & entry : rhi::available_backends())
		{
			rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
			ASSERT_TRUE(test::Ok(backends.add(entry))) << entry.canonicalName;

			const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
			if (!owner)
			{
				continue;
			}

			rhi::Device device				 = owner.value().get();
			const rhi::DeviceCaps & reported = device.get_caps();
			rhi::Error error{};

			if (reported.sparseTier > rhi::SparseTier::eNone)
			{
				rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics, 0, error);
				ASSERT_TRUE(test::Ok(queue.is_valid(), error));
				static_cast<void>(queue.bind_sparse(rhi::SparseBindDesc{}, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature)
					<< entry.canonicalName << " claims sparse binding and then declines the block behind it";
			}

			if (reported.supportsPipelineCache)
			{
				static_cast<void>(device.create_pipeline_cache(rhi::PipelineCacheDesc{}, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature) << entry.canonicalName << " claims a pipeline cache it declined";
			}

			if (reported.supportsMemoryBudget)
			{
				rhi::MemoryBudgetInfo budget{};
				static_cast<void>(device.query_memory_budget(rhi::HeapType::eGpuLocal, budget, error));
				EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature) << entry.canonicalName << " claims a memory budget it declined";
			}
		}
	}

	TEST(MinimalBackend, ReleasesEverythingItAllocated)
	{
		const std::size_t before = minimal::LiveObjectCount();

		{
			rhi::BackendSelection backends			   = Selection(&minimal::RegisterHeadless, "minimal");
			const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
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
		const rhi::Result<rhi::UniqueDevice> owner = backends.create_device(rhi::DeviceDesc{ .requireSwapchain = false });
		ASSERT_TRUE(test::Ok(owner));

		rhi::Device device = owner.value().get();
		rhi::Error error{};

		const rhi::BufferHandle never{};
		EXPECT_FALSE(device.destroy(never, {}, error)) << "a handle this device never handed out was accepted";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);

		const rhi::BufferHandle live = device.create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(live.is_valid(), error));
		ASSERT_TRUE(test::Ok(device.destroy(live, {}, error), error));

		error = {};
		EXPECT_FALSE(device.destroy(live, {}, error)) << "a handle destroyed twice was accepted the second time";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);

		error							 = {};
		const rhi::TextureHandle texture = device.create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));
		ASSERT_TRUE(test::Ok(device.destroy(texture, {}, error), error));

		error								  = {};
		const rhi::TextureViewHandle fromDead = device.create_texture_view(texture, rhi::TextureViewDesc{}, error);
		EXPECT_FALSE(fromDead.is_valid()) << "a view was made over a texture that had already gone back";
		EXPECT_NE(error.code, rhi::ErrorCode::eOk);
	}

}

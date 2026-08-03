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
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <thread>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class CommandTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(CommandTest);

	TEST_P(CommandTest, CreatesACommandPoolForEachQueueTypeItHas)
	{
		rhi::Error error{};

		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			if (Dev().GetQueueCount(type) == 0)
			{
				continue;
			}

			const rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(type), error);
			EXPECT_TRUE(test::Ok(pool.IsValid(), error)) << "no pool for queue type " << static_cast<int>(type);
		}
	}

	TEST_P(CommandTest, AllocatesSeveralListsFromOnePool)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		std::vector<rhi::CommandList> lists;
		for (int index = 0; index < 8; ++index)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error)) << "allocation " << index << " failed";
			lists.push_back(list);
		}

		for (std::size_t lhs = 0; lhs < lists.size(); ++lhs)
		{
			for (std::size_t rhs = lhs + 1; rhs < lists.size(); ++rhs)
			{
				ASSERT_NE(&lists[lhs], &lists[rhs]);
			}
		}
	}

	TEST_P(CommandTest, BracketsARecordingWithBeginAndEnd)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		EXPECT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

	TEST_P(CommandTest, RejectsBeginningAListThatIsAlreadyRecording)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(list.Begin(secondError)) << "a second Begin on a recording list was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(secondError));

		static_cast<void>(list.End(error));
	}

	TEST_P(CommandTest, RejectsEndingAListThatWasNeverBegun)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		rhi::Error endError{};
		EXPECT_FALSE(list.End(endError)) << "End was accepted on a list that had not begun recording";
		EXPECT_TRUE(test::ErrorIsPopulated(endError));
	}

	TEST_P(CommandTest, RejectsEndingTheSameListTwice)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(list.End(secondError)) << "a closed list was closed again";
	}

	TEST_P(CommandTest, RejectsRecordingFromAThreadThatDidNotBeginTheList)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		bool accepted = false;
		rhi::Error foreignError{};
		std::thread other(
			[&]
			{
				accepted = list.End(foreignError);
			});
		other.join();

		EXPECT_FALSE(accepted) << "a command list was closed from a thread other than the one recording it";

		static_cast<void>(list.End(error));
	}

	TEST_P(CommandTest, RecordsTransferWorkOutsideAnyRenderingScope)
	{
		rhi::Error error{};
		const rhi::BufferHandle source		= Dev().CreateBuffer(test::samples::UploadBuffer(), error);
		const rhi::BufferHandle destination = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(source.IsValid(), error));
		ASSERT_TRUE(test::Ok(destination.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
		rhi::CommandList & list = recording.List();

		EXPECT_TRUE(test::Ok(list.CopyBuffer(destination, 0, source, 0, test::samples::kBufferSize, error), error));
		EXPECT_TRUE(test::Ok(list.ClearBuffer(destination, 0, test::samples::kBufferSize, 0, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(destination, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(source, {}, error), error));
	}

	TEST_P(CommandTest, RecordsDynamicStateInsideARenderingScope)
	{
		// Dynamic state belongs to a rendering scope, not the whole list. Vulkan happens to accept vkCmdSetViewport outside a render pass, but Metal has
		// nowhere to put it until an encoder exists. The portable rule is the stricter one so the scope opens first.
		rhi::Error error{};
		const rhi::TextureHandle target = Dev().CreateTexture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.IsValid(), error));

		const rhi::TextureViewHandle view = Dev().CreateTextureView(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		const std::array colors{ rhi::RenderingAttachment{ .view = view,
			.state = { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment, },
			.load  = rhi::LoadOp::eClear,
			.store = rhi::StoreOp::eStore } };
		const rhi::BeginRenderingDesc rendering{
			.colors		  = colors,
			.depthStencil = nullptr,
			.x			  = 0,
			.y			  = 0,
			.width		  = 64,
			.height		  = 64,
			.layers		  = 1,
		};

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
		rhi::CommandList & list = recording.List();

		if (!list.BeginRendering(rendering, error))
		{
			static_cast<void>(recording.End());
			static_cast<void>(Dev().Destroy(view, {}, error));
			static_cast<void>(Dev().Destroy(target, {}, error));
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(list.SetViewport(
								 rhi::Viewport{
									 .x		   = 0.0f,
									 .y		   = 0.0f,
									 .width	   = 64.0f,
									 .height   = 64.0f,
									 .minDepth = 0.0f,
									 .maxDepth = 1.0f,
								 },
								 error),
			error));
		EXPECT_TRUE(test::Ok(list.SetScissor(
								 rhi::Rect2D{
									 .x		 = 0,
									 .y		 = 0,
									 .width	 = 64,
									 .height = 64,
								 },
								 error),
			error));
		EXPECT_TRUE(test::Ok(list.SetBlendConstants(0.0f, 0.0f, 0.0f, 1.0f, error), error));
		EXPECT_TRUE(test::Ok(list.SetStencilReference(1, error), error));
		EXPECT_TRUE(test::Ok(list.SetDepthBias(0.0f, 0.0f, 0.0f, error), error));

		EXPECT_TRUE(test::Ok(list.EndRendering(error), error));
		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, {}, error), error));
	}

	TEST_P(CommandTest, RecordsBalancedDebugLabels)
	{
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(recording.List().BeginDebugLabel("azoth.rhi.test.outer", 0xFF0000FFu, error), error));
		EXPECT_TRUE(test::Ok(recording.List().BeginDebugLabel("azoth.rhi.test.inner", 0, error), error));
		EXPECT_TRUE(test::Ok(recording.List().EndDebugLabel(error), error));
		EXPECT_TRUE(test::Ok(recording.List().EndDebugLabel(error), error));

		EXPECT_TRUE(recording.End());
	}

	TEST_P(CommandTest, RecordsDebugLabelsOnADeviceThatTurnedThemOff)
	{
		rhi::DeviceDesc desc   = MakeDeviceDesc();
		desc.enableDebugLabels = false;

		const test::DeviceHarness quiet(CurrentBackend(), desc);
		if (!quiet.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(quiet.GetError());
		}

		test::Recording recording(quiet.Get());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(recording.List().BeginDebugLabel("azoth.rhi.test.outer", 0xFF0000FFu, error), error));
		EXPECT_TRUE(test::Ok(recording.List().BeginDebugLabel("azoth.rhi.test.inner", 0, error), error));
		EXPECT_TRUE(test::Ok(recording.List().EndDebugLabel(error), error));
		EXPECT_TRUE(test::Ok(recording.List().EndDebugLabel(error), error));

		EXPECT_TRUE(recording.End());
	}

	TEST_P(CommandTest, ResetsAPoolAndRecordsFromItAgain)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error));

		rhi::CommandList afterReset = pool.Allocate("azoth.rhi.test.listAfterReset", error);
		ASSERT_TRUE(test::Ok(afterReset.IsValid(), error));
		EXPECT_TRUE(test::Ok(afterReset.Begin(error), error));
		EXPECT_TRUE(test::Ok(afterReset.End(error), error));
	}

	TEST_P(CommandTest, SubmitsAClosedListToItsQueue)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.debugName	  = "azoth.rhi.test.submit",
		};

		EXPECT_TRUE(test::Ok(queue.Submit(submit, error), error));
		EXPECT_TRUE(test::Ok(queue.WaitIdle(error), error));
	}

	TEST_P(CommandTest, SubmitsNothingWithoutComplaining)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		EXPECT_TRUE(test::Ok(queue.Submit(rhi::SubmitDesc{ .debugName = "azoth.rhi.test.emptySubmit" }, error), error));
	}

	TEST_P(CommandTest, RecordsAQueryPoolResetAndTimestampWhenTimestampsAreSupported)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries || IsNullBackend(), "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(test::Ok(recording.List().ResetQueryPool(pool, 0, 8, error), error));
		EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 0, rhi::PipelineStage::eAllCommands, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

	TEST_P(CommandTest, RecordsARenderingScopeOverAColorAttachment)
	{
		rhi::Error error{};
		const rhi::TextureHandle target = Dev().CreateTexture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.IsValid(), error));

		const rhi::TextureViewHandle view = Dev().CreateTextureView(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		const std::array colors{ rhi::RenderingAttachment{ .view = view,
			.state		= { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment, },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = rhi::ClearColor{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f } } };

		const rhi::BeginRenderingDesc rendering{
			.colors		  = colors,
			.depthStencil = nullptr,
			.x			  = 0,
			.y			  = 0,
			.width		  = 64,
			.height		  = 64,
			.layers		  = 1,
		};

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		if (!recording.List().BeginRendering(rendering, error))
		{
			static_cast<void>(recording.End());
			static_cast<void>(Dev().Destroy(view, {}, error));
			static_cast<void>(Dev().Destroy(target, {}, error));
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, {}, error), error));
	}

	TEST_P(CommandTest, ADefaultConstructedPoolAndListAreInert)
	{
		EXPECT_FALSE(rhi::CommandPool{}.IsValid());
		EXPECT_FALSE(rhi::CommandList{}.IsValid());
	}

} // namespace

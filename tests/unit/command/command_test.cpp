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

#include "azoth/rhi/backend/support/bounded_count.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/device/device.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
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
			if (Dev().get_queue_count(type) == 0)
			{
				continue;
			}

			const rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(type), error);
			EXPECT_TRUE(test::Ok(pool.is_valid(), error)) << "no pool for queue type " << static_cast<int>(type);
		}
	}

	TEST_P(CommandTest, AllocatesSeveralListsFromOnePool)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		std::vector<rhi::CommandList> lists;
		for (int index = 0; index < 8; ++index)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error)) << "allocation " << index << " failed";
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
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		EXPECT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

	TEST_P(CommandTest, RejectsBeginningAListThatIsAlreadyRecording)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
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
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		rhi::Error endError{};
		EXPECT_FALSE(list.End(endError)) << "End was accepted on a list that had not begun recording";
		EXPECT_TRUE(test::ErrorIsPopulated(endError));
	}

	TEST_P(CommandTest, RejectsEndingTheSameListTwice)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(list.End(secondError)) << "a closed list was closed again";
	}

	TEST_P(CommandTest, RejectsRecordingFromAThreadThatDidNotBeginTheList)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
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
		const rhi::BufferHandle source		= Dev().create_buffer(test::samples::UploadBuffer(), error);
		const rhi::BufferHandle destination = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(source.is_valid(), error));
		ASSERT_TRUE(test::Ok(destination.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
		rhi::CommandList & list = recording.List();

		EXPECT_TRUE(test::Ok(list.copy_buffer(destination, 0, source, 0, test::samples::kBufferSize, error), error));
		EXPECT_TRUE(test::Ok(list.clear_buffer(destination, 0, test::samples::kBufferSize, 0, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(destination, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(source, {}, error), error));
	}

	TEST_P(CommandTest, RecordsDynamicStateInsideARenderingScope)
	{
		rhi::Error error{};
		const rhi::TextureHandle target = Dev().create_texture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		const rhi::TextureViewHandle view = Dev().create_texture_view(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.is_valid(), error));

		const std::array colors{
			rhi::RenderingAttachment{
				.view  = view,
				.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load  = rhi::LoadOp::eClear,
				.store = rhi::StoreOp::eStore,
			},
		};
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

		if (!list.begin_rendering(rendering, error))
		{
			static_cast<void>(recording.End());
			static_cast<void>(Dev().destroy(view, {}, error));
			static_cast<void>(Dev().destroy(target, {}, error));
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(list.set_viewport(
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
		EXPECT_TRUE(test::Ok(list.set_scissor(
								 rhi::Rect2D{
									 .x		 = 0,
									 .y		 = 0,
									 .width	 = 64,
									 .height = 64,
								 },
								 error),
			error));
		EXPECT_TRUE(test::Ok(list.set_blend_constants(0.0f, 0.0f, 0.0f, 1.0f, error), error));
		EXPECT_TRUE(test::Ok(list.set_stencil_reference(1, error), error));
		EXPECT_TRUE(test::Ok(list.set_depth_bias(0.0f, 0.0f, 0.0f, error), error));

		EXPECT_TRUE(test::Ok(list.end_rendering(error), error));
		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(target, {}, error), error));
	}

	TEST_P(CommandTest, RecordsBalancedDebugLabels)
	{
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(recording.List().begin_debug_label("azoth.rhi.test.outer", 0xFF0000FFu, error), error));
		EXPECT_TRUE(test::Ok(recording.List().begin_debug_label("azoth.rhi.test.inner", 0, error), error));
		EXPECT_TRUE(test::Ok(recording.List().end_debug_label(error), error));
		EXPECT_TRUE(test::Ok(recording.List().end_debug_label(error), error));

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
		EXPECT_TRUE(test::Ok(recording.List().begin_debug_label("azoth.rhi.test.outer", 0xFF0000FFu, error), error));
		EXPECT_TRUE(test::Ok(recording.List().begin_debug_label("azoth.rhi.test.inner", 0, error), error));
		EXPECT_TRUE(test::Ok(recording.List().end_debug_label(error), error));
		EXPECT_TRUE(test::Ok(recording.List().end_debug_label(error), error));

		EXPECT_TRUE(recording.End());
	}

	TEST_P(CommandTest, ResetsAPoolAndRecordsFromItAgain)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error));

		rhi::CommandList afterReset = pool.Allocate("azoth.rhi.test.listAfterReset", error);
		ASSERT_TRUE(test::Ok(afterReset.is_valid(), error));
		EXPECT_TRUE(test::Ok(afterReset.Begin(error), error));
		EXPECT_TRUE(test::Ok(afterReset.End(error), error));
	}

	TEST_P(CommandTest, SubmitsAClosedListToItsQueue)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.debugName	  = "azoth.rhi.test.submit",
		};

		EXPECT_TRUE(test::Ok(queue.submit(submit, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
	}

	TEST_P(CommandTest, SubmitsNothingWithoutComplaining)
	{
		rhi::Error error{};
		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .debugName = "azoth.rhi.test.emptySubmit" }, error), error));
	}

	TEST_P(CommandTest, RecordsAQueryPoolResetAndTimestampWhenTimestampsAreSupported)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries || IsNullBackend(), "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().create_query_pool(test::samples::TimestampPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(test::Ok(recording.List().reset_query_pool(pool, 0, 8, error), error));
		EXPECT_TRUE(test::Ok(recording.List().write_timestamp(pool, 0, rhi::Stage::eAllCommands, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(pool, {}, error), error));
	}

	TEST_P(CommandTest, RecordsARenderingScopeOverAColorAttachment)
	{
		rhi::Error error{};
		const rhi::TextureHandle target = Dev().create_texture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		const rhi::TextureViewHandle view = Dev().create_texture_view(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.is_valid(), error));

		const std::array colors{
			rhi::RenderingAttachment{
				.view		= view,
				.state		= { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load		= rhi::LoadOp::eClear,
				.store		= rhi::StoreOp::eStore,
				.clearColor = rhi::ClearColor{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
			},
		};

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

		if (!recording.List().begin_rendering(rendering, error))
		{
			static_cast<void>(recording.End());
			static_cast<void>(Dev().destroy(view, {}, error));
			static_cast<void>(Dev().destroy(target, {}, error));
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(recording.List().end_rendering(error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(target, {}, error), error));
	}

	constexpr std::uint32_t kSmallOpenListBound = 16;

	TEST_P(CommandTest, HoldsOpenAsManyListsAsItsCapsPromiseAndNoFewer)
	{
		rhi::DeviceDesc desc			 = MakeDeviceDesc();
		desc.maxOpenCommandListsPerQueue = kSmallOpenListBound;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		const std::uint32_t bound = local.Get().get_caps().maxOpenCommandListsPerQueue;
		const bool bounded		  = bound != rhi::kUnlimitedOpenCommandLists;
		if (bounded)
		{
			EXPECT_EQ(bound, kSmallOpenListBound) << "a backend that bounds open lists did not report the setting it was given";
		}

		// A bounded backend must reach its own bound and then refuse. An unbounded one must not refuse at twice the setting.
		const std::uint32_t attempts = bounded ? bound + 1 : kSmallOpenListBound * 2;

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		std::vector<rhi::CommandList> lists;
		lists.reserve(attempts);

		const auto started = std::chrono::steady_clock::now();

		std::uint32_t begun	   = 0;
		std::uint32_t refusals = 0;
		rhi::Error refusal{};
		for (std::uint32_t index = 0; index < attempts; ++index)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error)) << "allocation " << index << " failed";
			lists.push_back(list);

			if (lists.back().Begin(refusal))
			{
				++begun;
				continue;
			}

			EXPECT_TRUE(test::ErrorIsPopulated(refusal)) << "Begin " << index << " was refused without saying why";
			EXPECT_EQ(refusal.code, rhi::ErrorCode::eInvalidState) << "a refused Begin reported something other than a state problem";
			lists.pop_back();
			++refusals;
			break;
		}

		const auto elapsed = std::chrono::steady_clock::now() - started;
		EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000) << "beginning " << begun << " lists waited on something";

		if (bounded)
		{
			EXPECT_EQ(begun, bound) << "a bounded backend stopped short of the bound it reported";
			EXPECT_EQ(refusals, 1u) << "a bounded backend did not refuse the list past its bound";
		}
		else
		{
			EXPECT_EQ(begun, attempts) << "an unbounded backend refused a Begin";
			EXPECT_EQ(refusals, 0u) << "an unbounded backend refused a Begin";
		}

		for (rhi::CommandList & list : lists)
		{
			static_cast<void>(list.End(error));
		}

		// Metal keeps command buffers back for the transient ones waitIdle commits, so a saturated queue must still be able to drain.
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error)) << "waiting the queue out while it still holds every begun list was refused";

		ASSERT_FALSE(lists.empty());
		std::array<const rhi::CommandList *, 1> submitted{ &lists.back() };
		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error))
			<< "submitting with the queue full of begun lists was refused";
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));

		// Submitting one list gave its slot back, so a bounded backend has room again.
		rhi::CommandList after = pool.Allocate("azoth.rhi.test.listAfterSubmit", error);
		ASSERT_TRUE(test::Ok(after.is_valid(), error));
		EXPECT_TRUE(test::Ok(after.Begin(error), error)) << "a submitted list did not give its slot back";
		EXPECT_TRUE(test::Ok(after.End(error), error));
	}

	TEST_P(CommandTest, CountsOpenListsCorrectlyWhenSeveralThreadsRecordAtOnce)
	{
		rhi::DeviceDesc desc			 = MakeDeviceDesc();
		desc.maxOpenCommandListsPerQueue = kSmallOpenListBound;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		constexpr std::uint32_t kThreads = 4;
		constexpr std::uint32_t kRounds	 = 20;

		// A pool each, because a command pool is recorded from one thread at a time on every backend here.
		std::array<std::thread, kThreads> threads;
		std::array<bool, kThreads> refused{};

		for (std::uint32_t thread = 0; thread < kThreads; ++thread)
		{
			threads[thread] = std::thread(
				[&local, &refused, thread]
				{
					rhi::Error error{};
					rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
					if (!pool.is_valid())
					{
						refused[thread] = true;
						return;
					}

					for (std::uint32_t round = 0; round < kRounds; ++round)
					{
						rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
						if (!list.is_valid() || !list.Begin(error))
						{
							refused[thread] = true;
							return;
						}

						if (!list.End(error))
						{
							refused[thread] = true;
							return;
						}

						if (!pool.Reset(rhi::RetirePoint{}, error))
						{
							refused[thread] = true;
							return;
						}
					}
				});
		}

		for (std::thread & worker : threads)
		{
			worker.join();
		}

		// Each thread holds at most one slot at a time, so four threads never reach a bound of sixteen unless the count leaks.
		for (std::uint32_t thread = 0; thread < kThreads; ++thread)
		{
			EXPECT_FALSE(refused[thread]) << "thread " << thread << " was refused while well inside the bound, so open slots are being lost";
		}
	}

	TEST_P(CommandTest, GivesEveryOpenSlotBackWhenItsPoolIsReset)
	{
		rhi::DeviceDesc desc			 = MakeDeviceDesc();
		desc.maxOpenCommandListsPerQueue = kSmallOpenListBound;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		std::vector<rhi::CommandList> lists;
		for (std::uint32_t index = 0; index < kSmallOpenListBound; ++index)
		{
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			lists.push_back(list);
			ASSERT_TRUE(test::Ok(lists.back().Begin(error), error)) << "Begin " << index << " was refused below the bound";
		}

		ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error));

		// A second pool, because re-beginning a recycled list would release its own slot and so prove nothing about the reset.
		rhi::CommandPool other = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(other.is_valid(), error));

		std::vector<rhi::CommandList> fresh;
		for (std::uint32_t index = 0; index < kSmallOpenListBound; ++index)
		{
			rhi::CommandList list = other.Allocate("azoth.rhi.test.listAfterReset", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			fresh.push_back(list);
			EXPECT_TRUE(test::Ok(fresh.back().Begin(error), error)) << "Begin " << index << " on a second pool was refused, so the reset kept the slots";
		}

		for (rhi::CommandList & list : fresh)
		{
			EXPECT_TRUE(test::Ok(list.End(error), error));
		}
	}

	TEST_P(CommandTest, RefusesASubmitCarryingAListThatIsStillRecording)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };

		rhi::Error refused{};
		EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, refused))
			<< "the backend submitted a list that was still recording";
		EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

		EXPECT_TRUE(test::Ok(list.End(error), error));
		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
	}

	TEST_P(CommandTest, RefusesASubmitCarryingANeverBegunListAndLeavesItsTimelineAlone)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle timeline = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = timeline, .value = 7 } };

		rhi::Error refused{};
		EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .signals = signals, .debugName = "azoth.rhi.test.submit" }, refused))
			<< "a never begun list was accepted for submission";
		EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

		static_cast<void>(queue.wait_idle(error));

		// The refusal has to take the signals with it, or the caller is handed a timeline value for work that never ran.
		std::uint64_t reached = 0;
		EXPECT_TRUE(test::Ok(queue.get_completed_value(timeline, reached, error), error));
		EXPECT_EQ(reached, 0u) << "a refused submit still advanced the timeline, so a caller fencing on it is told work completed that never ran";

		EXPECT_TRUE(test::Ok(local.Get().destroy(timeline, {}, error), error));
	}

	TEST_P(CommandTest, HandlesASecondSubmitOfTheSameListTheWayItsCapsPromise)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle timeline = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };
		const std::array first{ rhi::TimelinePoint{ .timeline = timeline, .value = 1 } };
		const std::array second{ rhi::TimelinePoint{ .timeline = timeline, .value = 2 } };

		ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .signals = first, .debugName = "azoth.rhi.test.submit" }, error), error));
		ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));

		// Taken rather than assumed, because a backend that processes no signals at all leaves this at zero.
		std::uint64_t before = 0;
		ASSERT_TRUE(test::Ok(queue.get_completed_value(timeline, before, error), error));

		if (local.Get().get_caps().supportsCommandListResubmit)
		{
			// The earlier submission has drained by now, so native allows this one and so must we.
			ASSERT_TRUE(
				test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .signals = second, .debugName = "azoth.rhi.test.resubmit" }, error), error));
			ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));

			std::uint64_t again = 0;
			EXPECT_TRUE(test::Ok(queue.get_completed_value(timeline, again, error), error));
			EXPECT_GE(again, before) << "a backend that allows resubmission ran the work again without moving the timeline forward";
		}
		else
		{
			rhi::Error refused{};
			EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .signals = second, .debugName = "azoth.rhi.test.resubmit" }, refused))
				<< "the same list was submitted twice without being begun again";
			EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

			static_cast<void>(queue.wait_idle(error));

			std::uint64_t reached = 0;
			EXPECT_TRUE(test::Ok(queue.get_completed_value(timeline, reached, error), error));
			EXPECT_EQ(reached, before) << "a refused resubmit still moved the timeline";
		}

		// Begin is how a list is reused, and it must still work whichever way the submit above went.
		EXPECT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));

		EXPECT_TRUE(test::Ok(local.Get().destroy(timeline, {}, error), error));
	}

	// The map sites bound this by the counter's own range, so no backend call can reach either edge. Proven here instead.
	TEST(BoundedCountRule, StopsAtItsBoundAndAtZero)
	{
		rhi::BoundedCount counted;

		EXPECT_TRUE(counted.try_acquire(2));
		EXPECT_TRUE(counted.try_acquire(2));
		EXPECT_FALSE(counted.try_acquire(2)) << "the count went past its bound";
		EXPECT_EQ(counted.load(), 2u);

		EXPECT_TRUE(counted.try_release());
		EXPECT_TRUE(counted.try_release());
		EXPECT_FALSE(counted.try_release()) << "the count went below zero";
		EXPECT_EQ(counted.load(), 0u);
	}

	// No backend here both resubmits and has work in flight, so this is the only place the pending arm of the rule is exercised.
	TEST(SubmitRefusal, AnswersEveryCombinationOfListStateBackendAndPendingWork)
	{
		using rhi::ListLifecycle;
		using rhi::submit_refusal_for;

		for (const bool resubmits : { false, true })
		{
			for (const bool pending : { false, true })
			{
				EXPECT_STREQ(submit_refusal_for(ListLifecycle::eFresh, resubmits, pending), rhi::kSubmitOfNeverBegunList);
				EXPECT_STREQ(submit_refusal_for(ListLifecycle::eRecording, resubmits, pending), rhi::kSubmitOfRecordingList);
				EXPECT_EQ(submit_refusal_for(ListLifecycle::eEnded, resubmits, pending), nullptr) << "an ended list was refused";
			}
		}

		EXPECT_STREQ(submit_refusal_for(ListLifecycle::eSubmitted, false, false), rhi::kSubmitOfSubmittedList);
		EXPECT_STREQ(submit_refusal_for(ListLifecycle::eSubmitted, false, true), rhi::kSubmitOfSubmittedList);

		EXPECT_STREQ(submit_refusal_for(ListLifecycle::eSubmitted, true, true), rhi::kSubmitOfPendingList);
		EXPECT_EQ(submit_refusal_for(ListLifecycle::eSubmitted, true, false), nullptr) << "a drained list was refused on a backend that resubmits";
	}

	TEST_P(CommandTest, BeginsAListAgainAfterItsSubmissionCompletedOnADefaultPool)
	{
		rhi::Error error{};

		// The default pool is eBulkReset, so its buffers cannot be reset in place and Begin has to find another way through.
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };

		for (int round = 0; round < 4; ++round)
		{
			ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "round " << round;
			ASSERT_TRUE(test::Ok(list.End(error), error)) << "round " << round;
			ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error))
				<< "round " << round;
			ASSERT_TRUE(test::Ok(queue.wait_idle(error), error)) << "round " << round;
		}
	}

	TEST_P(CommandTest, BeginsAListAgainWhileItsEarlierSubmissionIsStillExecuting)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		if (std::string_view(CurrentBackend().shortName) == "null")
		{
			GTEST_SKIP() << "nothing executes on null, so a submission is never still in flight to begin over";
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle blocker = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(blocker.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };
		const std::array held{ rhi::TimelinePoint{ .timeline = blocker, .value = 1 } };
		ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .waits = held, .debugName = "azoth.rhi.test.blocked" }, error), error));

		// The buffer underneath is still queued behind a wait nothing has signaled, so Begin has to leave it alone and record somewhere else.
		EXPECT_TRUE(test::Ok(list.Begin(error), error)) << "beginning a list whose earlier submission is still executing was refused";
		EXPECT_TRUE(test::Ok(list.End(error), error));

		EXPECT_TRUE(test::Ok(queue.Signal(blocker, 1, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));

		EXPECT_TRUE(test::Ok(local.Get().destroy(blocker, {}, error), error));
	}

	TEST_P(CommandTest, RefusesToResetAPoolWhoseListIsStillExecuting)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		if (std::string_view(CurrentBackend().shortName) == "null")
		{
			GTEST_SKIP() << "nothing executes on null, so a pool never holds a running list";
		}

		if (!local.Get().get_caps().supportsCommandListResubmit)
		{
			GTEST_SKIP() << "this backend does not track per list completion, so it cannot tell a running list from a finished one";
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle blocker = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(blocker.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };
		const std::array held{ rhi::TimelinePoint{ .timeline = blocker, .value = 1 } };
		ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .waits = held, .debugName = "azoth.rhi.test.blocked" }, error), error));

		rhi::Error refused{};
		EXPECT_FALSE(pool.Reset({}, refused)) << "a pool was reset while one of its lists was still executing";
		EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

		// Let the blocked submission through, after which the same reset has to be allowed.
		EXPECT_TRUE(test::Ok(queue.Signal(blocker, 1, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
		EXPECT_TRUE(test::Ok(pool.Reset({}, error), error)) << "a pool was refused after its work had drained";

		EXPECT_TRUE(test::Ok(local.Get().destroy(blocker, {}, error), error));
	}

	TEST_P(CommandTest, RefusesToResetAPoolWhoseListWasBegunAgainOverARunningSubmission)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		if (std::string_view(CurrentBackend().shortName) == "null")
		{
			GTEST_SKIP() << "nothing executes on null, so a pool never holds a running list";
		}

		if (!local.Get().get_caps().supportsCommandListResubmit)
		{
			GTEST_SKIP() << "this backend does not track per list completion, so it cannot tell a running list from a finished one";
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle blocker = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(blocker.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };
		const std::array held{ rhi::TimelinePoint{ .timeline = blocker, .value = 1 } };
		ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .waits = held, .debugName = "azoth.rhi.test.blocked" }, error), error));

		EXPECT_TRUE(test::Ok(list.Begin(error), error)) << "beginning a list whose earlier submission is still executing was refused";
		EXPECT_TRUE(test::Ok(list.End(error), error));

		// The earlier recording still executes from memory the pool owns, so the pool cannot be reset under it.
		rhi::Error refused{};
		EXPECT_FALSE(pool.Reset({}, refused)) << "a pool was reset while the submission its list was begun over was still executing";
		EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.rerecorded" }, error), error))
			<< "the new recording was refused while the earlier one was still queued";

		EXPECT_TRUE(test::Ok(queue.Signal(blocker, 1, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
		EXPECT_TRUE(test::Ok(pool.Reset({}, error), error)) << "a pool was refused after its work had drained";

		EXPECT_TRUE(test::Ok(local.Get().destroy(blocker, {}, error), error));
	}

	TEST_P(CommandTest, DestroysADeviceWhoseSubmissionIsBlockedOnAWaitNobodySignals)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		// The harness installs a callback on every device, so it is dropped here to leave stderr as the only place a leak can be announced.
		desc.nativeValidation.onMessage		  = nullptr;
		desc.nativeValidation.messageUserData = nullptr;
		testing::internal::CaptureStderr();

		const auto started = std::chrono::steady_clock::now();
		{
			const test::DeviceHarness local(CurrentBackend(), desc);
			if (!local.IsValid())
			{
				static_cast<void>(testing::internal::GetCapturedStderr());
				GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
			}

			rhi::Error error{};
			rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
			ASSERT_TRUE(test::Ok(queue.is_valid(), error));

			const rhi::TimelineHandle blocker = local.Get().create_timeline(test::samples::Timeline(), error);
			ASSERT_TRUE(test::Ok(blocker.is_valid(), error));

			rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
			ASSERT_TRUE(test::Ok(pool.is_valid(), error));

			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			ASSERT_TRUE(test::Ok(list.Begin(error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> submitted{ &list };
			const std::array held{ rhi::TimelinePoint{ .timeline = blocker, .value = 1 } };
			ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .waits = held, .debugName = "azoth.rhi.test.stuck" }, error), error));

			// Nothing ever signals the blocker, so this device goes out of scope with work that cannot complete.
		}

		// Four seconds sits between our own two second bound and MoltenVK's watchdog near five, so this fails if teardown
		// goes back to waiting on the driver to rescue it rather than on a bound of ours.
		const auto spent		   = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
		const std::string reported = testing::internal::GetCapturedStderr();
		EXPECT_LT(spent.count(), 4000) << "destroying a device with an unsatisfiable wait took " << spent.count() << "ms, so the drain is not bounded by us";

		// Spending the whole bound is the drain giving up, and giving up means a device was leaked, which has to be said out loud.
		if (spent.count() >= 1900)
		{
			EXPECT_NE(reported.find("did not drain in time"), std::string::npos)
				<< "a device leaked its objects without reporting it, stderr held: " << reported;
		}
	}

	TEST_P(CommandTest, DestroysADeviceThatStillHasSubmittedWorkInFlight)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		rhi::Error error{};
		{
			const test::DeviceHarness local(CurrentBackend(), desc);
			if (!local.IsValid())
			{
				GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
			}

			rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
			ASSERT_TRUE(test::Ok(queue.is_valid(), error));

			rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
			ASSERT_TRUE(test::Ok(pool.is_valid(), error));

			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			ASSERT_TRUE(test::Ok(list.Begin(error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> submitted{ &list };
			ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error));

			// Deliberately no WaitIdle. The device goes out of scope here with that submission possibly still running.
		}

		SUCCEED() << "the device tore down without waiting on its own submitted work";
	}

	// The native layer reports through a plain function pointer, so the count it feeds has to live somewhere the callback can reach.
	struct NativeMessages final
	{
		std::atomic<int> errors{ 0 };
		char first[256]{};

		static void Record(rhi::ValidationMessageSeverity severity, const char * message, void * userData) noexcept
		{
			auto * self = static_cast<NativeMessages *>(userData);
			if (severity != rhi::ValidationMessageSeverity::eError)
			{
				return;
			}

			if (self->errors.fetch_add(1, std::memory_order_relaxed) == 0 && message != nullptr)
			{
				// Copied rather than kept by pointer, because the layer owns the text only for the length of this call.
				char * out = self->first;
				for (std::size_t room = sizeof(self->first) - 1; room > 0 && *message != '\0'; --room)
				{
					*out++ = *message++;
				}
			}
		}
	};

	// Metal compiles source at run time, which is the only compute pipeline the command suite can build without a shader compiler of its own.
	constexpr const char * kCountingKernel = R"MSL(
#include <metal_stdlib>
kernel void azothRhiTestCount(uint index [[thread_position_in_grid]])
{
}
)MSL";

	TEST_P(CommandTest, WritesATimestampBetweenBindingAComputePipelineAndDispatchingIt)
	{
		if (!Caps().supportsTimestampQueries)
		{
			GTEST_SKIP() << "this device reports no timestamp queries, so there is no write to put between the two";
		}
		if (!Caps().supportsShaderSource)
		{
			GTEST_SKIP() << "this backend takes no shader source, and the command suite has no shader compiler to make a binary with";
		}

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().create_pipeline_layout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		const rhi::ShaderBinary kernel{
			.stage			 = rhi::ShaderStage::eCompute,
			.format			 = rhi::ShaderBinaryFormat::eBackendNative,
			.data			 = kCountingKernel,
			.size			 = std::strlen(kCountingKernel),
			.entryPoint		 = "azothRhiTestCount",
			.isSource		 = true,
			.threadgroupSize = { .x = 1, .y = 1, .z = 1 },
		};

		const rhi::ComputePipelineHandle pipeline =
			Dev().create_compute_pipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = kernel, .debugName = "azoth.rhi.test.counting" }, error);
		if (!pipeline.is_valid())
		{
			static_cast<void>(Dev().destroy(layout, {}, error));
			GTEST_SKIP() << "this backend refused a source compute pipeline: " << test::Describe(error);
		}

		const rhi::QueryPoolHandle pool = Dev().create_query_pool(test::samples::TimestampPool(2), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().reset_query_pool(pool, 0, 2, error), error));
			EXPECT_TRUE(test::Ok(recording.List().set_compute_pipeline(pipeline, error), error));

			// An adapter that samples only at stage boundaries refuses this write and says so, which is an answer rather than a failure.
			rhi::Error wrote{};
			if (!recording.List().write_timestamp(pool, 0, rhi::Stage::eCompute, wrote))
			{
				EXPECT_EQ(wrote.code, rhi::ErrorCode::eUnsupportedFeature) << "a mid dispatch timestamp was refused for an unexpected reason";
			}

			// Whichever answer that was, the write must not have cost the caller the pipeline it had already bound.
			EXPECT_TRUE(test::Ok(recording.List().Dispatch(1, 1, 1, error), error)) << "a dispatch was refused after a timestamp was written before it";

			ASSERT_TRUE(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(pool, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(pipeline, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(layout, {}, error), error));
	}

	TEST_P(CommandTest, DispatchesNothingWhenAGroupCountIsZero)
	{
		if (!Caps().supportsShaderSource)
		{
			GTEST_SKIP() << "this backend takes no shader source, and the command suite has no shader compiler to make a binary with";
		}

		rhi::Error error{};
		const rhi::PipelineLayoutHandle layout = Dev().create_pipeline_layout(rhi::PipelineLayoutDesc{ .debugName = "azoth.rhi.test.layout" }, error);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		const rhi::ShaderBinary kernel{
			.stage			 = rhi::ShaderStage::eCompute,
			.format			 = rhi::ShaderBinaryFormat::eBackendNative,
			.data			 = kCountingKernel,
			.size			 = std::strlen(kCountingKernel),
			.entryPoint		 = "azothRhiTestCount",
			.isSource		 = true,
			.threadgroupSize = { .x = 1, .y = 1, .z = 1 },
		};

		const rhi::ComputePipelineHandle pipeline =
			Dev().create_compute_pipeline(rhi::ComputePipelineDesc{ .layout = layout, .shader = kernel, .debugName = "azoth.rhi.test.counting" }, error);
		if (!pipeline.is_valid())
		{
			static_cast<void>(Dev().destroy(layout, {}, error));
			GTEST_SKIP() << "this backend refused a source compute pipeline: " << test::Describe(error);
		}

		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.emptyDispatch", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		EXPECT_TRUE(test::Ok(list.set_compute_pipeline(pipeline, error), error));
		EXPECT_TRUE(test::Ok(list.Dispatch(0, 1, 1, error), error)) << "a dispatch with no groups along x was refused";
		EXPECT_TRUE(test::Ok(list.Dispatch(1, 0, 1, error), error)) << "a dispatch with no groups along y was refused";
		EXPECT_TRUE(test::Ok(list.Dispatch(1, 1, 0, error), error)) << "a dispatch with no groups along z was refused";
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		EXPECT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = lists, .debugName = "azoth.rhi.test.emptyDispatch" }, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));

		EXPECT_TRUE(test::Ok(Dev().destroy(pipeline, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(layout, {}, error), error));
	}

	TEST_P(CommandTest, ReusesAListTheSameWayWhicheverResetModeItsPoolAsked)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();

		// The decorator refuses a re-Begin while recording, so only a device without it reaches the backend rule under test.
		desc.validation = rhi::ValidationMode::eOff;

		// With the decorator off the native layer is what can still see an invalid Begin, so turn it on independently and count what it says.
		NativeMessages seen{};
		desc.nativeValidation.apiValidation	  = rhi::NativeValidationPolicy::eEnabled;
		desc.nativeValidation.onMessage		  = &NativeMessages::Record;
		desc.nativeValidation.messageUserData = &seen;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		for (const rhi::ListReuse reuse : { rhi::ListReuse::eBulkReset, rhi::ListReuse::ePerListReset })
		{
			const bool perList = reuse == rhi::ListReuse::ePerListReset;

			rhi::CommandPoolDesc poolDesc = test::samples::CommandPool();
			poolDesc.reuse				  = reuse;

			rhi::Error made{};
			rhi::CommandPool pool = local.Get().create_command_pool(poolDesc, made);
			if (!pool.is_valid())
			{
				EXPECT_EQ(made.code, rhi::ErrorCode::eUnsupportedFeature) << "a pool reuse mode was refused for an unexpected reason";
				continue;
			}

			rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error)) << "perListReset " << perList;

			std::array<const rhi::CommandList *, 1> submitted{ &list };

			for (int round = 0; round < 3; ++round)
			{
				ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "perListReset " << perList << " round " << round;
				ASSERT_TRUE(test::Ok(list.End(error), error)) << "perListReset " << perList << " round " << round;
				ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error));
				ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));
			}

			// Beginning a list that is still recording discards that recording, whichever reset mode the pool asked for.
			EXPECT_TRUE(test::Ok(list.Begin(error), error)) << "perListReset " << perList;
			EXPECT_TRUE(test::Ok(list.Begin(error), error)) << "re-Begin while recording was refused, perListReset " << perList;
			EXPECT_TRUE(test::Ok(list.End(error), error)) << "perListReset " << perList;

			// And beginning one whose submission has completed works the same way.
			ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.submit" }, error), error));
			ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));
			EXPECT_TRUE(test::Ok(list.Begin(error), error)) << "re-Begin after completion was refused, perListReset " << perList;
			EXPECT_TRUE(test::Ok(list.End(error), error)) << "perListReset " << perList;
		}

		// Without this the test only asserts Begin returned true, and a returning VUID-00049 would pass in silence.
		EXPECT_EQ(seen.errors.load(), 0) << "the native validation layer reported " << seen.errors.load() << " error(s), first: " << seen.first;
	}

	TEST_P(CommandTest, AsksForQueuesWhileAnotherThreadIsSubmitting)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;
		desc.threading		 = rhi::ThreadingMode::eThreads;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		rhi::Queue graphics = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(graphics.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		std::atomic<bool> stop{ false };

		// Submit holds whatever per queue bookkeeping the backend keeps, while GetQueue on another thread may touch the same store.
		std::thread submitter(
			[&]
			{
				rhi::Error mine{};
				std::array<const rhi::CommandList *, 1> one{ &list };
				for (int round = 0; round < 200 && !stop.load(); ++round)
				{
					if (!list.Begin(mine) || !list.End(mine))
					{
						break;
					}
					if (!graphics.submit(rhi::SubmitDesc{ .commandLists = one, .debugName = "azoth.rhi.test.submit" }, mine))
					{
						break;
					}
					if (!graphics.wait_idle(mine))
					{
						break;
					}
				}
				stop.store(true);
			});

		std::thread fetcher(
			[&]
			{
				rhi::Error mine{};
				constexpr std::array asked{ rhi::QueueType::eCompute, rhi::QueueType::eCopy, rhi::QueueType::eGraphics };
				for (std::size_t round = 0; round < 600 && !stop.load(); ++round)
				{
					static_cast<void>(local.Get().get_queue(asked[round % asked.size()], 0, mine));
				}
			});

		fetcher.join();
		submitter.join();

		EXPECT_TRUE(test::Ok(graphics.wait_idle(error), error));
	}

	TEST_P(CommandTest, ReportsWhetherItCanSubmitACommandListTwice)
	{
		const std::string_view backend = CurrentBackend().shortName;

		// Vulkan records no usage flags so a drained buffer may go again, and null has nothing in flight at all. Both Metal backends commit once.
		// ExecuteCommandLists refuses a list only while a previous execution of it has not completed, so a finished D3D12 list may be submitted again.
		const bool expected = backend == "null" || backend == "vulkan" || backend == "d3d12";

		EXPECT_EQ(Dev().get_caps().supportsCommandListResubmit, expected) << "the resubmit cap does not match what " << backend << " can actually do";
	}

	TEST_P(CommandTest, RefusesASubmitOfAListWhoseEarlierSubmissionIsStillExecuting)
	{
		rhi::DeviceDesc desc = MakeDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eOff;

		const test::DeviceHarness local(CurrentBackend(), desc);
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		if (!local.Get().get_caps().supportsCommandListResubmit)
		{
			GTEST_SKIP() << "this backend refuses every resubmit, so a pending one cannot be told apart";
		}

		if (std::string_view(CurrentBackend().shortName) == "null")
		{
			GTEST_SKIP() << "nothing executes on null, so a submission is never still in flight to catch";
		}

		rhi::Error error{};
		rhi::Queue queue = local.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		const rhi::TimelineHandle blocker = local.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(blocker.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> submitted{ &list };

		// Nothing has signaled 1 yet, so this submission cannot start, which is what makes the refusal below deterministic.
		const std::array held{ rhi::TimelinePoint{ .timeline = blocker, .value = 1 } };
		ASSERT_TRUE(test::Ok(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .waits = held, .debugName = "azoth.rhi.test.blocked" }, error), error));

		rhi::Error refused{};
		EXPECT_FALSE(queue.submit(rhi::SubmitDesc{ .commandLists = submitted, .debugName = "azoth.rhi.test.whilePending" }, refused))
			<< "a list was submitted again while its earlier submission was still executing";
		EXPECT_EQ(refused.code, rhi::ErrorCode::eInvalidState);

		// Let the blocked submission through so the device can drain before teardown.
		EXPECT_TRUE(test::Ok(queue.Signal(blocker, 1, error), error));
		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));

		EXPECT_TRUE(test::Ok(local.Get().destroy(blocker, {}, error), error));
	}

	TEST_P(CommandTest, RebeginsOneListFarMoreTimesThanTheBackendHoldsWithoutEverSubmittingIt)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		for (int round = 0; round < 200; ++round)
		{
			ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "Begin was refused on round " << round << ", so a discarded recording never gave its slot back";
			ASSERT_TRUE(test::Ok(list.End(error), error)) << "End was refused on round " << round;
		}
	}

	TEST_P(CommandTest, DestroysADeviceHoldingAListThatPushedConstantsAndNeverEnded)
	{
		const test::DeviceHarness local(CurrentBackend(), MakeDeviceDesc());
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		const test::samples::UniformLayout setLayout;
		const rhi::DescriptorSetLayoutHandle set = local.Get().create_descriptor_set_layout(setLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(set.is_valid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(set);
		const rhi::PipelineLayoutHandle layout = local.Get().create_pipeline_layout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		const std::array<std::uint32_t, 4> constants{ 1, 2, 3, 4 };
		EXPECT_TRUE(test::Ok(list.push_constants(layout, rhi::ShaderStage::eAll, 0, sizeof(constants), constants.data(), error), error));
	}

	TEST_P(CommandTest, BeginsARecycledListThatPushedConstantsAndNeverEnded)
	{
		const test::DeviceHarness local(CurrentBackend(), MakeDeviceDesc());
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		const test::samples::UniformLayout setLayout;
		const rhi::DescriptorSetLayoutHandle set = local.Get().create_descriptor_set_layout(setLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(set.is_valid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout(set);
		const rhi::PipelineLayoutHandle layout = local.Get().create_pipeline_layout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		const std::array<std::uint32_t, 4> constants{ 1, 2, 3, 4 };
		ASSERT_TRUE(test::Ok(list.push_constants(layout, rhi::ShaderStage::eAll, 0, sizeof(constants), constants.data(), error), error));

		ASSERT_TRUE(test::Ok(pool.Reset(rhi::RetirePoint{}, error), error));

		rhi::CommandList recycled = pool.Allocate("azoth.rhi.test.listAgain", error);
		ASSERT_TRUE(test::Ok(recycled.is_valid(), error));
		EXPECT_TRUE(test::Ok(recycled.Begin(error), error));
	}

	TEST_P(CommandTest, DestroysADeviceHoldingAListLeftInsideARenderingScope)
	{
		const test::DeviceHarness local(CurrentBackend(), MakeDeviceDesc());
		if (!local.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(local.GetError());
		}

		rhi::Error error{};
		const rhi::TextureHandle target = local.Get().create_texture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		const rhi::TextureViewHandle view = local.Get().create_texture_view(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.is_valid(), error));

		const std::array colors{
			rhi::RenderingAttachment{
				.view  = view,
				.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load  = rhi::LoadOp::eClear,
				.store = rhi::StoreOp::eStore,
			},
		};
		const rhi::BeginRenderingDesc rendering{
			.colors		  = colors,
			.depthStencil = nullptr,
			.x			  = 0,
			.y			  = 0,
			.width		  = 64,
			.height		  = 64,
			.layers		  = 1,
		};

		rhi::CommandPool pool = local.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		if (!list.begin_rendering(rendering, error))
		{
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}
	}

	TEST_P(CommandTest, ADefaultConstructedPoolAndListAreInert)
	{
		EXPECT_FALSE(rhi::CommandPool{}.is_valid());
		EXPECT_FALSE(rhi::CommandList{}.is_valid());
	}

}

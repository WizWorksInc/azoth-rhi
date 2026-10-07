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

#include "azoth/rhi/device/device.hpp"

#include "conformance/backend_contract.hpp"
#include "conformance/matchers.hpp"
#include "conformance/overload_contract.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BackendContractTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BackendContractTest);

	TEST_P(BackendContractTest, SatisfiesTheDeviceIdentityContract)
	{
		test::oracle::CheckDeviceIdentity(Dev(), CurrentBackend());
	}

	TEST_P(BackendContractTest, SatisfiesTheQueueContract)
	{
		test::oracle::CheckQueueAvailability(Dev());
	}

	TEST_P(BackendContractTest, SatisfiesTheResourceLifecycleContract)
	{
		test::oracle::CheckResourceLifecycle(Dev());
	}

	TEST_P(BackendContractTest, SatisfiesTheErrorReportingContract)
	{
		test::oracle::CheckOverloadsAgree(Dev());
		test::oracle::CheckFailuresCarryDiagnostics(Dev());
	}

	TEST_P(BackendContractTest, SatisfiesTheCommandListContract)
	{
		test::oracle::CheckCommandListLifecycle(Dev());
	}

	TEST_P(BackendContractTest, SatisfiesTheGarbageCollectionContract)
	{
		test::oracle::CheckGarbageCollection(Dev());
	}

	TEST_P(BackendContractTest, SatisfiesTheWholeContractInOneRun)
	{
		test::oracle::CheckWholeContract(Dev(), CurrentBackend());
	}

	TEST_P(BackendContractTest, SurvivesTheContractRunTwiceOnOneDevice)
	{
		test::oracle::CheckWholeContract(Dev(), CurrentBackend());
		test::oracle::CheckWholeContract(Dev(), CurrentBackend());
	}

	TEST_P(BackendContractTest, RunsAWholeFrameShapedSequenceEndToEnd)
	{
		rhi::Error error{};

		const rhi::TimelineHandle frameTimeline = Dev().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(frameTimeline.is_valid(), error));

		const rhi::BufferHandle upload = Dev().create_buffer(test::samples::UploadBuffer(), error);
		const rhi::BufferHandle target = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(upload.is_valid(), error));
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.frame", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		const std::array toCopy{
			rhi::BufferBarrier{
				.buffer = target,
				.before = { .use = rhi::ResourceUse::eDiscard },
				.after	= { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			},
		};
		ASSERT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));
		ASSERT_TRUE(test::Ok(list.copy_buffer(target, 0, upload, 0, test::samples::kBufferSize, error), error));
		ASSERT_TRUE(test::Ok(list.end(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{
			rhi::TimelinePoint{ .timeline = frameTimeline, .value = 1, .waitStages = rhi::Stage::eAllCommands },
		};

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = signals,
			.debugName	  = "azoth.rhi.test.frameSubmit",
		};
		ASSERT_TRUE(test::Ok(queue.submit(submit, error), error));
		ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));

		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = frameTimeline, .value = 1 },
		};
		EXPECT_TRUE(test::Ok(Dev().destroy(target, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(upload, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().collect_garbage(frameTimeline, 1, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(frameTimeline, {}, error), error));

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "the frame sequence produced native validation errors");
	}

	TEST_P(BackendContractTest, RunsSeveralFramesThroughOnePoolWithoutDrift)
	{
		constexpr std::uint64_t kFrames = 8;

		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.is_valid(), error));

		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		rhi::CommandPool pool = Dev().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		for (std::uint64_t frame = 1; frame <= kFrames; ++frame)
		{
			const rhi::BufferHandle transient = Dev().create_buffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(transient.is_valid(), error)) << "frame " << frame << " could not allocate";

			rhi::CommandList list = pool.allocate("azoth.rhi.test.frameList", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error)) << "frame " << frame << " could not allocate a list";
			ASSERT_TRUE(test::Ok(list.begin(error), error)) << "frame " << frame << " could not begin";
			ASSERT_TRUE(test::Ok(list.clear_buffer(transient, 0, test::samples::kBufferSize, 0, error), error));
			ASSERT_TRUE(test::Ok(list.end(error), error));

			std::array<const rhi::CommandList *, 1> lists{ &list };
			const std::array signals{
				rhi::TimelinePoint{ .timeline = timeline, .value = frame, .waitStages = rhi::Stage::eAllCommands },
			};
			ASSERT_TRUE(test::Ok(queue.submit(
									 rhi::SubmitDesc{
										 .commandLists = lists,
										 .signals	   = signals,
									 },
									 error),
				error))
				<< "frame " << frame;
			ASSERT_TRUE(test::Ok(queue.wait_idle(error), error));

			const rhi::DestroyDesc retired{
				.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
				.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = frame },
			};
			ASSERT_TRUE(test::Ok(Dev().destroy(transient, retired, error), error));
			ASSERT_TRUE(test::Ok(Dev().collect_garbage(timeline, frame, error), error));
			ASSERT_TRUE(test::Ok(pool.reset(
									 rhi::RetirePoint{
										 .timeline = timeline,
										 .value	   = frame,
									 },
									 error),
				error))
				<< "frame " << frame << " could not reset its pool";
		}

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "running " << kFrames << " frames produced native validation errors");
		EXPECT_TRUE(test::Ok(Dev().destroy(timeline, {}, error), error));
	}

	TEST_P(BackendContractTest, KeepsTwoDevicesOnTheSameBackendIndependent)
	{
		test::DeviceHarness second(CurrentBackend(), MakeDeviceDesc());
		if (!second.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(second.GetError());
		}

		test::oracle::CheckWholeContract(Dev(), CurrentBackend());
		test::oracle::CheckWholeContract(second.Get(), CurrentBackend());

		test::oracle::CheckResourceLifecycle(Dev());
	}

}

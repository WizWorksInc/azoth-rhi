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

		const rhi::TimelineHandle frameTimeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(frameTimeline.IsValid(), error));

		const rhi::BufferHandle upload = Dev().CreateBuffer(test::samples::UploadBuffer(), error);
		const rhi::BufferHandle target = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(upload.IsValid(), error));
		ASSERT_TRUE(test::Ok(target.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.frame", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		const std::array toCopy{ rhi::BufferBarrier{ .buffer = target,
			.before										= { .stages = rhi::PipelineStage::eNone, .access = rhi::Access::eNone, .layout = rhi::TextureLayout::eUndefined, },
			.after = { .stages = rhi::PipelineStage::eCopy, .access = rhi::Access::eCopyWrite, .layout = rhi::TextureLayout::eUndefined } } };
		ASSERT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));
		ASSERT_TRUE(test::Ok(list.CopyBuffer(target, 0, upload, 0, test::samples::kBufferSize, error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{
			rhi::TimelinePoint{ .timeline = frameTimeline, .value = 1, .waitStages = rhi::PipelineStage::eAllCommands },
		};

		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.signals	  = signals,
			.debugName	  = "azoth.rhi.test.frameSubmit",
		};
		ASSERT_TRUE(test::Ok(queue.Submit(submit, error), error));
		ASSERT_TRUE(test::Ok(queue.WaitIdle(error), error));

		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = frameTimeline, .value = 1 },
		};
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(upload, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(frameTimeline, 1, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(frameTimeline, {}, error), error));

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "the frame sequence produced native validation errors");
	}

	TEST_P(BackendContractTest, RunsSeveralFramesThroughOnePoolWithoutDrift)
	{
		constexpr std::uint64_t kFrames = 8;

		rhi::Error error{};
		const rhi::TimelineHandle timeline = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.IsValid(), error));

		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));

		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		for (std::uint64_t frame = 1; frame <= kFrames; ++frame)
		{
			const rhi::BufferHandle transient = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(transient.IsValid(), error)) << "frame " << frame << " could not allocate";

			rhi::CommandList list = pool.Allocate("azoth.rhi.test.frameList", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error)) << "frame " << frame << " could not allocate a list";
			ASSERT_TRUE(test::Ok(list.Begin(error), error)) << "frame " << frame << " could not begin";
			ASSERT_TRUE(test::Ok(list.ClearBuffer(transient, 0, test::samples::kBufferSize, 0, error), error));
			ASSERT_TRUE(test::Ok(list.End(error), error));

			std::array<const rhi::CommandList *, 1> lists{ &list };
			const std::array signals{
				rhi::TimelinePoint{ .timeline = timeline, .value = frame, .waitStages = rhi::PipelineStage::eAllCommands },
			};
			ASSERT_TRUE(test::Ok(queue.Submit(
									 rhi::SubmitDesc{
										 .commandLists = lists,
										 .signals	   = signals,
									 },
									 error),
				error))
				<< "frame " << frame;
			ASSERT_TRUE(test::Ok(queue.WaitIdle(error), error));

			const rhi::DestroyDesc retired{
				.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
				.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = frame },
			};
			ASSERT_TRUE(test::Ok(Dev().Destroy(transient, retired, error), error));
			ASSERT_TRUE(test::Ok(Dev().CollectGarbage(timeline, frame, error), error));
			ASSERT_TRUE(test::Ok(pool.Reset(
									 rhi::RetirePoint{
										 .timeline = timeline,
										 .value	   = frame,
									 },
									 error),
				error))
				<< "frame " << frame << " could not reset its pool";
		}

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "running " << kFrames << " frames produced native validation errors");
		EXPECT_TRUE(test::Ok(Dev().Destroy(timeline, {}, error), error));
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

} // namespace

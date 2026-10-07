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
#include "azoth/rhi/host/profiler.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/spies.hpp"

#ifdef TRACY_ENABLE
	#include "azoth/rhi/host/tracy_profiler.hpp"
#endif

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	TEST(ProfilerSeam, NothingIsInstalledByDefault)
	{
		EXPECT_EQ(rhi::get_profiler(), nullptr);
	}

	TEST(ProfilerSeam, InstallingIsVisibleAndReversible)
	{
		test::RecordingProfiler sink;
		{
			const test::ScopedProfiler scope(&sink);
			EXPECT_EQ(rhi::get_profiler(), &sink);
		}
		EXPECT_EQ(rhi::get_profiler(), nullptr);
	}

	TEST(ProfilerSeam, ClearingWithNullIsSafe)
	{
		const test::ScopedProfiler scope(nullptr);
		EXPECT_EQ(rhi::get_profiler(), nullptr);
	}

	TEST(ProfilerSeam, TheSinkIsNeitherCopyableNorMovable)
	{
		static_assert(!std::is_copy_constructible_v<rhi::Profiler>);
		static_assert(!std::is_move_constructible_v<rhi::Profiler>);

		SUCCEED();
	}

	TEST(ProfilerSeam, EveryMethodHasADoNothingDefault)
	{
		class SilentProfiler final : public rhi::Profiler
		{
		};

		SilentProfiler sink;
		const test::ScopedProfiler scope(&sink);

		sink.begin_zone(
			rhi::ZoneLocation{
				.name  = "test",
				.file  = __FILE__,
				.line  = __LINE__,
				.color = 0,
			}
		);
		sink.end_zone();
		sink.plot("test", 1);
		sink.gpu_free(nullptr, "test");
		sink.leave_fiber(rhi::FiberId{ 1 });
		sink.enter_fiber(rhi::FiberId{ 1 }, nullptr);

		rhi::CommandList unset;
		EXPECT_FALSE(sink.initialize_gpu(rhi::Device{}, unset));
	}

	TEST(ProfilerSeam, InstallingForAScopePutsBackWhatItDisplaced)
	{
		test::RecordingProfiler outer;
		test::RecordingProfiler inner;

		const rhi::ScopedProfiler outerScope(&outer);
		ASSERT_EQ(rhi::get_profiler(), &outer);
		{
			const rhi::ScopedProfiler innerScope(&inner);
			EXPECT_EQ(rhi::get_profiler(), &inner);
			EXPECT_EQ(innerScope.previous(), &outer);
		}
		EXPECT_EQ(rhi::get_profiler(), &outer);
	}

	TEST(ProfilerSeam, ABroadcastReachesEverySink)
	{
		test::RecordingProfiler first;
		test::RecordingProfiler second;
		const std::array<rhi::Profiler *, 2> sinks{ &first, &second };
		rhi::BroadcastProfiler broadcast(sinks);

		const rhi::ScopedProfiler scope(&broadcast);
		rhi::get_profiler()->begin_zone(rhi::ZoneLocation{ .name = "broadcast", .file = __FILE__, .line = __LINE__ });
		rhi::get_profiler()->end_zone();
		rhi::get_profiler()->plot("depth", 3);
		rhi::get_profiler()->enter_fiber(rhi::FiberId{ 7 }, "worker");

		EXPECT_EQ(first.beginZoneCalls, 1);
		EXPECT_EQ(second.beginZoneCalls, 1);
		EXPECT_EQ(first.endZoneCalls, 1);
		EXPECT_EQ(second.endZoneCalls, 1);
		EXPECT_EQ(first.plots.size(), 1u);
		EXPECT_EQ(second.plots.size(), 1u);

		ASSERT_EQ(first.fiberEvents.size(), 1u);
		ASSERT_EQ(second.fiberEvents.size(), 1u);
		EXPECT_EQ(first.fiberEvents.front().fiber, rhi::FiberId{ 7 });
		EXPECT_EQ(second.fiberEvents.front().name, "worker");
	}

	TEST(ProfilerSeam, ABroadcastSkipsNullSinksRatherThanCrashing)
	{
		test::RecordingProfiler only;
		const std::array<rhi::Profiler *, 3> sinks{ nullptr, &only, nullptr };
		rhi::BroadcastProfiler broadcast(sinks);

		broadcast.begin_zone(rhi::ZoneLocation{ .name = "sparse", .file = __FILE__, .line = __LINE__ });
		broadcast.end_zone();

		EXPECT_EQ(only.beginZoneCalls, 1);
		EXPECT_EQ(only.endZoneCalls, 1);

		rhi::CommandList unset;
		EXPECT_FALSE(broadcast.initialize_gpu(rhi::Device{}, unset));
	}

	class ProfilerTest : public test::BackendTest
	{
	protected:
		void TearDown() override
		{
			EXPECT_EQ(rhi::get_profiler(), nullptr);
		}

		test::RecordingProfiler m_sink;
	};

	AZO_RHI_BACKEND_SUITE(ProfilerTest);

	TEST_P(ProfilerTest, BringingUpADeviceWithNoSinkInstalledIsSafe)
	{
		ASSERT_EQ(rhi::get_profiler(), nullptr);

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(buffer.is_valid(), error));
	}

#ifdef AZOTH_RHI_ENABLE_PROFILING

	TEST_P(ProfilerTest, LibraryWorkReachesAnInstalledSink)
	{
		const test::ScopedProfiler scope(&m_sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		EXPECT_GT(m_sink.beginZoneCalls, 0) << "the RHI's own zones are not reaching an installed sink";
		EXPECT_EQ(m_sink.beginZoneCalls, m_sink.endZoneCalls) << "zones are unbalanced";
		EXPECT_EQ(m_sink.depth, 0) << "a zone was left open";
		EXPECT_GT(m_sink.maxDepth, 0);
		EXPECT_TRUE(m_sink.everySiteHadFileAndLine) << "a zone location is missing its file or line";
		EXPECT_FALSE(m_sink.zoneNames.empty());
	}

	TEST_P(ProfilerTest, ZoneNamesAreNamespaced)
	{
		const test::ScopedProfiler scope(&m_sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		ASSERT_FALSE(m_sink.zoneNames.empty());

		for (const std::string & name : m_sink.zoneNames)
		{
			EXPECT_TRUE(name.starts_with("rhi.")) << "unprefixed zone name: " << name;
		}
	}

	TEST_P(ProfilerTest, ZonesStayBalancedAcrossAWholeRecording)
	{
		const test::ScopedProfiler scope(&m_sink);

		rhi::Error error{};
		const rhi::BufferHandle source		= Dev().create_buffer(test::samples::UploadBuffer(), error);
		const rhi::BufferHandle destination = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(source.is_valid(), error));
		ASSERT_TRUE(test::Ok(destination.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
			EXPECT_TRUE(test::Ok(recording.List().copy_buffer(destination, 0, source, 0, test::samples::kBufferSize, error), error));
			EXPECT_TRUE(recording.End());
		}

		EXPECT_EQ(m_sink.beginZoneCalls, m_sink.endZoneCalls);
		EXPECT_EQ(m_sink.depth, 0);
	}

	TEST_P(ProfilerTest, InstallingMidRunPicksUpTheWorkThatFollows)
	{
		rhi::Error error{};
		const rhi::BufferHandle before = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(before.is_valid(), error));
		EXPECT_EQ(m_sink.beginZoneCalls, 0);

		const test::ScopedProfiler scope(&m_sink);
		const rhi::BufferHandle after = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(after.is_valid(), error));

		EXPECT_GT(m_sink.beginZoneCalls, 0);
	}

#else

	TEST_P(ProfilerTest, CompiledOutProfilingNeverCallsTheSink)
	{
		const test::ScopedProfiler scope(&m_sink);

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		EXPECT_EQ(m_sink.beginZoneCalls, 0) << "instrumentation ran with AZOTH_RHI_ENABLE_PROFILING off";
		EXPECT_EQ(m_sink.endZoneCalls, 0);
		EXPECT_TRUE(m_sink.zoneNames.empty());
	}

#endif

#ifdef TRACY_ENABLE

	TEST_P(ProfilerTest, TheTracySinkHandlesRealWorkWithoutUnbalancingZones)
	{
		rhi::TracyProfiler tracy;
		const test::ScopedProfiler scope(&tracy);

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(buffer.is_valid(), error));
	}

	TEST(ProfilerSeam, TheTracySinkKeepsOneStableNamePerFiber)
	{
		constexpr std::uint64_t kFibers = 8;

		rhi::TracyProfiler tracy;

		for (std::uint64_t pass = 0; pass < 3; ++pass)
		{
			for (std::uint64_t id = 1; id <= kFibers; ++id)
			{
				const rhi::FiberId fiber{ id };

				tracy.enter_fiber(fiber, pass == 0 ? "azoth.rhi.test.fiber" : nullptr);
				tracy.leave_fiber(fiber);
			}
		}
	}

#endif

}

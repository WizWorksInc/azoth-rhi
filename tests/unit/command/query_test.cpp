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
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class QueryTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(QueryTest);

	constexpr std::uint32_t kPoolQueries  = 8;
	constexpr std::uint64_t kResultStride = sizeof(std::uint64_t);

	// What a backend writes into a slot nothing sampled. Metal resolves MTLCounterErrorValue there, not zero, and a test reading a pair back has to be
	// able to tell that apart from a plausible time.
	constexpr std::uint64_t kUnwritten = ~0ull;

	[[nodiscard]] bool SubmitAndWait(rhi::Device device, rhi::CommandList & list, rhi::Error & error)
	{
		rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics, 0, error);
		if (!queue.IsValid())
		{
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const rhi::SubmitDesc submit{
			.commandLists = lists,
			.debugName	  = "azoth.rhi.test.querySubmit",
		};
		return queue.Submit(submit, error) && queue.WaitIdle(error);
	}

	[[nodiscard]] bool ReadTimestampPair(rhi::Device device, rhi::BufferHandle results, std::uint64_t & first, std::uint64_t & second, rhi::Error & error)
	{
		const rhi::MappedMemory mapped = device.Map(results,
			rhi::MapDesc{
				.mode	= rhi::MapMode::eRead,
				.offset = 0,
				.size	= kResultStride * 2,
			},
			error);
		if (mapped.data == nullptr)
		{
			return false;
		}

		std::array<std::uint64_t, 2> values{};
		std::memcpy(values.data(), mapped.data, sizeof(values));
		static_cast<void>(device.Unmap(results));

		first  = values[0];
		second = values[1];
		return true;
	}

	TEST_P(QueryTest, ResolvesAPairOfTimestampsThatRunForward)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error)) << "a device reporting timestamp queries refused to create a pool";

		const rhi::BufferHandle results = Dev().CreateBuffer(test::samples::ReadbackBuffer(), error);
		ASSERT_TRUE(test::Ok(results.IsValid(), error));
		const rhi::BufferHandle scratch = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(scratch.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().ResetQueryPool(pool, 0, kPoolQueries, error), error));
			EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 0, rhi::PipelineStage::eAllCommands, error), error));
			EXPECT_TRUE(test::Ok(recording.List().ClearBuffer(scratch, 0, test::samples::kBufferSize, 0u, error), error));
			EXPECT_TRUE(test::Ok(recording.List().WriteTimestamp(pool, 1, rhi::PipelineStage::eAllCommands, error), error));
			EXPECT_TRUE(test::Ok(recording.List().ResolveQueryData(pool, 0, 2, results, 0, error), error));

			ASSERT_TRUE(recording.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), recording.List(), error), error));
		}

		std::uint64_t first	 = 0;
		std::uint64_t second = 0;
		if (!ReadTimestampPair(Dev(), results, first, second, error))
		{
			static_cast<void>(Dev().Destroy(pool, {}, error));
			static_cast<void>(Dev().Destroy(results, {}, error));
			static_cast<void>(Dev().Destroy(scratch, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		if (!IsNullBackend())
		{
			EXPECT_NE(first, kUnwritten) << "the first timestamp slot was never written";
			EXPECT_NE(second, kUnwritten) << "the second timestamp slot was never written";
			EXPECT_GE(second, first) << "the timestamp written after the work is earlier than the one written before it";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(results, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(scratch, {}, error), error));
	}

	TEST_P(QueryTest, TimesARenderingScopeThroughTheScopesOwnWrites)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		const rhi::BufferHandle results = Dev().CreateBuffer(test::samples::ReadbackBuffer(), error);
		ASSERT_TRUE(test::Ok(results.IsValid(), error));

		const rhi::TextureHandle target = Dev().CreateTexture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.IsValid(), error));
		const rhi::TextureViewHandle view = Dev().CreateTextureView(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		const std::array colors{ rhi::RenderingAttachment{ .view = view,
			.state		= { .stages = rhi::PipelineStage::eColorOutput, .access = rhi::Access::eColorWrite, .layout = rhi::TextureLayout::eColorAttachment, },
			.load		= rhi::LoadOp::eClear,
			.store		= rhi::StoreOp::eStore,
			.clearColor = rhi::ClearColor{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f } } };

		const rhi::RenderingTimestampWrites writes{ .pool = pool, .beginQuery = 0, .endQuery = 1 };

		const rhi::BeginRenderingDesc rendering{
			.colors		  = colors,
			.depthStencil = nullptr,
			.x			  = 0,
			.y			  = 0,
			.width		  = 64,
			.height		  = 64,
			.layers		  = 1,
			.timestamps	  = &writes,
		};

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().ResetQueryPool(pool, 0, kPoolQueries, error), error));
			if (!recording.List().BeginRendering(rendering, error))
			{
				static_cast<void>(recording.End());
				static_cast<void>(Dev().Destroy(pool, {}, error));
				static_cast<void>(Dev().Destroy(results, {}, error));
				static_cast<void>(Dev().Destroy(view, {}, error));
				static_cast<void>(Dev().Destroy(target, {}, error));
				GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
			}
			EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error));
			EXPECT_TRUE(test::Ok(recording.List().ResolveQueryData(pool, 0, 2, results, 0, error), error));

			ASSERT_TRUE(recording.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), recording.List(), error), error));
		}

		std::uint64_t opened = 0;
		std::uint64_t closed = 0;
		if (!ReadTimestampPair(Dev(), results, opened, closed, error))
		{
			static_cast<void>(Dev().Destroy(pool, {}, error));
			static_cast<void>(Dev().Destroy(results, {}, error));
			static_cast<void>(Dev().Destroy(view, {}, error));
			static_cast<void>(Dev().Destroy(target, {}, error));
			GTEST_SKIP() << "this backend does not expose mapped memory: " << test::Describe(error);
		}

		if (!IsNullBackend())
		{
			EXPECT_NE(opened, kUnwritten) << "the scope opened without writing its begin timestamp";
			EXPECT_NE(closed, kUnwritten) << "the scope closed without writing its end timestamp";
			EXPECT_GE(closed, opened) << "the rendering scope closed before it opened";
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(results, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, {}, error), error));
	}

	TEST_P(QueryTest, AcceptsOrRefusesAMidScopeWriteExactlyAsTheCapabilitySays)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

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
			static_cast<void>(Dev().Destroy(pool, {}, error));
			static_cast<void>(Dev().Destroy(view, {}, error));
			static_cast<void>(Dev().Destroy(target, {}, error));
			GTEST_SKIP() << "this backend refused a dynamic rendering scope: " << test::Describe(error);
		}

		rhi::Error writeError{};
		const bool written = recording.List().WriteTimestamp(pool, 2, rhi::PipelineStage::eAllCommands, writeError);

		if (Caps().supportsTimestampWritesInScope)
		{
			EXPECT_TRUE(test::Ok(written, writeError)) << "a device reporting mid-scope timestamp writes refused one";
		}
		else
		{
			EXPECT_FALSE(written) << "a device reporting no mid-scope timestamp writes accepted one anyway";
			EXPECT_TRUE(test::ErrorIsPopulated(writeError));
		}

		EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error));
		EXPECT_TRUE(recording.End());

		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, {}, error), error));
	}

	TEST_P(QueryTest, RefusesAQueryPastTheEndOfThePool)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that remembers how large a pool it created");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(kPoolQueries), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error writeError{};
		EXPECT_FALSE(recording.List().WriteTimestamp(pool, kPoolQueries, rhi::PipelineStage::eAllCommands, writeError))
			<< "a timestamp was written past the end of the pool";
		EXPECT_TRUE(test::ErrorIsPopulated(writeError));

		rhi::Error resetError{};
		EXPECT_FALSE(recording.List().ResetQueryPool(pool, kPoolQueries - 1, 2, resetError)) << "a reset ran past the end of the pool";
		EXPECT_TRUE(test::ErrorIsPopulated(resetError));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

	TEST_P(QueryTest, RefusesRenderingWritesNamingAPoolThisDeviceNeverCreated)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries, "timestamp queries");

		AZO_RHI_REQUIRE_CAP(!IsNullBackend(), "a backend that remembers which pools it created");

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

		const rhi::RenderingTimestampWrites writes{ .pool = rhi::QueryPoolHandle{ .index = 9001, .generation = 3 }, .beginQuery = 0, .endQuery = 1 };

		const rhi::BeginRenderingDesc rendering{
			.colors		  = colors,
			.depthStencil = nullptr,
			.x			  = 0,
			.y			  = 0,
			.width		  = 64,
			.height		  = 64,
			.layers		  = 1,
			.timestamps	  = &writes,
		};

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error beginError{};
		EXPECT_FALSE(recording.List().BeginRendering(rendering, beginError)) << "a rendering scope opened against a query pool that does not exist";
		EXPECT_TRUE(test::ErrorIsPopulated(beginError));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(target, {}, error), error));
	}

	TEST_P(QueryTest, CalibratesTheTwoClocksOrSaysItCannot)
	{
		rhi::Error error{};
		rhi::TimestampCalibration calibration{};
		const bool calibrated = Dev().CalibrateTimestamp(rhi::QueueType::eGraphics, calibration, error);

		if (!Caps().supportsTimestampCalibration)
		{
			EXPECT_FALSE(calibrated && calibration.calibrated) << "a device reporting no timestamp calibration returned a calibrated pair";
			return;
		}

		ASSERT_TRUE(test::Ok(calibrated, error)) << "a device reporting timestamp calibration refused to calibrate";
		EXPECT_EQ(calibration.queueType, rhi::QueueType::eGraphics) << "the calibration came back describing a different queue";

		if (!IsNullBackend())
		{
			EXPECT_TRUE(calibration.calibrated) << "the pair came back uncalibrated on a device that reports calibration";
			EXPECT_GT(calibration.gpuPeriodNanoseconds, 0.0f) << "a zero period makes every resolved timestamp meaningless";
		}
	}

} // namespace

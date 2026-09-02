// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
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
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class AliasBarrierTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(AliasBarrierTest);

	constexpr std::uint8_t kBeforePattern = 0xAAu;
	constexpr std::uint8_t kAfterPattern  = 0x55u;

	struct AliasedPair final
	{
		rhi::HeapHandle heap{};
		rhi::BufferHandle before{};
		rhi::BufferHandle after{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return heap.IsValid() && before.IsValid() && after.IsValid();
		}
	};

	[[nodiscard]] AliasedPair PlaceOverlapping(rhi::Device device, rhi::Error & error)
	{
		const rhi::BufferDesc desc = test::samples::StorageBuffer();

		AliasedPair pair{};
		pair.heap = device.CreateHeap(test::samples::GpuHeap(), error);
		if (!pair.heap.IsValid())
		{
			return {};
		}

		const rhi::PlacedBufferDesc placed{ .buffer = desc, .heap = pair.heap, .offset = 0 };
		pair.before = device.CreatePlacedBuffer(placed, error);
		if (!pair.before.IsValid())
		{
			return pair;
		}

		pair.after = device.CreatePlacedBuffer(placed, error);
		return pair;
	}

	void DestroyPair(rhi::Device device, const AliasedPair & pair)
	{
		rhi::Error ignored{};
		for (const rhi::BufferHandle buffer : { pair.after, pair.before })
		{
			if (buffer.IsValid())
			{
				static_cast<void>(device.Destroy(buffer, {}, ignored));
			}
		}
		if (pair.heap.IsValid())
		{
			static_cast<void>(device.Destroy(pair.heap, {}, ignored));
		}
	}

	[[nodiscard]] rhi::BufferHandle FilledUploadBuffer(rhi::Device device, const std::uint8_t pattern, rhi::Error & error)
	{
		const rhi::BufferHandle upload = device.CreateBuffer(test::samples::UploadBuffer(), error);
		if (!upload.IsValid())
		{
			return {};
		}

		const rhi::MappedMemory staging = device.Map(upload, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (staging.data == nullptr)
		{
			return {};
		}

		std::memset(staging.data, pattern, test::samples::kBufferSize);
		if (!staging.coherent)
		{
			static_cast<void>(device.FlushMappedRange(upload, 0, test::samples::kBufferSize, error));
		}
		static_cast<void>(device.Unmap(upload, error));

		return upload;
	}

	[[nodiscard]] std::array<rhi::BufferBarrier, 1> ToCopyDst(const rhi::BufferHandle buffer)
	{
		return { rhi::BufferBarrier{
			.buffer = buffer,
			.before = { .use = rhi::ResourceUse::eDiscard },
			.after	= { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
		} };
	}

	TEST_P(AliasBarrierTest, IsRefusedInsideARenderingScope)
	{
		rhi::Error error{};
		const rhi::TextureHandle target = Dev().CreateTexture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.IsValid(), error));

		const rhi::TextureViewHandle view = Dev().CreateTextureView(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.IsValid(), error));

		const AliasedPair pair = PlaceOverlapping(Dev(), error);

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array colors{ rhi::RenderingAttachment{
				.view  = view,
				.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
				.load  = rhi::LoadOp::eClear,
				.store = rhi::StoreOp::eStore,
			} };
			const rhi::BeginRenderingDesc rendering{ .colors = colors, .width = test::samples::kTextureDim, .height = test::samples::kTextureDim };

			if (!recording.List().BeginRendering(rendering, error))
			{
				static_cast<void>(recording.End());
				DestroyPair(Dev(), pair);
				static_cast<void>(Dev().Destroy(view, {}, error));
				static_cast<void>(Dev().Destroy(target, {}, error));
				GTEST_SKIP() << "this backend refused a rendering scope: " << test::Describe(error);
			}

			const std::array barriers{ rhi::AliasBarrier{ .beforeBuffer = pair.before, .afterBuffer = pair.after } };

			rhi::Error scopeError{};
			EXPECT_FALSE(recording.List().AliasBarriers(barriers, scopeError)) << "an alias barrier was accepted inside a rendering scope";
			EXPECT_TRUE(test::ErrorIsPopulated(scopeError));

			EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error)) << "the rendering scope did not survive the refusal";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyPair(Dev(), pair);
		static_cast<void>(Dev().Destroy(view, {}, error));
		static_cast<void>(Dev().Destroy(target, {}, error));
	}

	TEST_P(AliasBarrierTest, AnEmptyBatchIsAcceptedAndRecordsNothing)
	{
		rhi::Error error{};
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		EXPECT_TRUE(test::Ok(recording.List().AliasBarriers({}, error), error)) << "an empty alias batch was refused";
		EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
	}

	TEST_P(AliasBarrierTest, AcceptsAnAliasedPairWithNothingElseRecorded)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const AliasedPair pair = PlaceOverlapping(Dev(), error);
		if (!pair.IsValid())
		{
			DestroyPair(Dev(), pair);
			GTEST_SKIP() << "this backend refused an aliased placed pair: " << test::Describe(error);
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array barriers{ rhi::AliasBarrier{ .beforeBuffer = pair.before, .afterBuffer = pair.after } };
			EXPECT_TRUE(test::Ok(recording.List().AliasBarriers(barriers, error), error)) << "a well-formed aliased pair was refused";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyPair(Dev(), pair);
	}

	TEST_P(AliasBarrierTest, AcceptsAnAliasedPairAfterACopyIsAlreadyRecorded)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const AliasedPair pair = PlaceOverlapping(Dev(), error);
		if (!pair.IsValid())
		{
			DestroyPair(Dev(), pair);
			GTEST_SKIP() << "this backend refused an aliased placed pair: " << test::Describe(error);
		}

		const rhi::BufferHandle upload = FilledUploadBuffer(Dev(), kBeforePattern, error);
		if (!upload.IsValid())
		{
			DestroyPair(Dev(), pair);
			GTEST_SKIP() << "this backend does not expose a mapped upload buffer to copy from";
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array toCopyDst = ToCopyDst(pair.before);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toCopyDst }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().CopyBuffer(pair.before, 0, upload, 0, test::samples::kBufferSize, error), error));

			const std::array barriers{ rhi::AliasBarrier{ .beforeBuffer = pair.before, .afterBuffer = pair.after } };
			EXPECT_TRUE(test::Ok(recording.List().AliasBarriers(barriers, error), error)) << "an alias barrier was refused with a copy already recorded";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		static_cast<void>(Dev().Destroy(upload, {}, error));
		DestroyPair(Dev(), pair);
	}

	TEST_P(AliasBarrierTest, TheAfterResourceCarriesItsOwnWriteAndNotThePrecedingOne)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const AliasedPair pair = PlaceOverlapping(Dev(), error);
		if (!pair.IsValid())
		{
			DestroyPair(Dev(), pair);
			GTEST_SKIP() << "this backend refused an aliased placed pair: " << test::Describe(error);
		}

		const rhi::BufferHandle beforeSource = FilledUploadBuffer(Dev(), kBeforePattern, error);
		const rhi::BufferHandle afterSource	 = FilledUploadBuffer(Dev(), kAfterPattern, error);
		const rhi::BufferHandle readback	 = Dev().CreateBuffer(test::samples::ReadbackBuffer(), error);
		if (!beforeSource.IsValid() || !afterSource.IsValid() || !readback.IsValid())
		{
			for (const rhi::BufferHandle buffer : { readback, afterSource, beforeSource })
			{
				if (buffer.IsValid())
				{
					static_cast<void>(Dev().Destroy(buffer, {}, error));
				}
			}
			DestroyPair(Dev(), pair);
			GTEST_SKIP() << "this backend does not expose the mapped buffers the round trip copies through";
		}

		const rhi::TimelineHandle done = Dev().CreateTimeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(done.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array beforeToCopyDst = ToCopyDst(pair.before);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = beforeToCopyDst }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().CopyBuffer(pair.before, 0, beforeSource, 0, test::samples::kBufferSize, error), error));

			const std::array barriers{ rhi::AliasBarrier{ .beforeBuffer = pair.before, .afterBuffer = pair.after } };
			ASSERT_TRUE(test::Ok(recording.List().AliasBarriers(barriers, error), error));

			const std::array afterToCopyDst = ToCopyDst(pair.after);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = afterToCopyDst }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().CopyBuffer(pair.after, 0, afterSource, 0, test::samples::kBufferSize, error), error));

			const std::array afterToCopySrc{ rhi::BufferBarrier{
				.buffer = pair.after,
				.before = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.after	= { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			} };
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = afterToCopySrc }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().CopyBuffer(readback, 0, pair.after, 0, test::samples::kBufferSize, error), error));
			ASSERT_TRUE(test::Ok(recording.End(), recording.GetError()));

			rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics);
			std::array<const rhi::CommandList *, 1> lists{ &recording.List() };
			const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };
			ASSERT_TRUE(test::Ok(queue.Submit({ .commandLists = lists, .signals = signals, .debugName = "alias.roundTrip" }, error), error));
			ASSERT_TRUE(test::Ok(queue.Wait(done, 1, test::kWaitTimeoutNanoseconds, error), error)) << "the aliased round trip never completed";
		}

		const rhi::MappedMemory mapped = Dev().Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		ASSERT_NE(mapped.data, nullptr) << "the readback buffer could not be mapped, so nothing was checked";
		if (!mapped.coherent)
		{
			ASSERT_TRUE(test::Ok(Dev().InvalidateMappedRange(readback, 0, test::samples::kBufferSize, error), error));
		}

		std::vector<std::uint8_t> observed(test::samples::kBufferSize, 0);
		std::memcpy(observed.data(), mapped.data, observed.size());
		static_cast<void>(Dev().Unmap(readback, error));

		if (!IsNullBackend())
		{
			const std::vector<std::uint8_t> expected(test::samples::kBufferSize, kAfterPattern);
			EXPECT_EQ(observed, expected) << "the aliased buffer does not carry the write that followed the alias barrier";
		}

		static_cast<void>(Dev().Destroy(done, {}, error));
		static_cast<void>(Dev().Destroy(readback, {}, error));
		static_cast<void>(Dev().Destroy(afterSource, {}, error));
		static_cast<void>(Dev().Destroy(beforeSource, {}, error));
		DestroyPair(Dev(), pair);
	}

}

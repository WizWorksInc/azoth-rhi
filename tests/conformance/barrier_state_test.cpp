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

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BarrierStateTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BarrierStateTest);

	constexpr rhi::ResourceState kNothing{};
	constexpr rhi::ResourceState kDiscard{ .use = rhi::ResourceUse::eDiscard };
	constexpr rhi::ResourceState kCopyDst{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

	[[nodiscard]] rhi::BufferHandle MakeBuffer(rhi::Device device, rhi::Error & error)
	{
		return device.CreateBuffer(test::samples::StorageBuffer(), error);
	}

	// The rule ResourceState documents: an empty use says nothing about where the resource ends up, so it cannot be what a barrier leaves behind.
	TEST_P(BarrierStateTest, RefusesAnEmptyAfterStateOnAPlainBarrier)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array empty{ rhi::BufferBarrier{ .buffer = buffer, .before = kNothing, .after = kNothing } };

			rhi::Error emptyError{};
			EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = empty }, emptyError))
				<< "a barrier leaving the resource in no state at all was accepted";
			EXPECT_TRUE(test::ErrorIsPopulated(emptyError));

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	/*
	 * The one exemption, and the reason the rule takes the ownership op rather than the state alone. What the releasing queue leaves the resource in is not
	 * something that side can answer, so the release names none and the acquire names what the resource is being taken into.
	 */
	TEST_P(BarrierStateTest, AcceptsAnEmptyAfterStateOnAReleaseBarrier)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array released{ rhi::BufferBarrier{ .buffer = buffer,
				.before					   = kNothing,
				.after					   = kNothing,
				.ownership				   = { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } } };

			EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = released }, error), error))
				<< "a release naming no after-state was refused, which is the half of the pair that cannot answer it";

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	// The other half of the pair, which the exemption must not reach: an acquire is exactly the barrier that says what the resource is being taken into.
	TEST_P(BarrierStateTest, RefusesAnEmptyAfterStateOnAnAcquireBarrier)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array released{ rhi::BufferBarrier{ .buffer = buffer,
				.before					   = kNothing,
				.after					   = kNothing,
				.ownership				   = { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } } };
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = released }, error), error));

			const std::array acquired{ rhi::BufferBarrier{ .buffer = buffer,
				.before					   = kNothing,
				.after					   = kNothing,
				.ownership				   = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } } };

			rhi::Error acquireError{};
			EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = acquired }, acquireError))
				<< "an acquire naming no after-state was accepted, so the release exemption is reaching the wrong half";
			EXPECT_TRUE(test::ErrorIsPopulated(acquireError));

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	// The sibling rule from the same paragraph, which had no runtime coverage: eDiscard describes contents arriving at a barrier, not contents leaving one.
	TEST_P(BarrierStateTest, RefusesDiscardAsAnAfterState)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array discarded{ rhi::BufferBarrier{ .buffer = buffer, .before = kNothing, .after = kDiscard } };

			rhi::Error discardError{};
			EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = discarded }, discardError))
				<< "a barrier naming eDiscard as the state it leaves behind was accepted";
			EXPECT_TRUE(test::ErrorIsPopulated(discardError));

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	// The rule only governs the after-state, so the same empty state has to stay legal where the header says it is legal.
	TEST_P(BarrierStateTest, StillTakesAnEmptyBeforeState)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = kNothing, .after = kCopyDst } };
			EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
				<< "an empty before-state was refused, which the header calls the one place it is legal";

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

} // namespace

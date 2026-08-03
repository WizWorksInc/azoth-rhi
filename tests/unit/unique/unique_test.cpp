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

#include "azoth/rhi/ownership/unique.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class UniqueTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] rhi::BufferHandle MakeBuffer()
		{
			rhi::Error error{};
			const rhi::BufferHandle handle = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
			EXPECT_TRUE(test::Ok(handle.IsValid(), error));
			return handle;
		}
	};

	AZO_RHI_BACKEND_SUITE(UniqueTest);

	TEST_P(UniqueTest, DestroysTheHandleWhenTheScopeEnds)
	{
		const rhi::BufferHandle handle = MakeBuffer();
		ASSERT_TRUE(handle.IsValid());

		{
			const rhi::UniqueBuffer owner(Dev(), handle);
			EXPECT_TRUE(owner.IsValid());
			EXPECT_EQ(owner.Get(), handle);
		}

		if (test::kValidatesHandles)
		{
			rhi::Error error{};
			EXPECT_FALSE(Dev().Destroy(handle, {}, error)) << "the scope ended without destroying the buffer";
		}
	}

	TEST_P(UniqueTest, IsMoveOnlyAndLeavesAMovedFromOwnerEmpty)
	{
		static_assert(!std::is_copy_constructible_v<rhi::UniqueBuffer>);
		static_assert(!std::is_copy_assignable_v<rhi::UniqueBuffer>);
		static_assert(std::is_move_constructible_v<rhi::UniqueBuffer>);
		static_assert(std::is_move_assignable_v<rhi::UniqueBuffer>);

		const rhi::BufferHandle handle = MakeBuffer();

		rhi::UniqueBuffer source(Dev(), handle);
		rhi::UniqueBuffer moved = std::move(source);

		EXPECT_TRUE(moved.IsValid());
		EXPECT_EQ(moved.Get(), handle);
		EXPECT_FALSE(source.IsValid()) << "the moved-from owner still claims the handle"; // NOLINT(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
	}

	TEST_P(UniqueTest, MoveAssignmentDestroysWhatTheTargetHeld)
	{
		const rhi::BufferHandle first  = MakeBuffer();
		const rhi::BufferHandle second = MakeBuffer();
		ASSERT_NE(first, second);

		{
			rhi::UniqueBuffer target(Dev(), first);
			rhi::UniqueBuffer source(Dev(), second);
			target = std::move(source);
			EXPECT_EQ(target.Get(), second);
		}

		if (test::kValidatesHandles)
		{
			rhi::Error error{};
			EXPECT_FALSE(Dev().Destroy(first, {}, error)) << "move assignment leaked the handle the target already owned";
		}
	}

	TEST_P(UniqueTest, ReleaseHandsOwnershipBackToTheCaller)
	{
		const rhi::BufferHandle handle = MakeBuffer();

		rhi::BufferHandle released{};
		{
			rhi::UniqueBuffer owner(Dev(), handle);
			released = owner.Release();
			EXPECT_FALSE(owner.IsValid()) << "the owner still holds a handle it gave up";
		}

		EXPECT_EQ(released, handle);

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(Dev().Destroy(released, {}, error), error)) << "Release destroyed the handle instead of handing it over";
	}

	TEST_P(UniqueTest, ResetDestroysEarlyAndIsSafeToRepeat)
	{
		const rhi::BufferHandle handle = MakeBuffer();
		rhi::UniqueBuffer owner(Dev(), handle);

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(owner.Reset(error), error));
		EXPECT_FALSE(owner.IsValid());

		rhi::Error again{};
		EXPECT_TRUE(owner.Reset(again));
		EXPECT_EQ(again.code, rhi::ErrorCode::eOk);
	}

	TEST_P(UniqueTest, ADefaultOwnerHoldsNothingAndDestroysNothing)
	{
		rhi::UniqueBuffer owner;
		EXPECT_FALSE(owner.IsValid());
		EXPECT_FALSE(static_cast<bool>(owner));

		rhi::Error error{};
		EXPECT_TRUE(owner.Reset(error));
	}

	TEST_P(UniqueTest, CarriesTheDestroyPolicyItWasGiven)
	{
		const rhi::BufferHandle handle = MakeBuffer();

		rhi::UniqueBuffer owner(Dev(), handle);
		EXPECT_EQ(owner.GetDestroyDesc().policy, rhi::DestroyPolicy::eDeferUntilSafe);

		owner.SetDestroyDesc(rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle });
		EXPECT_EQ(owner.GetDestroyDesc().policy, rhi::DestroyPolicy::eRequireAlreadyIdle);

		rhi::Error error{};
		rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));
		ASSERT_TRUE(test::Ok(queue.WaitIdle(error), error));

		EXPECT_TRUE(test::Ok(owner.Reset(error), error)) << "the policy the owner was given did not reach the destroy";
	}

	TEST_P(UniqueTest, OwnsEveryResourceTypeTheDeviceCanDestroy)
	{
		static_assert(std::is_same_v<rhi::UniqueBuffer, rhi::Unique<rhi::BufferHandle>>);
		static_assert(std::is_same_v<rhi::UniqueTexture, rhi::Unique<rhi::TextureHandle>>);
		static_assert(std::is_same_v<rhi::UniqueTextureView, rhi::Unique<rhi::TextureViewHandle>>);
		static_assert(std::is_same_v<rhi::UniqueSampler, rhi::Unique<rhi::SamplerHandle>>);
		static_assert(std::is_same_v<rhi::UniqueHeap, rhi::Unique<rhi::HeapHandle>>);
		static_assert(std::is_same_v<rhi::UniqueDescriptorSetLayout, rhi::Unique<rhi::DescriptorSetLayoutHandle>>);
		static_assert(std::is_same_v<rhi::UniquePipelineLayout, rhi::Unique<rhi::PipelineLayoutHandle>>);
		static_assert(std::is_same_v<rhi::UniqueGraphicsPipeline, rhi::Unique<rhi::GraphicsPipelineHandle>>);
		static_assert(std::is_same_v<rhi::UniqueComputePipeline, rhi::Unique<rhi::ComputePipelineHandle>>);
		static_assert(std::is_same_v<rhi::UniqueRayTracingPipeline, rhi::Unique<rhi::RayTracingPipelineHandle>>);
		static_assert(std::is_same_v<rhi::UniquePipelineCache, rhi::Unique<rhi::PipelineCacheHandle>>);
		static_assert(std::is_same_v<rhi::UniqueAccelerationStructure, rhi::Unique<rhi::AccelerationStructureHandle>>);
		static_assert(std::is_same_v<rhi::UniqueQueryPool, rhi::Unique<rhi::QueryPoolHandle>>);
		static_assert(std::is_same_v<rhi::UniqueTimeline, rhi::Unique<rhi::TimelineHandle>>);
		static_assert(std::is_same_v<rhi::UniqueBinarySemaphore, rhi::Unique<rhi::BinarySemaphoreHandle>>);

		SUCCEED();
	}

	TEST(UniqueNames, TheWholeLifetimeOnlyTierSitsTogetherInOneNamespace)
	{
		static_assert(std::is_same_v<decltype(std::declval<rhi::UniqueBuffer>().Owner()), rhi::Device>);
		static_assert(std::is_same_v<decltype(std::declval<rhi::UniqueDevice>().Get()), rhi::Device>);

		SUCCEED();
	}

} // namespace

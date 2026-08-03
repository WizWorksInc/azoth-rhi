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
#include "azoth/rhi/resources/resources.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BufferTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BufferTest);

	TEST_P(BufferTest, CreatesAndDestroysAStorageBuffer)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);

		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(BufferTest, TheThreeCreationOverloadsAllProduceAUsableBuffer)
	{
		const rhi::BufferHandle sentinel = Dev().CreateBuffer(test::samples::StorageBuffer());
		EXPECT_TRUE(sentinel.IsValid());

		rhi::Error error{};
		const rhi::BufferHandle withError = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(withError.IsValid(), error));

		const rhi::Result<rhi::BufferHandle> asResult = Dev().CreateBufferWithResult(test::samples::StorageBuffer());
		ASSERT_TRUE(test::Ok(asResult));
		EXPECT_TRUE(asResult.Value().IsValid());

		EXPECT_NE(sentinel, withError);
		EXPECT_NE(withError, asResult.Value());

		EXPECT_TRUE(test::Ok(Dev().Destroy(sentinel, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(withError, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(asResult.Value(), {}, error), error));
	}

	TEST_P(BufferTest, AcceptsEveryMemoryUsageTheBaselineNeeds)
	{
		rhi::Error error{};
		for (const rhi::BufferDesc & desc : { test::samples::StorageBuffer(), test::samples::UploadBuffer(), test::samples::ReadbackBuffer() })
		{
			const rhi::BufferHandle buffer = Dev().CreateBuffer(desc, error);
			EXPECT_TRUE(test::Ok(buffer.IsValid(), error)) << "memory usage " << static_cast<int>(desc.memory) << " was refused";
			if (buffer.IsValid())
			{
				EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
			}
		}
	}

	TEST_P(BufferTest, HandsOutADistinctHandleForEveryLiveBuffer)
	{
		constexpr int kCount = 64;

		rhi::Error error{};
		std::vector<rhi::BufferHandle> buffers;
		buffers.reserve(kCount);

		for (int index = 0; index < kCount; ++index)
		{
			const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
			ASSERT_TRUE(test::Ok(buffer.IsValid(), error)) << "creation " << index << " failed";
			buffers.push_back(buffer);
		}

		for (std::size_t lhs = 0; lhs < buffers.size(); ++lhs)
		{
			for (std::size_t rhs = lhs + 1; rhs < buffers.size(); ++rhs)
			{
				ASSERT_NE(buffers[lhs], buffers[rhs]) << "two live buffers share a handle";
			}
		}

		for (const rhi::BufferHandle buffer : buffers)
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
		}
	}

	TEST_P(BufferTest, ReusesSlotsAfterDestructionWithoutReusingHandles)
	{
		// A frame allocator creates and destroys resources constantly. The index may come back, but the generation has to move or a handle held across a frame
		// boundary would silently name someone else's buffer.
		rhi::Error error{};

		const rhi::BufferHandle first = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(first.IsValid(), error));
		ASSERT_TRUE(test::Ok(Dev().Destroy(first, {}, error), error));

		const rhi::BufferHandle second = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(second.IsValid(), error));

		EXPECT_NE(first, second) << "a destroyed buffer's handle came back verbatim";

		EXPECT_TRUE(test::Ok(Dev().Destroy(second, {}, error), error));
	}

	TEST_P(BufferTest, RejectsDestroyingTheSameBufferTwice)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		ASSERT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(Dev().Destroy(buffer, {}, secondError)) << "a double destroy was accepted, which would free the native object twice";
		EXPECT_TRUE(test::ErrorIsPopulated(secondError));
	}

	TEST_P(BufferTest, RejectsDestroyingAHandleItNeverIssued)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		EXPECT_FALSE(Dev().Destroy(rhi::BufferHandle{ .index = 4096, .generation = 7 }, {}, error));
		EXPECT_TRUE(test::ErrorIsPopulated(error));

		rhi::Error invalidError{};
		EXPECT_FALSE(Dev().Destroy(rhi::BufferHandle{}, {}, invalidError));
	}

	TEST_P(BufferTest, RejectsAStaleHandleWhoseSlotWasAlreadyReissued)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle stale = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(stale.IsValid(), error));
		ASSERT_TRUE(test::Ok(Dev().Destroy(stale, {}, error), error));

		const rhi::BufferHandle successor = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(successor.IsValid(), error));

		rhi::Error staleError{};
		EXPECT_FALSE(Dev().Destroy(stale, {}, staleError)) << "a stale handle destroyed the resource that took over its slot";

		EXPECT_TRUE(test::Ok(Dev().Destroy(successor, {}, error), error));
	}

	TEST_P(BufferTest, AcceptsBothDestroyPolicies)
	{
		rhi::Error error{};

		const rhi::BufferHandle deferred = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(deferred.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(deferred, rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eDeferUntilSafe }, error), error));

		const rhi::BufferHandle immediate = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(immediate.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(immediate, rhi::DestroyDesc{ .policy = rhi::DestroyPolicy::eRequireAlreadyIdle }, error), error));
	}

	TEST_P(BufferTest, ReportsAMemoryFootprintForADescBeforeAnythingIsCreated)
	{
		rhi::Error error{};
		rhi::MemoryInfo info{};

		if (!Dev().GetBufferMemoryInfo(test::samples::StorageBuffer(), info, error))
		{
			GTEST_SKIP() << "this backend does not report buffer memory info: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(true, error));
	}

	TEST_P(BufferTest, ClearsTheOutputWhenAMemoryQueryFails)
	{
		rhi::MemoryInfo info{
			.size	   = 12345,
			.alignment = 678,
		};
		rhi::Error error{};

		rhi::BufferDesc absurd = test::samples::StorageBuffer();
		absurd.size			   = 0;

		if (Dev().GetBufferMemoryInfo(absurd, info, error))
		{
			GTEST_SKIP() << "this backend accepts a zero-sized buffer desc, so there is no failure to observe";
		}

		EXPECT_EQ(info.size, 0u) << "a failed query left the caller's output untouched";
		EXPECT_EQ(info.alignment, 0u);
	}

	TEST_P(BufferTest, KeepsBufferAndTextureHandleDomainsApartAtCompileTime)
	{
		static_assert(!std::is_convertible_v<rhi::BufferHandle, rhi::TextureHandle>);
		static_assert(!std::is_convertible_v<rhi::TextureHandle, rhi::BufferHandle>);

		SUCCEED();
	}

} // namespace

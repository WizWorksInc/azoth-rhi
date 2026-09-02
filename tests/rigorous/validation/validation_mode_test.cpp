// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/device/device.hpp"

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

	class ValidationModeTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ValidationModeTest);

	TEST_P(ValidationModeTest, TheDeviceWasCreatedWithTheModeThisBinaryWasBuiltFor)
	{
		EXPECT_EQ(MakeDeviceDesc().validation, test::kValidationMode);
	}

	TEST_P(ValidationModeTest, RefusesADoubleDestroyInEveryMode)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		ASSERT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));

		rhi::Error secondError{};
		EXPECT_FALSE(Dev().Destroy(buffer, {}, secondError)) << "a double destroy was accepted, which would free the native object twice";
		EXPECT_TRUE(test::ErrorIsPopulated(secondError));
	}

	TEST_P(ValidationModeTest, RefusesAZeroCountQueryPoolInEveryMode)
	{
		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(0), error);
		EXPECT_FALSE(pool.IsValid()) << "a query pool that asked for no queries was created";
		EXPECT_TRUE(test::ErrorIsPopulated(error));

		if (pool.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
		}
	}

	TEST_P(ValidationModeTest, TracksResourceStateOnlyWhenTheModeSaysItWill)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		constexpr rhi::ResourceState untouched{ .use = rhi::ResourceUse::eDiscard };
		constexpr rhi::ResourceState copyDst{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState shaderRead{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };

		const std::array first{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = copyDst },
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = first }, error), error));

		const std::array wrong{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = shaderRead },
		};

		rhi::Error wrongError{};
		const bool acceptedWrongState = recording.List().Barriers(rhi::BarrierBatch{ .buffers = wrong }, wrongError);

		if constexpr (test::kValidatesFully)
		{
			EXPECT_FALSE(acceptedWrongState) << "state is tracked in this mode, so a mismatched before-state has to be refused";
			EXPECT_TRUE(test::ErrorIsPopulated(wrongError));
		}
		else
		{
			EXPECT_TRUE(acceptedWrongState) << "state tracking ran in a mode that promised not to pay for it";
		}

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(ValidationModeTest, RefusesAnAliasBarrierNamingAnUnresolvableHandleInEveryMode)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsPlacedResources || IsNullBackend(), "placed resources");

		rhi::Error error{};
		const rhi::HeapHandle heap = Dev().CreateHeap(test::samples::GpuHeap(), error);
		if (!heap.IsValid())
		{
			GTEST_SKIP() << "this backend refused a heap to place over: " << test::Describe(error);
		}

		const rhi::PlacedBufferDesc placed{ .buffer = test::samples::StorageBuffer(), .heap = heap, .offset = 0 };
		const rhi::BufferHandle before = Dev().CreatePlacedBuffer(placed, error);
		const rhi::BufferHandle after  = Dev().CreatePlacedBuffer(placed, error);
		if (!before.IsValid() || !after.IsValid())
		{
			static_cast<void>(Dev().Destroy(heap, {}, error));
			GTEST_SKIP() << "this backend refused an aliased placed pair: " << test::Describe(error);
		}

		ASSERT_TRUE(test::Ok(Dev().Destroy(after, {}, error), error));
		ASSERT_TRUE(after.IsValid()) << "the handle stopped reading as valid, so nothing below reaches the resolve";

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array barriers{ rhi::AliasBarrier{ .beforeBuffer = before, .afterBuffer = after } };

			rhi::Error aliasError{};
			EXPECT_FALSE(recording.List().AliasBarriers(barriers, aliasError)) << "an alias barrier naming a destroyed buffer was swallowed";
			EXPECT_TRUE(test::ErrorIsPopulated(aliasError));

			static_cast<void>(recording.End());
		}

		static_cast<void>(Dev().Destroy(before, {}, error));
		static_cast<void>(Dev().Destroy(heap, {}, error));
	}

	TEST_P(ValidationModeTest, ChecksTheCommandListLifecycleOnlyWhenTheModeSaysItWill)
	{
		rhi::Error error{};
		rhi::CommandPool pool = Dev().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.list", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		rhi::Error secondError{};
		[[maybe_unused]] const bool acceptedSecondBegin = list.Begin(secondError);

		if constexpr (test::kValidatesFully)
		{
			EXPECT_FALSE(acceptedSecondBegin) << "the lifecycle is checked in this mode, so a second Begin has to be refused";
			EXPECT_EQ(secondError.code, rhi::ErrorCode::eValidationFailed);
		}
		else
		{
			EXPECT_NE(secondError.code, rhi::ErrorCode::eValidationFailed) << "the lifecycle check ran in a mode that promised not to";
		}

		static_cast<void>(list.End(error));
	}

	TEST_P(ValidationModeTest, EveryModeStillCreatesAndDestroysResourcesCorrectly)
	{
		rhi::Error error{};

		const rhi::BufferHandle buffer	 = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().CollectGarbage(error), error));
	}

	TEST_P(ValidationModeTest, NoModeLeavesNativeValidationComplaining)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
			EXPECT_TRUE(test::Ok(recording.List().ClearBuffer(buffer, 0, test::samples::kBufferSize, 0, error), error));
			EXPECT_TRUE(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "a clean run under " << AZOTH_RHI_TEST_CONFIGURATION_NAME << " still produced errors");
	}

}

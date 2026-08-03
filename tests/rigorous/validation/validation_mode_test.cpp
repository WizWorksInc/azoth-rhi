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

	/*
	 * Refused in every mode, though not by the same thing. Off, the decorator is not installed and the backend's own slot map answers, which it resolves anyway
	 * to find the object to free. In the other modes the registry catches it first. What the mode buys is where the answer comes from, not whether there is one.
	 */
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

	TEST_P(ValidationModeTest, TracksResourceStateOnlyWhenTheModeSaysItWill)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		constexpr rhi::ResourceState untouched{
			.stages = rhi::PipelineStage::eNone,
			.access = rhi::Access::eNone,
			.layout = rhi::TextureLayout::eUndefined,
		};
		constexpr rhi::ResourceState copyDst{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};
		constexpr rhi::ResourceState shaderRead{
			.stages = rhi::PipelineStage::eFragmentShader,
			.access = rhi::Access::eShaderRead,
			.layout = rhi::TextureLayout::eShaderReadOnly,
		};

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

} // namespace

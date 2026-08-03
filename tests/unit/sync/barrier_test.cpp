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

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class BarrierTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BarrierTest);

	[[nodiscard]] rhi::ResourceState UntouchedState() noexcept
	{
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eNone,
			.access = rhi::Access::eNone,
			.layout = rhi::TextureLayout::eUndefined,
		};
	}

	[[nodiscard]] rhi::ResourceState CopyDestinationState() noexcept
	{
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};
	}

	[[nodiscard]] rhi::ResourceState ShaderReadState() noexcept
	{
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eFragmentShader,
			.access = rhi::Access::eShaderRead,
			.layout = rhi::TextureLayout::eShaderReadOnly,
		};
	}

	TEST_P(BarrierTest, RecordsAnEmptyBarrierBatch)
	{
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{}, error), error));
		EXPECT_TRUE(recording.End());
	}

	TEST_P(BarrierTest, RecordsAMemoryBarrier)
	{
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array memory{ rhi::MemoryBarrier{
			.before = { .stages = rhi::PipelineStage::eComputeShader, .access = rhi::Access::eShaderWrite, .layout = {} },
			.after	= { .stages = rhi::PipelineStage::eFragmentShader, .access = rhi::Access::eShaderRead, .layout = {} } } };

		rhi::Error error{};
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .memory = memory }, error), error));
		EXPECT_TRUE(recording.End());
	}

	TEST_P(BarrierTest, TransitionsABufferThroughTheStatesACopyThenAReadWouldUse)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{
			rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() },
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		const std::array toRead{
			rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() },
		};
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toRead }, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(BarrierTest, RejectsABufferBarrierWhoseBeforeStateDisagreesWithWhatWasTracked)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{
			rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() },
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		const std::array wrong{
			rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = ShaderReadState() },
		};

		rhi::Error wrongError{};
		EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = wrong }, wrongError)) << "a barrier claiming the wrong before-state was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(wrongError));

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(BarrierTest, TransitionsATextureAcrossItsWholeSubresourceRange)
	{
		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array barriers{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };

		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = barriers }, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
	}

	TEST_P(BarrierTest, TakesBuffersAndTexturesInOneBatch)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer	 = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array buffers{
			rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() },
		};
		const std::array textures{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };

		EXPECT_TRUE(test::Ok(recording.List().Barriers(
								 rhi::BarrierBatch{
									 .memory   = {},
									 .buffers  = buffers,
									 .textures = textures,
								 },
								 error),
			error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(BarrierTest, TracksTwoResourcesIndependently)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle moved	 = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		const rhi::BufferHandle pristine = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(moved.IsValid(), error));
		ASSERT_TRUE(test::Ok(pristine.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array first{
			rhi::BufferBarrier{ .buffer = moved, .before = UntouchedState(), .after = CopyDestinationState() },
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = first }, error), error));

		const std::array second{
			rhi::BufferBarrier{ .buffer = pristine, .before = UntouchedState(), .after = ShaderReadState() },
		};
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = second }, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(pristine, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(moved, {}, error), error));
	}

	TEST_P(BarrierTest, StartsEachRecordingFromACleanSlate)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		const std::array toCopy{
			rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() },
		};

		{
			test::Recording first(Dev());
			ASSERT_TRUE(test::Ok(first.IsRecording(), first.GetError()));
			ASSERT_TRUE(test::Ok(first.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));
			ASSERT_TRUE(first.End());
		}

		{
			test::Recording second(Dev());
			ASSERT_TRUE(test::Ok(second.IsRecording(), second.GetError()));
			EXPECT_TRUE(test::Ok(second.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error))
				<< "a new recording inherited the state tracked by the previous one";
			EXPECT_TRUE(second.End());
		}

		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(BarrierTest, AcceptsAQueueFamilyTransferThatTransfersNothing)
	{
		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array barriers{ rhi::BufferBarrier{ .buffer = buffer,
			.before											   = UntouchedState(),
			.after											   = CopyDestinationState(),
			.ownership										   = rhi::QueueFamilyTransfer{ .src = rhi::kIgnoreQueueFamily, .dst = rhi::kIgnoreQueueFamily } } };

		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = barriers }, error), error));

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

} // namespace

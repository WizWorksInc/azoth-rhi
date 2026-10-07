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

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	constexpr rhi::Flags<rhi::ResourceUse> kStorageRead{ rhi::ResourceUse::eStorageRead };
	constexpr rhi::Flags<rhi::ResourceUse> kCopyDst{ rhi::ResourceUse::eCopyDst };
	constexpr rhi::Flags<rhi::ResourceUse> kCopySrc{ rhi::ResourceUse::eCopySrc };

	class TransitionTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(TransitionTest);

	[[nodiscard]] rhi::BufferHandle MakeBuffer(rhi::Device device, rhi::Error & error)
	{
		return device.create_buffer(test::samples::StorageBuffer(), error);
	}

	TEST_P(TransitionTest, LeavesTheResourceInTheStateItNames)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().transition(buffer, kStorageRead, kCopyDst, error), error));

			const std::array agreeing{ rhi::BufferBarrier{ .buffer = buffer, .before = { .use = kCopyDst }, .after = { .use = kCopySrc } } };

			EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = agreeing }, error), error))
				<< "a barrier claiming the state Transition named was refused, so Transition did not record one";

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(TransitionTest, IsSeenByTheStateTrackerLikeAnyOtherBarrier)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = MakeBuffer(Dev(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			ASSERT_TRUE(test::Ok(recording.List().transition(buffer, kStorageRead, kCopyDst, error), error));

			const std::array contradicting{ rhi::BufferBarrier{ .buffer = buffer, .before = { .use = kStorageRead }, .after = { .use = kCopySrc } } };

			rhi::Error staleError{};
			EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .buffers = contradicting }, staleError))
				<< "a barrier claiming the state Transition moved away from was accepted, so the transition went untracked";
			EXPECT_TRUE(test::ErrorIsPopulated(staleError));

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(TransitionTest, CoversEveryLevelOfTheTextureAndNotJustTheFirst)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		rhi::TextureDesc desc = test::samples::ColorTarget2D();
		desc.mipLevels		  = 4;

		const rhi::TextureHandle texture = Dev().create_texture(desc, error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			ASSERT_TRUE(test::Ok(recording.List().transition(texture, kCopySrc, kCopyDst, error), error));

			const std::array lastLevel{ rhi::TextureBarrier{ .texture = texture,
				.before												  = { .use = kStorageRead },
				.after												  = { .use = kCopySrc },
				.range												  = { .baseMip = desc.mipLevels - 1, .mipCount = 1 } } };

			rhi::Error staleError{};
			EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .textures = lastLevel }, staleError))
				<< "the last mip accepted a before-state contradicting the one Transition named, so it was left untracked and the transition covered only mip "
				   "0";
			EXPECT_TRUE(test::ErrorIsPopulated(staleError));

			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
	}

	TEST_P(TransitionTest, InheritsTheRefusalsOfTheBatchFormItRecordsThrough)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::TextureHandle target = Dev().create_texture(test::samples::ColorTarget2D(), error);
		ASSERT_TRUE(test::Ok(target.is_valid(), error));

		const rhi::TextureViewHandle view = Dev().create_texture_view(target, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.is_valid(), error));

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

			if (!recording.List().begin_rendering(rendering, error))
			{
				static_cast<void>(recording.End());
				GTEST_SKIP() << "this backend refused a rendering scope: " << test::Describe(error);
			}

			rhi::Error scopeError{};
			EXPECT_FALSE(recording.List().transition(target, kCopySrc, kCopyDst, scopeError))
				<< "Transition was accepted inside a rendering scope, so it is not recording through Barriers";
			EXPECT_TRUE(test::ErrorIsPopulated(scopeError));

			EXPECT_TRUE(test::Ok(recording.List().end_rendering(error), error));
			static_cast<void>(recording.End());
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(view, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(target, {}, error), error));
	}

} // namespace

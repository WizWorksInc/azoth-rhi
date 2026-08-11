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

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class RenderingScopeTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(RenderingScopeTest);

	struct Scene final
	{
		rhi::TextureHandle target{};
		rhi::TextureViewHandle view{};
		rhi::BufferHandle src{};
		rhi::BufferHandle dst{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return target.IsValid() && view.IsValid() && src.IsValid() && dst.IsValid();
		}
	};

	[[nodiscard]] Scene MakeScene(rhi::Device device, rhi::Error & error)
	{
		Scene scene{};
		scene.target = device.CreateTexture(test::samples::ColorTarget2D(), error);
		if (!scene.target.IsValid())
		{
			return {};
		}

		scene.view = device.CreateTextureView(scene.target, test::samples::FullTextureView(), error);
		scene.src  = device.CreateBuffer(test::samples::StorageBuffer(), error);
		scene.dst  = device.CreateBuffer(test::samples::StorageBuffer(), error);
		return scene;
	}

	void DestroyScene(rhi::Device device, const Scene & scene)
	{
		rhi::Error error{};
		static_cast<void>(device.Destroy(scene.dst, {}, error));
		static_cast<void>(device.Destroy(scene.src, {}, error));
		static_cast<void>(device.Destroy(scene.view, {}, error));
		static_cast<void>(device.Destroy(scene.target, {}, error));
	}

	[[nodiscard]] std::array<rhi::RenderingAttachment, 1> ColorAttachment(const rhi::TextureViewHandle view)
	{
		return { rhi::RenderingAttachment{
			.view  = view,
			.state = { .use = rhi::ResourceUse::eColorTarget, .stages = rhi::Stage::eColorOutput },
			.load  = rhi::LoadOp::eClear,
			.store = rhi::StoreOp::eStore,
		} };
	}

	// On Metal a copy inside a pass needs a blit encoder, which ends the pass the caller believes they are still in. Refused here so every backend agrees.
	TEST_P(RenderingScopeTest, TransfersAreRefusedInsideARenderingScope)
	{
		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array colors = ColorAttachment(scene.view);
			const rhi::BeginRenderingDesc rendering{ .colors = colors, .width = test::samples::kTextureDim, .height = test::samples::kTextureDim };

			if (!recording.List().BeginRendering(rendering, error))
			{
				static_cast<void>(recording.End());
				DestroyScene(Dev(), scene);
				GTEST_SKIP() << "this backend refused a rendering scope: " << test::Describe(error);
			}

			rhi::Error copyError{};
			EXPECT_FALSE(recording.List().CopyBuffer(scene.dst, 0, scene.src, 0, test::samples::kBufferSize, copyError))
				<< "a buffer copy was accepted inside a rendering scope";
			EXPECT_TRUE(test::ErrorIsPopulated(copyError));

			rhi::Error clearError{};
			EXPECT_FALSE(recording.List().ClearBuffer(scene.dst, 0, test::samples::kBufferSize, 0u, clearError))
				<< "a buffer clear was accepted inside a rendering scope";
			EXPECT_TRUE(test::ErrorIsPopulated(clearError));

			EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error)) << "the rendering scope did not survive the refusal";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

	// The same call with the same arguments and no scope open, so the case above is refusing the scope rather than the copy.
	TEST_P(RenderingScopeTest, TheSameTransferIsAcceptedBetweenPasses)
	{
		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().CopyBuffer(scene.dst, 0, scene.src, 0, test::samples::kBufferSize, error), error));
			EXPECT_TRUE(test::Ok(recording.List().ClearBuffer(scene.dst, 0, test::samples::kBufferSize, 0u, error), error));
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

	// No compute pipeline is bound, which is also a refusal. The scope is checked first, so this cannot pass for that reason.
	TEST_P(RenderingScopeTest, DispatchIsRefusedInsideARenderingScope)
	{
		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array colors = ColorAttachment(scene.view);
			const rhi::BeginRenderingDesc rendering{ .colors = colors, .width = test::samples::kTextureDim, .height = test::samples::kTextureDim };

			if (!recording.List().BeginRendering(rendering, error))
			{
				static_cast<void>(recording.End());
				DestroyScene(Dev(), scene);
				GTEST_SKIP() << "this backend refused a rendering scope: " << test::Describe(error);
			}

			rhi::Error dispatchError{};
			EXPECT_FALSE(recording.List().Dispatch(1, 1, 1, dispatchError)) << "a dispatch was accepted inside a rendering scope";
			EXPECT_TRUE(test::ErrorIsPopulated(dispatchError));

			EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error)) << "the rendering scope did not survive the refusal";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

} // namespace

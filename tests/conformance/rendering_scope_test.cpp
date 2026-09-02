// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

	constexpr rhi::ResourceState kStorageRead{ .use = rhi::ResourceUse::eStorageRead, .stages = rhi::Stage::eCompute };
	constexpr rhi::ResourceState kCopyDst{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

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

	TEST_P(RenderingScopeTest, SubmitIsRefusedWhileTheListIsStillRecording)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			EXPECT_TRUE(test::Ok(recording.List().CopyBuffer(scene.dst, 0, scene.src, 0, test::samples::kBufferSize, error), error));

			rhi::Queue queue = Dev().GetQueue(rhi::QueueType::eGraphics);
			std::array<const rhi::CommandList *, 1> lists{ &recording.List() };

			rhi::Error submitError{};
			EXPECT_FALSE(queue.Submit({ .commandLists = lists, .debugName = "scope.unclosed" }, submitError))
				<< "a command list was submitted while still recording, so End was never called on it";
			EXPECT_TRUE(test::ErrorIsPopulated(submitError));

			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

	TEST_P(RenderingScopeTest, IndirectDrawsAreRefusedWhereTheirDirectFormsAre)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Error directError{};
			EXPECT_FALSE(recording.List().Draw(3, 1, 0, 0, directError)) << "a direct draw outside a rendering scope was accepted";

			rhi::Error indirectError{};
			EXPECT_FALSE(recording.List().DrawIndirect(scene.src, 0, 1, 0, indirectError))
				<< "an indirect draw outside a rendering scope was accepted while its direct form was refused";
			EXPECT_TRUE(test::ErrorIsPopulated(indirectError));

			rhi::Error countError{};
			EXPECT_FALSE(recording.List().DrawIndirectCount(scene.src, 0, scene.dst, 0, 1, 0, countError))
				<< "an indirect count draw outside a rendering scope was accepted while its direct form was refused";
			EXPECT_TRUE(test::ErrorIsPopulated(countError));

			rhi::Error dispatchError{};
			EXPECT_FALSE(recording.List().DispatchIndirect(scene.src, 0, dispatchError))
				<< "an indirect dispatch with no compute pipeline bound was accepted while its direct form was refused";
			EXPECT_TRUE(test::ErrorIsPopulated(dispatchError));

			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

	TEST_P(RenderingScopeTest, BarriersAreRefusedInsideARenderingScope)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

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

			const std::array barriers{ rhi::BufferBarrier{ .buffer = scene.dst, .before = kStorageRead, .after = kCopyDst } };

			rhi::Error barrierError{};
			EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = barriers }, barrierError))
				<< "a barrier was accepted inside a rendering scope, which Vulkan forbids outright, Metal 3 discards and Metal 4 honours only for vertex";
			EXPECT_TRUE(test::ErrorIsPopulated(barrierError));

			EXPECT_TRUE(test::Ok(recording.List().EndRendering(error), error)) << "the rendering scope did not survive the refusal";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}

	TEST_P(RenderingScopeTest, TheSameBarrierIsAcceptedBetweenPasses)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const Scene scene = MakeScene(Dev(), error);
		ASSERT_TRUE(test::Ok(scene.IsValid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array barriers{ rhi::BufferBarrier{ .buffer = scene.dst, .before = kStorageRead, .after = kCopyDst } };

			EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = barriers }, error), error))
				<< "the same barrier was refused between passes, so the refusal above is not the rendering scope";
			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		DestroyScene(Dev(), scene);
	}
}

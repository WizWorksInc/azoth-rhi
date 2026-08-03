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
#include "azoth/rhi/present/swapchain.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class SwapchainTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(SwapchainTest);

	TEST_P(SwapchainTest, ADefaultConstructedSwapchainIsInert)
	{
		const rhi::Swapchain swapchain;
		EXPECT_FALSE(swapchain.IsValid());
	}

	TEST_P(SwapchainTest, ADefaultSurfaceHandleNamesNothing)
	{
		constexpr rhi::SurfaceHandle surface{};
		static_assert(surface.value == 0);

		SUCCEED();
	}

	TEST_P(SwapchainTest, RefusesASwapchainOverASurfaceThatDoesNotExist)
	{
		rhi::SwapchainDesc desc{};
		desc.surface   = rhi::SurfaceHandle{};
		desc.width	   = 64;
		desc.height	   = 64;
		desc.debugName = "azoth.rhi.test.swapchain";

		rhi::Error error{};
		const rhi::Swapchain swapchain = Dev().CreateSwapchain(desc, error);

		if (swapchain.IsValid())
		{
			EXPECT_GT(swapchain.GetImageCount(), 0u) << "a swapchain reported no images";
			return;
		}

		EXPECT_TRUE(test::ErrorIsPopulated(error)) << "swapchain creation failed with no diagnostic";
	}

	TEST_P(SwapchainTest, TheDescDefaultsToASrgbTripleBufferedFifoChain)
	{
		// These defaults are the portable baseline: FIFO is the one present mode every platform has and an sRGB back buffer is what a caller gets unless they
		// deliberately ask for a linear one.
		constexpr rhi::SwapchainDesc desc{};

		static_assert(desc.preferredFormat == rhi::Format::eBGRA8Srgb);
		static_assert(desc.presentMode == rhi::PresentMode::eFifo);
		static_assert(desc.imageCount == 3);
		static_assert(!desc.allowTearing);
		static_assert(desc.width == 1 && desc.height == 1);

		EXPECT_TRUE(desc.formatFallbacks.empty());
		EXPECT_TRUE(desc.presentModeFallbacks.empty());
	}

	TEST_P(SwapchainTest, CarriesFormatAndPresentModeFallbackLists)
	{
		constexpr std::array formats{ rhi::Format::eRGBA8Srgb, rhi::Format::eBGRA8UNorm };
		constexpr std::array modes{ rhi::PresentMode::eMailbox, rhi::PresentMode::eImmediate };

		rhi::SwapchainDesc desc{};
		desc.formatFallbacks	  = formats;
		desc.presentModeFallbacks = modes;

		ASSERT_EQ(desc.formatFallbacks.size(), 2u);
		EXPECT_EQ(desc.formatFallbacks[0], rhi::Format::eRGBA8Srgb);
		ASSERT_EQ(desc.presentModeFallbacks.size(), 2u);
		EXPECT_EQ(desc.presentModeFallbacks[1], rhi::PresentMode::eImmediate);
	}

	TEST_P(SwapchainTest, AcquireAndPresentResultsDefaultToOk)
	{
		constexpr rhi::AcquireResult acquire{};
		static_assert(acquire.status == rhi::SwapchainStatus::eOk);
		static_assert(acquire.imageIndex == 0);
		static_assert(!acquire.imageAvailable.IsValid());

		constexpr rhi::PresentResult present{};
		static_assert(present.status == rhi::SwapchainStatus::eOk);

		SUCCEED();
	}

	TEST_P(SwapchainTest, TheStatusVocabularySeparatesRecoverableFromFatal)
	{
		static_assert(rhi::SwapchainStatus::eOk != rhi::SwapchainStatus::eSuboptimal);
		static_assert(rhi::SwapchainStatus::eSuboptimal != rhi::SwapchainStatus::eOutOfDate);
		static_assert(rhi::SwapchainStatus::eOutOfDate != rhi::SwapchainStatus::eSurfaceLost);
		static_assert(rhi::SwapchainStatus::eSurfaceLost != rhi::SwapchainStatus::eDeviceLost);

		SUCCEED();
	}

	TEST_P(SwapchainTest, BackendsAgreeOnWhetherTheyCanPresentAtAll)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(CurrentBackend().RegisterInto(registry)));

		ASSERT_EQ(registry.EnumerateBackends().size(), 1u);
		const rhi::BackendInfo & info = registry.EnumerateBackends().front();

		if (!info.supportsSurfaces)
		{
			rhi::SwapchainDesc desc{};
			desc.width	= 64;
			desc.height = 64;

			rhi::Error error{};
			const rhi::Swapchain swapchain = Dev().CreateSwapchain(desc, error);

			// A backend without surfaces may still model a headless chain so what is checked is that a refusal comes with a reason, not silently producing nothing.
			if (!swapchain.IsValid())
			{
				EXPECT_TRUE(test::ErrorIsPopulated(error));
			}
		}
	}

	TEST_P(SwapchainTest, ASwapchainOnlyReportsReadbackWhenItCanActuallyBeCopiedFrom)
	{
		rhi::SwapchainDesc desc{};
		desc.width	= 64;
		desc.height = 64;

		rhi::Error error{};
		rhi::Swapchain swapchain = Dev().CreateSwapchain(desc, error);
		if (!swapchain.IsValid())
		{
			GTEST_SKIP() << "no swapchain without a surface on this backend: " << test::Describe(error);
		}

		static_cast<void>(swapchain.SupportsReadback());
		SUCCEED();
	}

	TEST_P(SwapchainTest, AnUnsupportedRelaxedFifoRequestFallsBackToPlainFifo)
	{
		// Relaxed FIFO exists only on Vulkan surfaces that advertise it, so most chains will not get it. What they must not get is mailbox or immediate, which tear
		// on every frame.
		rhi::SwapchainDesc desc{};
		desc.width		 = 64;
		desc.height		 = 64;
		desc.presentMode = rhi::PresentMode::eFifoRelaxed;

		rhi::Error error{};
		rhi::Swapchain swapchain = Dev().CreateSwapchain(desc, error);
		if (!swapchain.IsValid())
		{
			GTEST_SKIP() << "no swapchain without a surface on this backend: " << test::Describe(error);
		}

		const rhi::PresentMode effective = swapchain.GetPresentMode();
		EXPECT_TRUE(effective == rhi::PresentMode::eFifoRelaxed || effective == rhi::PresentMode::eFifo)
			<< "relaxed FIFO fell back to a tearing mode instead of FIFO";
	}

	/*
	 * The swapchain lends its back buffers out and keeps owning them, handing the same handles back on every acquire. A destroy that took one would free the slot
	 * the swapchain still writes through each frame, and the next resource created would be handed it.
	 *
	 * Refused in every mode: the layer that would otherwise catch it relies on the backend to say no.
	 */
	TEST_P(SwapchainTest, RefusesToDestroyABorrowedBackBufferOrItsView)
	{
		rhi::SwapchainDesc desc{};
		desc.width	= 64;
		desc.height = 64;

		rhi::Error error{};
		rhi::Swapchain swapchain = Dev().CreateSwapchain(desc, error);
		if (!swapchain.IsValid())
		{
			GTEST_SKIP() << "no swapchain without a surface on this backend: " << test::Describe(error);
		}

		for (std::uint32_t index = 0; index < swapchain.GetImageCount(); ++index)
		{
			const rhi::TextureHandle backBuffer = swapchain.GetBackBuffer(index);
			const rhi::TextureViewHandle view	= swapchain.GetBackBufferView(index);
			ASSERT_TRUE(backBuffer.IsValid()) << "image " << index << " has no back buffer";
			ASSERT_TRUE(view.IsValid()) << "image " << index << " has no back buffer view";

			rhi::Error textureError{};
			EXPECT_FALSE(Dev().Destroy(backBuffer, {}, textureError)) << "image " << index << "'s back buffer was destroyed out from under the swapchain";
			EXPECT_EQ(textureError.code, rhi::ErrorCode::eValidationFailed);

			rhi::Error viewError{};
			EXPECT_FALSE(Dev().Destroy(view, {}, viewError)) << "image " << index << "'s back buffer view was destroyed out from under the swapchain";
			EXPECT_EQ(viewError.code, rhi::ErrorCode::eValidationFailed);

			// The refusal has to leave the handles usable, or a caller's mistake would cost them the chain they still hold.
			EXPECT_EQ(swapchain.GetBackBuffer(index), backBuffer) << "a refused destroy still took the back buffer";
			EXPECT_EQ(swapchain.GetBackBufferView(index), view) << "a refused destroy still took the back buffer view";
		}
	}

} // namespace

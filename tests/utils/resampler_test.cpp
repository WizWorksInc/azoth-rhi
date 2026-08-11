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
#include "azoth/rhi/utils/resampler.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace rhi	= azo::rhi;
namespace utils = azo::rhi::utils;
namespace test	= azo::rhi::test;

namespace
{

	class ResamplerTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ResamplerTest);

	constexpr std::uint32_t kDim = 64;

	// What Resampler::GenerateMips documents it leaves behind, spelled here so the assertion reads against the promise and not against the implementation.
	constexpr rhi::ResourceState kPromisedExit{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };

	[[nodiscard]] rhi::TextureDesc ResampleTarget(const std::uint32_t levels) noexcept
	{
		rhi::TextureDesc desc = test::samples::MippedTexture2D(kDim, levels);
		desc.usage			  = desc.usage | rhi::TextureUsage::eStorage;
		return desc;
	}

	[[nodiscard]] std::array<rhi::TextureBarrier, 1> ToUploaded(const rhi::TextureHandle texture, const std::uint32_t levels)
	{
		return { rhi::TextureBarrier{
			.texture = texture,
			.before	 = { .use = rhi::ResourceUse::eDiscard },
			.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			.range	 = { .baseMip = 0, .mipCount = levels },
		} };
	}

	// A barrier claiming the promised exit state. The validation tracker refuses it when the resampler left the levels somewhere else, so it is the assertion.
	[[nodiscard]] std::array<rhi::TextureBarrier, 1> FromPromisedExit(const rhi::TextureHandle texture, const std::uint32_t levels)
	{
		return { rhi::TextureBarrier{
			.texture = texture,
			.before	 = kPromisedExit,
			.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.range	 = { .baseMip = 0, .mipCount = levels },
		} };
	}

	void ExpectExitStateHolds(rhi::Device device, const std::uint32_t levels)
	{
		rhi::Error error{};
		rhi::DescriptorArena arena = device.CreateDescriptorArena(test::samples::DescriptorArena(), error);
		if (!arena.IsValid())
		{
			GTEST_SKIP() << "this backend did not give up a descriptor arena: " << test::Describe(error);
		}

		azo::rhi::Result<utils::Resampler> made =
			utils::Resampler::Create(device, utils::ResamplerDesc{ .arena = &arena, .debugName = "azoth.rhi.test.resampler" });
		if (!made.HasValue())
		{
			GTEST_SKIP() << "no resampler for this backend: " << test::Describe(made.GetError());
		}
		utils::Resampler & resampler = made.Value();

		const rhi::TextureHandle texture = device.CreateTexture(ResampleTarget(levels), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		{
			test::Recording recording(device);
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array uploaded = ToUploaded(texture, levels);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = uploaded }, error), error));

			ASSERT_TRUE(test::Ok(resampler.GenerateMips(recording.List(), texture, error), error));

			const std::array exit = FromPromisedExit(texture, levels);
			EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = exit }, error), error))
				<< "the resampler did not leave every level where its documentation says it does";

			EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
		}

		static_cast<void>(device.Destroy(texture, {}, error));
	}

	// The path that used to return before recording anything, leaving a level the caller was told was fragment readable still a copy destination.
	TEST_P(ResamplerTest, LeavesASingleLevelTextureWhereItsContractSaysItDoes)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		ExpectExitStateHolds(Dev(), 1);
	}

	TEST_P(ResamplerTest, LeavesAWholeChainWhereItsContractSaysItDoes)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		ExpectExitStateHolds(Dev(), 4);
	}

} // namespace

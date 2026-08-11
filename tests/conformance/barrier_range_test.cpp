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

	class BarrierRangeTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(BarrierRangeTest);

	constexpr std::uint32_t kLevels = 4;

	constexpr rhi::ResourceState kDiscard{ .use = rhi::ResourceUse::eDiscard };
	constexpr rhi::ResourceState kCopyDst{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
	constexpr rhi::ResourceState kCopySrc{ .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy };
	constexpr rhi::ResourceState kSampled{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };

	[[nodiscard]] rhi::TextureBarrier Over(const rhi::TextureHandle texture, const std::uint32_t baseMip, const std::uint32_t mipCount,
		const rhi::ResourceState & before, const rhi::ResourceState & after)
	{
		return rhi::TextureBarrier{ .texture = texture, .before = before, .after = after, .range = { .baseMip = baseMip, .mipCount = mipCount } };
	}

	class MippedTexture final
	{
	public:
		explicit MippedTexture(const rhi::Device device) : m_device(device)
		{
			m_texture = m_device.CreateTexture(test::samples::MippedTexture2D(test::samples::kTextureDim, kLevels), m_error);
		}

		MippedTexture(const MippedTexture &)			 = delete;
		MippedTexture & operator=(const MippedTexture &) = delete;
		MippedTexture(MippedTexture &&)					 = delete;
		MippedTexture & operator=(MippedTexture &&)		 = delete;

		~MippedTexture()
		{
			if (m_texture.IsValid())
			{
				rhi::Error ignored{};
				static_cast<void>(m_device.Destroy(m_texture, {}, ignored));
			}
		}

		[[nodiscard]] rhi::TextureHandle Get() const noexcept
		{
			return m_texture;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_texture.IsValid();
		}

		[[nodiscard]] rhi::Error GetError() const noexcept
		{
			return m_error;
		}

	private:
		rhi::Device m_device;
		rhi::TextureHandle m_texture{};
		rhi::Error m_error{};
	};

	// The regression itself: level zero read as a copy source while the levels under it are written, which is what GenerateMips demands on entry.
	TEST_P(BarrierRangeTest, TakesTwoStatesOverOneTextureInOneBatch)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		const MippedTexture texture(Dev());
		ASSERT_TRUE(test::Ok(texture.IsValid(), texture.GetError()));

		rhi::Error error{};
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array split{
			Over(texture.Get(), 0, 1, kDiscard, kCopySrc),
			Over(texture.Get(), 1, kLevels - 1, kDiscard, kCopyDst),
		};
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = split }, error), error))
			<< "two ranges of one texture in one batch were refused, which is the state the tracker cannot express";

		static_cast<void>(recording.End());
	}

	// The loosening risk. Range tracking is only worth having if a range that disagrees is still caught, and a tracker that forgot to compare would pass this.
	TEST_P(BarrierRangeTest, StillRefusesARangeThatDisagreesWithWhatTheWholeTextureWasLeftIn)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		const MippedTexture texture(Dev());
		ASSERT_TRUE(test::Ok(texture.IsValid(), texture.GetError()));

		rhi::Error error{};
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array whole{ Over(texture.Get(), 0, kLevels, kDiscard, kCopyDst) };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = whole }, error), error));

		const std::array wrong{ Over(texture.Get(), 1, 1, kCopySrc, kSampled) };

		rhi::Error wrongError{};
		EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .textures = wrong }, wrongError))
			<< "a range claiming a before-state the whole-texture barrier did not leave it in was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(wrongError));

		static_cast<void>(recording.End());
	}

	// Half of the second range is tracked and agrees, half is untouched and is taken rather than checked, which is the arithmetic a disjoint case never reaches.
	TEST_P(BarrierRangeTest, TakesARangeOverlappingATrackedOneWhenTheOverlapAgrees)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		const MippedTexture texture(Dev());
		ASSERT_TRUE(test::Ok(texture.IsValid(), texture.GetError()));

		rhi::Error error{};
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array lower{ Over(texture.Get(), 0, 2, kDiscard, kCopyDst) };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = lower }, error), error));

		const std::array overlapping{ Over(texture.Get(), 1, 2, kCopyDst, kCopySrc) };
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = overlapping }, error), error))
			<< "an overlapping range whose tracked half agrees was refused";

		static_cast<void>(recording.End());
	}

	TEST_P(BarrierRangeTest, RefusesARangeOverlappingATrackedOneWhenTheOverlapDisagrees)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		const MippedTexture texture(Dev());
		ASSERT_TRUE(test::Ok(texture.IsValid(), texture.GetError()));

		rhi::Error error{};
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array lower{ Over(texture.Get(), 0, 2, kDiscard, kCopyDst) };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = lower }, error), error));

		const std::array overlapping{ Over(texture.Get(), 1, 2, kCopySrc, kSampled) };

		rhi::Error wrongError{};
		EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .textures = overlapping }, wrongError))
			<< "an overlapping range whose tracked half disagrees was accepted, so the untracked half is deciding for both";
		EXPECT_TRUE(test::ErrorIsPopulated(wrongError));

		static_cast<void>(recording.End());
	}

} // namespace

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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <ostream>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class GenerateMipsTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(GenerateMipsTest);

	constexpr std::uint32_t kBaseDim	   = 64;
	constexpr std::uint32_t kLevels		   = 4;
	constexpr std::uint64_t kBytesPerTexel = 4;

	// Downsampling a constant image reproduces it exactly, so a level that reads back different was never written or was blitted out of undefined contents.
	constexpr std::array<std::uint8_t, kBytesPerTexel> kTexel{ 0x20u, 0x48u, 0x90u, 0xFFu };

	// The placement granularity a buffer-texture copy takes on Direct3D 12, applied to every level so one readback buffer holds them all.
	constexpr std::uint64_t kCopyAlignment = 256;

	[[nodiscard]] constexpr std::uint32_t LevelDim(const std::uint32_t level) noexcept
	{
		const std::uint32_t dim = kBaseDim >> level;
		return dim > 0 ? dim : 1;
	}

	[[nodiscard]] constexpr std::uint64_t LevelBytes(const std::uint32_t level) noexcept
	{
		return std::uint64_t{ LevelDim(level) } * LevelDim(level) * kBytesPerTexel;
	}

	[[nodiscard]] constexpr std::uint64_t LevelOffset(const std::uint32_t level) noexcept
	{
		std::uint64_t offset = 0;
		for (std::uint32_t below = 0; below < level; ++below)
		{
			offset += (LevelBytes(below) + kCopyAlignment - 1) / kCopyAlignment * kCopyAlignment;
		}
		return offset;
	}

	constexpr std::uint64_t kReadbackBytes = LevelOffset(kLevels);

	[[nodiscard]] std::vector<std::uint8_t> Pattern(const std::uint64_t bytes)
	{
		std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes), 0);
		for (std::size_t byte = 0; byte < data.size(); ++byte)
		{
			data.at(byte) = kTexel.at(byte % kTexel.size());
		}
		return data;
	}

	[[nodiscard]] ::testing::AssertionResult MatchesPattern(const std::vector<std::uint8_t> & observed, const std::uint64_t offset, const std::uint64_t bytes)
	{
		if (observed.size() < offset + bytes)
		{
			return ::testing::AssertionFailure() << "the readback is " << observed.size() << " bytes, short of the level at " << offset;
		}

		for (std::uint64_t byte = 0; byte < bytes; ++byte)
		{
			const std::uint8_t expected = kTexel.at(static_cast<std::size_t>(byte % kTexel.size()));
			const std::uint8_t actual	= observed.at(static_cast<std::size_t>(offset + byte));
			if (actual != expected)
			{
				return ::testing::AssertionFailure() << "byte " << byte << " of the level reads 0x" << std::hex << unsigned{ actual } << " and not 0x"
													 << unsigned{ expected } << std::dec;
			}
		}
		return ::testing::AssertionSuccess();
	}

	// Everything one case creates, so a skip or a failed assertion still hands the handles back.
	struct MipChain final
	{
		rhi::Device device;
		rhi::TextureHandle texture{};
		rhi::BufferHandle upload{};
		rhi::BufferHandle readback{};
		rhi::TimelineHandle done{};

		explicit MipChain(const rhi::Device owner) noexcept : device(owner) {}

		MipChain(const MipChain &)			   = delete;
		MipChain & operator=(const MipChain &) = delete;
		MipChain(MipChain &&)				   = delete;
		MipChain & operator=(MipChain &&)	   = delete;

		~MipChain()
		{
			rhi::Error ignored{};
			if (done.IsValid())
			{
				static_cast<void>(device.Destroy(done, {}, ignored));
			}
			if (readback.IsValid())
			{
				static_cast<void>(device.Destroy(readback, {}, ignored));
			}
			if (upload.IsValid())
			{
				static_cast<void>(device.Destroy(upload, {}, ignored));
			}
			if (texture.IsValid())
			{
				static_cast<void>(device.Destroy(texture, {}, ignored));
			}
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return texture.IsValid() && upload.IsValid() && readback.IsValid() && done.IsValid();
		}
	};

	[[nodiscard]] bool FillUpload(rhi::Device device, const rhi::BufferHandle upload, const std::vector<std::uint8_t> & bytes, rhi::Error & error)
	{
		const rhi::MappedMemory staging = device.Map(upload, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
		if (staging.data == nullptr)
		{
			return false;
		}

		std::memcpy(staging.data, bytes.data(), bytes.size());
		if (!staging.coherent && !device.FlushMappedRange(upload, 0, bytes.size(), error))
		{
			return false;
		}
		return device.Unmap(upload, error);
	}

	[[nodiscard]] bool Build(MipChain & chain, const std::uint32_t levels, rhi::Error & error)
	{
		const std::vector<std::uint8_t> source = Pattern(LevelBytes(0));

		chain.texture  = chain.device.CreateTexture(test::samples::MippedTexture2D(kBaseDim, levels), error);
		chain.upload   = chain.device.CreateBuffer(test::samples::UploadBuffer(source.size()), error);
		chain.readback = chain.device.CreateBuffer(test::samples::ReadbackBuffer(kReadbackBytes), error);
		chain.done	   = chain.device.CreateTimeline(test::samples::Timeline(), error);

		return chain.IsValid() && FillUpload(chain.device, chain.upload, source, error);
	}

	[[nodiscard]] std::array<rhi::TextureBarrier, 1> WholeChainToCopyDst(const rhi::TextureHandle texture, const std::uint32_t levels)
	{
		return { rhi::TextureBarrier{
			.texture = texture,
			.before	 = { .use = rhi::ResourceUse::eDiscard },
			.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			.range	 = { .baseMip = 0, .mipCount = levels },
		} };
	}

	[[nodiscard]] std::array<rhi::TextureBarrier, 1> LevelZeroToCopySrc(const rhi::TextureHandle texture)
	{
		return { rhi::TextureBarrier{
			.texture = texture,
			.before	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.range	 = { .baseMip = 0, .mipCount = 1 },
		} };
	}

	[[nodiscard]] std::array<rhi::BufferTextureCopy, 1> LevelZeroRegion()
	{
		return { rhi::BufferTextureCopy{
			.subresource   = { .mip = 0 },
			.textureExtent = { .width = LevelDim(0), .height = LevelDim(0) },
		} };
	}

	[[nodiscard]] std::vector<rhi::BufferTextureCopy> LevelsBelowZero(const std::uint32_t levels)
	{
		std::vector<rhi::BufferTextureCopy> regions;
		regions.reserve(levels - 1);
		for (std::uint32_t level = 1; level < levels; ++level)
		{
			regions.push_back(rhi::BufferTextureCopy{
				.bufferOffset  = LevelOffset(level),
				.subresource   = { .mip = level },
				.textureExtent = { .width = LevelDim(level), .height = LevelDim(level) },
			});
		}
		return regions;
	}

	[[nodiscard]] std::vector<std::uint8_t> ReadBack(rhi::Device device, const rhi::BufferHandle readback, rhi::Error & error)
	{
		const rhi::MappedMemory mapped = device.Map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		if (mapped.data == nullptr)
		{
			return {};
		}
		if (!mapped.coherent && !device.InvalidateMappedRange(readback, 0, kReadbackBytes, error))
		{
			return {};
		}

		std::vector<std::uint8_t> observed(static_cast<std::size_t>(kReadbackBytes), 0);
		std::memcpy(observed.data(), mapped.data, observed.size());
		static_cast<void>(device.Unmap(readback, error));
		return observed;
	}

	[[nodiscard]] ::testing::AssertionResult RunAndWait(test::Recording & recording, rhi::Device device, const rhi::TimelineHandle done)
	{
		rhi::Error error{};
		if (!recording.End())
		{
			return ::testing::AssertionFailure() << "the recording did not close: " << test::Describe(recording.GetError());
		}

		rhi::Queue queue = device.GetQueue(rhi::QueueType::eGraphics);
		std::array<const rhi::CommandList *, 1> lists{ &recording.List() };
		const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };
		if (!queue.Submit({ .commandLists = lists, .signals = signals, .debugName = "generateMips" }, error))
		{
			return ::testing::AssertionFailure() << "the submit was refused: " << test::Describe(error);
		}
		if (!queue.Wait(done, 1, test::kWaitTimeoutNanoseconds, error))
		{
			return ::testing::AssertionFailure() << "the mip generation never completed: " << test::Describe(error);
		}
		return ::testing::AssertionSuccess();
	}

	// The failure this covers returns success while blitting out of undefined contents, so only the contents say whether it worked.
	TEST_P(GenerateMipsTest, FillsEveryLevelBelowZeroFromTheContentsOfLevelZero)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array toCopyDst = WholeChainToCopyDst(chain.texture, kLevels);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error), error));

			const std::array upload = LevelZeroRegion();
			ASSERT_TRUE(test::Ok(recording.List().CopyBufferToTexture(chain.texture, chain.upload, upload, error), error));

			const std::array toCopySrc = LevelZeroToCopySrc(chain.texture);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopySrc }, error), error));

			if (!recording.List().GenerateMips(chain.texture, error))
			{
				if (error.code == rhi::ErrorCode::eUnsupportedFeature)
				{
					GTEST_SKIP() << CurrentBackend().displayName << " has no mip generation: " << test::Describe(error);
				}
				FAIL() << "generateMips was refused: " << test::Describe(error);
			}

			// The contract leaves every level a copy source, level zero included, so the readback needs no barrier of its own.
			const std::vector regions = LevelsBelowZero(kLevels);
			ASSERT_TRUE(test::Ok(recording.List().CopyTextureToBuffer(chain.readback, chain.texture, regions, error), error));

			ASSERT_TRUE(RunAndWait(recording, Dev(), chain.done));
		}

		const std::vector<std::uint8_t> observed = ReadBack(Dev(), chain.readback, error);
		ASSERT_FALSE(observed.empty()) << "the readback buffer could not be mapped, so nothing was checked: " << test::Describe(error);

		if (!IsNullBackend())
		{
			for (std::uint32_t level = 1; level < kLevels; ++level)
			{
				EXPECT_TRUE(MatchesPattern(observed, LevelOffset(level), LevelBytes(level))) << "level " << level << " does not carry level zero's contents";
			}
		}

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "mip generation reported errors to the native validation layer");
	}

	// Two ranges of one texture in one batch, which is how the header states the entry contract and what the validation state tracker cannot yet express.
	TEST_P(GenerateMipsTest, TakesTheSplitEntryStateTheContractAsksFor)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array toCopyDst = WholeChainToCopyDst(chain.texture, 1);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error), error));

			const std::array upload = LevelZeroRegion();
			ASSERT_TRUE(test::Ok(recording.List().CopyBufferToTexture(chain.texture, chain.upload, upload, error), error));

			const std::array split{
				rhi::TextureBarrier{
					.texture = chain.texture,
					.before	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
					.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
					.range	 = { .baseMip = 0, .mipCount = 1 },
				},
				rhi::TextureBarrier{
					.texture = chain.texture,
					.before	 = { .use = rhi::ResourceUse::eDiscard },
					.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
					.range	 = { .baseMip = 1, .mipCount = kLevels - 1 },
				},
			};
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = split }, error), error))
				<< "the entry states CommandList::GenerateMips documents cannot be recorded";

			if (!recording.List().GenerateMips(chain.texture, error))
			{
				if (error.code == rhi::ErrorCode::eUnsupportedFeature)
				{
					GTEST_SKIP() << CurrentBackend().displayName << " has no mip generation: " << test::Describe(error);
				}
				FAIL() << "generateMips was refused: " << test::Describe(error);
			}

			// The exit half of the same contract, recorded the way ibl.cpp does: the whole chain leaves a copy source, level zero included.
			const std::array toSampled{ rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.after	 = { .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading },
				.range	 = { .baseMip = 0, .mipCount = kLevels },
			} };
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toSampled }, error), error))
				<< "the exit state CommandList::GenerateMips documents cannot be claimed afterwards";

			const std::array backToCopySrc{ rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading },
				.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.range	 = { .baseMip = 0, .mipCount = kLevels },
			} };
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = backToCopySrc }, error), error));

			const std::vector regions = LevelsBelowZero(kLevels);
			ASSERT_TRUE(test::Ok(recording.List().CopyTextureToBuffer(chain.readback, chain.texture, regions, error), error));

			ASSERT_TRUE(RunAndWait(recording, Dev(), chain.done));
		}

		const std::vector<std::uint8_t> observed = ReadBack(Dev(), chain.readback, error);
		ASSERT_FALSE(observed.empty()) << "the readback buffer could not be mapped, so nothing was checked: " << test::Describe(error);

		if (!IsNullBackend())
		{
			for (std::uint32_t level = 1; level < kLevels; ++level)
			{
				EXPECT_TRUE(MatchesPattern(observed, LevelOffset(level), LevelBytes(level))) << "level " << level << " does not carry level zero's contents";
			}
		}

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "mip generation reported errors to the native validation layer");
	}

	// Entry equalling exit is what removed the single-level carve-out: a texture already a copy source records nothing and keeps what it holds.
	TEST_P(GenerateMipsTest, LeavesASingleLevelTextureAsACopySourceHoldingItsContents)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, 1, error))
		{
			GTEST_SKIP() << "this backend did not give up a texture and the buffers to round trip through: " << test::Describe(error);
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array toCopyDst = WholeChainToCopyDst(chain.texture, 1);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error), error));

			const std::array upload = LevelZeroRegion();
			ASSERT_TRUE(test::Ok(recording.List().CopyBufferToTexture(chain.texture, chain.upload, upload, error), error));

			const std::array toCopySrc = LevelZeroToCopySrc(chain.texture);
			ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopySrc }, error), error));

			EXPECT_TRUE(test::Ok(recording.List().GenerateMips(chain.texture, error), error)) << "a single-level texture was refused";

			const std::array<rhi::BufferTextureCopy, 1> regions{ rhi::BufferTextureCopy{
				.subresource   = { .mip = 0 },
				.textureExtent = { .width = LevelDim(0), .height = LevelDim(0) },
			} };
			ASSERT_TRUE(test::Ok(recording.List().CopyTextureToBuffer(chain.readback, chain.texture, regions, error), error));

			ASSERT_TRUE(RunAndWait(recording, Dev(), chain.done));
		}

		const std::vector<std::uint8_t> observed = ReadBack(Dev(), chain.readback, error);
		ASSERT_FALSE(observed.empty()) << "the readback buffer could not be mapped, so nothing was checked: " << test::Describe(error);

		if (!IsNullBackend())
		{
			EXPECT_TRUE(MatchesPattern(observed, LevelOffset(0), LevelBytes(0))) << "the single level did not survive the call that had nothing to do";
		}

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "mip generation reported errors to the native validation layer");
	}

	// Two chains in one list. The second is refused by a span covering levels the texture does not have, left behind when a concrete range cut an unbounded one.
	TEST_P(GenerateMipsTest, TakesASecondChainInTheSameRecording)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array split{
			rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.range	 = { .baseMip = 0, .mipCount = 1 },
			},
			rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.range	 = { .baseMip = 1, .mipCount = kLevels - 1 },
			},
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = split }, error), error));

		if (!recording.List().GenerateMips(chain.texture, error))
		{
			if (error.code == rhi::ErrorCode::eUnsupportedFeature)
			{
				GTEST_SKIP() << CurrentBackend().displayName << " has no mip generation: " << test::Describe(error);
			}
			FAIL() << "the first generateMips was refused: " << test::Describe(error);
		}

		// Naming the real level count, which is what leaves the phantom behind. Naming kAllMips here would hide it.
		const std::array again{
			rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.range	 = { .baseMip = 1, .mipCount = kLevels - 1 },
			},
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = again }, error), error));

		EXPECT_TRUE(test::Ok(recording.List().GenerateMips(chain.texture, error), error))
			<< "the entry states are the ones the contract asks for, so the second chain has to be taken too";

		EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
	}

	// The sentinel is what a caller reaches for when they do not know the level count, so mixing it with a concrete range has to stay legal.
	TEST_P(GenerateMipsTest, TakesAChainDeclaredThroughTheWholeRangeSentinel)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array whole{ rhi::TextureBarrier{
			.texture = chain.texture,
			.before	 = { .use = rhi::ResourceUse::eDiscard },
			.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.range	 = { .mipCount = rhi::kAllMips, .layerCount = rhi::kAllLayers },
		} };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = whole }, error), error));

		// Named by count, which is what leaves a residual behind when the span it cuts was saturated rather than resolved.
		const std::array below{ rhi::TextureBarrier{
			.texture = chain.texture,
			.before	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
			.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			.range	 = { .baseMip = 1, .mipCount = kLevels - 1 },
		} };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = below }, error), error));

		if (!recording.List().GenerateMips(chain.texture, error))
		{
			if (error.code == rhi::ErrorCode::eUnsupportedFeature)
			{
				GTEST_SKIP() << CurrentBackend().displayName << " has no mip generation: " << test::Describe(error);
			}
			FAIL() << "the entry states are the ones the contract asks for, reached through the sentinel: " << test::Describe(error);
		}

		EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
	}

	TEST_P(GenerateMipsTest, IsRefusedWhenLevelZeroIsNotACopySource)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopyDst = WholeChainToCopyDst(chain.texture, kLevels);
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error), error));

		rhi::Error entryError{};
		EXPECT_FALSE(recording.List().GenerateMips(chain.texture, entryError)) << "the whole chain was left a copy destination and generateMips took it";
		EXPECT_TRUE(test::ErrorIsPopulated(entryError));
		EXPECT_NE(entryError.code, rhi::ErrorCode::eUnsupportedFeature) << "refused for having no mip generation, so the entry state was never reached";

		EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
	}

	TEST_P(GenerateMipsTest, IsRefusedWhenALevelBelowZeroIsNotACopyDestination)
	{
		rhi::Error error{};
		MipChain chain(Dev());
		if (!Build(chain, kLevels, error))
		{
			GTEST_SKIP() << "this backend did not give up a mipped texture and the buffers to round trip through: " << test::Describe(error);
		}

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		// Level zero right and the levels under it wrong, which is the arm the case above cannot reach.
		const std::array split{
			rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.range	 = { .baseMip = 0, .mipCount = 1 },
			},
			rhi::TextureBarrier{
				.texture = chain.texture,
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading },
				.range	 = { .baseMip = 1, .mipCount = kLevels - 1 },
			},
		};
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .textures = split }, error), error));

		rhi::Error entryError{};
		EXPECT_FALSE(recording.List().GenerateMips(chain.texture, entryError)) << "the levels below zero were left readable and generateMips took it";
		EXPECT_TRUE(test::ErrorIsPopulated(entryError));
		EXPECT_NE(entryError.code, rhi::ErrorCode::eUnsupportedFeature) << "refused for having no mip generation, so the entry state was never reached";

		EXPECT_TRUE(test::Ok(recording.End(), recording.GetError()));
	}

} // namespace

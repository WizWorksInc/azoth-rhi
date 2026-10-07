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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class ExternalSharingTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] bool SharedTypeFor(const rhi::ExternalObjectKind kind, rhi::ExternalHandleType & out) const
		{
			for (const rhi::ExternalHandleType handleType : rhi::kAllExternalHandleTypes)
			{
				rhi::Error error{};
				rhi::ExternalHandleSupport support{};
				const rhi::ExternalHandleSupportDesc desc{
					.adapterIndex = 0,
					.kind		  = kind,
					.handleType	  = handleType,
					.format		  = rhi::Format::eRGBA8UNorm,
				};

				if (Inst().query_external_handle_support(desc, support, error) && support.exportable && support.importable)
				{
					out = handleType;
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] bool SharesAnything() const
		{
			rhi::ExternalHandleType ignored = rhi::kAllExternalHandleTypes.front();
			return SharedTypeFor(rhi::ExternalObjectKind::eBuffer, ignored) || SharedTypeFor(rhi::ExternalObjectKind::eTexture, ignored) ||
				   SharedTypeFor(rhi::ExternalObjectKind::eHeap, ignored) || SharedTypeFor(rhi::ExternalObjectKind::eTimeline, ignored) ||
				   SharedTypeFor(rhi::ExternalObjectKind::eBinarySemaphore, ignored);
		}

		void SetUp() override
		{
			test::BackendTest::SetUp();
			if (::testing::Test::IsSkipped())
			{
				return;
			}

			rhi::InstanceDesc desc{};
			desc.applicationName = "external_sharing_test";
			desc.validation		 = rhi::ValidationMode::eOff;

			const std::array<rhi::GraphicsApiId, 1> only{ CurrentBackend().id };
			rhi::Result<rhi::UniqueInstance> created = rhi::create_instance(Harness().Registry(), only, desc);
			ASSERT_TRUE(test::Ok(created)) << "the backend under test could not make an instance to ask";
			m_instance = std::move(created).value();
		}

		[[nodiscard]] rhi::Instance Inst() const noexcept
		{
			return m_instance.get();
		}

	private:
		rhi::UniqueInstance m_instance;
	};

	AZO_RHI_BACKEND_SUITE(ExternalSharingTest);

	constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

	constexpr std::uint32_t kExtent		= 4;
	constexpr std::uint64_t kImageBytes = std::uint64_t{ kExtent } * kExtent * 4u;

	constexpr std::array<std::uint8_t, 4> kTexel{ 0x1Au, 0x2Bu, 0x3Cu, 0xFFu };

	[[nodiscard]] rhi::TextureDesc SharedTextureDesc(const rhi::ExternalHandleType handleType)
	{
		rhi::TextureDesc desc	   = test::samples::SampledTexture2D(kExtent);
		desc.usage				   = rhi::Flags<rhi::TextureUsage>(rhi::TextureUsage::eCopySrc) | rhi::TextureUsage::eCopyDst | rhi::TextureUsage::eSampled;
		desc.exportableHandleTypes = handleType;
		desc.debugName			   = "azoth.rhi.test.sharedTexture";
		return desc;
	}

	TEST_P(ExternalSharingTest, AWriteOnOneDeviceIsVisibleToAnotherAfterAnImportedTimelineWait)
	{
		rhi::ExternalHandleType textureType	 = rhi::kAllExternalHandleTypes.front();
		rhi::ExternalHandleType timelineType = rhi::kAllExternalHandleTypes.front();
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTexture, textureType), "a shareable texture");
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTimeline, timelineType), "a shareable timeline");

		test::DeviceHarness second(CurrentBackend());
		ASSERT_TRUE(test::Ok(second.IsValid(), second.GetError())) << "the second device on the same adapter could not be created";
		rhi::Device consumer = second.Get();

		rhi::Error error{};
		const rhi::TextureHandle produced = Dev().create_texture(SharedTextureDesc(textureType), error);
		ASSERT_TRUE(test::Ok(produced.is_valid(), error)) << "an exportable texture the query reported supported was refused at creation";

		rhi::TimelineDesc timelineDesc{};
		timelineDesc.exportableHandleTypes		   = timelineType;
		timelineDesc.debugName					   = "azoth.rhi.test.sharedTimeline";
		const rhi::TimelineHandle producedTimeline = Dev().create_timeline(timelineDesc, error);
		ASSERT_TRUE(test::Ok(producedTimeline.is_valid(), error)) << "an exportable timeline the query reported supported was refused at creation";

		rhi::ExternalHandle textureHandle{};
		ASSERT_TRUE(test::Ok(Dev().export_texture(produced, textureType, textureHandle, error), error)) << "exporting the texture failed";
		rhi::ExternalHandle timelineHandle{};
		ASSERT_TRUE(test::Ok(Dev().export_timeline(producedTimeline, timelineType, timelineHandle, error), error)) << "exporting the timeline failed";

		rhi::TextureDesc importedTextureDesc	  = SharedTextureDesc(textureType);
		importedTextureDesc.exportableHandleTypes = {};
		const rhi::TextureHandle consumed		  = consumer.import_texture({ .handle = textureHandle, .desc = importedTextureDesc }, error);
		ASSERT_TRUE(test::Ok(consumed.is_valid(), error)) << "importing the texture onto a second device on the same adapter failed";

		rhi::TimelineDesc importedTimelineDesc{};
		importedTimelineDesc.debugName			   = "azoth.rhi.test.importedTimeline";
		const rhi::TimelineHandle consumedTimeline = consumer.import_timeline({ .handle = timelineHandle, .desc = importedTimelineDesc }, error);
		ASSERT_TRUE(test::Ok(consumedTimeline.is_valid(), error)) << "importing the timeline failed";

		const rhi::BufferHandle readback = consumer.create_buffer(test::samples::ReadbackBuffer(kImageBytes), error);
		ASSERT_TRUE(test::Ok(readback.is_valid(), error));

		const rhi::BufferHandle upload = Dev().create_buffer(test::samples::UploadBuffer(kImageBytes), error);
		ASSERT_TRUE(test::Ok(upload.is_valid(), error));
		{
			const rhi::MappedMemory staging = Dev().map(upload, rhi::MapDesc{ .mode = rhi::MapMode::eWrite }, error);
			ASSERT_NE(staging.data, nullptr) << "the upload buffer could not be mapped, so there was nothing to share";
			for (std::uint64_t texel = 0; texel < kImageBytes; texel += kTexel.size())
			{
				std::memcpy(static_cast<std::uint8_t *>(staging.data) + texel, kTexel.data(), kTexel.size());
			}

			if (!staging.coherent)
			{
				ASSERT_TRUE(test::Ok(Dev().flush_mapped_range(upload, 0, kImageBytes, error), error));
			}

			ASSERT_TRUE(test::Ok(Dev().unmap(upload, error), error));
		}

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Queue producerQueue = Dev().get_queue(rhi::QueueType::eGraphics);
			const std::array toCopyDst{ rhi::TextureBarrier{
				.texture = produced,
				.before	 = { .use = rhi::ResourceUse::eDiscard },
				.after	 = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
			} };
			const std::array release{ rhi::TextureBarrier{
				.texture   = produced,
				.before	   = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.ownership = { .op = rhi::OwnershipOp::eReleaseToExternal },
			} };
			const std::array uploadRegions{ rhi::BufferTextureCopy{
				.subresource   = { .aspects = rhi::TextureAspect::eColor },
				.textureExtent = { .width = kExtent, .height = kExtent, .depth = 1 },
			} };

			ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = toCopyDst }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().copy_buffer_to_texture(produced, upload, uploadRegions, error), error));
			ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = release }, error), error));
			ASSERT_TRUE(test::Ok(recording.End(), recording.GetError()));

			std::array<const rhi::CommandList *, 1> lists{ &recording.List() };
			const std::array signals{ rhi::TimelinePoint{ .timeline = producedTimeline, .value = 1 } };
			ASSERT_TRUE(test::Ok(producerQueue.submit({ .commandLists = lists, .signals = signals, .debugName = "external.produce" }, error), error));
		}

		{
			test::Recording recording(consumer);
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			rhi::Queue consumerQueue = consumer.get_queue(rhi::QueueType::eGraphics);
			const std::array acquire{ rhi::TextureBarrier{
				.texture   = consumed,
				.before	   = { .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy },
				.after	   = { .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy },
				.ownership = { .op = rhi::OwnershipOp::eAcquireFromExternal },
			} };
			const std::array regions{ rhi::BufferTextureCopy{
				.subresource   = { .aspects = rhi::TextureAspect::eColor },
				.textureExtent = { .width = kExtent, .height = kExtent, .depth = 1 },
			} };

			ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = acquire }, error), error));
			ASSERT_TRUE(test::Ok(recording.List().copy_texture_to_buffer(readback, consumed, regions, error), error));
			ASSERT_TRUE(test::Ok(recording.End(), recording.GetError()));

			const rhi::TimelineHandle done = consumer.create_timeline({}, error);
			ASSERT_TRUE(test::Ok(done.is_valid(), error));

			std::array<const rhi::CommandList *, 1> lists{ &recording.List() };
			const std::array waits{ rhi::TimelinePoint{ .timeline = consumedTimeline, .value = 1 } };
			const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };
			ASSERT_TRUE(
				test::Ok(consumerQueue.submit({ .commandLists = lists, .waits = waits, .signals = signals, .debugName = "external.consume" }, error), error));

			ASSERT_TRUE(test::Ok(consumerQueue.wait(done, 1, kNoTimeout, error), error)) << "the consumer never reached its own completion signal";
		}

		const rhi::MappedMemory mapped = consumer.map(readback, rhi::MapDesc{ .mode = rhi::MapMode::eRead }, error);
		ASSERT_NE(mapped.data, nullptr) << "the readback buffer could not be mapped, so nothing was checked";
		if (!mapped.coherent)
		{
			ASSERT_TRUE(test::Ok(consumer.invalidate_mapped_range(readback, 0, kImageBytes, error), error));
		}

		std::array<std::uint8_t, kTexel.size()> texel{};
		std::memcpy(texel.data(), mapped.data, texel.size());
		static_cast<void>(consumer.unmap(readback, error));

		EXPECT_EQ(texel, kTexel) << "the imported texture does not carry what the exporting device wrote";

		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(Dev(), "the producing device reported validation errors during the round trip");
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(consumer, "the consuming device reported validation errors during the round trip");

		EXPECT_TRUE(test::Ok(Dev().destroy(upload, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().close_exported_handle(textureHandle, error), error));
		EXPECT_TRUE(test::Ok(Dev().close_exported_handle(timelineHandle, error), error));
	}

	TEST_P(ExternalSharingTest, RefusesToExportAHandleTypeTheObjectNeverDeclared)
	{
		rhi::ExternalHandleType textureType = rhi::kAllExternalHandleTypes.front();
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTexture, textureType), "a shareable texture");

		rhi::Error error{};
		const rhi::TextureHandle plain = Dev().create_texture(test::samples::SampledTexture2D(kExtent), error);
		ASSERT_TRUE(test::Ok(plain.is_valid(), error));

		rhi::ExternalHandle handle{};
		EXPECT_FALSE(Dev().export_texture(plain, textureType, handle, error)) << "a texture created with no export declaration was exported anyway";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
		EXPECT_EQ(handle.fd, -1) << "a refused export left a handle behind";
		EXPECT_EQ(handle.handle, nullptr) << "a refused export left a handle behind";

		EXPECT_TRUE(test::Ok(Dev().destroy(plain, {}, error), error));
	}

	TEST_P(ExternalSharingTest, RefusesToExportATimelineThatNeverDeclaredOne)
	{
		rhi::ExternalHandleType timelineType = rhi::kAllExternalHandleTypes.front();
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTimeline, timelineType), "a shareable timeline");

		rhi::Error error{};
		const rhi::TimelineHandle plain = Dev().create_timeline({}, error);
		ASSERT_TRUE(test::Ok(plain.is_valid(), error));

		rhi::ExternalHandle handle{};
		EXPECT_FALSE(Dev().export_timeline(plain, timelineType, handle, error)) << "a timeline created with no export declaration was exported anyway";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
		EXPECT_EQ(handle.handle, nullptr) << "a refused export left a handle behind";

		EXPECT_TRUE(test::Ok(Dev().destroy(plain, {}, error), error));
	}

	TEST_P(ExternalSharingTest, RefusesAnImportOfAHandleThatNamesNothing)
	{
		rhi::ExternalHandleType textureType = rhi::kAllExternalHandleTypes.front();
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTexture, textureType), "a shareable texture");

		rhi::Error error{};
		const rhi::ExternalTextureImportDesc desc{
			.handle = { .type = textureType, .fd = -1, .handle = nullptr },
			.desc	= test::samples::SampledTexture2D(kExtent),
		};

		const rhi::TextureHandle imported = Dev().import_texture(desc, error);
		EXPECT_FALSE(imported.is_valid()) << "an import of a handle carrying nothing produced a texture";
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(ExternalSharingTest, TheSurfaceIsPresentExactlyWhereTheAdapterReportsItCanShare)
	{
		const bool canShare = SharesAnything();

		rhi::Error error{};
		const rhi::TextureHandle plain = Dev().create_texture(test::samples::SampledTexture2D(kExtent), error);
		ASSERT_TRUE(test::Ok(plain.is_valid(), error));

		rhi::ExternalHandle handle{};
		EXPECT_FALSE(Dev().export_texture(plain, rhi::ExternalHandleType::eOpaqueFd, handle, error));
		if (canShare)
		{
			EXPECT_NE(error.code, rhi::ErrorCode::eUnsupportedFeature)
				<< "an adapter reporting it can share declined the sharing surface as absent, so the block was not published";
		}
		else
		{
			EXPECT_EQ(error.code, rhi::ErrorCode::eUnsupportedFeature)
				<< "an adapter reporting it can share nothing published a sharing surface anyway, which is the reportable-but-unusable shape this "
				   "avoids";
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(plain, {}, error), error));
	}

	TEST_P(ExternalSharingTest, ClosingAnExportedHandleAfterItWasImportedIsSafe)
	{
		rhi::ExternalHandleType textureType = rhi::kAllExternalHandleTypes.front();
		AZO_RHI_REQUIRE_CAP(SharedTypeFor(rhi::ExternalObjectKind::eTexture, textureType), "a shareable texture");

		rhi::Error error{};
		const rhi::TextureHandle exported = Dev().create_texture(SharedTextureDesc(textureType), error);
		ASSERT_TRUE(test::Ok(exported.is_valid(), error));

		rhi::ExternalHandle handle{};
		ASSERT_TRUE(test::Ok(Dev().export_texture(exported, textureType, handle, error), error));

		rhi::TextureDesc importedDesc	   = SharedTextureDesc(textureType);
		importedDesc.exportableHandleTypes = {};
		const rhi::TextureHandle imported  = Dev().import_texture({ .handle = handle, .desc = importedDesc }, error);
		ASSERT_TRUE(test::Ok(imported.is_valid(), error)) << "a device could not import a payload it had just exported itself";

		EXPECT_TRUE(test::Ok(Dev().close_exported_handle(handle, error), error));

		EXPECT_TRUE(test::Ok(Dev().destroy(imported, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(exported, {}, error), error));
	}

}

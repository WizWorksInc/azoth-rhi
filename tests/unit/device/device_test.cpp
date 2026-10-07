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
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"
#include "harness/spies.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint> // NOLINT
#include <string_view>
#include <type_traits>
#include <utility>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class DeviceTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(DeviceTest);

	TEST_P(DeviceTest, ReportsTheBackendThatCreatedIt)
	{
		EXPECT_EQ(Dev().get_graphics_api_id(), CurrentBackend().id);
		EXPECT_EQ(Dev().get_graphics_api_name(), std::string_view{ CurrentBackend().displayName });
	}

	TEST_P(DeviceTest, CapsAndAdapterAgreeOnWhichBackendThisIs)
	{
		EXPECT_EQ(Caps().apiId, CurrentBackend().id);
		EXPECT_EQ(Dev().get_adapter_info().apiId, CurrentBackend().id);
	}

	TEST_P(DeviceTest, ASparseTierComesWithATileSizeToBindWith)
	{
		if (Caps().sparseTier == rhi::SparseTier::eNone)
		{
			GTEST_SKIP() << "this device binds no sparse memory";
		}

		EXPECT_GT(Caps().sparseTileSizeBytes, 0u) << "this device reports a sparse tier but no tile size, so nothing can be sized against it";
	}

	TEST_P(DeviceTest, TheDescriptorLimitsAreFilledIn)
	{
		EXPECT_GT(Caps().maxDescriptorSets, 0u) << "maxDescriptorSets reads zero, which no device means";
		EXPECT_GT(Caps().maxDescriptorsPerSet, 0u) << "maxDescriptorsPerSet reads zero, which no device means";
	}

	TEST_P(DeviceTest, TheBufferAlignmentsAreFilledIn)
	{
		EXPECT_GE(Caps().minUniformBufferOffsetAlignment, 1u) << "minUniformBufferOffsetAlignment reads zero, which is not an alignment";
		EXPECT_GE(Caps().minStorageBufferOffsetAlignment, 1u) << "minStorageBufferOffsetAlignment reads zero, which is not an alignment";
		EXPECT_GE(Caps().minTexelBufferOffsetAlignment, 1u) << "minTexelBufferOffsetAlignment reads zero, which is not an alignment";
		EXPECT_GE(Caps().optimalBufferCopyOffsetAlignment, 1u) << "optimalBufferCopyOffsetAlignment reads zero, which is not an alignment";
		EXPECT_GE(Caps().optimalBufferCopyRowPitchAlignment, 1u) << "optimalBufferCopyRowPitchAlignment reads zero, which is not an alignment";
	}

	TEST_P(DeviceTest, CapsAndAdapterAreStableReferencesRatherThanTemporaries)
	{
		const rhi::DeviceCaps & first  = Dev().get_caps();
		const rhi::DeviceCaps & second = Dev().get_caps();
		EXPECT_EQ(&first, &second);

		const rhi::AdapterInfo & firstAdapter  = Dev().get_adapter_info();
		const rhi::AdapterInfo & secondAdapter = Dev().get_adapter_info();
		EXPECT_EQ(&firstAdapter, &secondAdapter);
	}

	TEST_P(DeviceTest, DescribesItsAdapter)
	{
		const rhi::AdapterInfo & adapter = Dev().get_adapter_info();

		ASSERT_NE(adapter.name, nullptr) << "the adapter has no name to log or show a user";
		EXPECT_GT(std::string_view{ adapter.name }.size(), 0u);
	}

	TEST_P(DeviceTest, ReportsTheDeviceIdentityAnImporterWouldMatchOn)
	{
		const rhi::AdapterInfo & adapter = Dev().get_adapter_info();

		const auto anySet = [](const auto & bytes)
		{
			return std::ranges::any_of(
				bytes,
				[](const std::uint8_t byte)
				{
					return byte != 0;
				}
			);
		};

		if (CurrentBackend().id == rhi::VulkanApi::kId)
		{
			EXPECT_TRUE(anySet(adapter.deviceUUID)) << "Vulkan reported an all-zero device UUID, which no conformant driver produces";
			EXPECT_TRUE(anySet(adapter.driverUUID)) << "Vulkan reported an all-zero driver UUID, which is what gates whether sharing is possible at all";
		}

		if (adapter.deviceLUIDValid)
		{
			EXPECT_TRUE(anySet(adapter.deviceLUID)) << "the adapter claims a valid LUID and reported all zeroes for it";
		}

		const rhi::AdapterUuidString deviceText = rhi::format_adapter_uuid(adapter.deviceUUID);
		EXPECT_EQ(std::string_view{ deviceText.data() }.size(), 36u) << "an adapter UUID did not format to the canonical 8-4-4-4-12 length";
		EXPECT_EQ(deviceText.at(8), '-');
		EXPECT_EQ(deviceText.at(13), '-');
		EXPECT_EQ(deviceText.at(18), '-');
		EXPECT_EQ(deviceText.at(23), '-');

		const rhi::AdapterLuidString luidText = rhi::format_adapter_luid(adapter.deviceLUID);
		EXPECT_EQ(std::string_view{ luidText.data() }.size(), 16u) << "an adapter LUID did not format to sixteen hexadecimal digits";
	}

	TEST_P(DeviceTest, HasTheGraphicsQueueItWasCreatedWith)
	{
		ASSERT_GE(Dev().get_queue_count(rhi::QueueType::eGraphics), 1u);

		rhi::Error error{};
		const rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));
		EXPECT_EQ(queue.get_type(), rhi::QueueType::eGraphics);
	}

	TEST_P(DeviceTest, ResolvesEveryQueueIndexItClaimsToHave)
	{
		for (const rhi::QueueType type : { rhi::QueueType::eGraphics, rhi::QueueType::eCompute, rhi::QueueType::eCopy })
		{
			const std::uint32_t count = Dev().get_queue_count(type);
			for (std::uint32_t index = 0; index < count; ++index)
			{
				rhi::Error error{};
				const rhi::Queue queue = Dev().get_queue(type, index, error);
				EXPECT_TRUE(test::Ok(queue.is_valid(), error)) << "queue index " << index << " of " << count << " did not resolve";
				EXPECT_EQ(queue.get_type(), type);
			}
		}
	}

	TEST_P(DeviceTest, RejectsAQueueIndexPastWhatItReports)
	{
		const std::uint32_t outOfRange = Dev().get_queue_count(rhi::QueueType::eGraphics);

		rhi::Error error{};
		const rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, outOfRange, error);

		EXPECT_FALSE(queue.is_valid());
		EXPECT_TRUE(test::ErrorIsPopulated(error));
	}

	TEST_P(DeviceTest, AQueueThatFailedToResolveIsInertRatherThanDangerous)
	{
		const rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics, Dev().get_queue_count(rhi::QueueType::eGraphics));
		ASSERT_FALSE(queue.is_valid());

		EXPECT_FALSE(rhi::Queue{}.is_valid());
	}

	TEST_P(DeviceTest, ReportsFormatSupportForTheFormatItWasAsked)
	{
		for (const rhi::Format format : { rhi::Format::eRGBA8UNorm, rhi::Format::eBGRA8Srgb, rhi::Format::eD32Float, rhi::Format::eR32Float })
		{
			const rhi::FormatSupport support = Dev().get_format_support(format);
			EXPECT_EQ(support.format, format) << "format support came back describing a different format";
		}
	}

	TEST_P(DeviceTest, ReportsNoSupportForTheUndefinedFormat)
	{
		const rhi::FormatSupport support = Dev().get_format_support(rhi::Format::eUndefined);

		EXPECT_FALSE(support.sampled);
		EXPECT_FALSE(support.storage);
		EXPECT_FALSE(support.colorAttachment);
		EXPECT_FALSE(support.depthStencilAttachment);
	}

	TEST_P(DeviceTest, StartsWithNoNativeValidationErrors)
	{
		const rhi::ValidationMessageCounts counts = Dev().get_validation_message_counts();
		EXPECT_EQ(counts.errors, 0u) << "the device reported native validation errors before the test did anything";
	}

	TEST_P(DeviceTest, AnswersForValidationCountsWhetherOrNotItCanCountThem)
	{
		const rhi::DeviceCaps & caps			  = Dev().get_caps();
		const rhi::ValidationMessageCounts counts = Dev().get_validation_message_counts();

		if (!caps.reportsValidationMessageCounts)
		{
			EXPECT_EQ(counts.errors, 0u) << "a device that reports no counts returned a non-zero one";
			EXPECT_EQ(counts.warnings, 0u);
		}
	}

	TEST_P(DeviceTest, CollectGarbageSucceedsWithNothingToCollect)
	{
		rhi::Error error{};
		EXPECT_TRUE(test::Ok(Dev().collect_garbage(error), error));
	}

	TEST_P(DeviceTest, CollectGarbageReachesEveryKindADeferredDestroyCanQueueUnder)
	{
		rhi::Error error{};

		const rhi::TimelineHandle timeline = Dev().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(timeline.is_valid(), error));

		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const rhi::TextureHandle texture = Dev().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		const rhi::TextureViewHandle view = Dev().create_texture_view(texture, test::samples::FullTextureView(), error);
		ASSERT_TRUE(test::Ok(view.is_valid(), error));

		const rhi::SamplerHandle sampler = Dev().create_sampler(test::samples::LinearSampler(), error);
		ASSERT_TRUE(test::Ok(sampler.is_valid(), error));

		const test::samples::UniformLayout setLayout{};
		const rhi::DescriptorSetLayoutHandle layout = Dev().create_descriptor_set_layout(setLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		const test::samples::SimplePipelineLayout pipelineLayout{ layout };
		const rhi::PipelineLayoutHandle pipeline = Dev().create_pipeline_layout(pipelineLayout.Desc(), error);
		ASSERT_TRUE(test::Ok(pipeline.is_valid(), error));

		const rhi::BinarySemaphoreHandle semaphore = Dev().create_binary_semaphore(rhi::BinarySemaphoreDesc{}, error);
		ASSERT_TRUE(test::Ok(semaphore.is_valid(), error));

		const rhi::DestroyDesc retired{
			.policy	   = rhi::DestroyPolicy::eDeferUntilSafe,
			.safeAfter = rhi::RetirePoint{ .timeline = timeline, .value = 0 },
		};

		EXPECT_TRUE(test::Ok(Dev().destroy(view, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(sampler, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(pipeline, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(layout, retired, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(semaphore, retired, error), error));

		EXPECT_TRUE(test::Ok(Dev().collect_garbage(timeline, 0, error), error));
		EXPECT_TRUE(test::Ok(Dev().collect_garbage(error), error));

		EXPECT_TRUE(test::Ok(Dev().destroy(timeline, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().collect_garbage(error), error));
	}

	TEST_P(DeviceTest, EveryFacadeWithADefaultConstructorReportsWhetherItIsUsable)
	{
		EXPECT_FALSE(rhi::Queue{}.is_valid());
		EXPECT_FALSE(rhi::CommandPool{}.is_valid());
		EXPECT_FALSE(rhi::CommandList{}.is_valid());
		EXPECT_FALSE(rhi::DescriptorArena{}.is_valid());
		EXPECT_FALSE(rhi::Swapchain{}.is_valid());
		EXPECT_FALSE(rhi::Device{}.is_valid());
		EXPECT_FALSE(rhi::UniqueDevice{}.is_valid());

		EXPECT_TRUE(Dev().is_valid());
	}

	TEST_P(DeviceTest, ADeviceGrantsNoOptionalFeatureThatWasNotDeclared)
	{
		static constexpr std::array kEveryFeature{
			rhi::DeviceFeature::eTimestampQueries,
			rhi::DeviceFeature::eSamplerAnisotropy,
			rhi::DeviceFeature::eIndependentBlend,
			rhi::DeviceFeature::eDepthBounds,
			rhi::DeviceFeature::ePipelineStatisticsQueries,
			rhi::DeviceFeature::eMultiDrawIndirect,
			rhi::DeviceFeature::eDrawIndirectFirstInstance,
			rhi::DeviceFeature::eShaderDrawParameters,
			rhi::DeviceFeature::eSparseResources,
			rhi::DeviceFeature::eSparseBuffers,
			rhi::DeviceFeature::eSparseTextures,
			rhi::DeviceFeature::eSparseVolumes,
			rhi::DeviceFeature::eTextureViewSwizzle,
			rhi::DeviceFeature::eMultiPlanarFormats,
			rhi::DeviceFeature::eSamplerYcbcrConversion,
		};

		rhi::DeviceDesc silent	 = MakeDeviceDesc();
		silent.requiredFeatures	 = {};
		silent.preferredFeatures = {};

		test::DeviceHarness undeclared(CurrentBackend(), silent);
		if (!undeclared.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(undeclared.GetError());
		}

		const rhi::DeviceCaps & granted = undeclared.Get().get_caps();
		for (const rhi::DeviceFeature feature : kEveryFeature)
		{
			EXPECT_FALSE(granted.supports(feature)) << "a device granted an optional feature nobody asked for";
		}

		EXPECT_EQ(granted.sparseTier, rhi::SparseTier::eNone) << "sparse is a ladder, so an undeclared device sits at the bottom of it, not partway up";

		const rhi::DeviceCaps & declared = Caps();
		const bool anyGranted			 = std::ranges::any_of(
			kEveryFeature,
			[&declared](const rhi::DeviceFeature feature) noexcept
			{
				return declared.supports(feature);
			}
		);

		for (const rhi::DeviceFeature feature : kEveryFeature)
		{
			EXPECT_TRUE(declared.supports(feature) || !granted.supports(feature))
				<< "a feature was granted to the device that did not ask and withheld from the one that did";
		}

		if (!anyGranted)
		{
			GTEST_SKIP() << "this adapter exposes none of the declarable features, so there is nothing to have been granted";
		}
	}

	TEST_P(DeviceTest, TheTimestampCapsThatHaveNoFeatureOfTheirOwnFollowTheOneThatDoes)
	{
		rhi::DeviceDesc silent	 = MakeDeviceDesc();
		silent.requiredFeatures	 = {};
		silent.preferredFeatures = {};

		const test::DeviceHarness undeclared(CurrentBackend(), silent);
		if (!undeclared.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(undeclared.GetError());
		}

		const rhi::DeviceCaps & withheld = undeclared.Get().get_caps();
		EXPECT_FALSE(withheld.supportsTimestampQueries) << "timestamp queries were granted to a device that declared nothing";
		EXPECT_FALSE(withheld.supportsTimestampWritesInScope) << "a device with no timestamp queries still offers to write one inside a scope";
		EXPECT_FALSE(withheld.supportsTimestampCalibration) << "a device with no timestamp queries still offers to correlate their clocks";
		EXPECT_FALSE(withheld.supportsOrderedTimestamps) << "a device with no timestamp queries still offers to order a pair of them";

		// The shared device declares the feature, so there the three reported caps are the backend's own answers and none may outrun queries.
		const rhi::DeviceCaps & granted = Caps();
		EXPECT_TRUE(granted.supportsTimestampQueries || !granted.supportsTimestampWritesInScope);
		EXPECT_TRUE(granted.supportsTimestampQueries || !granted.supportsTimestampCalibration);
		EXPECT_TRUE(granted.supportsTimestampQueries || !granted.supportsOrderedTimestamps);
	}

	TEST_P(DeviceTest, ARequiredFeatureTheDeviceCannotGiveRefusesCreation)
	{
		static constexpr std::array kEveryFeature{
			rhi::DeviceFeature::eTimestampQueries,
			rhi::DeviceFeature::eSamplerAnisotropy,
			rhi::DeviceFeature::eIndependentBlend,
			rhi::DeviceFeature::eDepthBounds,
			rhi::DeviceFeature::ePipelineStatisticsQueries,
			rhi::DeviceFeature::eMultiDrawIndirect,
			rhi::DeviceFeature::eDrawIndirectFirstInstance,
			rhi::DeviceFeature::eShaderDrawParameters,
			rhi::DeviceFeature::eSparseResources,
			rhi::DeviceFeature::eSparseBuffers,
			rhi::DeviceFeature::eSparseTextures,
			rhi::DeviceFeature::eSparseVolumes,
			rhi::DeviceFeature::eTextureViewSwizzle,
			rhi::DeviceFeature::eMultiPlanarFormats,
			rhi::DeviceFeature::eSamplerYcbcrConversion,
		};

		const rhi::DeviceCaps & available = Caps();
		// NOLINTNEXTLINE(readability-qualified-auto): libc++ makes this array iterator a raw pointer and MSVC does not, so auto * here builds on one and not the other.
		const auto missing = std::ranges::find_if(
			kEveryFeature,
			[&available](const rhi::DeviceFeature feature) noexcept
			{
				return !available.supports(feature);
			}
		);

		if (missing == kEveryFeature.end())
		{
			GTEST_SKIP() << "this adapter has every declarable feature, so none can be required and refused";
		}

		const std::array<rhi::DeviceFeature, 1> required{ *missing };
		rhi::DeviceDesc desc  = MakeDeviceDesc();
		desc.requiredFeatures = required;

		const test::DeviceHarness impossible(CurrentBackend(), desc);

		EXPECT_FALSE(impossible.IsValid()) << "a device came up without a feature the caller said it required";
		EXPECT_EQ(impossible.GetError().code, rhi::ErrorCode::eUnsupportedFeature);
		EXPECT_NE(impossible.GetError().message, nullptr) << "a refusal has to say which feature could not be met";
	}

	TEST_P(DeviceTest, TwoDevicesOnTheSameBackendAreIndependent)
	{
		test::DeviceHarness second(CurrentBackend(), MakeDeviceDesc());
		if (!second.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(second.GetError());
		}

		rhi::Error error{};
		const rhi::BufferHandle fromFirst = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(fromFirst.is_valid(), error));

		const rhi::BufferHandle fromSecond = second.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(fromSecond.is_valid(), error));

		if (test::kValidatesHandles)
		{
			rhi::Error foreignError{};
			EXPECT_FALSE(second.Get().destroy(fromFirst, {}, foreignError)) << "one device accepted a handle minted by another";
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(fromFirst, {}, error), error));
		EXPECT_TRUE(test::Ok(second.Get().destroy(fromSecond, {}, error), error));
	}

	TEST_P(DeviceTest, RefusesADeviceThatCouldNotHoldOneOpenCommandList)
	{
		rhi::DeviceDesc desc			 = MakeDeviceDesc();
		desc.maxOpenCommandListsPerQueue = 0;

		const test::DeviceHarness impossible(CurrentBackend(), desc);

		EXPECT_FALSE(impossible.IsValid()) << "a device came up that could not hold a single open command list";
		EXPECT_EQ(impossible.GetError().code, rhi::ErrorCode::eInvalidArgument);
		EXPECT_NE(impossible.GetError().message, nullptr) << "a refusal has to say what was wrong with the setting";
	}

	TEST_P(DeviceTest, ReportsWhetherItBoundsHowManyCommandListsStayOpen)
	{
		constexpr std::uint32_t kBound = 16;

		rhi::DeviceDesc desc			 = MakeDeviceDesc();
		desc.maxOpenCommandListsPerQueue = kBound;

		const test::DeviceHarness bounded(CurrentBackend(), desc);
		if (!bounded.IsValid())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(bounded.GetError());
		}

		const std::uint32_t reported = bounded.Get().get_caps().maxOpenCommandListsPerQueue;

		// A backend whose native API caps open lists reports the setting it was given. One that does not says so instead.
		EXPECT_TRUE(reported == kBound || reported == rhi::kUnlimitedOpenCommandLists)
			<< "maxOpenCommandListsPerQueue reads " << reported << ", which is neither the setting nor unlimited";

		EXPECT_GT(Caps().maxOpenCommandListsPerQueue, 0u) << "the default device reports a bound of zero, which would allow no recording at all";
	}

	class DeviceOwnershipTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(DeviceOwnershipTest);

	TEST_P(DeviceOwnershipTest, UniqueDeviceIsMoveOnly)
	{
		static_assert(!std::is_copy_constructible_v<rhi::UniqueDevice>);
		static_assert(!std::is_copy_assignable_v<rhi::UniqueDevice>);
		static_assert(std::is_move_constructible_v<rhi::UniqueDevice>);
		static_assert(std::is_move_assignable_v<rhi::UniqueDevice>);

		SUCCEED();
	}

	TEST_P(DeviceOwnershipTest, MovingAnOwnerLeavesTheSourceEmptyAndTheDeviceUsable)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(CurrentBackend().RegisterInto(registry)));

		const std::array preferred{ CurrentBackend().id };
		rhi::Result<rhi::UniqueDevice> created = rhi::create_device(registry, preferred, MakeDeviceDesc());
		ASSERT_TRUE(test::Ok(created));

		rhi::UniqueDevice owner = std::move(created).value();
		ASSERT_TRUE(owner.is_valid());

		rhi::UniqueDevice moved = std::move(owner);
		EXPECT_TRUE(moved.is_valid());
		EXPECT_FALSE(owner.is_valid()) << "the moved-from owner still claims to own a device"; // NOLINT(bugprone-use-after-move, clang-analyzer-cplusplus.Move)

		rhi::Error error{};
		const rhi::BufferHandle buffer = moved.get().create_buffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(buffer.is_valid(), error)) << "the moved device stopped working";
	}

	TEST_P(DeviceOwnershipTest, MoveAssignmentReleasesWhatTheTargetHeld)
	{
		rhi::GraphicsApiRegistry firstRegistry;
		rhi::GraphicsApiRegistry secondRegistry;
		ASSERT_TRUE(test::Ok(CurrentBackend().RegisterInto(firstRegistry)));
		ASSERT_TRUE(test::Ok(CurrentBackend().RegisterInto(secondRegistry)));

		const std::array preferred{ CurrentBackend().id };

		rhi::Result<rhi::UniqueDevice> first = rhi::create_device(firstRegistry, preferred, MakeDeviceDesc());
		ASSERT_TRUE(test::Ok(first));
		rhi::Result<rhi::UniqueDevice> second = rhi::create_device(secondRegistry, preferred, MakeDeviceDesc());
		ASSERT_TRUE(test::Ok(second));

		rhi::UniqueDevice target = std::move(first).value();
		rhi::UniqueDevice source = std::move(second).value();

		target = std::move(source);

		EXPECT_TRUE(target.is_valid());
		EXPECT_FALSE(source.is_valid()); // NOLINT(bugprone-use-after-move, clang-analyzer-cplusplus.Move)

		rhi::Error error{};
		const rhi::BufferHandle buffer = target.get().create_buffer(test::samples::StorageBuffer(), error);
		EXPECT_TRUE(test::Ok(buffer.is_valid(), error));
	}

	TEST_P(DeviceOwnershipTest, BringingADeviceUpAndDownRepeatedlyDoesNotAccumulateBackendState)
	{
		rhi::GraphicsApiRegistry registry;
		ASSERT_TRUE(test::Ok(CurrentBackend().RegisterInto(registry)));

		const std::array preferred{ CurrentBackend().id };

		test::CountingHostAllocator allocator;
		const test::ScopedHostAllocator scope(&allocator);

		rhi::Error lastError{};
		const auto round = [&]() -> bool
		{
			rhi::Result<rhi::UniqueDevice> created = rhi::create_device(registry, preferred, MakeDeviceDesc());
			if (!created)
			{
				lastError = created.get_error();
				return false;
			}

			const rhi::UniqueDevice device = std::move(created).value();
			return device.is_valid();
		};

		if (!round())
		{
			GTEST_SKIP() << "this backend does not allow a second device: " << test::Describe(lastError);
		}

		const std::size_t settled = allocator.LiveBlocks();

		constexpr int rounds = 8;
		for (int index = 0; index < rounds; ++index)
		{
			ASSERT_TRUE(round()) << "round " << index << " could not create a device: " << test::Describe(lastError);
		}

		EXPECT_EQ(allocator.LiveBlocks(), settled) << rounds << " create and destroy rounds left " << (allocator.LiveBlocks() - settled)
												   << " blocks behind, so something the backend takes per device outlives the device";
		EXPECT_EQ(allocator.mismatchedFrees, 0u);
	}

	TEST_P(DeviceOwnershipTest, ADefaultConstructedOwnerOwnsNothingAndTearsDownCleanly)
	{
		rhi::UniqueDevice empty;
		EXPECT_FALSE(empty.is_valid());

		rhi::UniqueDevice moved = std::move(empty);
		EXPECT_FALSE(moved.is_valid());
	}

} // namespace

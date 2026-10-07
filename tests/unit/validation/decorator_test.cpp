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

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "validation/decorator.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint> // NOLINT
#include <string_view>
#include <thread>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class DecoratorTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(DecoratorTest);

	[[nodiscard]] rhi::validation::DeviceValidator * ValidatorOf(const rhi::Device & device) noexcept
	{
		return rhi::validation::validator_of(rhi::detail::FacadeBuilder::impl_of(device));
	}

	template <class Fn>
	// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): check runs once per backend, so forwarding it would move from it on the first one.
	std::size_t ForEachBackendDevice(const rhi::ValidationMode mode, Fn && check)
	{
		std::size_t declinedForAnotherReason = 0;
		for (const test::Backend & backend : test::SelectedBackends())
		{
			rhi::DeviceDesc desc = test::DefaultDeviceDesc();
			desc.validation		 = mode;

			const test::DeviceHarness device{ backend, desc };
			if (!device.IsValid())
			{
				declinedForAnotherReason += test::NoAdapterHere(device.GetError()) ? 0 : 1;
				continue;
			}

			check(backend, device.Get());
		}

		return declinedForAnotherReason;
	}

	TEST_P(DecoratorTest, ADeviceWithValidationOnIsBehindTheLayer)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		ASSERT_NE(ValidatorOf(device.Get()), nullptr) << "validation was asked for and the layer was not installed";
		EXPECT_EQ(ValidatorOf(device.Get())->mode(), rhi::ValidationMode::eDeveloper);
	}

	TEST(Decorator, ADeviceWithValidationOffIsNotBehindAnything)
	{
		ForEachBackendDevice(
			rhi::ValidationMode::eOff,
			[](const test::Backend & backend, const rhi::Device device)
			{
				EXPECT_EQ(ValidatorOf(device), nullptr) << backend.displayName << " came up behind the validation layer with validation off";
			}
		);
	}

	TEST(Decorator, gate_ValidationOffIsFree)
	{
		ForEachBackendDevice(
			rhi::ValidationMode::eOff,
			[](const test::Backend & backend, const rhi::Device device)
			{
				void * impl						  = rhi::detail::FacadeBuilder::impl_of(device);
				const rhi::CoreDeviceApi * held	  = rhi::detail::FacadeBuilder::blocks_of(device)->device().core;
				const rhi::CoreDeviceApi * theirs = rhi::detail::query_block<rhi::CoreDeviceApi>(impl);

				ASSERT_NE(theirs, nullptr);
				EXPECT_EQ(held, theirs) << backend.displayName << " resolved something other than the block its backend published";
			}
		);
	}

	TEST(Decorator, EveryBackendIsCheckedByTheSameCodeWhenValidationIsOn)
	{
		const rhi::CoreDeviceApi * shared = nullptr;
		std::size_t checked				  = 0;

		const std::size_t declinedForAnotherReason = ForEachBackendDevice(
			rhi::ValidationMode::eDeveloper,
			[&](const test::Backend & backend, const rhi::Device device)
			{
				const rhi::CoreDeviceApi * held = rhi::detail::FacadeBuilder::blocks_of(device)->device().core;
				ASSERT_NE(held, nullptr);

				if (shared == nullptr)
				{
					shared = held;
				}

				EXPECT_EQ(held, shared) << backend.displayName << " is being checked by something other than the one validation layer";
				++checked;
			}
		);

		if (checked == 0 && declinedForAnotherReason == 0)
		{
			GTEST_SKIP() << "no adapter on this machine can back any selected backend";
		}

		EXPECT_GT(checked, 0u) << "no backend came up, so this proved nothing";
	}

	TEST(Decorator, gate_ValidationParity)
	{
		if constexpr (!test::kValidatesHandles)
		{
			GTEST_SKIP() << "handle liveness is not tracked under " << AZOTH_RHI_TEST_CONFIGURATION_NAME;
		}

		struct Verdict final
		{
			bool refusedUnissued		 = false;
			bool refusedDestroyed		 = false;
			rhi::ErrorCode unissuedCode	 = rhi::ErrorCode::eOk;
			rhi::ErrorCode destroyedCode = rhi::ErrorCode::eOk;
		};

		const test::Backend * first = nullptr;
		Verdict expected{};
		std::size_t compared = 0;

		ForEachBackendDevice(
			rhi::ValidationMode::eDeveloper,
			[&](const test::Backend & backend, rhi::Device device)
			{
				rhi::Error error{};
				Verdict seen{};

				const rhi::BufferHandle never{};
				seen.refusedUnissued = !device.destroy(never, {}, error);
				seen.unissuedCode	 = error.code;

				error						 = {};
				const rhi::BufferHandle live = device.create_buffer(test::samples::StorageBuffer(), error);
				ASSERT_TRUE(test::Ok(live.is_valid(), error)) << backend.displayName;
				ASSERT_TRUE(test::Ok(device.destroy(live, {}, error), error)) << backend.displayName;

				error				  = {};
				seen.refusedDestroyed = !device.destroy(live, {}, error);
				seen.destroyedCode	  = error.code;

				if (first == nullptr)
				{
					first	 = &backend;
					expected = seen;

					EXPECT_TRUE(seen.refusedUnissued) << backend.displayName << " accepted a handle it never handed out";
					EXPECT_TRUE(seen.refusedDestroyed) << backend.displayName << " accepted a handle destroyed twice";
					EXPECT_NE(seen.unissuedCode, rhi::ErrorCode::eOk) << backend.displayName;
					EXPECT_NE(seen.destroyedCode, rhi::ErrorCode::eOk) << backend.displayName;
					return;
				}

				EXPECT_EQ(seen.refusedUnissued, expected.refusedUnissued)
					<< backend.displayName << " and " << first->displayName << " disagree on an unissued handle";
				EXPECT_EQ(seen.refusedDestroyed, expected.refusedDestroyed)
					<< backend.displayName << " and " << first->displayName << " disagree on a double destroy";
				EXPECT_EQ(seen.unissuedCode, expected.unissuedCode)
					<< backend.displayName << " and " << first->displayName << " report different codes for an unissued handle";
				EXPECT_EQ(seen.destroyedCode, expected.destroyedCode)
					<< backend.displayName << " and " << first->displayName << " report different codes for a double destroy";
				++compared;
			}
		);

		if (compared == 0 && test::SelectedBackends().size() < 2)
		{
			GTEST_SKIP() << "this run selected one backend, so there is no second answer to compare against";
		}

		EXPECT_GT(compared, 0u) << "only one backend came up, so no two answers were compared";
	}

	TEST(Decorator, TheLayerDeclinesWhatTheBackendDeclined)
	{
		ForEachBackendDevice(
			rhi::ValidationMode::eDeveloper,
			[](const test::Backend & backend, const rhi::Device device)
			{
				const rhi::DeviceCaps & caps = device.get_caps();

				const rhi::DeviceBlocks & blocks = rhi::detail::FacadeBuilder::blocks_of(device)->device();
				EXPECT_EQ(caps.supportsSurfaces, blocks.present != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsPlacedResources, blocks.placedMemory != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsPipelineCache, blocks.pipelineCache != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsMemoryBudget, blocks.residency != nullptr) << backend.displayName;
			}
		);
	}

	TEST_P(DecoratorTest, RealWorkGoesThroughTheLayerUnchanged)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};

		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));
		EXPECT_EQ(queue.get_type(), rhi::QueueType::eGraphics);

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.validatedList", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));
		ASSERT_TRUE(test::Ok(list.clear_buffer(buffer, 0, test::samples::kBufferSize, 0, error), error));
		ASSERT_TRUE(test::Ok(list.end(error), error));

		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
		EXPECT_EQ(ValidatorOf(device.Get())->failures(), 0u) << "ordinary work tripped a validation rule";
	}

	TEST_P(DecoratorTest, AHandleDestroyedTwiceIsRefusedTheSecondTime)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
		EXPECT_EQ(ValidatorOf(device.Get())->failures(), 0u);

		EXPECT_FALSE(device.Get().destroy(buffer, {}, error)) << "a second destroy of the same handle was accepted";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_TRUE(test::ErrorIsPopulated(error));
		EXPECT_EQ(ValidatorOf(device.Get())->failures(), 1u);
	}

	TEST_P(DecoratorTest, AHandleThisDeviceNeverHandedOutIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle invented{ .index = 4242, .generation = 7 };

		EXPECT_FALSE(device.Get().destroy(invented, {}, error)) << "a handle this device never handed out was accepted for destroy";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(ValidatorOf(device.Get())->failures(), 1u);
	}

	TEST_P(DecoratorTest, AHandleHeldPastItsDestroyDoesNotComeBackWithTheSlot)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle first = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(first.is_valid(), error));
		ASSERT_TRUE(test::Ok(device.Get().destroy(first, {}, error), error));

		const rhi::BufferHandle second = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(second.is_valid(), error));

		if (second.index == first.index)
		{
			EXPECT_FALSE(device.Get().destroy(first, {}, error)) << "a handle held across a destroy came back with its slot";
			EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		}

		EXPECT_TRUE(test::Ok(device.Get().destroy(second, {}, error), error));
	}

	TEST_P(DecoratorTest, AHandleFromAnotherDeviceIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness first{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(first.IsValid(), first.GetError()));
		const test::DeviceHarness second{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(second.IsValid(), second.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle theirs = first.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(theirs.is_valid(), error));

		EXPECT_FALSE(second.Get().destroy(theirs, {}, error)) << "one device accepted another device's handle";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(ValidatorOf(second.Get())->failures(), 1u);

		EXPECT_TRUE(test::Ok(first.Get().destroy(theirs, {}, error), error));
	}

	TEST_P(DecoratorTest, ACommandRecordedOutsideBeginAndEndIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.unopened", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		rhi::Error beforeError{};
		EXPECT_FALSE(list.barriers(rhi::BarrierBatch{}, beforeError)) << "a barrier was recorded on a list that never opened";
		EXPECT_EQ(beforeError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.begin(error), error));
		ASSERT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{}, error), error)) << "a barrier between Begin and End was refused";
		ASSERT_TRUE(test::Ok(list.end(error), error));

		rhi::Error afterError{};
		EXPECT_FALSE(list.barriers(rhi::BarrierBatch{}, afterError)) << "a barrier was recorded on a list that had already closed";
		EXPECT_EQ(afterError.code, rhi::ErrorCode::eValidationFailed);
	}

	TEST_P(DecoratorTest, ARecordingIsRefusedOutOfOrder)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.scope", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		rhi::Error endError{};
		EXPECT_FALSE(list.end(endError)) << "End was accepted on a list that is not recording";
		EXPECT_EQ(endError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.begin(error), error));

		rhi::Error beginError{};
		EXPECT_FALSE(list.begin(beginError)) << "Begin was accepted on a list that is already recording";
		EXPECT_EQ(beginError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error drawError{};
		EXPECT_FALSE(list.draw(3, 1, 0, 0, drawError)) << "a draw outside a rendering scope was accepted";
		EXPECT_EQ(drawError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error dispatchError{};
		EXPECT_FALSE(list.dispatch(1, 1, 1, dispatchError)) << "a dispatch with no compute pipeline bound was accepted";
		EXPECT_EQ(dispatchError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(list.end(error), error));
		EXPECT_GT(ValidatorOf(device.Get())->failures(), 0u);
	}

	TEST_P(DecoratorTest, ACommandOnAClosedListSaysTheListIsClosedRatherThanNamingItsScope)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		const rhi::BufferHandle args = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(args.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.closed", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		ASSERT_TRUE(test::Ok(list.begin(error), error));
		ASSERT_TRUE(test::Ok(list.end(error), error));

		constexpr std::string_view closed = "a command recorded on a list that is not between Begin and End";

		rhi::Error drawError{};
		EXPECT_FALSE(list.draw_indirect(args, 0, 1, 0, drawError)) << "an indirect draw was recorded on a list that had already closed";
		ASSERT_NE(drawError.message, nullptr);
		EXPECT_EQ(std::string_view{ drawError.message }, closed) << "the indirect draw reported its rendering scope instead of the closed list";

		rhi::Error dispatchError{};
		EXPECT_FALSE(list.dispatch_indirect(args, 0, dispatchError)) << "an indirect dispatch was recorded on a list that had already closed";
		ASSERT_NE(dispatchError.message, nullptr);
		EXPECT_EQ(std::string_view{ dispatchError.message }, closed) << "the indirect dispatch reported its bound pipeline instead of the closed list";

		EXPECT_TRUE(test::Ok(device.Get().destroy(args, {}, error), error));
	}

	TEST_P(DecoratorTest, ATraceIsRefusedUntilARayTracingPipelineIsBound)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		if (!device.Get().get_caps().supportsRayTracing)
		{
			GTEST_SKIP() << "this backend registers no ray tracing blocks, so a trace never reaches validation";
		}

		rhi::Error error{};
		const rhi::RayTracingPipelineHandle pipeline = device.Get().create_ray_tracing_pipeline(rhi::RayTracingPipelineDesc{}, error);
		ASSERT_TRUE(test::Ok(pipeline.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.trace", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		rhi::Error unboundError{};
		EXPECT_FALSE(list.trace_rays(rhi::ShaderBindingTableDesc{}, 1, 1, 1, unboundError)) << "a trace with no ray tracing pipeline bound was accepted";
		EXPECT_EQ(unboundError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.set_ray_tracing_pipeline(pipeline, error), error));
		EXPECT_TRUE(test::Ok(list.trace_rays(rhi::ShaderBindingTableDesc{}, 1, 1, 1, error), error))
			<< "a trace was refused after its pipeline was bound, so the refusal above proves nothing";

		EXPECT_TRUE(test::Ok(list.end(error), error));

		ASSERT_TRUE(test::Ok(list.begin(error), error));

		rhi::Error rerecordedError{};
		EXPECT_FALSE(list.trace_rays(rhi::ShaderBindingTableDesc{}, 1, 1, 1, rerecordedError))
			<< "the binding survived Begin, so a re-recorded list traced with whatever the last recording left bound";
		EXPECT_EQ(rerecordedError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(list.end(error), error));
	}

	TEST_P(DecoratorTest, PrerecordedListsFollowSubmissionStateOrder)
	{
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "seeded validation registry states");
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;
		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));
		rhi::Error error{};
		const rhi::BufferHandle buffer	 = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		const rhi::TextureHandle texture = device.Get().create_texture(test::samples::MippedTexture2D(test::samples::kTextureDim, 1), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));
		constexpr rhi::ResourceState copied{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState source{ .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState stored{ .use = rhi::ResourceUse::eStorageRead, .stages = rhi::Stage::eCompute };
		constexpr rhi::ResourceState sampled{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };
		auto & handles		 = ValidatorOf(device.Get())->Handles();
		auto * bufferRecord	 = handles.lookup({ .type = rhi::ResourceType::eBuffer, .index = buffer.index, .generation = buffer.generation });
		auto * textureRecord = handles.lookup({ .type = rhi::ResourceType::eTexture, .index = texture.index, .generation = texture.generation });
		ASSERT_NE(bufferRecord, nullptr);
		ASSERT_NE(textureRecord, nullptr);
		bufferRecord->use.store(copied.use.bits());
		bufferRecord->useKnown.store(true);
		textureRecord->use.store(copied.use.bits());
		textureRecord->useKnown.store(true);
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList first	= pool.allocate("azoth.rhi.test.arrival.first", error);
		rhi::CommandList second = pool.allocate("azoth.rhi.test.arrival.second", error);
		ASSERT_TRUE(test::Ok(first.begin(error), error));
		const std::array firstBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = copied, .after = source } };
		const std::array firstTextures{
			rhi::TextureBarrier{ .texture = texture, .before = copied, .after = source, .range = test::samples::WholeColorRange() }
		};
		ASSERT_TRUE(test::Ok(first.barriers({ .buffers = firstBuffers, .textures = firstTextures }, error), error));
		ASSERT_TRUE(test::Ok(first.end(error), error));
		ASSERT_TRUE(test::Ok(second.begin(error), error));
		const std::array secondBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = source, .after = stored } };
		const std::array secondTextures{
			rhi::TextureBarrier{ .texture = texture, .before = source, .after = sampled, .range = test::samples::WholeColorRange() }
		};
		ASSERT_TRUE(test::Ok(second.barriers({ .buffers = secondBuffers, .textures = secondTextures }, error), error));
		ASSERT_TRUE(test::Ok(second.end(error), error));
		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics);
		std::array<const rhi::CommandList *, 2> reversed{ &second, &first };
		rhi::Error reversedError{};
		EXPECT_FALSE(queue.submit({ .commandLists = reversed }, reversedError));
		EXPECT_EQ(reversedError.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(bufferRecord->use.load(), copied.use.bits());
		EXPECT_EQ(textureRecord->use.load(), copied.use.bits());
		std::array<const rhi::CommandList *, 2> ordered{ &first, &second };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = ordered }, error), error));
		EXPECT_EQ(bufferRecord->use.load(), stored.use.bits());
		EXPECT_EQ(textureRecord->use.load(), sampled.use.bits());
		bufferRecord->use.store(copied.use.bits());
		textureRecord->use.store(copied.use.bits());
		std::array<const rhi::CommandList *, 1> firstOnly{ &first };
		std::array<const rhi::CommandList *, 1> secondOnly{ &second };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = firstOnly }, error), error));
		EXPECT_EQ(bufferRecord->use.load(), source.use.bits());
		EXPECT_EQ(textureRecord->use.load(), source.use.bits());
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = secondOnly }, error), error));
		EXPECT_EQ(bufferRecord->use.load(), stored.use.bits());
		EXPECT_EQ(textureRecord->use.load(), sampled.use.bits());
		EXPECT_TRUE(test::Ok(device.Get().destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, PrerecordedListsFollowSubmissionOwnershipOrder)
	{
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "seeded validation registry ownership");
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;
		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));
		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		auto * record =
			ValidatorOf(device.Get())->Handles().lookup({ .type = rhi::ResourceType::eBuffer, .index = buffer.index, .generation = buffer.generation });
		ASSERT_NE(record, nullptr);
		record->owner.store(static_cast<std::uint8_t>(rhi::QueueType::eCompute));
		record->owned.store(true);
		constexpr rhi::ResourceState copied{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList acquire = pool.allocate("azoth.rhi.test.owner.acquire", error);
		rhi::CommandList release = pool.allocate("azoth.rhi.test.owner.release", error);
		ASSERT_TRUE(test::Ok(acquire.begin(error), error));
		const std::array acquired{ rhi::BufferBarrier{ .buffer = buffer,
			.before											   = copied,
			.after											   = copied,
			.ownership										   = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } } };
		ASSERT_TRUE(test::Ok(acquire.barriers({ .buffers = acquired }, error), error));
		ASSERT_TRUE(test::Ok(acquire.end(error), error));
		ASSERT_TRUE(test::Ok(release.begin(error), error));
		const std::array released{ rhi::BufferBarrier{ .buffer = buffer,
			.before											   = copied,
			.after											   = copied,
			.ownership										   = { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } } };
		ASSERT_TRUE(test::Ok(release.barriers({ .buffers = released }, error), error));
		ASSERT_TRUE(test::Ok(release.end(error), error));
		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics);
		std::array<const rhi::CommandList *, 2> reversed{ &release, &acquire };
		rhi::Error reversedError{};
		EXPECT_FALSE(queue.submit({ .commandLists = reversed }, reversedError));
		EXPECT_EQ(reversedError.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(record->owner.load(), static_cast<std::uint8_t>(rhi::QueueType::eCompute));
		std::array<const rhi::CommandList *, 2> ordered{ &acquire, &release };
		auto * wrappedQueue				   = static_cast<rhi::validation::WrappedQueue *>(rhi::detail::unwrapped_impl_of(queue));
		const rhi::QueueApi * backendQueue = wrappedQueue->blocks.core;
		rhi::QueueApi refusedQueue		   = *backendQueue;
		refusedQueue.submit				   = [](void *, const rhi::SubmitDesc &, rhi::Error * failure) noexcept
		{
			*failure = { .code = rhi::ErrorCode::eInvalidState, .message = "backend submit refusal" };
			return false;
		};
		wrappedQueue->blocks.core = &refusedQueue;
		rhi::Error refusedError{};
		EXPECT_FALSE(queue.submit({ .commandLists = ordered }, refusedError));
		EXPECT_EQ(refusedError.code, rhi::ErrorCode::eInvalidState);
		EXPECT_TRUE(record->owned.load());
		EXPECT_EQ(record->owner.load(), static_cast<std::uint8_t>(rhi::QueueType::eCompute));
		EXPECT_FALSE(record->useKnown.load());
		wrappedQueue->blocks.core = backendQueue;
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = ordered }, error), error));
		EXPECT_TRUE(record->owned.load());
		EXPECT_EQ(record->owner.load(), static_cast<std::uint8_t>(rhi::QueueType::eCompute));
		std::array<const rhi::CommandList *, 1> acquireOnly{ &acquire };
		std::array<const rhi::CommandList *, 1> releaseOnly{ &release };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = acquireOnly }, error), error));
		EXPECT_EQ(record->owner.load(), static_cast<std::uint8_t>(rhi::QueueType::eGraphics));
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = releaseOnly }, error), error));
		EXPECT_EQ(record->owner.load(), static_cast<std::uint8_t>(rhi::QueueType::eCompute));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, PrerecordedListsKeepSubresourceArrivalStates)
	{
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "seeded validation registry states");
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;
		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));
		rhi::Error error{};
		const rhi::BufferDesc bufferDesc = test::samples::StorageBuffer();
		rhi::TextureDesc textureDesc	 = test::samples::MippedTexture2D();
		textureDesc.arrayLayers			 = 2;
		const rhi::BufferHandle buffer	 = device.Get().create_buffer(bufferDesc, error);
		const rhi::TextureHandle texture = device.Get().create_texture(textureDesc, error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));
		const auto split = bufferDesc.size / 2;
		const rhi::TextureSubresourceRange whole{ .mipCount = textureDesc.mipLevels, .layerCount = textureDesc.arrayLayers };
		const rhi::TextureSubresourceRange corner{};
		constexpr rhi::ResourceState copied{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState source{ .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy };
		constexpr rhi::ResourceState stored{ .use = rhi::ResourceUse::eStorageRead, .stages = rhi::Stage::eCompute };
		constexpr rhi::ResourceState sampled{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };
		auto & handles		 = ValidatorOf(device.Get())->Handles();
		auto * bufferRecord	 = handles.lookup({ .type = rhi::ResourceType::eBuffer, .index = buffer.index, .generation = buffer.generation });
		auto * textureRecord = handles.lookup({ .type = rhi::ResourceType::eTexture, .index = texture.index, .generation = texture.generation });
		ASSERT_NE(bufferRecord, nullptr);
		ASSERT_NE(textureRecord, nullptr);
		bufferRecord->use.store(copied.use.bits());
		bufferRecord->useKnown.store(true);
		textureRecord->use.store(copied.use.bits());
		textureRecord->useKnown.store(true);
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList first = pool.allocate("azoth.rhi.test.ranges.first", error);
		ASSERT_TRUE(test::Ok(first.begin(error), error));
		const std::array firstBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = copied, .after = source, .size = split } };
		const std::array firstTextures{ rhi::TextureBarrier{ .texture = texture, .before = copied, .after = source, .range = corner } };
		ASSERT_TRUE(test::Ok(first.barriers({ .buffers = firstBuffers, .textures = firstTextures }, error), error));
		ASSERT_TRUE(test::Ok(first.end(error), error));
		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics);
		for (const bool checkBuffer : { true, false })
		{
			for (const rhi::ResourceState wrongBefore : { copied, source })
			{
				rhi::CommandList wrong = pool.allocate("azoth.rhi.test.ranges.wrong", error);
				ASSERT_TRUE(test::Ok(wrong.begin(error), error));
				const std::array wrongBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = wrongBefore, .after = stored } };
				const std::array wrongTextures{ rhi::TextureBarrier{ .texture = texture, .before = wrongBefore, .after = sampled, .range = whole } };
				ASSERT_TRUE(
					test::Ok(
						wrong.barriers(checkBuffer ? rhi::BarrierBatch{ .buffers = wrongBuffers } : rhi::BarrierBatch{ .textures = wrongTextures }, error),
						error
					)
				);
				ASSERT_TRUE(test::Ok(wrong.end(error), error));
				std::array<const rhi::CommandList *, 2> mismatched{ &first, &wrong };
				rhi::Error wrongError{};
				EXPECT_FALSE(queue.submit({ .commandLists = mismatched }, wrongError));
				EXPECT_EQ(wrongError.code, rhi::ErrorCode::eValidationFailed);
			}
		}
		rhi::CommandList second = pool.allocate("azoth.rhi.test.ranges.second", error);
		ASSERT_TRUE(test::Ok(second.begin(error), error));
		const std::array secondBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = source, .after = stored, .size = split },
			rhi::BufferBarrier{ .buffer = buffer, .before = copied, .after = stored, .offset = split, .size = bufferDesc.size - split } };
		const std::array secondTextures{ rhi::TextureBarrier{ .texture = texture, .before = source, .after = sampled, .range = corner },
			rhi::TextureBarrier{ .texture = texture,
				.before					  = copied,
				.after					  = sampled,
				.range					  = { .baseLayer = 1, .layerCount = textureDesc.arrayLayers - 1 } },
			rhi::TextureBarrier{ .texture = texture,
				.before					  = copied,
				.after					  = sampled,
				.range					  = { .baseMip = 1, .mipCount = textureDesc.mipLevels - 1, .layerCount = textureDesc.arrayLayers } } };
		ASSERT_TRUE(test::Ok(second.barriers({ .buffers = secondBuffers, .textures = secondTextures }, error), error));
		ASSERT_TRUE(test::Ok(second.end(error), error));
		rhi::CommandList third = pool.allocate("azoth.rhi.test.ranges.third", error);
		ASSERT_TRUE(test::Ok(third.begin(error), error));
		const std::array thirdBuffers{ rhi::BufferBarrier{ .buffer = buffer, .before = stored, .after = copied } };
		const std::array thirdTextures{ rhi::TextureBarrier{ .texture = texture, .before = sampled, .after = copied, .range = whole } };
		ASSERT_TRUE(test::Ok(third.barriers({ .buffers = thirdBuffers, .textures = thirdTextures }, error), error));
		ASSERT_TRUE(test::Ok(third.end(error), error));
		std::array<const rhi::CommandList *, 3> ordered{ &first, &second, &third };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = ordered }, error), error));
		EXPECT_TRUE(bufferRecord->useKnown.load());
		EXPECT_TRUE(textureRecord->useKnown.load());
		EXPECT_EQ(bufferRecord->use.load(), copied.use.bits());
		EXPECT_EQ(textureRecord->use.load(), copied.use.bits());
		EXPECT_TRUE(test::Ok(device.Get().destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, ABarrierReleasingFromAQueueThatDoesNotOwnTheResourceIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.ownership", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		constexpr rhi::ResourceState untouched{};
		constexpr rhi::ResourceState acquired{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		const std::array released{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= untouched,
				.ownership				= { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } },
		};
		ASSERT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .buffers = released }, error), error));

		const std::array wrong{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= untouched,
				.ownership				= { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCopy } },
		};

		rhi::Error wrongError{};
		EXPECT_FALSE(list.barriers(rhi::BarrierBatch{ .buffers = wrong }, wrongError)) << "a release from a queue that does not own the resource was accepted";
		EXPECT_EQ(wrongError.code, rhi::ErrorCode::eValidationFailed);

		const std::array back{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= acquired,
				.ownership				= { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eCompute } },
		};
		EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .buffers = back }, error), error));

		static_cast<void>(list.end(error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, AQueueOwnershipReleaseMovesNothingUntilItsListIsSubmitted)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		constexpr rhi::ResourceState untouched{};
		const std::array toCompute{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= untouched,
				.ownership				= { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } },
		};

		rhi::CommandList discarded = pool.allocate("azoth.rhi.test.discarded", error);
		ASSERT_TRUE(test::Ok(discarded.is_valid(), error));
		ASSERT_TRUE(test::Ok(discarded.begin(error), error));
		ASSERT_TRUE(test::Ok(discarded.barriers(rhi::BarrierBatch{ .buffers = toCompute }, error), error));
		ASSERT_TRUE(test::Ok(discarded.end(error), error));

		rhi::CommandList submitted = pool.allocate("azoth.rhi.test.submitted", error);
		ASSERT_TRUE(test::Ok(submitted.is_valid(), error));
		ASSERT_TRUE(test::Ok(submitted.begin(error), error));
		EXPECT_TRUE(test::Ok(submitted.barriers(rhi::BarrierBatch{ .buffers = toCompute }, error), error))
			<< "a list that was never submitted moved the buffer to the compute queue, so this release read as coming from a queue that does not own it";
		ASSERT_TRUE(test::Ok(submitted.end(error), error));

		const rhi::TimelineHandle done = device.Get().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(done.is_valid(), error));

		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		std::array<const rhi::CommandList *, 1> lists{ &submitted };
		const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = lists, .signals = signals, .debugName = "azoth.rhi.test.ownership" }, error), error));
		ASSERT_TRUE(test::Ok(queue.wait(done, 1, test::kWaitTimeoutNanoseconds, error), error));

		rhi::CommandList after = pool.allocate("azoth.rhi.test.after", error);
		ASSERT_TRUE(test::Ok(after.is_valid(), error));
		ASSERT_TRUE(test::Ok(after.begin(error), error));

		rhi::Error afterError{};
		ASSERT_TRUE(test::Ok(after.barriers(rhi::BarrierBatch{ .buffers = toCompute }, error), error));
		ASSERT_TRUE(test::Ok(after.end(error), error));
		std::array<const rhi::CommandList *, 1> afterOnly{ &after };
		EXPECT_FALSE(queue.submit({ .commandLists = afterOnly }, afterError))
			<< "the submitted release never reached the device record, so the compute queue's ownership was forgotten";
		EXPECT_EQ(afterError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(device.Get().destroy(done, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, ASecondReleaseOfAResourceAlreadyHandedAwayIsRefusedAtSubmit)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		constexpr rhi::ResourceState untouched{};
		const std::array toCompute{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= untouched,
				.ownership				= { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } },
		};

		const auto record = [&](const char * name)
		{
			rhi::CommandList list = pool.allocate(name, error);
			EXPECT_TRUE(test::Ok(list.is_valid(), error));
			EXPECT_TRUE(test::Ok(list.begin(error), error));
			EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .buffers = toCompute }, error), error));
			EXPECT_TRUE(test::Ok(list.end(error), error));
			return list;
		};

		rhi::CommandList first	= record("azoth.rhi.test.release.first");
		rhi::CommandList second = record("azoth.rhi.test.release.second");

		rhi::Queue queue = device.Get().get_queue(rhi::QueueType::eGraphics);
		ASSERT_TRUE(test::Ok(queue.is_valid(), error));

		std::array<const rhi::CommandList *, 1> firstOnly{ &first };
		ASSERT_TRUE(test::Ok(queue.submit({ .commandLists = firstOnly, .debugName = "azoth.rhi.test.firstRelease" }, error), error))
			<< "the first release was refused, so the refusal below would prove nothing";

		std::array<const rhi::CommandList *, 1> secondOnly{ &second };

		rhi::Error secondError{};
		EXPECT_FALSE(queue.submit({ .commandLists = secondOnly, .debugName = "azoth.rhi.test.secondRelease" }, secondError))
			<< "the buffer was already handed to the compute queue, so releasing it again from graphics is not a transfer anyone can honour";
		EXPECT_EQ(secondError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(queue.wait_idle(error), error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

	TEST_P(DecoratorTest, ADescriptorWriteDisagreeingWithItsLayoutIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};

		const std::array bindings{
			rhi::DescriptorBinding{ .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .count = 1 },
		};
		const rhi::DescriptorSetLayoutHandle layout = device.Get().create_descriptor_set_layout(
			rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.test.oneStorageBinding" },
			error
		);
		ASSERT_TRUE(test::Ok(layout.is_valid(), error));

		rhi::DescriptorArena arena = device.Get().create_descriptor_arena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.is_valid(), error));

		const rhi::DescriptorSetHandle set = arena.allocate(rhi::DescriptorSetAllocDesc{ .layout = layout }, error);
		ASSERT_TRUE(test::Ok(set.is_valid(), error));

		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const std::array agreeing{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .buffer = buffer },
		};
		EXPECT_TRUE(test::Ok(device.Get().update_descriptors(agreeing, error), error));

		const std::array wrongType{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eUniformBuffer, .buffer = buffer },
		};
		rhi::Error typeError{};
		EXPECT_FALSE(device.Get().update_descriptors(wrongType, typeError)) << "a write claiming a type the layout did not declare was accepted";
		EXPECT_EQ(typeError.code, rhi::ErrorCode::eValidationFailed);

		const std::array undeclared{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 3, .type = rhi::DescriptorType::eStorageBuffer, .buffer = buffer },
		};
		rhi::Error bindingError{};
		EXPECT_FALSE(device.Get().update_descriptors(undeclared, bindingError)) << "a write to a binding the layout never declared was accepted";
		EXPECT_EQ(bindingError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(layout, {}, error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(device.Get(), "the descriptor case produced native validation errors of its own");
	}

	TEST_P(DecoratorTest, AStaleHandleIsRefusedOnUseAndNotOnlyOnDestroy)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};

		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		const rhi::TextureHandle texture = device.Get().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.stale", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		ASSERT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
		ASSERT_TRUE(test::Ok(device.Get().destroy(texture, {}, error), error));

		const std::uint64_t before = ValidatorOf(device.Get())->failures();

		rhi::Error mapError{};
		EXPECT_EQ(device.Get().map(buffer, rhi::MapDesc{}, mapError).data, nullptr) << "a destroyed buffer was mapped";
		EXPECT_EQ(mapError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error viewError{};
		EXPECT_FALSE(device.Get().create_texture_view(texture, test::samples::FullTextureView(), viewError).is_valid())
			<< "a view was made of a destroyed texture";
		EXPECT_EQ(viewError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.begin(error), error));
		rhi::Error clearError{};
		EXPECT_FALSE(list.clear_buffer(buffer, 0, test::samples::kBufferSize, 0, clearError)) << "a destroyed buffer was cleared";
		EXPECT_EQ(clearError.code, rhi::ErrorCode::eValidationFailed);
		static_cast<void>(list.end(error));

		EXPECT_EQ(ValidatorOf(device.Get())->failures(), before + 3u) << "one of the three refusals came from somewhere other than this layer";
	}

	TEST_P(DecoratorTest, TheObjectBehindAFacadeIsReachableThroughTheLayer)
	{
		for (const rhi::ValidationMode mode : { rhi::ValidationMode::eOff, rhi::ValidationMode::eReleaseLight, rhi::ValidationMode::eDeveloper })
		{
			rhi::DeviceDesc desc = test::DefaultDeviceDesc();
			desc.validation		 = mode;

			const test::DeviceHarness harness{ CurrentBackend(), desc };
			if (!harness.IsValid())
			{
				GTEST_SKIP() << CurrentBackend().displayName << " has no driver here";
			}

			rhi::Error error{};
			rhi::CommandPool pool = harness.Get().create_command_pool(test::samples::CommandPool(), error);
			ASSERT_TRUE(test::Ok(pool.is_valid(), error));
			rhi::CommandList list = pool.allocate("azoth.rhi.test.unwrap", error);
			ASSERT_TRUE(test::Ok(list.is_valid(), error));
			rhi::Queue queue = harness.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
			ASSERT_TRUE(test::Ok(queue.is_valid(), error));

			const std::array<void *, 4> impls{
				rhi::detail::FacadeBuilder::impl_of(harness.Get()),
				rhi::detail::FacadeBuilder::impl_of(pool),
				rhi::detail::FacadeBuilder::impl_of(list),
				rhi::detail::FacadeBuilder::impl_of(queue),
			};

			for (void * const impl : impls)
			{
				ASSERT_NE(impl, nullptr);
				void * const native = rhi::detail::native_impl_of(impl);

				EXPECT_EQ(rhi::detail::query_block<rhi::NativeObjectApi>(native), nullptr) << "unwrapping stopped on something that is still a layer";

				if (mode == rhi::ValidationMode::eOff)
				{
					EXPECT_EQ(native, impl) << "off there is no layer, so there is nothing to see through";
				}
				else
				{
					EXPECT_NE(native, impl) << "the layer was installed and the object behind it was not reachable";
				}
			}
		}
	}

	TEST_P(DecoratorTest, ACommandRecordedFromAnotherThreadIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.affinity", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		rhi::Error strayError{};
		bool accepted = true;
		std::thread stray(
			[&list, &strayError, &accepted]
			{
				accepted = list.begin_debug_label("azoth.rhi.test.stray", 0, strayError);
			}
		);
		stray.join();

		EXPECT_FALSE(accepted) << "a command recorded from a thread other than the one that began the list was accepted";
		EXPECT_EQ(strayError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(list.begin_debug_label("azoth.rhi.test.owned", 0, error), error));
		EXPECT_TRUE(test::Ok(list.end_debug_label(error), error));
		EXPECT_TRUE(test::Ok(list.end(error), error));
	}

	TEST_P(DecoratorTest, ACommandListSubmittedToAQueueOfAnotherTypeIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::Queue graphics = device.Get().get_queue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(graphics.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(rhi::QueueType::eCompute), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));

		rhi::CommandList list = pool.allocate("azoth.rhi.test.wrongQueue", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));
		ASSERT_TRUE(test::Ok(list.end(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };

		rhi::Error submitError{};
		EXPECT_FALSE(graphics.submit(rhi::SubmitDesc{ .commandLists = lists }, submitError)) << "a compute list was accepted by a graphics queue";
		EXPECT_EQ(submitError.code, rhi::ErrorCode::eValidationFailed);
	}

	TEST_P(DecoratorTest, AnOwnershipTransferBetweenOneQueueAndItselfIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		rhi::CommandPool pool = device.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.selfTransfer", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));
		ASSERT_TRUE(test::Ok(list.begin(error), error));

		constexpr rhi::ResourceState untouched{};
		constexpr rhi::ResourceState settled{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };

		const std::array toItself{
			rhi::BufferBarrier{ .buffer = buffer,
				.before					= untouched,
				.after					= untouched,
				.ownership				= { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eGraphics } },
		};

		rhi::Error selfError{};
		EXPECT_FALSE(list.barriers(rhi::BarrierBatch{ .buffers = toItself }, selfError)) << "a transfer between one queue and itself was accepted";
		EXPECT_EQ(selfError.code, rhi::ErrorCode::eValidationFailed);

		const std::array neither{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = settled },
		};
		EXPECT_TRUE(test::Ok(list.barriers(rhi::BarrierBatch{ .buffers = neither }, error), error));

		static_cast<void>(list.end(error));
		EXPECT_TRUE(test::Ok(device.Get().destroy(buffer, {}, error), error));
	}

} // namespace

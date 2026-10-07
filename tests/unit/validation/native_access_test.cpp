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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/commands/command.hpp"
#include "azoth/rhi/commands/sync.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/native/null_native.hpp"

#include "conformance/matchers.hpp"
#include "conformance/recording.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class NativeAccessTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(NativeAccessTest);

	[[nodiscard]] rhi::ResourceState UntouchedState() noexcept
	{
		return rhi::ResourceState{ .use = rhi::ResourceUse::eDiscard };
	}

	[[nodiscard]] rhi::ResourceState CopyDestinationState() noexcept
	{
		return rhi::ResourceState{ .use = rhi::ResourceUse::eCopyDst, .stages = rhi::Stage::eCopy };
	}

	[[nodiscard]] rhi::ResourceState ShaderReadState() noexcept
	{
		return rhi::ResourceState{ .use = rhi::ResourceUse::eSampledRead, .stages = rhi::Stage::eFragmentShading };
	}

	[[nodiscard]] bool SubmitAndWait(rhi::Device device, rhi::CommandList & list, rhi::Error & error)
	{
		const rhi::TimelineHandle done = device.create_timeline(test::samples::Timeline(), error);
		if (!done.is_valid())
		{
			return false;
		}

		rhi::Queue queue = device.get_queue(rhi::QueueType::eGraphics);
		if (!queue.is_valid())
		{
			return false;
		}

		std::array<const rhi::CommandList *, 1> lists{ &list };
		const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };

		const bool ran = queue.submit({ .commandLists = lists, .signals = signals, .debugName = "azoth.rhi.test.nativeMutation" }, error) &&
						 queue.wait(done, 1, test::kWaitTimeoutNanoseconds, error);

		rhi::Error ignored{};
		static_cast<void>(device.destroy(done, {}, ignored));
		return ran;
	}

	TEST_P(NativeAccessTest, gate_NativeAccessIsModeInvariant)
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

			void * const facade = rhi::detail::FacadeBuilder::impl_of(harness.Get());
			ASSERT_NE(facade, nullptr);

			void * const native = rhi::detail::native_impl_of(facade);
			ASSERT_NE(native, nullptr);

			EXPECT_EQ(rhi::detail::query_block<rhi::NativeObjectApi>(native), nullptr) << "resolving stopped on a layer and not on the backend";

			if (mode == rhi::ValidationMode::eOff)
			{
				EXPECT_EQ(native, facade) << "off there is no layer, so there is nothing to resolve through";
			}
			else
			{
				EXPECT_NE(native, facade) << "the layer was installed and the backend's own object was not reached";
			}

			const rhi::CoreDeviceApi * published = rhi::detail::query_block<rhi::CoreDeviceApi>(native);
			ASSERT_NE(published, nullptr) << "unwrapping reached something that publishes no core device block";

			EXPECT_EQ(rhi::detail::native_impl_of(facade, *published), native) << "the checked resolve refused the backend's own device";

			const rhi::CoreDeviceApi impostor{};
			EXPECT_EQ(rhi::detail::native_impl_of(facade, impostor), nullptr) << "a device that publishes another table was accepted";

			EXPECT_EQ(published->getGraphicsApiId(native), CurrentBackend().id) << "the object behind the facade belongs to a different backend";
		}
	}

	TEST_P(NativeAccessTest, EveryKindOfObjectIsReachedTheSameWay)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness harness{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(harness.IsValid(), harness.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = harness.Get().create_command_pool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.is_valid(), error));
		rhi::CommandList list = pool.allocate("azoth.rhi.test.nativeAccess", error);
		ASSERT_TRUE(test::Ok(list.is_valid(), error));

		void * const poolFacade = rhi::detail::FacadeBuilder::impl_of(pool);
		void * const listFacade = rhi::detail::FacadeBuilder::impl_of(list);

		EXPECT_EQ(rhi::detail::query_block<rhi::NativeObjectApi>(rhi::detail::native_impl_of(poolFacade)), nullptr);
		EXPECT_EQ(rhi::detail::query_block<rhi::NativeObjectApi>(rhi::detail::native_impl_of(listFacade)), nullptr);
		EXPECT_NE(rhi::detail::native_impl_of(poolFacade), poolFacade);
		EXPECT_NE(rhi::detail::native_impl_of(listFacade), listFacade);

		const rhi::CommandPoolApi * poolBlock	= rhi::detail::query_block<rhi::CommandPoolApi>(rhi::detail::native_impl_of(poolFacade));
		const rhi::RenderCommandApi * listBlock = rhi::detail::query_block<rhi::RenderCommandApi>(rhi::detail::native_impl_of(listFacade));
		ASSERT_NE(poolBlock, nullptr);
		ASSERT_NE(listBlock, nullptr);

		EXPECT_EQ(rhi::detail::native_impl_of(poolFacade, *poolBlock), rhi::detail::native_impl_of(poolFacade));
		EXPECT_EQ(rhi::detail::native_impl_of(listFacade, *listBlock), rhi::detail::native_impl_of(listFacade));

		EXPECT_EQ(rhi::detail::native_impl_of(poolFacade, *listBlock), nullptr) << "a command pool answered to a command list's table";
		EXPECT_EQ(rhi::detail::native_impl_of(listFacade, *poolBlock), nullptr) << "a command list answered to a command pool's table";
	}

	TEST_P(NativeAccessTest, ANativeMutationMovesOnlyTheSubresourcesItNamed)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::MippedTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array wholeToCopy{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = wholeToCopy }, error), error));

		constexpr rhi::TextureSubresourceRange secondMip{
			.aspects	= rhi::TextureAspect::eColor,
			.baseMip	= 1,
			.mipCount	= 1,
			.baseLayer	= 0,
			.layerCount = 1,
		};

		const std::array touched{ rhi::NativeTouchedTexture{
			.texture = texture, .access = rhi::NativeMutationAccess::eReadWrite, .range = secondMip, .finalState = ShaderReadState() } };
		ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .textures = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		constexpr rhi::TextureSubresourceRange firstMip{
			.aspects	= rhi::TextureAspect::eColor,
			.baseMip	= 0,
			.mipCount	= 1,
			.baseLayer	= 0,
			.layerCount = 1,
		};

		const std::array untouchedOnward{ rhi::TextureBarrier{
			.texture = texture, .before = CopyDestinationState(), .after = ShaderReadState(), .ownership = {}, .range = firstMip } };
		EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = untouchedOnward }, error), error))
			<< "a mip the native scope never named lost the state this recording left it in";

		const std::array staleOnTouched{ rhi::TextureBarrier{
			.texture = texture, .before = CopyDestinationState(), .after = ShaderReadState(), .ownership = {}, .range = secondMip } };

		rhi::Error staleError{};
		EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .textures = staleOnTouched }, staleError))
			<< "the mip the native scope declared kept its old state, so the declared range was never read";
		EXPECT_EQ(staleError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
	}

	TEST_P(NativeAccessTest, DiscardReplacesOnlyTheNamedRecordedSubresources)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::MippedTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array wholeToCopy{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = wholeToCopy }, error), error));

		constexpr rhi::TextureSubresourceRange firstMip{ .aspects = rhi::TextureAspect::eColor, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1 };
		constexpr rhi::TextureSubresourceRange secondMip{ .aspects = rhi::TextureAspect::eColor, .baseMip = 1, .mipCount = 1, .baseLayer = 0, .layerCount = 1 };
		const std::array discarded{ rhi::TextureBarrier{ .texture = texture, .before = UntouchedState(), .after = ShaderReadState(), .range = firstMip } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = discarded }, error), error));

		const std::array stale{ rhi::TextureBarrier{ .texture = texture, .before = CopyDestinationState(), .after = ShaderReadState(), .range = firstMip } };
		rhi::Error staleError{};
		EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .textures = stale }, staleError));
		EXPECT_EQ(staleError.code, rhi::ErrorCode::eValidationFailed);

		const std::array untouched{ rhi::TextureBarrier{
			.texture = texture, .before = CopyDestinationState(), .after = ShaderReadState(), .range = secondMip } };
		EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = untouched }, error), error));
		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
	}

	TEST_P(NativeAccessTest, DiscardAcceptsSubmittedBufferAndTextureStates)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::MippedTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));
		const std::array buffers{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		const std::array textures{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };
		{
			test::Recording first(Dev());
			ASSERT_TRUE(test::Ok(first.IsRecording(), first.GetError()));
			ASSERT_TRUE(test::Ok(first.List().barriers(rhi::BarrierBatch{ .buffers = buffers, .textures = textures }, error), error));
			ASSERT_TRUE(first.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), first.List(), error), error));
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));
		EXPECT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = buffers }, error), error));
		EXPECT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .textures = textures }, error), error));
		EXPECT_TRUE(next.End());
		EXPECT_TRUE(test::Ok(SubmitAndWait(Dev(), next.List(), error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, DiscardDoesNotBypassQueueOwnership)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "ownership bookkeeping without native queue transfers");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));
		const std::array release{ rhi::BufferBarrier{ .buffer = buffer,
			.before											  = UntouchedState(),
			.after											  = CopyDestinationState(),
			.ownership										  = { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = release }, error), error));

		rhi::Error ownershipError{};
		EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .buffers = release }, ownershipError));
		EXPECT_EQ(ownershipError.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_NE(std::string_view(ownershipError.message).find("does not own"), std::string_view::npos);
		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ABarrierNamingWhatANativeMutationDeclaredIsAccepted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer	 = Dev().create_buffer(test::samples::StorageBuffer(), error);
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array buffersToCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		const std::array texturesToCopy{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .memory = {}, .buffers = buffersToCopy, .textures = texturesToCopy }, error), error));

		const std::array touchedBuffers{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };
		const std::array touchedTextures{ rhi::NativeTouchedTexture{
			.texture = texture, .access = rhi::NativeMutationAccess::eReadWrite, .range = test::samples::WholeColorRange(), .finalState = ShaderReadState() } };

		bool recorded = false;
		ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touchedBuffers, .textures = touchedTextures },
								 [&recorded](const rhi::native::NullCommandListView &)
								 {
									 recorded = true;
								 },
								 error),
			error));
		EXPECT_TRUE(recorded) << "the callback the scope brackets never ran";

		const std::array buffersOnward{ rhi::BufferBarrier{ .buffer = buffer, .before = ShaderReadState(), .after = CopyDestinationState() } };
		const std::array texturesOnward{ rhi::TextureBarrier{
			.texture = texture, .before = ShaderReadState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };
		EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .memory = {}, .buffers = buffersOnward, .textures = texturesOnward }, error), error))
			<< "a barrier naming exactly what the native scope declared was refused, so the declaration is not read";

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ABarrierNamingTheStateFromBeforeANativeMutationIsRefused)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		const std::array touched{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };

		ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		const std::array stale{ rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() } };

		rhi::Error staleError{};
		EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .buffers = stale }, staleError))
			<< "a barrier restating the state from before the native scope was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(staleError));

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ALaterRecordingIsCheckedAgainstWhatTheNativeMutationDeclared)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle named	 = Dev().create_buffer(test::samples::StorageBuffer(), error);
		const rhi::BufferHandle misnamed = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(named.is_valid(), error));
		ASSERT_TRUE(test::Ok(misnamed.is_valid(), error));

		const std::array touched{
			rhi::NativeTouchedBuffer{ .buffer = named, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() },
			rhi::NativeTouchedBuffer{ .buffer = misnamed, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() },
		};

		{
			test::Recording moving(Dev());
			ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));
			ASSERT_TRUE(test::Ok(moving.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(moving.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), moving.List(), error), error));
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array onward{ rhi::BufferBarrier{ .buffer = named, .before = ShaderReadState(), .after = CopyDestinationState() } };
		EXPECT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a new recording refused a barrier naming what the native scope in the previous one declared";

		const std::array cleared{ rhi::BufferBarrier{ .buffer = misnamed, .before = CopyDestinationState(), .after = CopyDestinationState() } };

		rhi::Error clearedError{};
		ASSERT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = cleared }, error), error));
		ASSERT_TRUE(next.End());
		EXPECT_FALSE(SubmitAndWait(Dev(), next.List(), clearedError))
			<< "a submitted recording took a state the native scope had already moved the resource out of";
		EXPECT_TRUE(test::ErrorIsPopulated(clearedError));
		EXPECT_TRUE(test::Ok(Dev().destroy(misnamed, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(named, {}, error), error));
	}

	TEST_P(NativeAccessTest, ABarrierAfterANativeMutationWithAnUnknownFinalStateIsTrusted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		{
			test::Recording recording(Dev());
			ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

			const std::array toCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
			ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

			const std::array touched{ rhi::NativeTouchedBuffer{
				.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalStateUnknown = true } };
			ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));

			const std::array named{ rhi::BufferBarrier{ .buffer = buffer, .before = ShaderReadState(), .after = CopyDestinationState() } };
			EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = named }, error), error))
				<< "a barrier after a native scope that declared its final state unknown was refused, so the caller cannot say where it left the resource";

			const std::array stale{ rhi::BufferBarrier{ .buffer = buffer, .before = ShaderReadState(), .after = ShaderReadState() } };
			rhi::Error staleError{};
			EXPECT_FALSE(recording.List().barriers(rhi::BarrierBatch{ .buffers = stale }, staleError))
				<< "tracking did not resume after the barrier that named the unknown state";
			EXPECT_TRUE(test::ErrorIsPopulated(staleError));

			ASSERT_TRUE(recording.End());
			EXPECT_TRUE(test::Ok(SubmitAndWait(Dev(), recording.List(), error), error))
				<< "submit checked the trusted barrier against the state the resource held before the native scope";
		}

		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ALaterRecordingAfterAnUnknownFinalStateIsTrusted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		{
			test::Recording moving(Dev());
			ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));

			const std::array toCopy{ rhi::TextureBarrier{
				.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };
			ASSERT_TRUE(test::Ok(moving.List().barriers(rhi::BarrierBatch{ .textures = toCopy }, error), error));

			const std::array touched{ rhi::NativeTouchedTexture{
				.texture = texture, .access = rhi::NativeMutationAccess::eReadWrite, .finalStateUnknown = true, .range = test::samples::WholeColorRange() } };
			ASSERT_TRUE(test::Ok(moving.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .textures = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(moving.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), moving.List(), error), error));
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array onward{ rhi::TextureBarrier{
			.texture = texture, .before = ShaderReadState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .textures = onward }, error), error));
		ASSERT_TRUE(next.End());
		EXPECT_TRUE(test::Ok(SubmitAndWait(Dev(), next.List(), error), error))
			<< "a later submit checked its barrier against the state from before a native scope that declared its final state unknown";

		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
	}

	TEST_P(NativeAccessTest, AListSubmittedWithOneThatLeftAnUnknownFinalStateIsTrusted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		test::Recording moving(Dev());
		ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));
		const std::array touched{ rhi::NativeTouchedBuffer{ .buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalStateUnknown = true } };
		ASSERT_TRUE(test::Ok(moving.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));
		ASSERT_TRUE(moving.End());

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));
		const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = ShaderReadState(), .after = CopyDestinationState() } };
		ASSERT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error));
		ASSERT_TRUE(next.End());

		const rhi::TimelineHandle done = Dev().create_timeline(test::samples::Timeline(), error);
		ASSERT_TRUE(test::Ok(done.is_valid(), error));
		rhi::Queue queue = Dev().get_queue(rhi::QueueType::eGraphics);
		ASSERT_TRUE(queue.is_valid());

		std::array<const rhi::CommandList *, 2> lists{ &moving.List(), &next.List() };
		const std::array signals{ rhi::TimelinePoint{ .timeline = done, .value = 1 } };
		EXPECT_TRUE(test::Ok(queue.submit({ .commandLists = lists, .signals = signals, .debugName = "azoth.rhi.test.nativeMutation" }, error) &&
								 queue.wait(done, 1, test::kWaitTimeoutNanoseconds, error),
			error))
			<< "a submit checked the second list against the unknown state the first list's native scope left";

		EXPECT_TRUE(test::Ok(Dev().destroy(done, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, GenerateMipsAfterAnUnknownFinalStateWaitsForABarrier)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::TextureHandle texture = Dev().create_texture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = toCopy }, error), error));

		const std::array touched{ rhi::NativeTouchedTexture{
			.texture = texture, .access = rhi::NativeMutationAccess::eReadWrite, .finalStateUnknown = true, .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .textures = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		rhi::Error refused{};
		EXPECT_FALSE(recording.List().generate_mips(texture, refused)) << "generateMips trusted a state no barrier had named since the native scope";
		EXPECT_TRUE(test::ErrorIsPopulated(refused));

		const rhi::ResourceState copySource{ .use = rhi::ResourceUse::eCopySrc, .stages = rhi::Stage::eCopy };
		const std::array named{ rhi::TextureBarrier{
			.texture = texture, .before = ShaderReadState(), .after = copySource, .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .textures = named }, error), error));
		EXPECT_TRUE(test::Ok(recording.List().generate_mips(texture, error), error)) << "generateMips refused a chain a barrier had just named";

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(texture, {}, error), error));
	}

	TEST_P(NativeAccessTest, ARefusedDestroyKeepsTheArrivalStateTheRecordHeld)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		rhi::Swapchain swapchain = Dev().create_swapchain(rhi::SwapchainDesc{ .width = 64, .height = 64 }, error);
		if (!swapchain.is_valid())
		{
			GTEST_SKIP() << "no swapchain without a surface on this backend: " << test::Describe(error);
		}

		const rhi::TextureHandle backBuffer = swapchain.get_back_buffer(0);
		ASSERT_TRUE(backBuffer.is_valid()) << "the swapchain handed out no back buffer";

		const std::array touched{ rhi::NativeTouchedTexture{ .texture = backBuffer,
			.access													  = rhi::NativeMutationAccess::eReadWrite,
			.range													  = test::samples::WholeColorRange(),
			.finalState												  = ShaderReadState() } };

		{
			test::Recording moving(Dev());
			ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));
			ASSERT_TRUE(test::Ok(moving.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .textures = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(moving.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), moving.List(), error), error));
		}

		rhi::Error refused{};
		ASSERT_FALSE(Dev().destroy(backBuffer, {}, refused)) << "the backend accepted a destroy of a handle it lends out and still owns";
		EXPECT_TRUE(test::ErrorIsPopulated(refused));

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array stale{ rhi::TextureBarrier{
			.texture = backBuffer, .before = CopyDestinationState(), .after = CopyDestinationState(), .range = test::samples::WholeColorRange() } };

		rhi::Error staleError{};
		ASSERT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .textures = stale }, error), error));
		ASSERT_TRUE(next.End());
		EXPECT_FALSE(SubmitAndWait(Dev(), next.List(), staleError)) << "the refused destroy dropped the arrival state the native scope had declared";
		EXPECT_TRUE(test::ErrorIsPopulated(staleError));
	}

	TEST_P(NativeAccessTest, ANativeMutationMovesNothingUntilItsListIsSubmitted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const std::array touched{
			rhi::NativeTouchedBuffer{ .buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() },
		};

		{
			test::Recording discarded(Dev());
			ASSERT_TRUE(test::Ok(discarded.IsRecording(), discarded.GetError()));
			ASSERT_TRUE(test::Ok(discarded.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(discarded.End());
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		EXPECT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a native scope in a list that was never submitted rewrote the device's idea of where the buffer arrived";

		static_cast<void>(next.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, AReadOnlyTouchLeavesTheTrackedStateAlone)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		ASSERT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		const std::array touched{ rhi::NativeTouchedBuffer{ .buffer = buffer, .access = rhi::NativeMutationAccess::eReadOnly } };

		ASSERT_TRUE(test::Ok(recording.List().modify_native<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() } };
		EXPECT_TRUE(test::Ok(recording.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a read-only touch overwrote the state the last barrier left";

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ASubmittedBarrierCarriesTheStateItLeftForTheNextRecording)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		const std::array touched{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };

		{
			test::Recording arriving(Dev());
			ASSERT_TRUE(test::Ok(arriving.IsRecording(), arriving.GetError()));
			ASSERT_TRUE(test::Ok(arriving.List().modify_native<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(arriving.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), arriving.List(), error), error));
		}

		{
			test::Recording moving(Dev());
			ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));
			const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = ShaderReadState(), .after = CopyDestinationState() } };
			ASSERT_TRUE(test::Ok(moving.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error));
			ASSERT_TRUE(moving.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), moving.List(), error), error));
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() } };
		EXPECT_TRUE(test::Ok(next.List().barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a submitted barrier's after-state never reached the device record, so the next recording was held to the state before it";

		static_cast<void>(next.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, AQueueThatAcquiresWhatAnotherReleasedToItIsAccepted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));

		{
			test::Recording releasing(Dev(), rhi::QueueType::eGraphics);
			ASSERT_TRUE(test::Ok(releasing.IsRecording(), releasing.GetError()));

			const std::array handOver{ rhi::BufferBarrier{ .buffer = buffer,
				.before											   = UntouchedState(),
				.after											   = CopyDestinationState(),
				.ownership										   = { .op = rhi::OwnershipOp::eRelease, .counterpart = rhi::QueueType::eCompute } } };
			ASSERT_TRUE(test::Ok(releasing.List().barriers(rhi::BarrierBatch{ .buffers = handOver }, error), error));
			ASSERT_TRUE(releasing.End());
			ASSERT_TRUE(test::Ok(SubmitAndWait(Dev(), releasing.List(), error), error));
		}

		test::Recording acquiring(Dev(), rhi::QueueType::eCompute);
		ASSERT_TRUE(test::Ok(acquiring.IsRecording(), acquiring.GetError()));

		const std::array takeOver{ rhi::BufferBarrier{ .buffer = buffer,
			.before											   = CopyDestinationState(),
			.after											   = ShaderReadState(),
			.ownership										   = { .op = rhi::OwnershipOp::eAcquire, .counterpart = rhi::QueueType::eGraphics } } };
		EXPECT_TRUE(test::Ok(acquiring.List().barriers(rhi::BarrierBatch{ .buffers = takeOver }, error), error))
			<< "the queue the release handed the buffer to was refused when it acquired it";

		static_cast<void>(acquiring.End());
		EXPECT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ANativeMutationNamingARetiredHandleIsRefused)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().create_buffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.is_valid(), error));
		ASSERT_TRUE(test::Ok(Dev().destroy(buffer, {}, error), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array touched{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };

		bool recorded = false;
		rhi::Error retiredError{};
		EXPECT_FALSE(recording.List().modify_native<rhi::NullApi>(
			rhi::NativeMutationDesc{ .buffers = touched },
			[&recorded](const rhi::native::NullCommandListView &)
			{
				recorded = true;
			},
			retiredError))
			<< "a scope declaring a handle this device has taken back was opened";
		EXPECT_FALSE(recorded) << "the callback ran against a resource that no longer exists";
		EXPECT_TRUE(test::ErrorIsPopulated(retiredError));

		EXPECT_TRUE(recording.End());
	}

}

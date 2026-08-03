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
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eNone,
			.access = rhi::Access::eNone,
			.layout = rhi::TextureLayout::eUndefined,
		};
	}

	[[nodiscard]] rhi::ResourceState CopyDestinationState() noexcept
	{
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eCopy,
			.access = rhi::Access::eCopyWrite,
			.layout = rhi::TextureLayout::eCopyDst,
		};
	}

	[[nodiscard]] rhi::ResourceState ShaderReadState() noexcept
	{
		return rhi::ResourceState{
			.stages = rhi::PipelineStage::eFragmentShader,
			.access = rhi::Access::eShaderRead,
			.layout = rhi::TextureLayout::eShaderReadOnly,
		};
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

			void * const facade = rhi::detail::FacadeBuilder::ImplOf(harness.Get());
			ASSERT_NE(facade, nullptr);

			void * const native = rhi::detail::NativeImplOf(facade);
			ASSERT_NE(native, nullptr);

			EXPECT_EQ(rhi::detail::QueryBlock<rhi::NativeObjectApi>(native), nullptr) << "resolving stopped on a layer and not on the backend";

			if (mode == rhi::ValidationMode::eOff)
			{
				EXPECT_EQ(native, facade) << "off there is no layer, so there is nothing to resolve through";
			}
			else
			{
				EXPECT_NE(native, facade) << "the layer was installed and the backend's own object was not reached";
			}

			const rhi::CoreDeviceApi * published = rhi::detail::QueryBlock<rhi::CoreDeviceApi>(native);
			ASSERT_NE(published, nullptr) << "unwrapping reached something that publishes no core device block";

			EXPECT_EQ(rhi::detail::NativeImplOf(facade, *published), native) << "the checked resolve refused the backend's own device";

			const rhi::CoreDeviceApi impostor{};
			EXPECT_EQ(rhi::detail::NativeImplOf(facade, impostor), nullptr) << "a device that publishes another table was accepted";

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
		rhi::CommandPool pool = harness.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.nativeAccess", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		void * const poolFacade = rhi::detail::FacadeBuilder::ImplOf(pool);
		void * const listFacade = rhi::detail::FacadeBuilder::ImplOf(list);

		EXPECT_EQ(rhi::detail::QueryBlock<rhi::NativeObjectApi>(rhi::detail::NativeImplOf(poolFacade)), nullptr);
		EXPECT_EQ(rhi::detail::QueryBlock<rhi::NativeObjectApi>(rhi::detail::NativeImplOf(listFacade)), nullptr);
		EXPECT_NE(rhi::detail::NativeImplOf(poolFacade), poolFacade);
		EXPECT_NE(rhi::detail::NativeImplOf(listFacade), listFacade);

		const rhi::CommandPoolApi * poolBlock	= rhi::detail::QueryBlock<rhi::CommandPoolApi>(rhi::detail::NativeImplOf(poolFacade));
		const rhi::RenderCommandApi * listBlock = rhi::detail::QueryBlock<rhi::RenderCommandApi>(rhi::detail::NativeImplOf(listFacade));
		ASSERT_NE(poolBlock, nullptr);
		ASSERT_NE(listBlock, nullptr);

		EXPECT_EQ(rhi::detail::NativeImplOf(poolFacade, *poolBlock), rhi::detail::NativeImplOf(poolFacade));
		EXPECT_EQ(rhi::detail::NativeImplOf(listFacade, *listBlock), rhi::detail::NativeImplOf(listFacade));

		EXPECT_EQ(rhi::detail::NativeImplOf(poolFacade, *listBlock), nullptr) << "a command pool answered to a command list's table";
		EXPECT_EQ(rhi::detail::NativeImplOf(listFacade, *poolBlock), nullptr) << "a command list answered to a command pool's table";
	}

	// A native scope is templated on an API tag and refused when it is not the device's, so the ones below name the Null backend's and skip on every other one.
	TEST_P(NativeAccessTest, ABarrierNamingWhatANativeMutationDeclaredIsAccepted)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer	 = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		const rhi::TextureHandle texture = Dev().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array buffersToCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		const std::array texturesToCopy{ rhi::TextureBarrier{
			.texture = texture, .before = UntouchedState(), .after = CopyDestinationState(), .ownership = {}, .range = test::samples::WholeColorRange() } };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .memory = {}, .buffers = buffersToCopy, .textures = texturesToCopy }, error), error));

		const std::array touchedBuffers{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };
		const std::array touchedTextures{ rhi::NativeTouchedTexture{
			.texture = texture, .access = rhi::NativeMutationAccess::eReadWrite, .range = test::samples::WholeColorRange(), .finalState = ShaderReadState() } };

		bool recorded = false;
		ASSERT_TRUE(test::Ok(recording.List().ModifyNative<rhi::NullApi>(
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
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .memory = {}, .buffers = buffersOnward, .textures = texturesOnward }, error), error))
			<< "a barrier naming exactly what the native scope declared was refused, so the declaration is not read";

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(texture, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ABarrierNamingTheStateFromBeforeANativeMutationIsRefused)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		const std::array touched{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };

		ASSERT_TRUE(test::Ok(recording.List().ModifyNative<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		const std::array stale{ rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() } };

		rhi::Error staleError{};
		EXPECT_FALSE(recording.List().Barriers(rhi::BarrierBatch{ .buffers = stale }, staleError))
			<< "a barrier restating the state from before the native scope was accepted";
		EXPECT_TRUE(test::ErrorIsPopulated(staleError));

		static_cast<void>(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ALaterRecordingIsCheckedAgainstWhatTheNativeMutationDeclared)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle named	 = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		const rhi::BufferHandle misnamed = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(named.IsValid(), error));
		ASSERT_TRUE(test::Ok(misnamed.IsValid(), error));

		const std::array touched{
			rhi::NativeTouchedBuffer{ .buffer = named, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() },
			rhi::NativeTouchedBuffer{ .buffer = misnamed, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() },
		};

		{
			test::Recording moving(Dev());
			ASSERT_TRUE(test::Ok(moving.IsRecording(), moving.GetError()));
			ASSERT_TRUE(test::Ok(moving.List().ModifyNative<rhi::NullApi>(
									 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
				error));
			ASSERT_TRUE(moving.End());
		}

		test::Recording next(Dev());
		ASSERT_TRUE(test::Ok(next.IsRecording(), next.GetError()));

		const std::array onward{ rhi::BufferBarrier{ .buffer = named, .before = ShaderReadState(), .after = CopyDestinationState() } };
		EXPECT_TRUE(test::Ok(next.List().Barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a new recording refused a barrier naming what the native scope in the previous one declared";

		const std::array cleared{ rhi::BufferBarrier{ .buffer = misnamed, .before = UntouchedState(), .after = CopyDestinationState() } };

		rhi::Error clearedError{};
		EXPECT_FALSE(next.List().Barriers(rhi::BarrierBatch{ .buffers = cleared }, clearedError))
			<< "a new recording took a state the native scope had already moved the resource out of";
		EXPECT_TRUE(test::ErrorIsPopulated(clearedError));

		static_cast<void>(next.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(misnamed, {}, error), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(named, {}, error), error));
	}

	// A read leaves the resource where it was, so the declaration on a read-only touch has nothing to reconcile and the tracking already there has to survive it.
	TEST_P(NativeAccessTest, AReadOnlyTouchLeavesTheTrackedStateAlone)
	{
		AZO_RHI_REQUIRE_FULL_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array toCopy{ rhi::BufferBarrier{ .buffer = buffer, .before = UntouchedState(), .after = CopyDestinationState() } };
		ASSERT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = toCopy }, error), error));

		// Left cleared on purpose, which is what a caller who read a resource and moved nothing writes down.
		const std::array touched{ rhi::NativeTouchedBuffer{ .buffer = buffer, .access = rhi::NativeMutationAccess::eReadOnly } };

		ASSERT_TRUE(test::Ok(recording.List().ModifyNative<rhi::NullApi>(
								 rhi::NativeMutationDesc{ .buffers = touched }, [](const rhi::native::NullCommandListView &) {}, error),
			error));

		const std::array onward{ rhi::BufferBarrier{ .buffer = buffer, .before = CopyDestinationState(), .after = ShaderReadState() } };
		EXPECT_TRUE(test::Ok(recording.List().Barriers(rhi::BarrierBatch{ .buffers = onward }, error), error))
			<< "a read-only touch overwrote the state the last barrier left";

		EXPECT_TRUE(recording.End());
		EXPECT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));
	}

	TEST_P(NativeAccessTest, ANativeMutationNamingARetiredHandleIsRefused)
	{
		AZO_RHI_REQUIRE_HANDLE_VALIDATION();
		AZO_RHI_REQUIRE_CAP(IsNullBackend(), "recording against the Null API tag");

		rhi::Error error{};
		const rhi::BufferHandle buffer = Dev().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		ASSERT_TRUE(test::Ok(Dev().Destroy(buffer, {}, error), error));

		test::Recording recording(Dev());
		ASSERT_TRUE(test::Ok(recording.IsRecording(), recording.GetError()));

		const std::array touched{ rhi::NativeTouchedBuffer{
			.buffer = buffer, .access = rhi::NativeMutationAccess::eReadWrite, .finalState = ShaderReadState() } };

		bool recorded = false;
		rhi::Error retiredError{};
		EXPECT_FALSE(recording.List().ModifyNative<rhi::NullApi>(
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

} // namespace

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
#include <cstdint>
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
		return rhi::validation::ValidatorOf(rhi::detail::FacadeBuilder::ImplOf(device));
	}

	template <class Fn>
	// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): check runs once per backend, so forwarding it would move from it on the first one.
	void ForEachBackendDevice(const rhi::ValidationMode mode, Fn && check)
	{
		for (const test::Backend & backend : test::SelectedBackends())
		{
			rhi::DeviceDesc desc = test::DefaultDeviceDesc();
			desc.validation		 = mode;

			const test::DeviceHarness device{ backend, desc };
			if (!device.IsValid())
			{
				GTEST_SKIP() << backend.displayName << " has no driver here";
			}

			check(backend, device.Get());
		}
	}

	TEST_P(DecoratorTest, ADeviceWithValidationOnIsBehindTheLayer)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		ASSERT_NE(ValidatorOf(device.Get()), nullptr) << "validation was asked for and the layer was not installed";
		EXPECT_EQ(ValidatorOf(device.Get())->Mode(), rhi::ValidationMode::eDeveloper);
	}

	TEST(Decorator, ADeviceWithValidationOffIsNotBehindAnything)
	{
		ForEachBackendDevice(rhi::ValidationMode::eOff,
			[](const test::Backend & backend, const rhi::Device device)
			{
				EXPECT_EQ(ValidatorOf(device), nullptr) << backend.displayName << " came up behind the validation layer with validation off";
			});
	}

	TEST(Decorator, gate_ValidationOffIsFree)
	{
		ForEachBackendDevice(rhi::ValidationMode::eOff,
			[](const test::Backend & backend, const rhi::Device device)
			{
				void * impl						  = rhi::detail::FacadeBuilder::ImplOf(device);
				const rhi::CoreDeviceApi * held	  = rhi::detail::FacadeBuilder::BlocksOf(device)->Device().core;
				const rhi::CoreDeviceApi * theirs = rhi::detail::QueryBlock<rhi::CoreDeviceApi>(impl);

				ASSERT_NE(theirs, nullptr);
				EXPECT_EQ(held, theirs) << backend.displayName << " resolved something other than the block its backend published";
			});
	}

	TEST(Decorator, EveryBackendIsCheckedByTheSameCodeWhenValidationIsOn)
	{
		const rhi::CoreDeviceApi * shared = nullptr;
		std::size_t checked				  = 0;

		ForEachBackendDevice(rhi::ValidationMode::eDeveloper,
			[&](const test::Backend & backend, const rhi::Device device)
			{
				const rhi::CoreDeviceApi * held = rhi::detail::FacadeBuilder::BlocksOf(device)->Device().core;
				ASSERT_NE(held, nullptr);

				if (shared == nullptr)
				{
					shared = held;
				}

				EXPECT_EQ(held, shared) << backend.displayName << " is being checked by something other than the one validation layer";
				++checked;
			});

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

		ForEachBackendDevice(rhi::ValidationMode::eDeveloper,
			[&](const test::Backend & backend, rhi::Device device)
			{
				rhi::Error error{};
				Verdict seen{};

				const rhi::BufferHandle never{};
				seen.refusedUnissued = !device.Destroy(never, {}, error);
				seen.unissuedCode	 = error.code;

				error						 = {};
				const rhi::BufferHandle live = device.CreateBuffer(test::samples::StorageBuffer(), error);
				ASSERT_TRUE(test::Ok(live.IsValid(), error)) << backend.displayName;
				ASSERT_TRUE(test::Ok(device.Destroy(live, {}, error), error)) << backend.displayName;

				error				  = {};
				seen.refusedDestroyed = !device.Destroy(live, {}, error);
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
			});

		EXPECT_GT(compared, 0u) << "only one backend came up, so no two answers were compared";
	}

	TEST(Decorator, TheLayerDeclinesWhatTheBackendDeclined)
	{
		ForEachBackendDevice(rhi::ValidationMode::eDeveloper,
			[](const test::Backend & backend, const rhi::Device device)
			{
				const rhi::DeviceCaps & caps = device.GetCaps();

				const rhi::DeviceBlocks & blocks = rhi::detail::FacadeBuilder::BlocksOf(device)->Device();
				EXPECT_EQ(caps.supportsSurfaces, blocks.present != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsPlacedResources, blocks.placedMemory != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsPipelineCache, blocks.pipelineCache != nullptr) << backend.displayName;
				EXPECT_EQ(caps.supportsMemoryBudget, blocks.residency != nullptr) << backend.displayName;
			});
	}

	TEST_P(DecoratorTest, RealWorkGoesThroughTheLayerUnchanged)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};

		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		rhi::Queue queue = device.Get().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(queue.IsValid(), error));
		EXPECT_EQ(queue.GetType(), rhi::QueueType::eGraphics);

		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.validatedList", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.ClearBuffer(buffer, 0, test::samples::kBufferSize, 0, error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		EXPECT_EQ(ValidatorOf(device.Get())->Failures(), 0u) << "ordinary work tripped a validation rule";
	}

	TEST_P(DecoratorTest, AHandleDestroyedTwiceIsRefusedTheSecondTime)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		EXPECT_EQ(ValidatorOf(device.Get())->Failures(), 0u);

		EXPECT_FALSE(device.Get().Destroy(buffer, {}, error)) << "a second destroy of the same handle was accepted";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_TRUE(test::ErrorIsPopulated(error));
		EXPECT_EQ(ValidatorOf(device.Get())->Failures(), 1u);
	}

	TEST_P(DecoratorTest, AHandleThisDeviceNeverHandedOutIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle invented{ .index = 4242, .generation = 7 };

		EXPECT_FALSE(device.Get().Destroy(invented, {}, error)) << "a handle this device never handed out was accepted for destroy";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(ValidatorOf(device.Get())->Failures(), 1u);
	}

	TEST_P(DecoratorTest, AHandleHeldPastItsDestroyDoesNotComeBackWithTheSlot)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle first = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(first.IsValid(), error));
		ASSERT_TRUE(test::Ok(device.Get().Destroy(first, {}, error), error));

		const rhi::BufferHandle second = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(second.IsValid(), error));

		if (second.index == first.index)
		{
			EXPECT_FALSE(device.Get().Destroy(first, {}, error)) << "a handle held across a destroy came back with its slot";
			EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		}

		EXPECT_TRUE(test::Ok(device.Get().Destroy(second, {}, error), error));
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
		const rhi::BufferHandle theirs = first.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(theirs.IsValid(), error));

		EXPECT_FALSE(second.Get().Destroy(theirs, {}, error)) << "one device accepted another device's handle";
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		EXPECT_EQ(ValidatorOf(second.Get())->Failures(), 1u);

		EXPECT_TRUE(test::Ok(first.Get().Destroy(theirs, {}, error), error));
	}

	TEST_P(DecoratorTest, ACommandRecordedOutsideBeginAndEndIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.unopened", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		/*
		 * A barrier, because it is the entry that reaches the driver with the least standing in its way: it carries no handles when its batch is empty, and the
		 * state tracking has nothing recorded yet to disagree with. Before Begin the thread rule stands aside as well, no thread having claimed the list, so
		 * this is the only rule left to speak.
		 */
		rhi::Error beforeError{};
		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{}, beforeError)) << "a barrier was recorded on a list that never opened";
		EXPECT_EQ(beforeError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{}, error), error)) << "a barrier between Begin and End was refused";
		ASSERT_TRUE(test::Ok(list.End(error), error));

		rhi::Error afterError{};
		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{}, afterError)) << "a barrier was recorded on a list that had already closed";
		EXPECT_EQ(afterError.code, rhi::ErrorCode::eValidationFailed);
	}

	TEST_P(DecoratorTest, ARecordingIsRefusedOutOfOrder)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.scope", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		rhi::Error endError{};
		EXPECT_FALSE(list.End(endError)) << "End was accepted on a list that is not recording";
		EXPECT_EQ(endError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		rhi::Error beginError{};
		EXPECT_FALSE(list.Begin(beginError)) << "Begin was accepted on a list that is already recording";
		EXPECT_EQ(beginError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error drawError{};
		EXPECT_FALSE(list.Draw(3, 1, 0, 0, drawError)) << "a draw outside a rendering scope was accepted";
		EXPECT_EQ(drawError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error dispatchError{};
		EXPECT_FALSE(list.Dispatch(1, 1, 1, dispatchError)) << "a dispatch with no compute pipeline bound was accepted";
		EXPECT_EQ(dispatchError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(list.End(error), error));
		EXPECT_GT(ValidatorOf(device.Get())->Failures(), 0u);
	}

	TEST_P(DecoratorTest, ABarrierReleasingFromAQueueThatDoesNotOwnTheResourceIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.ownership", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		constexpr rhi::ResourceState untouched{};

		const std::array released{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = untouched, .ownership = { .src = 0, .dst = 1 } },
		};
		ASSERT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .buffers = released }, error), error));

		const std::array wrong{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = untouched, .ownership = { .src = 2, .dst = 0 } },
		};

		rhi::Error wrongError{};
		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .buffers = wrong }, wrongError)) << "a release from a family that does not own the resource was accepted";
		EXPECT_EQ(wrongError.code, rhi::ErrorCode::eValidationFailed);

		const std::array back{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = untouched, .ownership = { .src = 1, .dst = 0 } },
		};
		EXPECT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .buffers = back }, error), error));

		static_cast<void>(list.End(error));
		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
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
		const rhi::DescriptorSetLayoutHandle layout = device.Get().CreateDescriptorSetLayout(
			rhi::DescriptorSetLayoutDesc{ .bindings = bindings, .debugName = "azoth.rhi.test.oneStorageBinding" }, error);
		ASSERT_TRUE(test::Ok(layout.IsValid(), error));

		rhi::DescriptorArena arena = device.Get().CreateDescriptorArena(test::samples::DescriptorArena(), error);
		ASSERT_TRUE(test::Ok(arena.IsValid(), error));

		const rhi::DescriptorSetHandle set = arena.Allocate(rhi::DescriptorSetAllocDesc{ .layout = layout }, error);
		ASSERT_TRUE(test::Ok(set.IsValid(), error));

		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		const std::array agreeing{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eStorageBuffer, .buffer = buffer },
		};
		EXPECT_TRUE(test::Ok(device.Get().UpdateDescriptors(agreeing, error), error));

		const std::array wrongType{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 0, .type = rhi::DescriptorType::eUniformBuffer, .buffer = buffer },
		};
		rhi::Error typeError{};
		EXPECT_FALSE(device.Get().UpdateDescriptors(wrongType, typeError)) << "a write claiming a type the layout did not declare was accepted";
		EXPECT_EQ(typeError.code, rhi::ErrorCode::eValidationFailed);

		const std::array undeclared{
			rhi::DescriptorWriteBuffer{ .set = set, .binding = 3, .type = rhi::DescriptorType::eStorageBuffer, .buffer = buffer },
		};
		rhi::Error bindingError{};
		EXPECT_FALSE(device.Get().UpdateDescriptors(undeclared, bindingError)) << "a write to a binding the layout never declared was accepted";
		EXPECT_EQ(bindingError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(device.Get().Destroy(layout, {}, error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(device.Get(), "the descriptor case produced native validation errors of its own");
	}

	TEST_P(DecoratorTest, AStaleHandleIsRefusedOnUseAndNotOnlyOnDestroy)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};

		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::UploadBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));
		const rhi::TextureHandle texture = device.Get().CreateTexture(test::samples::SampledTexture2D(), error);
		ASSERT_TRUE(test::Ok(texture.IsValid(), error));

		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.stale", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));

		ASSERT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
		ASSERT_TRUE(test::Ok(device.Get().Destroy(texture, {}, error), error));

		const std::uint64_t before = ValidatorOf(device.Get())->Failures();

		rhi::Error mapError{};
		EXPECT_EQ(device.Get().Map(buffer, rhi::MapDesc{}, mapError).data, nullptr) << "a destroyed buffer was mapped";
		EXPECT_EQ(mapError.code, rhi::ErrorCode::eValidationFailed);

		rhi::Error viewError{};
		EXPECT_FALSE(device.Get().CreateTextureView(texture, test::samples::FullTextureView(), viewError).IsValid())
			<< "a view was made of a destroyed texture";
		EXPECT_EQ(viewError.code, rhi::ErrorCode::eValidationFailed);

		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		rhi::Error clearError{};
		EXPECT_FALSE(list.ClearBuffer(buffer, 0, test::samples::kBufferSize, 0, clearError)) << "a destroyed buffer was cleared";
		EXPECT_EQ(clearError.code, rhi::ErrorCode::eValidationFailed);
		static_cast<void>(list.End(error));

		EXPECT_EQ(ValidatorOf(device.Get())->Failures(), before + 3u) << "one of the three refusals came from somewhere other than this layer";
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
			rhi::CommandPool pool = harness.Get().CreateCommandPool(test::samples::CommandPool(), error);
			ASSERT_TRUE(test::Ok(pool.IsValid(), error));
			rhi::CommandList list = pool.Allocate("azoth.rhi.test.unwrap", error);
			ASSERT_TRUE(test::Ok(list.IsValid(), error));
			rhi::Queue queue = harness.Get().GetQueue(rhi::QueueType::eGraphics, 0, error);
			ASSERT_TRUE(test::Ok(queue.IsValid(), error));

			const std::array<void *, 4> impls{
				rhi::detail::FacadeBuilder::ImplOf(harness.Get()),
				rhi::detail::FacadeBuilder::ImplOf(pool),
				rhi::detail::FacadeBuilder::ImplOf(list),
				rhi::detail::FacadeBuilder::ImplOf(queue),
			};

			for (void * const impl : impls)
			{
				ASSERT_NE(impl, nullptr);
				void * const native = rhi::detail::NativeImplOf(impl);

				EXPECT_EQ(rhi::detail::QueryBlock<rhi::NativeObjectApi>(native), nullptr) << "unwrapping stopped on something that is still a layer";

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
		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.affinity", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		rhi::Error strayError{};
		bool accepted = true;
		std::thread stray(
			[&list, &strayError, &accepted]
			{
				accepted = list.BeginDebugLabel("azoth.rhi.test.stray", 0, strayError);
			});
		stray.join();

		EXPECT_FALSE(accepted) << "a command recorded from a thread other than the one that began the list was accepted";
		EXPECT_EQ(strayError.code, rhi::ErrorCode::eValidationFailed);

		EXPECT_TRUE(test::Ok(list.BeginDebugLabel("azoth.rhi.test.owned", 0, error), error));
		EXPECT_TRUE(test::Ok(list.EndDebugLabel(error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));
	}

	TEST_P(DecoratorTest, ACommandListSubmittedToAQueueOfAnotherTypeIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		rhi::Queue graphics = device.Get().GetQueue(rhi::QueueType::eGraphics, 0, error);
		ASSERT_TRUE(test::Ok(graphics.IsValid(), error));

		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(rhi::QueueType::eCompute), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.wrongQueue", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		ASSERT_TRUE(test::Ok(list.End(error), error));

		std::array<const rhi::CommandList *, 1> lists{ &list };

		rhi::Error submitError{};
		EXPECT_FALSE(graphics.Submit(rhi::SubmitDesc{ .commandLists = lists }, submitError)) << "a compute list was accepted by a graphics queue";
		EXPECT_EQ(submitError.code, rhi::ErrorCode::eValidationFailed);
	}

	TEST_P(DecoratorTest, AQueueFamilyTransferThatNamesOnlyOneEndIsRefused)
	{
		rhi::DeviceDesc desc = test::DefaultDeviceDesc();
		desc.validation		 = rhi::ValidationMode::eDeveloper;

		const test::DeviceHarness device{ CurrentBackend(), desc };
		ASSERT_TRUE(test::Ok(device.IsValid(), device.GetError()));

		rhi::Error error{};
		const rhi::BufferHandle buffer = device.Get().CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		rhi::CommandPool pool = device.Get().CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		rhi::CommandList list = pool.Allocate("azoth.rhi.test.halfTransfer", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));

		constexpr rhi::ResourceState untouched{};

		const std::array halfFilled{
			rhi::BufferBarrier{
				.buffer	   = buffer,
				.before	   = untouched,
				.after	   = untouched,
				.ownership = { .src = 0, .dst = rhi::kIgnoreQueueFamily },
			},
		};

		rhi::Error halfError{};
		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .buffers = halfFilled }, halfError)) << "a transfer naming one end and ignoring the other was accepted";
		EXPECT_EQ(halfError.code, rhi::ErrorCode::eValidationFailed);

		const std::array toItself{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = untouched, .ownership = { .src = 1, .dst = 1 } },
		};

		rhi::Error selfError{};
		EXPECT_FALSE(list.Barriers(rhi::BarrierBatch{ .buffers = toItself }, selfError)) << "a transfer from a family to itself was accepted";
		EXPECT_EQ(selfError.code, rhi::ErrorCode::eValidationFailed);

		const std::array neither{
			rhi::BufferBarrier{ .buffer = buffer, .before = untouched, .after = untouched },
		};
		EXPECT_TRUE(test::Ok(list.Barriers(rhi::BarrierBatch{ .buffers = neither }, error), error));

		static_cast<void>(list.End(error));
		EXPECT_TRUE(test::Ok(device.Get().Destroy(buffer, {}, error), error));
	}

} // namespace

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

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class DeviceCoexistenceTest : public test::BackendTest
	{
	protected:
		[[nodiscard]] static rhi::DeviceDesc DescWithMode(const rhi::ValidationMode mode) noexcept
		{
			rhi::DeviceDesc desc = test::DefaultDeviceDesc();
			desc.validation		 = mode;
			return desc;
		}

		[[nodiscard]] std::unique_ptr<test::DeviceHarness> MakeDevice(const rhi::ValidationMode mode) const
		{
			auto harness = std::make_unique<test::DeviceHarness>(CurrentBackend(), DescWithMode(mode));
			if (!harness->IsValid())
			{
				const rhi::Error error = harness->GetError();
				ADD_FAILURE() << CurrentBackend().displayName
							  << " made a device for the fixture but not a second one: " << (error.message != nullptr ? error.message : "no diagnostic");
				return nullptr;
			}

			return harness;
		}
	};

	AZO_RHI_BACKEND_SUITE(DeviceCoexistenceTest);

	TEST_P(DeviceCoexistenceTest, TearsDownTwoDevicesThatWereCreatedWithDifferentValidationModes)
	{
		// Order matters: created first and destroyed last, so a backend that reconfigures process-wide state on the second device is caught at teardown.
		std::unique_ptr<test::DeviceHarness> validating = MakeDevice(rhi::ValidationMode::eDeveloper);
		ASSERT_NE(validating, nullptr);

		std::unique_ptr<test::DeviceHarness> quiet = MakeDevice(rhi::ValidationMode::eOff);
		ASSERT_NE(quiet, nullptr);

		quiet.reset();
		validating.reset();
	}

	TEST_P(DeviceCoexistenceTest, KeepsTheFirstDeviceUsableAfterASecondWithADifferentValidationModeIsCreated)
	{
		std::unique_ptr<test::DeviceHarness> validating = MakeDevice(rhi::ValidationMode::eDeveloper);
		ASSERT_NE(validating, nullptr);

		std::unique_ptr<test::DeviceHarness> quiet = MakeDevice(rhi::ValidationMode::eOff);
		ASSERT_NE(quiet, nullptr);

		rhi::Error error{};
		rhi::Device first = validating->Get();

		const rhi::BufferHandle buffer = first.CreateBuffer(test::samples::StorageBuffer(), error);
		ASSERT_TRUE(test::Ok(buffer.IsValid(), error));

		rhi::CommandPool pool = first.CreateCommandPool(test::samples::CommandPool(), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));

		rhi::CommandList list = pool.Allocate("azoth.rhi.test.coexistence", error);
		ASSERT_TRUE(test::Ok(list.IsValid(), error));
		ASSERT_TRUE(test::Ok(list.Begin(error), error));
		EXPECT_TRUE(test::Ok(list.ClearBuffer(buffer, 0, test::samples::kBufferSize, 0, error), error));
		EXPECT_TRUE(test::Ok(list.End(error), error));

		EXPECT_TRUE(test::Ok(first.Destroy(buffer, {}, error), error));
		EXPECT_TRUE(test::Ok(first.CollectGarbage(error), error));
		AZO_RHI_EXPECT_NO_VALIDATION_ERRORS(first, "a second device with a different validation mode was created alongside this one");
	}

	TEST_P(DeviceCoexistenceTest, TearsDownTwoDevicesThatWereCreatedWithTheSameValidationMode)
	{
		std::unique_ptr<test::DeviceHarness> first = MakeDevice(rhi::ValidationMode::eDeveloper);
		ASSERT_NE(first, nullptr);

		std::unique_ptr<test::DeviceHarness> second = MakeDevice(rhi::ValidationMode::eDeveloper);
		ASSERT_NE(second, nullptr);

		second.reset();
		first.reset();
	}

	TEST_P(DeviceCoexistenceTest, gate_ConcurrentLifetimeIsGuardedByTheRhi)
	{
		constexpr int kThreads	 = 4;
		constexpr int kPerThread = 8;

		std::atomic<int> created{ 0 };
		std::atomic<int> refused{ 0 };
		std::vector<std::thread> workers;
		workers.reserve(kThreads);

		for (int worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&]
				{
					for (int i = 0; i < kPerThread; ++i)
					{
						const test::DeviceHarness harness{ CurrentBackend(), test::DefaultDeviceDesc() };
						if (harness.IsValid())
						{
							created.fetch_add(1, std::memory_order_relaxed);
						}
						else
						{
							refused.fetch_add(1, std::memory_order_relaxed);
						}
					}
				});
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		EXPECT_EQ(created.load() + refused.load(), kThreads * kPerThread) << CurrentBackend().displayName << " lost a device create between threads";
		EXPECT_GT(created.load(), 0) << CurrentBackend().displayName << " refused every concurrent create, so this proved nothing";
	}

} // namespace

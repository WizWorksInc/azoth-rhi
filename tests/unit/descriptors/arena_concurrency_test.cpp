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

#include "azoth/rhi/core/resource_handles.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/descriptors.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class ArenaConcurrencyTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(ArenaConcurrencyTest);

	TEST_P(ArenaConcurrencyTest, gate_ConcurrentArenaAllocationIsGuardedByTheRhi)
	{
		rhi::Error error{};
		const test::samples::UniformLayout layout;
		const rhi::DescriptorSetLayoutHandle setLayout = Dev().CreateDescriptorSetLayout(layout.Desc(), error);
		ASSERT_TRUE(test::Ok(setLayout.IsValid(), error));

		constexpr int kPerThread = 64;

		rhi::Error firstError{};
		rhi::Error secondError{};
		rhi::DescriptorArena first	= Dev().CreateDescriptorArena(test::samples::DescriptorArena(256, 1024), firstError);
		rhi::DescriptorArena second = Dev().CreateDescriptorArena(test::samples::DescriptorArena(256, 1024), secondError);
		ASSERT_TRUE(test::Ok(first.IsValid(), firstError));
		ASSERT_TRUE(test::Ok(second.IsValid(), secondError));

		const auto hammer = [setLayout](rhi::DescriptorArena & arena, std::vector<rhi::DescriptorSetHandle> & out)
		{
			for (int index = 0; index < kPerThread; ++index)
			{
				rhi::Error allocError{};
				out.push_back(arena.Allocate(
					rhi::DescriptorSetAllocDesc{
						.layout					 = setLayout,
						.variableDescriptorCount = 0,
						.debugName				 = "azoth.rhi.test.concurrentSet",
					},
					allocError));
			}
		};

		std::vector<rhi::DescriptorSetHandle> fromFirst;
		std::vector<rhi::DescriptorSetHandle> fromSecond;
		fromFirst.reserve(kPerThread);
		fromSecond.reserve(kPerThread);

		std::thread firstThread(
			[&]
			{
				hammer(first, fromFirst);
			});
		std::thread secondThread(
			[&]
			{
				hammer(second, fromSecond);
			});
		firstThread.join();
		secondThread.join();

		std::unordered_set<std::uint64_t> seen;
		for (const std::vector<rhi::DescriptorSetHandle> * batch : { &fromFirst, &fromSecond })
		{
			for (const rhi::DescriptorSetHandle handle : *batch)
			{
				ASSERT_TRUE(handle.IsValid()) << "an allocation inside the arena's declared capacity was refused";
				const auto key = (static_cast<std::uint64_t>(handle.index) << 32U) | handle.generation;
				EXPECT_TRUE(seen.insert(key).second) << "two sets came back naming index " << handle.index << " generation " << handle.generation;
			}
		}

		EXPECT_EQ(seen.size(), static_cast<std::size_t>(2 * kPerThread));
	}

} // namespace

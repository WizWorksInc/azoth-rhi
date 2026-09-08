// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/resources/query.hpp"

#include "conformance/matchers.hpp"
#include "conformance/samples.hpp"
#include "harness/backends.hpp"
#include "harness/environment.hpp"

#include <gtest/gtest.h>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	class QueryPoolCountTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(QueryPoolCountTest);

	TEST_P(QueryPoolCountTest, RefusesAPoolThatAskedForNoQueries)
	{
		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(0), error);
		EXPECT_FALSE(pool.IsValid()) << "a query pool that asked for no queries was created";
		EXPECT_TRUE(test::ErrorIsPopulated(error));

		if (pool.IsValid())
		{
			EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
		}
	}

	TEST_P(QueryPoolCountTest, CreatesAPoolThatAskedForOne)
	{
		AZO_RHI_REQUIRE_CAP(Caps().supportsTimestampQueries || IsNullBackend(), "timestamp queries");

		rhi::Error error{};
		const rhi::QueryPoolHandle pool = Dev().CreateQueryPool(test::samples::TimestampPool(1), error);
		ASSERT_TRUE(test::Ok(pool.IsValid(), error));
		EXPECT_TRUE(test::Ok(Dev().Destroy(pool, {}, error), error));
	}

}

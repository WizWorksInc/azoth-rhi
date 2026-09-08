// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "conformance/block_contract.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

namespace test = azo::rhi::test;

namespace
{

	class InterfaceBlockTest : public test::BackendTest
	{
	};

	AZO_RHI_BACKEND_SUITE(InterfaceBlockTest);

	TEST_P(InterfaceBlockTest, gate_BlockStability)
	{
		test::oracle::CheckBlockStability(Dev());
	}

	TEST_P(InterfaceBlockTest, gate_ConcurrentBlockQuery)
	{
		test::oracle::CheckConcurrentBlockQuery(Dev());
	}

	TEST_P(InterfaceBlockTest, gate_RequiredBlocksAreNamedWhenMissing)
	{
		test::oracle::CheckRequiredBlocks(Dev());
	}

	TEST_P(InterfaceBlockTest, gate_DerivationIsWired)
	{
		test::oracle::CheckCapsFollowBlocks(Dev());
	}

}

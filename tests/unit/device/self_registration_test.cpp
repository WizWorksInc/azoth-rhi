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

#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	constexpr std::string_view kObjectLibraryBackend = "studio.rhi.objectlib";

	constexpr std::string_view kStaticLibraryBackend = "studio.rhi.staticlib";

	[[nodiscard]] bool OrderContains(const rhi::BackendSelection & backends, const std::string_view canonicalName)
	{
		const std::span<const rhi::BackendInfo> order = backends.preferred();
		return std::ranges::any_of(order,
			[canonicalName](const rhi::BackendInfo & info)
			{
				return info.canonicalName == canonicalName;
			});
	}

	[[nodiscard]] bool SelfRegisteredContains(const std::string_view canonicalName)
	{
		for (const rhi::StaticBackendRegistration * node = rhi::self_registered_backends(); node != nullptr; node = node->next())
		{
			if (node->entry().canonicalName == canonicalName)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] rhi::BackendRank RankOf(const rhi::GraphicsApiId id)
	{
		if (const rhi::BackendEntry * bundled = rhi::find_available_backend(id); bundled != nullptr)
		{
			return bundled->rank;
		}

		for (const rhi::StaticBackendRegistration * node = rhi::self_registered_backends(); node != nullptr; node = node->next())
		{
			if (node->entry().id == id)
			{
				return node->entry().rank;
			}
		}

		ADD_FAILURE() << "an entry in the order came from neither the bundled table nor the self-registration list";
		return rhi::BackendRank::eHardware;
	}

	TEST(SelfRegistration, gate_ObjectLibraryBackendRegistersWithoutBeingNamed)
	{
		ASSERT_TRUE(SelfRegisteredContains(kObjectLibraryBackend))
			<< "the registrar in the object library never ran, so its translation unit did not reach the link";

		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.add_catalog()));

		EXPECT_TRUE(OrderContains(backends, kObjectLibraryBackend)) << "the backend registered itself and AddCatalog did not pick it up";

		EXPECT_TRUE(OrderContains(backends, rhi::NullApi::kCanonicalName)) << "AddCatalog dropped the bundled backends";
	}

	TEST(SelfRegistration, gate_StaticLibraryIsDeadStripped)
	{
		EXPECT_FALSE(SelfRegisteredContains(kStaticLibraryBackend))
			<< "the static library's registrar ran, which is not what an archive normally does. The route still needs an OBJECT library or "
			   "whole-archive to be relied on, so this is worth understanding and not celebrating.";

		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.add_catalog()));

		EXPECT_FALSE(OrderContains(backends, kStaticLibraryBackend)) << "a backend in a static library reached the order";
	}

	TEST(SelfRegistration, gate_RegistrationOrderReproducible)
	{
		const rhi::BackendPreference sortOnly{
			.requested			= "studio.rhi.nothing-answers-to-this",
			.consultEnvironment = false,
			.includeAvailable	= false,
		};

		rhi::BackendSelection first{ sortOnly };
		ASSERT_TRUE(test::Ok(first.add_catalog()));

		rhi::BackendSelection second{ sortOnly };
		ASSERT_TRUE(test::Ok(second.add_catalog()));

		ASSERT_GT(first.preferred().size(), 1u) << "one backend cannot disagree about its own order";
		ASSERT_EQ(first.preferred().size(), second.preferred().size());

		for (std::size_t index = 0; index < first.preferred().size(); ++index)
		{
			EXPECT_EQ(first.preferred()[index].canonicalName, second.preferred()[index].canonicalName) << "two catalogs disagreed at position " << index;
		}

		const std::span<const rhi::BackendInfo> order = first.preferred();
		EXPECT_TRUE(std::ranges::is_sorted(order,
			[](const rhi::BackendInfo & lhs, const rhi::BackendInfo & rhs)
			{
				const rhi::BackendRank lhsRank = RankOf(lhs.id);
				const rhi::BackendRank rhsRank = RankOf(rhs.id);
				return lhsRank != rhsRank ? lhsRank < rhsRank : lhs.canonicalName < rhs.canonicalName;
			}))
			<< "the order is not rank then canonical name, so something positional reached it";
	}

}

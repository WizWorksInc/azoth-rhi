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

#include "azoth/rhi/core/hash.hpp"
#include "azoth/rhi/device/api_tags.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace rhi = azo::rhi;

namespace
{

	TEST(GraphicsApiId, IsDerivedFromTheCanonicalName)
	{
		static_assert(rhi::MakeGraphicsApiId("azoth.rhi.vulkan") == rhi::VulkanApi::id);
		static_assert(rhi::MakeGraphicsApiId("azoth.rhi.d3d12") == rhi::D3D12Api::id);
		static_assert(rhi::MakeGraphicsApiId("azoth.rhi.metal") == rhi::MetalApi::id);
		static_assert(rhi::MakeGraphicsApiId("azoth.rhi.metal4") == rhi::Metal4Api::id);
		static_assert(rhi::MakeGraphicsApiId("azoth.rhi.null") == rhi::NullApi::id);

		static_assert(rhi::MakeGraphicsApiId(rhi::VulkanApi::canonicalName) == rhi::VulkanApi::id);
		static_assert(rhi::MakeGraphicsApiId(rhi::NullApi::canonicalName) == rhi::NullApi::id);

		SUCCEED();
	}

	TEST(GraphicsApiId, HoldsTheValuesThisReleasePublished)
	{
		static_assert(rhi::VulkanApi::id.value == 0x299d772fb8075109ULL);
		static_assert(rhi::D3D12Api::id.value == 0x7941a2f4ac8cf74eULL);
		static_assert(rhi::MetalApi::id.value == 0x3cfc979e01c9c8cdULL);
		static_assert(rhi::NullApi::id.value == 0x0a57d2b2badd572fULL);

		EXPECT_EQ(rhi::Metal4Api::id.value, rhi::hash::Fnv1a64Hash("azoth.rhi.metal4"));

		EXPECT_EQ(rhi::VulkanApi::id.value, rhi::hash::Fnv1a64Hash("azoth.rhi.vulkan"));
		EXPECT_EQ(rhi::NullApi::id.value, rhi::hash::Fnv1a64Hash("azoth.rhi.null"));
	}

	TEST(GraphicsApiId, IsDistinctForEveryBackend)
	{
		constexpr std::array ids{ rhi::VulkanApi::id, rhi::D3D12Api::id, rhi::MetalApi::id, rhi::Metal4Api::id, rhi::NullApi::id };

		for (std::size_t lhs = 0; lhs < ids.size(); ++lhs)
		{
			for (std::size_t rhs = lhs + 1; rhs < ids.size(); ++rhs)
			{
				EXPECT_NE(ids[lhs], ids[rhs]) << "two backends share an id, so a registry lookup cannot tell them apart";
			}
		}
	}

	TEST(GraphicsApiId, IsATrivialComparableValue)
	{
		static_assert(std::is_trivially_copyable_v<rhi::GraphicsApiId>);
		static_assert(sizeof(rhi::GraphicsApiId) == sizeof(std::uint64_t));
		static_assert(rhi::GraphicsApiId{ 5 } == rhi::GraphicsApiId{ 5 });
		static_assert(rhi::GraphicsApiId{ 5 } != rhi::GraphicsApiId{ 6 });

		constexpr rhi::GraphicsApiId unset{};
		static_assert(unset.value == 0);
		static_assert(unset != rhi::VulkanApi::id);

		SUCCEED();
	}

	TEST(GraphicsApiTagConcept, AcceptsEveryShippedTag)
	{
		static_assert(rhi::GraphicsApiTag<rhi::VulkanApi>);
		static_assert(rhi::GraphicsApiTag<rhi::D3D12Api>);
		static_assert(rhi::GraphicsApiTag<rhi::MetalApi>);
		static_assert(rhi::GraphicsApiTag<rhi::Metal4Api>);
		static_assert(rhi::GraphicsApiTag<rhi::NullApi>);

		SUCCEED();
	}

	struct NotDerived final
	{
		static constexpr std::string_view canonicalName = "test.tag";
		static constexpr std::string_view displayName	= "Test Tag";
		static constexpr rhi::GraphicsApiId id{ 1 };
	};

	struct NotEmpty final : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "test.tag";
		static constexpr std::string_view displayName	= "Test Tag";
		static constexpr rhi::GraphicsApiId id{ 1 };
		int state = 0;
	};

	struct NotFinal : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view canonicalName = "test.tag";
		static constexpr std::string_view displayName	= "Test Tag";
		static constexpr rhi::GraphicsApiId id{ 1 };
	};

	struct NoNames final : rhi::GraphicsApiTagRoot
	{
		static constexpr rhi::GraphicsApiId id{ 1 };
	};

	TEST(GraphicsApiTagConcept, RejectsTypesThatOnlyLookLikeTags)
	{
		static_assert(!rhi::GraphicsApiTag<NotDerived>);
		static_assert(!rhi::GraphicsApiTag<NotEmpty>);
		static_assert(!rhi::GraphicsApiTag<NotFinal>);
		static_assert(!rhi::GraphicsApiTag<NoNames>);
		static_assert(!rhi::GraphicsApiTag<int>);

		SUCCEED();
	}

	TEST(ShortApiName, DistinguishesTheTwoMetalBackends)
	{
		static_assert(rhi::ShortApiName(rhi::MetalApi::canonicalName) == "metal");
		static_assert(rhi::ShortApiName(rhi::Metal4Api::canonicalName) == "metal4");

		static_assert(rhi::IsMetalFamily(rhi::MetalApi::id));
		static_assert(rhi::IsMetalFamily(rhi::Metal4Api::id));
		static_assert(!rhi::IsMetalFamily(rhi::VulkanApi::id));
		static_assert(!rhi::IsMetalFamily(rhi::NullApi::id));

		SUCCEED();
	}

	TEST(GraphicsApiTag, CarriesBothANameForCodeAndANameForPeople)
	{
		static_assert(rhi::VulkanApi::canonicalName == "azoth.rhi.vulkan");
		static_assert(rhi::VulkanApi::displayName == "Vulkan");
		static_assert(rhi::D3D12Api::canonicalName == "azoth.rhi.d3d12");
		static_assert(rhi::D3D12Api::displayName == "Direct3D 12");
		static_assert(rhi::MetalApi::canonicalName == "azoth.rhi.metal");
		static_assert(rhi::MetalApi::displayName == "Metal 3");
		static_assert(rhi::Metal4Api::canonicalName == "azoth.rhi.metal4");
		static_assert(rhi::Metal4Api::displayName == "Metal 4");
		static_assert(rhi::NullApi::canonicalName == "azoth.rhi.null");
		static_assert(rhi::NullApi::displayName == "Null RHI");

		SUCCEED();
	}

	TEST(GraphicsApiTag, CostsNothingToPassAround)
	{
		static_assert(std::is_empty_v<rhi::VulkanApi>);
		static_assert(std::is_empty_v<rhi::D3D12Api>);
		static_assert(std::is_empty_v<rhi::MetalApi>);
		static_assert(std::is_empty_v<rhi::Metal4Api>);
		static_assert(std::is_empty_v<rhi::NullApi>);

		SUCCEED();
	}

	TEST(GraphicsApiTag, IsNameableOnEveryPlatform)
	{
		// Naming a backend is not using it. This case would fail to compile on Linux if the D3D12 tag were guarded by a platform macro, which is the
		// arrangement the header set out to avoid.
		constexpr std::array names{ rhi::VulkanApi::canonicalName, rhi::D3D12Api::canonicalName, rhi::MetalApi::canonicalName, rhi::Metal4Api::canonicalName,
			rhi::NullApi::canonicalName };

		for (const std::string_view name : names)
		{
			EXPECT_TRUE(name.starts_with("azoth.rhi.")) << name << " is outside the reserved id namespace";
		}
	}

} // namespace

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
		static_assert(rhi::make_graphics_api_id("azoth.rhi.vulkan") == rhi::VulkanApi::kId);
		static_assert(rhi::make_graphics_api_id("azoth.rhi.d3d12") == rhi::D3D12Api::kId);
		static_assert(rhi::make_graphics_api_id("azoth.rhi.metal") == rhi::MetalApi::kId);
		static_assert(rhi::make_graphics_api_id("azoth.rhi.metal4") == rhi::Metal4Api::kId);
		static_assert(rhi::make_graphics_api_id("azoth.rhi.null") == rhi::NullApi::kId);

		static_assert(rhi::make_graphics_api_id(rhi::VulkanApi::kCanonicalName) == rhi::VulkanApi::kId);
		static_assert(rhi::make_graphics_api_id(rhi::NullApi::kCanonicalName) == rhi::NullApi::kId);

		SUCCEED();
	}

	TEST(GraphicsApiId, HoldsTheValuesThisReleasePublished)
	{
		static_assert(rhi::VulkanApi::kId.value == 0x299d772fb8075109ULL);
		static_assert(rhi::D3D12Api::kId.value == 0x7941a2f4ac8cf74eULL);
		static_assert(rhi::MetalApi::kId.value == 0x3cfc979e01c9c8cdULL);
		static_assert(rhi::NullApi::kId.value == 0x0a57d2b2badd572fULL);

		EXPECT_EQ(rhi::Metal4Api::kId.value, rhi::hash::fnv1a64_hash("azoth.rhi.metal4"));

		EXPECT_EQ(rhi::VulkanApi::kId.value, rhi::hash::fnv1a64_hash("azoth.rhi.vulkan"));
		EXPECT_EQ(rhi::NullApi::kId.value, rhi::hash::fnv1a64_hash("azoth.rhi.null"));
	}

	TEST(GraphicsApiId, IsDistinctForEveryBackend)
	{
		constexpr std::array ids{ rhi::VulkanApi::kId, rhi::D3D12Api::kId, rhi::MetalApi::kId, rhi::Metal4Api::kId, rhi::NullApi::kId };

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
		static_assert(unset != rhi::VulkanApi::kId);

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
		static constexpr std::string_view kCanonicalName = "test.tag";
		static constexpr std::string_view kDisplayName	= "Test Tag";
		static constexpr rhi::GraphicsApiId id{ 1 };
	};

	struct NotEmpty final : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view kCanonicalName = "test.tag";
		static constexpr std::string_view kDisplayName	= "Test Tag";
		static constexpr rhi::GraphicsApiId id{ 1 };
		int state = 0;
	};

	struct NotFinal : rhi::GraphicsApiTagRoot
	{
		static constexpr std::string_view kCanonicalName = "test.tag";
		static constexpr std::string_view kDisplayName	= "Test Tag";
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
		static_assert(rhi::short_api_name(rhi::MetalApi::kCanonicalName) == "metal");
		static_assert(rhi::short_api_name(rhi::Metal4Api::kCanonicalName) == "metal4");

		static_assert(rhi::is_metal_family(rhi::MetalApi::kId));
		static_assert(rhi::is_metal_family(rhi::Metal4Api::kId));
		static_assert(!rhi::is_metal_family(rhi::VulkanApi::kId));
		static_assert(!rhi::is_metal_family(rhi::NullApi::kId));

		SUCCEED();
	}

	TEST(GraphicsApiTag, CarriesBothANameForCodeAndANameForPeople)
	{
		static_assert(rhi::VulkanApi::kCanonicalName == "azoth.rhi.vulkan");
		static_assert(rhi::VulkanApi::kDisplayName == "Vulkan");
		static_assert(rhi::D3D12Api::kCanonicalName == "azoth.rhi.d3d12");
		static_assert(rhi::D3D12Api::kDisplayName == "Direct3D 12");
		static_assert(rhi::MetalApi::kCanonicalName == "azoth.rhi.metal");
		static_assert(rhi::MetalApi::kDisplayName == "Metal 3");
		static_assert(rhi::Metal4Api::kCanonicalName == "azoth.rhi.metal4");
		static_assert(rhi::Metal4Api::kDisplayName == "Metal 4");
		static_assert(rhi::NullApi::kCanonicalName == "azoth.rhi.null");
		static_assert(rhi::NullApi::kDisplayName == "Null RHI");

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
		constexpr std::array names{ rhi::VulkanApi::kCanonicalName,
			rhi::D3D12Api::kCanonicalName,
			rhi::MetalApi::kCanonicalName,
			rhi::Metal4Api::kCanonicalName,
			rhi::NullApi::kCanonicalName };

		for (const std::string_view name : names)
		{
			EXPECT_TRUE(name.starts_with("azoth.rhi.")) << name << " is outside the reserved id namespace";
		}
	}

}

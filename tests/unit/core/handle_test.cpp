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

#include "azoth/rhi/core/constants.hpp"
#include "azoth/rhi/core/handle.hpp"
#include "azoth/rhi/core/resource_handles.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace rhi = azo::rhi;

namespace
{

	struct FirstTag final
	{
	};

	struct SecondTag final
	{
	};

	using FirstHandle  = rhi::Handle<FirstTag>;
	using SecondHandle = rhi::Handle<SecondTag>;

	TEST(HandleType, IsATrivialValueType)
	{
		static_assert(std::is_trivially_copyable_v<rhi::BufferHandle>);
		static_assert(std::is_standard_layout_v<rhi::BufferHandle>);
		static_assert(std::is_trivially_destructible_v<rhi::BufferHandle>);
		static_assert(sizeof(rhi::BufferHandle) == 2 * sizeof(std::uint32_t), "a handle is an index and a generation and nothing else");

		SUCCEED();
	}

	TEST(HandleType, TagsKeepDomainsFromMixing)
	{
		static_assert(!std::same_as<FirstHandle, SecondHandle>);
		static_assert(!std::convertible_to<FirstHandle, SecondHandle>);
		static_assert(!std::same_as<rhi::BufferHandle, rhi::TextureHandle>);
		static_assert(!std::convertible_to<rhi::BufferHandle, rhi::TextureHandle>);

		SUCCEED();
	}

	TEST(HandleValue, DefaultConstructsInvalid)
	{
		constexpr rhi::BufferHandle handle{};

		static_assert(!handle.IsValid());
		static_assert(handle.index == rhi::kInvalidHandleIndex);
		static_assert(handle.generation == 0);

		EXPECT_FALSE(handle.IsValid());
	}

	TEST(HandleValue, IsValidOnlyReadsTheIndex)
	{
		constexpr rhi::BufferHandle retired{
			.index		= 3,
			.generation = 9,
		};
		static_assert(retired.IsValid());

		constexpr rhi::BufferHandle sentinel{
			.index		= rhi::kInvalidHandleIndex,
			.generation = 9,
		};
		static_assert(!sentinel.IsValid());

		EXPECT_TRUE(retired.IsValid());
		EXPECT_FALSE(sentinel.IsValid());
	}

	TEST(HandleValue, EqualityComparesBothFields)
	{
		constexpr rhi::BufferHandle base{
			.index		= 7,
			.generation = 2,
		};

		static_assert(base == rhi::BufferHandle{
								  .index	  = 7,
								  .generation = 2,
							  });
		static_assert(base != rhi::BufferHandle{
								  .index	  = 7,
								  .generation = 3,
							  });
		static_assert(base != rhi::BufferHandle{
								  .index	  = 8,
								  .generation = 2,
							  });

		EXPECT_NE(base,
			(rhi::BufferHandle{
				.index		= 7,
				.generation = 3,
			}));
	}

	TEST(HandleValue, InvalidIndexIsTheMaximumSoItNeverNamesARealSlot)
	{
		static_assert(rhi::kInvalidHandleIndex == std::numeric_limits<std::uint32_t>::max());
		static_assert(rhi::kInvalidHandleIndex == rhi::kInvalidIndex, "the handle sentinel and the descriptor sentinel have to agree");

		SUCCEED();
	}

	TEST(HandleValue, WorksAsAKeyInAHostContainer)
	{
		const auto hashHandle = [](const rhi::BufferHandle handle) noexcept
		{
			return (static_cast<std::size_t>(handle.index) << 32u) ^ handle.generation;
		};
		std::unordered_set<rhi::BufferHandle, decltype(hashHandle)> live(8, hashHandle);

		live.insert(rhi::BufferHandle{
			.index		= 1,
			.generation = 1,
		});
		live.insert(rhi::BufferHandle{
			.index		= 1,
			.generation = 1,
		});
		live.insert(rhi::BufferHandle{
			.index		= 1,
			.generation = 2,
		});

		EXPECT_EQ(live.size(), 2u) << "a recycled slot and its predecessor collapsed into one entry";
	}

	TEST(HandleAliases, CoverEveryPublicDomainAndAreAllDistinct)
	{
		// Adding a resource kind without giving it its own tag would silently make it interchangeable with whichever kind it borrowed from and nothing else in
		// the build would object.
		static_assert(!std::same_as<rhi::BufferHandle, rhi::TextureHandle>);
		static_assert(!std::same_as<rhi::TextureHandle, rhi::TextureViewHandle>);
		static_assert(!std::same_as<rhi::TextureViewHandle, rhi::SamplerHandle>);
		static_assert(!std::same_as<rhi::SamplerHandle, rhi::HeapHandle>);
		static_assert(!std::same_as<rhi::DescriptorSetLayoutHandle, rhi::DescriptorSetHandle>);
		static_assert(!std::same_as<rhi::DescriptorSetHandle, rhi::DescriptorArenaHandle>);
		static_assert(!std::same_as<rhi::PipelineLayoutHandle, rhi::GraphicsPipelineHandle>);
		static_assert(!std::same_as<rhi::GraphicsPipelineHandle, rhi::ComputePipelineHandle>);
		static_assert(!std::same_as<rhi::ComputePipelineHandle, rhi::RayTracingPipelineHandle>);
		static_assert(!std::same_as<rhi::RayTracingPipelineHandle, rhi::PipelineCacheHandle>);
		static_assert(!std::same_as<rhi::PipelineCacheHandle, rhi::AccelerationStructureHandle>);
		static_assert(!std::same_as<rhi::AccelerationStructureHandle, rhi::QueryPoolHandle>);
		static_assert(!std::same_as<rhi::QueryPoolHandle, rhi::TimelineHandle>);
		static_assert(!std::same_as<rhi::TimelineHandle, rhi::BinarySemaphoreHandle>);

		SUCCEED();
	}

} // namespace

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

#include "azoth/rhi/backend/table_validation.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rhi = azo::rhi;

namespace
{

	void Anchor() noexcept {}

	template <typename Table>
	using BlockWords = std::array<rhi::detail::AnyDispatchEntry, rhi::detail::kBlockHeaderWords + rhi::detail::BlockEntries<Table>::kNames.size()>;

	template <typename Table>
	[[nodiscard]] Table WholeTable() noexcept
	{
		Table table{};

		BlockWords<Table> words = std::bit_cast<BlockWords<Table>>(table);
		for (std::size_t index = rhi::detail::kBlockHeaderWords; index < words.size(); ++index)
		{
			words[index] = &Anchor;
		}

		return std::bit_cast<Table>(words);
	}

	// Nulls one entry by name and reports whether it found it so a renamed entry fails the case, not silently testing a whole table.
	template <typename Table>
	[[nodiscard]] bool ClearEntry(Table & table, const std::string_view name) noexcept
	{
		constexpr std::size_t count = rhi::detail::BlockEntries<Table>::kNames.size();

		BlockWords<Table> words = std::bit_cast<BlockWords<Table>>(table);
		for (std::size_t index = 0; index < count; ++index)
		{
			if (rhi::detail::BlockEntries<Table>::kNames[index] == name)
			{
				words[rhi::detail::kBlockHeaderWords + index] = nullptr;
				table										  = std::bit_cast<Table>(words);
				return true;
			}
		}

		return false;
	}

	TEST(DispatchTableValidation, AcceptsATableWithEveryEntryFilled)
	{
		const rhi::CoreDeviceApi table = WholeTable<rhi::CoreDeviceApi>();

		rhi::Error error{};
		EXPECT_TRUE(rhi::detail::RequireCompleteBlock(&table, &error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eOk);
	}

	TEST(DispatchTableValidation, RejectsAMissingEntryAndNamesIt)
	{
		rhi::CoreDeviceApi table = WholeTable<rhi::CoreDeviceApi>();
		ASSERT_TRUE(ClearEntry(table, "CoreDeviceApi::createBuffer is null"));

		rhi::Error error{};
		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&table, &error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		ASSERT_NE(error.message, nullptr);
		EXPECT_EQ(std::string_view{ error.message }, "CoreDeviceApi::createBuffer is null");
	}

	TEST(DispatchTableValidation, ReportsTheFirstMissingEntryRatherThanAnyOfThem)
	{
		rhi::CoreDeviceApi table = WholeTable<rhi::CoreDeviceApi>();
		ASSERT_TRUE(ClearEntry(table, "CoreDeviceApi::createTexture is null"));
		ASSERT_TRUE(ClearEntry(table, "CoreDeviceApi::destroy is null"));

		rhi::Error error{};
		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&table, &error));
		ASSERT_NE(error.message, nullptr);
		EXPECT_EQ(std::string_view{ error.message }, "CoreDeviceApi::createTexture is null") << "the sweep reported a later hole than the first one";
	}

	TEST(DispatchTableValidation, TreatsEveryEntryAsRequiredIncludingTheValidationCounts)
	{
		rhi::CoreDeviceApi table = WholeTable<rhi::CoreDeviceApi>();
		ASSERT_TRUE(ClearEntry(table, "CoreDeviceApi::getValidationMessageCounts is null"));

		rhi::Error error{};
		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&table, &error));
		ASSERT_NE(error.message, nullptr);
		EXPECT_EQ(std::string_view{ error.message }, "CoreDeviceApi::getValidationMessageCounts is null");
	}

	TEST(DispatchTableValidation, CatchesAMissingEntryWhereverItSitsInTheTable)
	{
		rhi::CoreDeviceApi table = WholeTable<rhi::CoreDeviceApi>();
		ASSERT_TRUE(ClearEntry(table, "CoreDeviceApi::destroyDevice is null"));

		rhi::Error error{};
		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&table, &error));
		ASSERT_NE(error.message, nullptr);
		EXPECT_EQ(std::string_view{ error.message }, "CoreDeviceApi::destroyDevice is null");
	}

	TEST(DispatchTableValidation, CoversAllTwentyFourBlocks)
	{
		constexpr std::size_t required =
			rhi::detail::BlockEntries<rhi::InstanceApi>::kNames.size() + rhi::detail::BlockEntries<rhi::CoreDeviceApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::QueueApi>::kNames.size() + rhi::detail::BlockEntries<rhi::CommandPoolApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::DescriptorArenaApi>::kNames.size() + rhi::detail::BlockEntries<rhi::RenderCommandApi>::kNames.size();
		static_assert(required == 77);

		constexpr std::size_t declinable =
			rhi::detail::BlockEntries<rhi::PresentApi>::kNames.size() + rhi::detail::BlockEntries<rhi::SwapchainApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::PlacedMemoryApi>::kNames.size() + rhi::detail::BlockEntries<rhi::AliasingCommandApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::RayTracingApi>::kNames.size() + rhi::detail::BlockEntries<rhi::RayTracingCommandApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::QueryApi>::kNames.size() + rhi::detail::BlockEntries<rhi::QueryCommandApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::PipelineCacheApi>::kNames.size() + rhi::detail::BlockEntries<rhi::ResidencyApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::AdoptionApi>::kNames.size() + rhi::detail::BlockEntries<rhi::IndirectApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::IndirectCountApi>::kNames.size() + rhi::detail::BlockEntries<rhi::SparseApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::NativeEscapeApi>::kNames.size() + rhi::detail::BlockEntries<rhi::ExternalCapabilityApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::ExternalSharingApi>::kNames.size() +
			rhi::detail::BlockEntries<rhi::ResourceIntrospectionApi>::kNames.size();
		static_assert(declinable == 73);

		EXPECT_EQ(rhi::detail::BlockEntries<rhi::InstanceApi>::kNames.size(), 4u);
		EXPECT_EQ(rhi::detail::BlockEntries<rhi::CoreDeviceApi>::kNames.size(), 30u);
		EXPECT_EQ(rhi::detail::BlockEntries<rhi::QueueApi>::kNames.size(), 9u);
		EXPECT_EQ(rhi::detail::BlockEntries<rhi::CommandPoolApi>::kNames.size(), 2u);
		EXPECT_EQ(rhi::detail::BlockEntries<rhi::DescriptorArenaApi>::kNames.size(), 2u);
		EXPECT_EQ(rhi::detail::BlockEntries<rhi::RenderCommandApi>::kNames.size(), 30u);

		SUCCEED();
	}

	TEST(DispatchTableValidation, gate_ShortBlockRejection)
	{
		rhi::CoreDeviceApi block = WholeTable<rhi::CoreDeviceApi>();

		constexpr std::uint32_t kBuiltAgainst = 10;
		block.header.byteSize = static_cast<std::uint32_t>(sizeof(rhi::InterfaceHeader) + (kBuiltAgainst * sizeof(rhi::detail::AnyDispatchEntry)));

		rhi::Error error{};
		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&block, &error));
		EXPECT_EQ(error.code, rhi::ErrorCode::eValidationFailed);
		ASSERT_NE(error.message, nullptr);
		EXPECT_EQ(std::string_view{ error.message }, rhi::detail::BlockEntries<rhi::CoreDeviceApi>::kNames[kBuiltAgainst])
			<< "the sweep named an entry other than the first one past what the block declared";

		EXPECT_EQ(rhi::detail::DeclaredEntryCount(block), kBuiltAgainst);
	}

	TEST(DispatchTableValidation, ReadsALongerBlockOnlyAsFarAsItKnows)
	{
		rhi::CoreDeviceApi block = WholeTable<rhi::CoreDeviceApi>();
		block.header.byteSize	 = static_cast<std::uint32_t>(sizeof(block) + (4 * sizeof(rhi::detail::AnyDispatchEntry)));

		rhi::Error error{};
		EXPECT_TRUE(rhi::detail::RequireCompleteBlock(&block, &error));
		EXPECT_EQ(rhi::detail::DeclaredEntryCount(block), rhi::detail::BlockEntries<rhi::CoreDeviceApi>::kNames.size());
	}

	TEST(DispatchTableValidation, ReportsNothingWhenTheCallerWantsNoError)
	{
		rhi::InstanceApi table = WholeTable<rhi::InstanceApi>();
		ASSERT_TRUE(ClearEntry(table, "InstanceApi::createDevice is null"));

		EXPECT_FALSE(rhi::detail::RequireCompleteBlock(&table, nullptr));
	}

} // namespace

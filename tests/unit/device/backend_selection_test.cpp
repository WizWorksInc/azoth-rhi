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

#include "azoth/rhi/backend/dispatch.hpp"
#include "azoth/rhi/core/hash.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"
#include "azoth/rhi/device/selection.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	[[nodiscard]] std::string_view NameOf(const rhi::BackendInfo & info)
	{
		return rhi::short_api_name(info.canonicalName);
	}

	namespace custom
	{

		struct StudioApi final : rhi::GraphicsApiTagRoot
		{
			static constexpr std::string_view canonicalName = "studio.rhi.custom";
			static constexpr std::string_view displayName	= "Studio Custom";
			static constexpr rhi::GraphicsApiId id			= rhi::make_graphics_api_id(canonicalName);
		};

		static_assert(rhi::GraphicsApiTag<StudioApi>);

		struct Instance final
		{
			const rhi::BackendObject * object = nullptr;
		};

		inline rhi::InstanceApi g_table{};
		inline Instance g_instance{};

		[[nodiscard]] const void * QueryInterface(void *, const rhi::InterfaceId id, const std::uint32_t minVersion) noexcept
		{
			if (id != rhi::InterfaceTraits<rhi::InstanceApi>::kId || minVersion > rhi::InterfaceTraits<rhi::InstanceApi>::kVersion)
			{
				return nullptr;
			}

			return &g_table;
		}

		constexpr rhi::BackendObject kPublished{ .queryInterface = &QueryInterface };
		inline std::uint32_t g_destroyCalls = 0;

		void DestroyInstance(void *) noexcept
		{
			++g_destroyCalls;
		}

		void ResetTable() noexcept
		{
			g_destroyCalls = 0;
			g_table		   = rhi::InstanceApi{};

			g_table.getGraphicsApiId = [](void *) noexcept
			{
				return StudioApi::id;
			};
			g_table.enumerateAdapters = [](void *, std::span<rhi::AdapterInfo>, std::uint32_t *, rhi::Error *) noexcept
			{
				return false;
			};
			g_table.createDevice = [](void *, const rhi::DeviceDesc &, rhi::Error *) noexcept
			{
				return static_cast<void *>(nullptr);
			};
			g_table.destroyInstance = &DestroyInstance;

			g_instance.object = &kPublished;
		}

		[[nodiscard]] rhi::BackendCreateInfo CreateInfo()
		{
			rhi::BackendCreateInfo info{};
			info.info.canonicalName		   = StudioApi::canonicalName;
			info.info.displayName		   = StudioApi::displayName;
			info.info.supportsSurfaces	   = true;
			info.info.supportsDebugMarkers = false;
			info.createInstance			   = [](const void *, rhi::Error *) noexcept
			{
				return static_cast<void *>(&g_instance);
			};
			return info;
		}

		rhi::Result<void> RegisterInto(rhi::GraphicsApiRegistry & registry)
		{
			return registry.Register<StudioApi>(CreateInfo());
		}

	}

	namespace many
	{

		constexpr std::size_t kCount = 24;

		constexpr std::array<std::string_view, kCount> kNames{ "studio.rhi.many00",
			"studio.rhi.many01",
			"studio.rhi.many02",
			"studio.rhi.many03",
			"studio.rhi.many04",
			"studio.rhi.many05",
			"studio.rhi.many06",
			"studio.rhi.many07",
			"studio.rhi.many08",
			"studio.rhi.many09",
			"studio.rhi.many10",
			"studio.rhi.many11",
			"studio.rhi.many12",
			"studio.rhi.many13",
			"studio.rhi.many14",
			"studio.rhi.many15",
			"studio.rhi.many16",
			"studio.rhi.many17",
			"studio.rhi.many18",
			"studio.rhi.many19",
			"studio.rhi.many20",
			"studio.rhi.many21",
			"studio.rhi.many22",
			"studio.rhi.many23" };

		template <std::size_t N>
		struct NumberedApi final : rhi::GraphicsApiTagRoot
		{
			static constexpr std::string_view canonicalName = kNames.at(N);
			static constexpr std::string_view displayName	= kNames.at(N);
			static constexpr rhi::GraphicsApiId id			= rhi::make_graphics_api_id(canonicalName);
		};

		template <std::size_t N>
		rhi::Result<void> RegisterNumbered(rhi::GraphicsApiRegistry & registry)
		{
			rhi::BackendCreateInfo info{};
			info.info.canonicalName = NumberedApi<N>::canonicalName;
			info.info.displayName	= NumberedApi<N>::displayName;
			info.createInstance		= [](const void *, rhi::Error *) noexcept
			{
				return static_cast<void *>(nullptr);
			};

			return registry.Register<NumberedApi<N>>(info);
		}

		[[nodiscard]] std::span<const rhi::BackendEntry> Entries()
		{
			static const std::array<rhi::BackendEntry, kCount> entries = []
			{
				std::array<rhi::BackendEntry, kCount> built{};
				[&]<std::size_t... N>(std::index_sequence<N...>)
				{
					((built.at(N) = rhi::BackendEntry{ .id = NumberedApi<N>::id,
						  .canonicalName				   = NumberedApi<N>::canonicalName,
						  .displayName					   = NumberedApi<N>::displayName,
						  .Register						   = &RegisterNumbered<N>,
						  .rank							   = rhi::BackendRank::eHardware }),
						...);
				}(std::make_index_sequence<kCount>{});
				return built;
			}();

			return entries;
		}

	}

	TEST(ShortApiName, TrimsACanonicalNameToWhatACommandLineWouldType)
	{
		static_assert(rhi::short_api_name("azoth.rhi.vulkan") == "vulkan");
		static_assert(rhi::short_api_name("studio.rhi.custom") == "custom");
		static_assert(rhi::short_api_name("null") == "null");
		static_assert(rhi::short_api_name("").empty());

		SUCCEED();
	}

	TEST(AvailableBackendTable, IsNeverEmptyAndEndsWithNull)
	{
		const std::span<const rhi::BackendEntry> bundled = rhi::AvailableBackends();

		ASSERT_FALSE(bundled.empty());
		EXPECT_EQ(bundled.back().id, rhi::NullApi::id);
		EXPECT_EQ(bundled.back().rank, rhi::BackendRank::eFallback) << "Null goes last because of what it is, not because of where it was added";
	}

	TEST(AvailableBackendTable, IsOrderedByRankRatherThanByHowItWasBuilt)
	{
		const std::span<const rhi::BackendEntry> bundled = rhi::AvailableBackends();

		for (std::size_t index = 1; index < bundled.size(); ++index)
		{
			EXPECT_LE(bundled[index - 1].rank, bundled[index].rank) << bundled[index - 1].canonicalName << " outranks " << bundled[index].canonicalName;
		}
	}

	TEST(MakeBackendEntry, DefaultsToHardwareAndCarriesWhateverRankItWasGiven)
	{
		constexpr rhi::BackendEntry unstated = rhi::MakeBackendEntry<custom::StudioApi>(&custom::RegisterInto);
		static_assert(unstated.rank == rhi::BackendRank::eHardware);

		constexpr rhi::BackendEntry rasterizer = rhi::MakeBackendEntry<custom::StudioApi>(&custom::RegisterInto, rhi::BackendRank::eSoftware);
		static_assert(rasterizer.rank == rhi::BackendRank::eSoftware);

		constexpr rhi::BackendEntry standIn = rhi::MakeBackendEntry<custom::StudioApi>(&custom::RegisterInto, rhi::BackendRank::eFallback);
		static_assert(standIn.rank == rhi::BackendRank::eFallback);

		SUCCEED();
	}

	TEST(AvailableBackendTable, CarriesTheIdItsOwnNameHashesTo)
	{
		for (const rhi::BackendEntry & backend : rhi::AvailableBackends())
		{
			EXPECT_EQ(backend.id, rhi::GraphicsApiId{ azo::rhi::hash::fnv1a64_hash(backend.canonicalName) }) << backend.canonicalName;
			EXPECT_NE(backend.Register, nullptr) << backend.canonicalName << " is listed with no entry point";
			EXPECT_FALSE(backend.displayName.empty()) << backend.canonicalName;
		}
	}

	TEST(AvailableBackendTable, AgreesWithWhatTheSuiteWasBuiltAgainst)
	{
		ASSERT_EQ(rhi::AvailableBackends().size(), test::AvailableBackends().size());
		for (const test::Backend & backend : test::AvailableBackends())
		{
			EXPECT_NE(rhi::find_available_backend(backend.id), nullptr) << backend.shortName << " is available to the suite but not to the library";
		}
	}

	TEST(FindAvailableBackend, AcceptsEitherFormOfTheNameAndKnowsNothingOfBackendsItDidNotShip)
	{
		const rhi::BackendEntry * byShortName = rhi::find_available_backend("null");
		ASSERT_NE(byShortName, nullptr);
		EXPECT_EQ(byShortName->id, rhi::NullApi::id);
		EXPECT_EQ(rhi::find_available_backend("azoth.rhi.null"), byShortName);
		EXPECT_EQ(rhi::find_available_backend(rhi::NullApi::id), byShortName);

		EXPECT_EQ(rhi::find_available_backend("custom"), nullptr) << "the catalog is what the RHI compiled in, not what a run has";
		EXPECT_EQ(rhi::find_available_backend(""), nullptr);
	}

	TEST(BackendSelection, RegistersEverythingTheBuildHas)
	{
		rhi::BackendSelection backends;

		ASSERT_FALSE(backends.is_empty());
		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size());

		for (const rhi::BackendInfo & backend : backends.preferred())
		{
			EXPECT_TRUE(backends.registry().is_registered(backend.id)) << NameOf(backend);
		}
	}

	TEST(BackendSelection, ReportsWhatEachBackendDeclaredRatherThanACopyOfItsName)
	{
		rhi::BackendSelection backends;

		for (const rhi::BackendInfo & backend : backends.preferred())
		{
			const rhi::BackendInfo * registered = backends.registry().Find(backend.id);
			ASSERT_NE(registered, nullptr) << NameOf(backend);
			EXPECT_EQ(registered->canonicalName, backend.canonicalName);
			EXPECT_EQ(registered->displayName, backend.displayName);
			EXPECT_EQ(registered->supportsSurfaces, backend.supportsSurfaces);
		}
	}

	TEST(BackendSelection, KeepsTheTwoViewsOfItsOrderInStep)
	{
		rhi::BackendSelection backends;

		const std::span<const rhi::BackendInfo> preferred = backends.preferred();
		const std::span<const rhi::GraphicsApiId> apis	  = backends.preferred_apis();

		ASSERT_EQ(preferred.size(), apis.size());
		for (std::size_t index = 0; index < preferred.size(); ++index)
		{
			EXPECT_EQ(preferred[index].id, apis[index]);
		}
	}

	TEST(BackendSelection, PutsTheRequestedBackendFirst)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null" } };

		ASSERT_FALSE(backends.is_empty());
		EXPECT_TRUE(backends.honored_request());
		EXPECT_EQ(backends.requested_name(), "null");
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id);

		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size());
	}

	TEST(BackendSelection, FallsBackToWhatTheRunHasWhenTheRequestNamesSomethingElse)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "notabackend" } };

		EXPECT_FALSE(backends.honored_request());
		EXPECT_EQ(backends.requested_name(), "notabackend");
		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size());
	}

	TEST(BackendSelection, LeavesEveryFallbackOutWhenAskedToRatherThanSilentlyDrawingNothing)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "notabackend", .includeNull = false } };

		for (const rhi::BackendInfo & backend : backends.preferred())
		{
			const rhi::BackendEntry * entry = rhi::find_available_backend(backend.id);
			ASSERT_NE(entry, nullptr) << NameOf(backend);
			EXPECT_NE(entry->rank, rhi::BackendRank::eFallback) << NameOf(backend) << " draws nothing and was left in anyway";
		}

		const std::size_t fallbacks =
			static_cast<std::size_t>(std::ranges::count(rhi::AvailableBackends(), rhi::BackendRank::eFallback, &rhi::BackendEntry::rank));
		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size() - fallbacks);
	}

	TEST(BackendSelection, StillHonorsNullWhenItIsTheOneAskedForByName)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null", .includeNull = false } };

		ASSERT_FALSE(backends.is_empty());
		EXPECT_TRUE(backends.honored_request());
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id);
	}

	TEST(BackendSelection, KeepsOnlyTheNamedBackendWhenTheRequestIsForced)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null", .request = rhi::BackendRequest::eForce } };

		ASSERT_FALSE(backends.is_empty());
		EXPECT_TRUE(backends.honored_request());
		EXPECT_EQ(backends.request(), rhi::BackendRequest::eForce);
		EXPECT_EQ(backends.preferred().size(), 1u) << "a backend the run was not forced onto was left in the order behind the one it was";
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id);
	}

	TEST(BackendSelection, LeavesTheOrderEmptyWhenAForcedRequestNamesNothingThisBuildHas)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "notabackend", .request = rhi::BackendRequest::eForce } };

		EXPECT_FALSE(backends.honored_request());
		EXPECT_TRUE(backends.is_empty()) << "the run was forced onto a backend this build has no entry for and something else answered anyway";
	}

	TEST(BackendSelection, StillTakesTheWholeOrderWhenTheRequestIsOnlyATry)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null" } };

		EXPECT_EQ(backends.request(), rhi::BackendRequest::eTry);
		EXPECT_TRUE(backends.honored_request());
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id);
		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size()) << "eTry is a preference and dropped the rest of the order";
	}

	TEST(BackendSelection, PrefersAnExplicitRequestOverTheEnvironment)
	{
		const rhi::BackendSelection fromRequest{ rhi::BackendPreference{ .requested = "null", .consultEnvironment = true } };
		EXPECT_EQ(fromRequest.requested_name(), "null");

		const rhi::BackendSelection ignoringEnvironment{ rhi::BackendPreference{ .requested = nullptr, .consultEnvironment = false } };
		EXPECT_FALSE(ignoringEnvironment.requested_name().empty()) << "with nothing requested the build default should still name something";
	}

	TEST(BackendSelection, CreatesOnTheOneBackendItWasNamedRatherThanThePreferredOrder)
	{
		rhi::BackendSelection backends;

		rhi::InstanceDesc desc{};
		desc.applicationName = "backend_selection_test";
		desc.validation		 = rhi::ValidationMode::eOff;

		const rhi::Result<rhi::UniqueInstance> instance = backends.create_instance(rhi::NullApi::id, desc);
		ASSERT_TRUE(test::Ok(instance));
		EXPECT_TRUE(instance.Value().IsValid());
		EXPECT_EQ(instance.Value().Get().GetGraphicsApiId(), rhi::NullApi::id);
	}

	TEST(BackendSelection, ReportsAnUnsupportedApiForABackendNothingRegistered)
	{
		rhi::BackendSelection backends;

		const rhi::Result<rhi::UniqueInstance> instance = backends.create_instance(rhi::make_graphics_api_id("azoth.rhi.notabackend"), rhi::InstanceDesc{});
		EXPECT_TRUE(test::Failed(instance, rhi::ErrorCode::eUnsupportedApi));
		EXPECT_TRUE(test::ErrorIsPopulated(instance.get_error()));
	}

	TEST(BackendSelection, TakesABackendOfYourOwnOnTheSameFootingAsTheOnesItShipped)
	{
		custom::ResetTable();

		rhi::BackendSelection backends;
		const std::size_t bundled = backends.preferred().size();

		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));

		ASSERT_EQ(backends.preferred().size(), bundled + 1);
		EXPECT_EQ(backends.preferred().back().id, custom::StudioApi::id) << "an unrequested backend joins at the back";
		EXPECT_TRUE(backends.registry().is_registered(custom::StudioApi::id));

		const rhi::BackendInfo * found = backends.registry().Find("custom");
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->id, custom::StudioApi::id);
		EXPECT_EQ(backends.registry().Find("studio.rhi.custom"), found);
		EXPECT_EQ(found->displayName, custom::StudioApi::displayName);
	}

	TEST(BackendSelection, TakesABackendThatRegistersItself)
	{
		custom::ResetTable();

		rhi::BackendSelection backends;
		ASSERT_TRUE(test::Ok(backends.add(rhi::MakeBackendEntry<custom::StudioApi>(&custom::RegisterInto))));

		EXPECT_EQ(backends.preferred().back().id, custom::StudioApi::id);
		EXPECT_NE(backends.registry().Find(custom::StudioApi::id), nullptr);
	}

	TEST(BackendSelection, HonorsARequestForABackendAddedAfterConstruction)
	{
		custom::ResetTable();

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "custom" } };
		ASSERT_FALSE(backends.honored_request());
		const std::size_t bundled = backends.preferred().size();

		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));

		EXPECT_TRUE(backends.honored_request());
		ASSERT_EQ(backends.preferred().size(), bundled + 1);
		EXPECT_EQ(backends.preferred().front().id, custom::StudioApi::id);
		EXPECT_EQ(backends.preferred_apis().front(), custom::StudioApi::id);

		for (std::size_t index = 0; index < bundled; ++index)
		{
			EXPECT_EQ(backends.preferred()[index + 1].id, rhi::AvailableBackends()[index].id);
		}
	}

	TEST(BackendSelection, LetsYouPutYourOwnBackendAheadOfTheBundledOnes)
	{
		custom::ResetTable();

		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
		ASSERT_TRUE(backends.is_empty());
		ASSERT_FALSE(backends.requested_name().empty()) << "the build default should still name something for this to be a real test";

		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));
		ASSERT_TRUE(test::Ok(backends.add_available()));

		EXPECT_EQ(backends.preferred().front().id, custom::StudioApi::id);
		EXPECT_EQ(backends.preferred().size(), rhi::AvailableBackends().size() + 1);
	}

	TEST(BackendSelection, StillLetsAnAskedForBackendPastOneAddedByHand)
	{
		custom::ResetTable();

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "null", .includeAvailable = false } };

		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));
		ASSERT_EQ(backends.preferred().front().id, custom::StudioApi::id) << "nothing has answered to the request yet";

		ASSERT_TRUE(test::Ok(backends.add_available()));

		ASSERT_GE(backends.preferred().size(), 2u);
		EXPECT_TRUE(backends.honored_request());
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id) << "an explicit request did not outrank insertion order";
		EXPECT_EQ(backends.preferred_apis().front(), rhi::NullApi::id);

		EXPECT_EQ(backends.preferred()[1].id, custom::StudioApi::id);
	}

	TEST(BackendSelection, AddsTheBundledOnesAroundWhateverIsAlreadyRegistered)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.add(*rhi::find_available_backend(rhi::NullApi::id))));
		ASSERT_EQ(backends.preferred().size(), 1u);

		ASSERT_TRUE(test::Ok(backends.add_available()));

		EXPECT_EQ(backends.preferred().size(), rhi::available_backends().size()) << "Null was registered twice or the rest were skipped";
		EXPECT_EQ(backends.preferred().front().id, rhi::NullApi::id);
	}

	TEST(BackendSelection, CreatesOnABackendOfYourOwnThroughTheSameCall)
	{
		custom::ResetTable();

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "custom" } };
		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));

		{
			const rhi::Result<rhi::UniqueInstance> instance = backends.create_instance();
			ASSERT_TRUE(test::Ok(instance));
			EXPECT_TRUE(instance.Value().IsValid());
			EXPECT_EQ(instance.Value().Get().GetGraphicsApiId(), custom::StudioApi::id) << "the preferred order did not reach the custom backend first";
		}

		EXPECT_EQ(custom::g_destroyCalls, 1u) << "the owner released it on the way out";
	}

	TEST(BackendSelection, RefusesTheSameBackendTwiceRatherThanListingItTwice)
	{
		custom::ResetTable();

		rhi::BackendSelection backends;
		ASSERT_TRUE(test::Ok(backends.add<custom::StudioApi>(custom::CreateInfo())));
		const std::size_t after = backends.preferred().size();

		const rhi::Result<void> again = backends.add<custom::StudioApi>(custom::CreateInfo());
		EXPECT_TRUE(test::Failed(again, rhi::ErrorCode::eInvalidState));
		EXPECT_EQ(backends.preferred().size(), after) << "a rejected registration still joined the order";
	}

	TEST(BackendSelection, ReportsAnEntryWithNoEntryPointRatherThanTakingIt)
	{
		rhi::BackendSelection backends;

		const rhi::Result<void> added = backends.add(rhi::BackendEntry{});
		EXPECT_TRUE(test::Failed(added, rhi::ErrorCode::eInvalidArgument));
		EXPECT_TRUE(test::ErrorIsPopulated(added.get_error()));
	}

	TEST(BackendSelection, gate_CatalogBeyondSixteen)
	{
		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };

		for (const rhi::BackendEntry & entry : many::Entries())
		{
			ASSERT_TRUE(test::Ok(backends.add(entry))) << "adding " << entry.canonicalName << " was refused";
		}

		ASSERT_EQ(backends.preferred().size(), many::kCount) << "the order dropped backends past a ceiling";

		for (const rhi::BackendEntry & entry : many::Entries())
		{
			const std::span<const rhi::BackendInfo> order = backends.preferred();
			const auto found							  = std::ranges::find(order, entry.id, &rhi::BackendInfo::id);
			EXPECT_NE(found, order.end()) << entry.canonicalName << " registered but is not in the order";
			EXPECT_NE(backends.registry().Find(entry.id), nullptr) << entry.canonicalName << " is in the order but not in the registry";
		}

		EXPECT_EQ(backends.preferred_apis().size(), backends.preferred().size());
		for (std::size_t index = 0; index < backends.preferred().size(); ++index)
		{
			EXPECT_EQ(backends.preferred_apis()[index], backends.preferred()[index].id) << "the id order and the info order disagree at " << index;
		}
	}

	TEST(BackendSelection, gate_ConcurrentRegistration)
	{
		constexpr std::size_t kThreads = 4;
		static_assert(many::kCount % kThreads == 0, "the entries have to divide evenly for each thread to take a run");
		constexpr std::size_t kPerThread = many::kCount / kThreads;

		rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };

		std::atomic<std::size_t> refused{ 0 };
		std::vector<std::thread> workers;
		workers.reserve(kThreads);

		for (std::size_t worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&, worker]
				{
					for (std::size_t index = worker * kPerThread; index < (worker + 1) * kPerThread; ++index)
					{
						if (!backends.add(many::Entries()[index]))
						{
							refused.fetch_add(1, std::memory_order_relaxed);
						}
					}
				});
		}

		for (std::thread & thread : workers)
		{
			thread.join();
		}

		EXPECT_EQ(refused.load(), 0u) << "a registration was refused, and every thread took a disjoint run so none of them collided";
		ASSERT_EQ(backends.preferred().size(), many::kCount) << "registrations were lost between threads";

		for (const rhi::BackendEntry & entry : many::Entries())
		{
			const std::span<const rhi::BackendInfo> order = backends.preferred();
			EXPECT_NE(std::ranges::find(order, entry.id, &rhi::BackendInfo::id), order.end()) << entry.canonicalName << " was lost";
		}
	}

}

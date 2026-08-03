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

#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/module/backend_module.hpp"

#include "conformance/matchers.hpp"
#include "harness/backends.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace rhi  = azo::rhi;
namespace test = azo::rhi::test;

namespace
{

	constexpr std::string_view kLoadableBackend = "studio.rhi.loadable";
	constexpr const char * kModulePath			= AZOTH_RHI_TEST_LOADABLE_MODULE_PATH;

	[[nodiscard]] bool OrderContains(const rhi::BackendSelection & backends, const std::string_view canonicalName)
	{
		const std::span<const rhi::BackendInfo> order = backends.Preferred();
		return std::ranges::any_of(order,
			[canonicalName](const rhi::BackendInfo & info)
			{
				return info.canonicalName == canonicalName;
			});
	}

	TEST(ModuleLoading, LoadsAndDescribesItself)
	{
		rhi::Result<rhi::BackendModule> loaded = rhi::BackendModule::Load(kModulePath);
		ASSERT_TRUE(test::Ok(loaded)) << "the module at " << kModulePath << " did not load";

		EXPECT_TRUE(loaded.Value().IsLoaded());
		ASSERT_EQ(loaded.Value().Entries().size(), 1u);
		EXPECT_EQ(loaded.Value().Entries().front().canonicalName, kLoadableBackend);
		EXPECT_EQ(loaded.Value().LiveObjects(), 0u);

		EXPECT_TRUE(test::Ok(loaded.Value().Unload()));
		EXPECT_FALSE(loaded.Value().IsLoaded());
	}

	TEST(ModuleLoading, NamesTheModuleOwnsOutliveTheEntryPointCall)
	{
		rhi::Result<rhi::BackendModule> loaded = rhi::BackendModule::Load(kModulePath);
		ASSERT_TRUE(test::Ok(loaded));

		const rhi::BackendEntry & entry = loaded.Value().Entries().front();
		EXPECT_EQ(entry.canonicalName, kLoadableBackend);
		EXPECT_FALSE(entry.displayName.empty());

		const char * inside = entry.canonicalName.data();
		EXPECT_TRUE(test::Ok(loaded.Value().Unload()));
		EXPECT_NE(inside, nullptr) << "nothing here dereferences that, it is recorded to say what the copy is for";
	}

	TEST(ModuleLoading, gate_ModuleAbiStamp)
	{
		const rhi::AbiStamp mine = rhi::CurrentAbiStamp();
		EXPECT_EQ(mine, rhi::CurrentAbiStamp()) << "the stamp is not stable within one build, so it can compare nothing";

		rhi::AbiStamp differentLibrary	 = mine;
		differentLibrary.standardLibrary = mine.standardLibrary + 1;
		EXPECT_FALSE(differentLibrary == mine) << "a different standard library was accepted, which is the layout case this exists for";

		rhi::AbiStamp differentWidth = mine;
		differentWidth.pointerBits	 = mine.pointerBits == 64 ? 32 : 64;
		EXPECT_FALSE(differentWidth == mine);

		rhi::AbiStamp differentLanguage = mine;
		differentLanguage.languageValue = mine.languageValue + 1;
		EXPECT_FALSE(differentLanguage == mine);

		rhi::AbiStamp differentAbi	  = mine;
		differentAbi.moduleAbiVersion = mine.moduleAbiVersion + 1;
		EXPECT_FALSE(differentAbi == mine);

		rhi::AbiStamp differentRhi	 = mine;
		differentRhi.rhiVersionMajor = mine.rhiVersionMajor + 1;
		EXPECT_FALSE(differentRhi == mine);

		const rhi::Result<rhi::BackendModule> notAModule = rhi::BackendModule::Load("azoth.rhi.test.nothing-is-here");
		EXPECT_FALSE(notAModule.HasValue());
		EXPECT_TRUE(test::ErrorIsPopulated(notAModule.GetError()));
	}

	TEST(ModuleLoading, gate_ModuleUnloadRefusedWhileLive)
	{
		rhi::Result<rhi::BackendModule> loaded = rhi::BackendModule::Load(kModulePath);
		ASSERT_TRUE(test::Ok(loaded));

		rhi::BackendSelection backends{ rhi::BackendPreference{ .requested = "loadable", .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(backends.AddModule(loaded.Value().Entries())));
		ASSERT_TRUE(OrderContains(backends, kLoadableBackend)) << "the module's backend did not join the order";

		{
			const rhi::Result<rhi::UniqueInstance> instance = backends.CreateInstance();
			ASSERT_TRUE(test::Ok(instance)) << "the loaded backend could not bring up an instance";
			ASSERT_EQ(loaded.Value().LiveObjects(), 1u) << "the module did not count what it handed out";

			const rhi::Result<void> refused = loaded.Value().Unload();
			EXPECT_TRUE(test::Failed(refused, rhi::ErrorCode::eInvalidState)) << "the module unloaded with a live object through it";
			EXPECT_TRUE(loaded.Value().IsLoaded()) << "a refused unload still closed the image";
		}

		EXPECT_EQ(loaded.Value().LiveObjects(), 0u);
		EXPECT_TRUE(test::Ok(loaded.Value().Unload()));
		EXPECT_FALSE(loaded.Value().IsLoaded());
	}

	TEST(ModuleLoading, gate_NoDanglingAfterUnload)
	{
		{
			rhi::Result<rhi::BackendModule> loaded = rhi::BackendModule::Load(kModulePath);
			ASSERT_TRUE(test::Ok(loaded));

			rhi::BackendSelection backends{ rhi::BackendPreference{ .includeAvailable = false } };
			ASSERT_TRUE(test::Ok(backends.AddModule(loaded.Value().Entries())));

			EXPECT_TRUE(test::Ok(loaded.Value().Unload()));
			EXPECT_EQ(loaded.Value().Entries().size(), 0u) << "the entries survived the unload that invalidated them";
		}

		rhi::BackendSelection after{ rhi::BackendPreference{ .includeAvailable = false } };
		ASSERT_TRUE(test::Ok(after.AddCatalog()));
		EXPECT_FALSE(OrderContains(after, kLoadableBackend)) << "an unloaded module's backend is still reachable by name";
	}

	TEST(ModuleLoading, gate_ConcurrentModuleLoad)
	{
		constexpr int kThreads = 4;

		std::atomic<int> loadedCount{ 0 };
		std::atomic<int> unloadedCount{ 0 };
		std::vector<std::thread> workers;
		workers.reserve(kThreads);

		for (int worker = 0; worker < kThreads; ++worker)
		{
			workers.emplace_back(
				[&]
				{
					rhi::Result<rhi::BackendModule> loaded = rhi::BackendModule::Load(kModulePath);
					if (!loaded)
					{
						return;
					}

					loadedCount.fetch_add(1, std::memory_order_relaxed);
					if (loaded.Value().Unload())
					{
						unloadedCount.fetch_add(1, std::memory_order_relaxed);
					}
				});
		}

		for (std::thread & worker : workers)
		{
			worker.join();
		}

		EXPECT_EQ(loadedCount.load(), kThreads) << "a concurrent load was lost";
		EXPECT_EQ(unloadedCount.load(), kThreads) << "a concurrent unload was refused or lost";
	}

} // namespace

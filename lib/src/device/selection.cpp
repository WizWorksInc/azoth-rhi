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

#include "azoth/rhi/device/api_tags.hpp"

#include "backends/registration.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string_view>

namespace azo::rhi
{
	namespace
	{

		/*
		 * Vulkan, Direct3D 12, the two Metal generations and Null. Sizes this table only, which is what the RHI itself compiled in and is therefore known here. What a
		 * run can choose between is not bounded at all, since backends of your own join the same order and a module can bring one while the run is going.
		 */
		constexpr std::size_t kMaxAvailableBackends = 5;

		struct AvailableTable final
		{
			std::array<BackendEntry, kMaxAvailableBackends> entries{};
			std::size_t count = 0;

			constexpr void Add(const BackendEntry & entry) noexcept
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				entries[count] = entry;
				++count;
			}

			/*
			 * Insertion sort by rank, which is stable so backends of the same rank keep the order they were added in.
			 *
			 * Written out, not reached for because std::stable_sort is not usable in a constant expression until C++26 and a plain sort would let two hardware backends
			 * swap places between toolchains.
			 */
			constexpr void SortByRank() noexcept
			{
				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				for (std::size_t index = 1; index < count; ++index)
				{
					const BackendEntry entry = entries[index];

					std::size_t at = index;
					while (at > 0 && entries[at - 1].rank > entry.rank)
					{
						entries[at] = entries[at - 1];
						--at;
					}

					entries[at] = entry;
				}
				// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}
		};

		/*
		 * The one place a build's backend configuration is read. The order comes off the rank and not the order they are added in. A build that has a driver
		 * therefore reports it and not the backend that draws nothing. Identity comes off the API tag through MakeBackendEntry so a backend cannot end up listed
		 * under a name that disagrees with the one it registers.
		 */
		[[nodiscard]] consteval AvailableTable MakeAvailableTable()
		{
			AvailableTable table;

			/*
			 * The platform's own API first, then Vulkan. The sort below is stable and every hardware backend shares one rank, so this order decides the
			 * default.
			 *
			 * On Apple the Vulkan beside Metal is MoltenVK, which reaches the same driver through an extra layer. On Windows both are native and Direct3D 12 is
			 * the one the platform ships.
			 *
			 * Metal 4 goes ahead of Metal 3 because creating a device on it fails cleanly where it does not exist.
			 */
#ifdef AZOTH_RHI_BACKEND_METAL4
			table.Add(MakeBackendEntry<Metal4Api>(&RegisterMetal4Backend));
#endif

#ifdef AZOTH_RHI_BACKEND_METAL
			table.Add(MakeBackendEntry<MetalApi>(&RegisterMetalBackend));
#endif

#ifdef AZOTH_RHI_BACKEND_D3D12
			table.Add(MakeBackendEntry<D3D12Api>(&RegisterD3D12Backend));
#endif

#ifdef AZOTH_RHI_BACKEND_VULKAN
			table.Add(MakeBackendEntry<VulkanApi>(&RegisterVulkanBackend));
#endif

			// Always compiled in so there is no configuration to consult and no build where this list comes out empty.
			table.Add(MakeBackendEntry<NullApi>(&RegisterNullBackend, BackendRank::eFallback));

			table.SortByRank();
			return table;
		}

		constexpr AvailableTable kAvailableTable = MakeAvailableTable();

		struct ResolvedRequest final
		{
			std::string_view name;

			// Somebody named this backend and the build did not just fall back to it.
			bool asked = false;
		};

		/*
		 * Precedence matches SelectGraphicsApi: an explicit request, then the environment, then what the build defaulted to.
		 *
		 * An empty string counts as nothing set so AZOTH_RHI_BACKEND= behaves like an unset variable without naming a backend that cannot exist.
		 */
		[[nodiscard]] ResolvedRequest ResolveRequestedName(const BackendPreference & preference)
		{
			if (preference.requested != nullptr && *preference.requested != '\0')
			{
				return ResolvedRequest{ .name = preference.requested, .asked = true };
			}

			if (preference.consultEnvironment)
			{
				// NOLINTNEXTLINE(concurrency-mt-unsafe): one-time startup env read before worker threads exist.
				const char * fromEnvironment = std::getenv("AZOTH_RHI_BACKEND");
				if (fromEnvironment != nullptr && *fromEnvironment != '\0')
				{
					return ResolvedRequest{ .name = fromEnvironment, .asked = true };
				}
			}

			return ResolvedRequest{ .name = AZOTH_RHI_BACKEND_DEFAULT, .asked = false };
		}

		// Either form of the name refers to the backend so a command line can say vulkan and a configuration file azoth.rhi.vulkan.
		[[nodiscard]] bool NameRefersTo(const std::string_view name, const std::string_view canonicalName) noexcept
		{
			return name == canonicalName || name == ShortApiName(canonicalName);
		}

		/*
		 * The head of the self-registration list. constinit and not a function-local static so it is a valid empty head before any dynamic initialization anywhere in
		 * the program. A registrar is a global in someone else's translation unit with no ordering against anything here.
		 *
		 * Append only. The entries hold borrowed name views that live as long as the image, which is why module loading is a separate mechanism. Unlinking during
		 * static destruction would race whatever is still walking it.
		 */
		constinit std::atomic<const StaticBackendRegistration *> g_selfRegistered{ nullptr };

		/*
		 * Rank first, then canonical name.
		 *
		 * The walk order of the self-registration list is load order, which no two builds have to agree on and which a linker may change for reasons that have
		 * nothing to do with this. Sorting by something intrinsic to the backend is what makes the resulting order the same twice and the name is the only total
		 * ordering available, identity being derived from it.
		 */
		void SortForReproducibleOrder(detail::HostVector<BackendEntry> & entries)
		{
			std::ranges::sort(entries,
				[](const BackendEntry & lhs, const BackendEntry & rhs)
				{
					return lhs.rank != rhs.rank ? lhs.rank < rhs.rank : lhs.canonicalName < rhs.canonicalName;
				});
		}

	} // namespace

	StaticBackendRegistration::StaticBackendRegistration(const BackendEntry & entry) noexcept
		: m_entry(entry),
		  m_next(g_selfRegistered.load(std::memory_order_relaxed))
	{
		/*
		 * Release on the push and acquire on the walk so a reader that sees this node sees the entry inside it too.
		 *
		 * Says nothing about unloading, which is a module's problem: a node can never be taken off so the only ordering that has to hold is the one between writing a
		 * node and another thread reading it.
		 */

		while (!g_selfRegistered.compare_exchange_weak(m_next, this, std::memory_order_release, std::memory_order_relaxed))
		{
		}
	}

	const StaticBackendRegistration * SelfRegisteredBackends() noexcept
	{
		return g_selfRegistered.load(std::memory_order_acquire);
	}

	std::span<const BackendEntry> AvailableBackends() noexcept
	{
		return std::span<const BackendEntry>{ kAvailableTable.entries.data(), kAvailableTable.count };
	}

	const BackendEntry * FindAvailableBackend(const std::string_view name) noexcept
	{
		for (const BackendEntry & entry : AvailableBackends())
		{
			if (NameRefersTo(name, entry.canonicalName))
			{
				return &entry;
			}
		}

		return nullptr;
	}

	const BackendEntry * FindAvailableBackend(const GraphicsApiId id) noexcept
	{
		for (const BackendEntry & entry : AvailableBackends())
		{
			if (entry.id == id)
			{
				return &entry;
			}
		}

		return nullptr;
	}

	Result<void> RegisterBackend(GraphicsApiRegistry & registry, const GraphicsApiId id)
	{
		const BackendEntry * entry = FindAvailableBackend(id);
		if (entry == nullptr || entry->Register == nullptr)
		{
			// Says nothing about whether the API exists, only that this build has no implementation of it to register.
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "this build compiled no backend in for that graphics API",
			};
		}

		return entry->Register(registry);
	}

	BackendSelection::BackendSelection(const BackendPreference & preference) : m_includeNull(preference.includeNull)
	{
		const ResolvedRequest requested = ResolveRequestedName(preference);
		m_requestedName					= requested.name;
		m_wasAskedFor					= requested.asked;

		if (preference.includeAvailable)
		{
			AddAvailable();
		}
	}

	/*
	 * The source's guard is taken and the destination's is not because nothing can be reaching a half constructed object or one being assigned into. What has to
	 * be excluded is a registration arriving on the object being moved out of.
	 *
	 * The guard itself does not travel. It is not part of what this holds, it is what keeps what this holds consistent and a fresh one on each side is the correct
	 * state after the values have separated.
	 */
	BackendSelection::BackendSelection(BackendSelection && other) noexcept
	{
		const std::scoped_lock guard(other.m_guard);

		// Moved in the body and not the initializer list, which runs before the guard is taken and so would read the vectors while a registration was growing them.
		m_registry		 = std::move(other.m_registry);
		m_preferred		 = std::move(other.m_preferred);
		m_preferredApis	 = std::move(other.m_preferredApis);
		m_requestedName	 = other.m_requestedName;
		m_wasAskedFor	 = other.m_wasAskedFor;
		m_honoredRequest = other.m_honoredRequest;
		m_includeNull	 = other.m_includeNull;
	}

	BackendSelection & BackendSelection::operator=(BackendSelection && other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		const std::scoped_lock guard(other.m_guard);

		m_registry		 = std::move(other.m_registry);
		m_preferred		 = std::move(other.m_preferred);
		m_preferredApis	 = std::move(other.m_preferredApis);
		m_requestedName	 = other.m_requestedName;
		m_wasAskedFor	 = other.m_wasAskedFor;
		m_honoredRequest = other.m_honoredRequest;
		m_includeNull	 = other.m_includeNull;
		return *this;
	}

	Result<void> BackendSelection::AddOne(const BackendEntry & entry)
	{
		if (entry.Register == nullptr)
		{
			return Error{
				.code	 = ErrorCode::eInvalidArgument,
				.message = "BackendSelection was handed an entry with no registration entry point",
			};
		}

		const std::size_t before	  = Registered();
		const Result<void> registered = entry.Register(m_registry);
		if (!registered)
		{
			return registered;
		}

		RecordFrom(before);
		return {};
	}

	Result<void> BackendSelection::Add(const BackendEntry & entry)
	{
		const std::scoped_lock guard(m_guard);
		return AddOne(entry);
	}

	Result<void> BackendSelection::AddAll(const std::span<const BackendEntry> entries)
	{
		const std::scoped_lock guard(m_guard);

		Result<void> firstFailure;

		for (const BackendEntry & entry : entries)
		{
			const Result<void> added = AddOne(entry);
			if (!added && firstFailure)
			{
				firstFailure = added.GetError();
			}
		}

		return firstFailure;
	}

	void BackendSelection::Take(const BackendEntry & entry, Result<void> & firstFailure)
	{
		// Skipped, not reported so this composes with a run that already added one of them by hand.
		if (m_registry.IsRegistered(entry.id))
		{
			return;
		}

		// A fallback still joins when it is what the name asks for, since pointing a run at Null deliberately is how a host exercises its own fallback handling on a
		// machine that also has a driver.
		const bool unwantedFallback = !m_includeNull && entry.rank == BackendRank::eFallback && !NameRefersTo(m_requestedName, entry.canonicalName);
		if (unwantedFallback)
		{
			return;
		}

		if (const Result<void> added = AddOne(entry); !added && firstFailure)
		{
			firstFailure = added.GetError();
		}
	}

	/*
	 * The named one goes in first without being moved up afterwards.
	 *
	 * That is what keeps the build default in its place. It names a backend so it settles the order among the set it came from and ordering it here means it does
	 * that without stepping in front of a backend the host added by hand. A name somebody actually asked for outranks everything and Record is where that happens.
	 */
	Result<void> BackendSelection::AddInOrder(const std::span<const BackendEntry> entries)
	{
		Result<void> firstFailure;

		const auto named = std::ranges::find_if(entries,
			[this](const BackendEntry & entry)
			{
				return NameRefersTo(m_requestedName, entry.canonicalName);
			});

		if (named != entries.end())
		{
			Take(*named, firstFailure);
		}

		for (const BackendEntry & entry : entries)
		{
			Take(entry, firstFailure);
		}

		return firstFailure;
	}

	Result<void> BackendSelection::AddAvailable()
	{
		const std::scoped_lock guard(m_guard);
		return AddInOrder(AvailableBackends());
	}

	Result<void> BackendSelection::AddSelfRegistered()
	{
		const std::scoped_lock guard(m_guard);

		detail::HostVector<BackendEntry> registered;
		for (const StaticBackendRegistration * node = SelfRegisteredBackends(); node != nullptr; node = node->Next())
		{
			registered.push_back(node->Entry());
		}

		SortForReproducibleOrder(registered);
		return AddInOrder(registered);
	}

	Result<void> BackendSelection::AddModule(const std::span<const BackendEntry> entries)
	{
		const std::scoped_lock guard(m_guard);

		// Sorted like every other route so where a module's backends land does not depend on when the host loaded it.
		detail::HostVector<BackendEntry> fromModule(entries.begin(), entries.end());
		SortForReproducibleOrder(fromModule);
		return AddInOrder(fromModule);
	}

	Result<void> BackendSelection::AddCatalog()
	{
		const std::scoped_lock guard(m_guard);

		/*
		 * Self-registered entries go in first so that the shadowing below reads the way it is meant to: a backend somebody linked into this image outranks one the
		 * RHI happens to ship under the same ID. Replacing a bundled backend with your own is the reason to register under its ID at all and the alternative would be
		 * a registration that silently did nothing.
		 */
		detail::HostVector<BackendEntry> catalog;
		for (const StaticBackendRegistration * node = SelfRegisteredBackends(); node != nullptr; node = node->Next())
		{
			catalog.push_back(node->Entry());
		}

		const std::size_t selfRegistered = catalog.size();

		for (const BackendEntry & entry : AvailableBackends())
		{
			const std::span<const BackendEntry> mine{ catalog.data(), selfRegistered };
			const bool shadowed = std::ranges::any_of(mine,
				[&entry](const BackendEntry & ours)
				{
					return ours.id == entry.id;
				});

			if (!shadowed)
			{
				catalog.push_back(entry);
			}
		}

		SortForReproducibleOrder(catalog);
		return AddInOrder(catalog);
	}

	void BackendSelection::RecordFrom(const std::size_t firstNew)
	{
		// Read fresh each time around, since registering may have moved the entries.
		for (std::size_t index = firstNew; index < Registered(); ++index)
		{
			Record(m_registry.EnumerateBackends()[index]);
		}
	}

	void BackendSelection::Record(const BackendInfo & info)
	{
		/*
		 * No ceiling. It used to stop at sixteen and report the seventeenth as an error, which was defensible only while the count was whatever this build compiled
		 * in. A module can bring one at runtime so the number is the host's and not ours to cap.
		 */
		const bool matchesRequest = !m_honoredRequest && NameRefersTo(m_requestedName, info.canonicalName);
		if (matchesRequest)
		{
			m_honoredRequest = true;
		}

		// Only a name somebody asked for moves a backend up. The build default is not that and promoting it would put it in front of whatever the host registered
		// itself, which is the one order a host cannot have meant.
		if (matchesRequest && m_wasAskedFor)
		{
			// Everything already added keeps its order behind it so honoring a late request reorders, not displaces.
			m_preferred.insert(m_preferred.begin(), info);
			m_preferredApis.insert(m_preferredApis.begin(), info.id);
			return;
		}

		m_preferred.push_back(info);
		m_preferredApis.push_back(info.id);
	}

	/*
	 * Both of these take a copy of the order and not the span PreferredApis hands back.
	 *
	 * The span points into the vector and a registration on another thread can grow it while a create is walking it. The order is a handful of IDs so copying it
	 * is nothing next to bringing up a device and it makes creating while a module loads a supported thing and not a race nobody would find.
	 */
	Result<UniqueInstance> BackendSelection::CreateInstance(const InstanceDesc & desc)
	{
		detail::HostVector<GraphicsApiId> order;
		{
			const std::scoped_lock guard(m_guard);
			order = m_preferredApis;
		}

		return azo::rhi::CreateInstance(m_registry, order, desc);
	}

	Result<UniqueDevice> BackendSelection::CreateDevice(const DeviceDesc & desc)
	{
		detail::HostVector<GraphicsApiId> order;
		{
			const std::scoped_lock guard(m_guard);
			order = m_preferredApis;
		}

		return azo::rhi::CreateDevice(m_registry, order, desc);
	}

	Result<UniqueInstance> BackendSelection::CreateInstance(const GraphicsApiId api, const InstanceDesc & desc)
	{
		const std::array<GraphicsApiId, 1> only{ api };
		return azo::rhi::CreateInstance(m_registry, only, desc);
	}

	Result<UniqueDevice> BackendSelection::CreateDevice(const GraphicsApiId api, const DeviceDesc & desc)
	{
		const std::array<GraphicsApiId, 1> only{ api };
		return azo::rhi::CreateDevice(m_registry, only, desc);
	}

} // namespace azo::rhi

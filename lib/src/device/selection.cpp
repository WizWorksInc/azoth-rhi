// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
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

		[[nodiscard]] consteval AvailableTable MakeAvailableTable()
		{
			AvailableTable table;

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

			table.Add(MakeBackendEntry<NullApi>(&RegisterNullBackend, BackendRank::eFallback));

			table.SortByRank();
			return table;
		}

		constexpr AvailableTable kAvailableTable = MakeAvailableTable();

		struct ResolvedRequest final
		{
			std::string_view name;

			bool asked = false;
		};

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

		[[nodiscard]] BackendRequest ResolveRequest(const BackendPreference & preference)
		{
			if (preference.request == BackendRequest::eForce || !preference.consultEnvironment)
			{
				return preference.request;
			}

			// NOLINTNEXTLINE(concurrency-mt-unsafe): one-time startup env read before worker threads exist.
			const char * fromEnvironment = std::getenv("AZOTH_RHI_BACKEND_FORCE");
			const bool forced			 = fromEnvironment != nullptr && *fromEnvironment != '\0' && std::string_view{ fromEnvironment } != "0";
			return forced ? BackendRequest::eForce : BackendRequest::eTry;
		}

		[[nodiscard]] bool NameRefersTo(const std::string_view name, const std::string_view canonicalName) noexcept
		{
			return name == canonicalName || name == ShortApiName(canonicalName);
		}

		constinit std::atomic<const StaticBackendRegistration *> g_selfRegistered{ nullptr };

		void SortForReproducibleOrder(detail::HostVector<BackendEntry> & entries)
		{
			std::ranges::sort(entries,
				[](const BackendEntry & lhs, const BackendEntry & rhs)
				{
					return lhs.rank != rhs.rank ? lhs.rank < rhs.rank : lhs.canonicalName < rhs.canonicalName;
				});
		}

	}

	StaticBackendRegistration::StaticBackendRegistration(const BackendEntry & entry) noexcept
		: m_entry(entry),
		  m_next(g_selfRegistered.load(std::memory_order_relaxed))
	{

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
			return Error{
				.code	 = ErrorCode::eUnsupportedApi,
				.message = "this build compiled no backend in for that graphics API",
			};
		}

		return entry->Register(registry);
	}

	BackendSelection::BackendSelection(const BackendPreference & preference) : m_includeNull(preference.includeNull), m_request(ResolveRequest(preference))
	{
		const ResolvedRequest requested = ResolveRequestedName(preference);
		m_requestedName					= requested.name;
		m_wasAskedFor					= requested.asked;

		if (preference.includeAvailable)
		{
			AddAvailable();
		}
	}

	BackendSelection::BackendSelection(BackendSelection && other) noexcept
	{
		const std::scoped_lock guard(other.m_guard);

		m_registry		 = std::move(other.m_registry);
		m_preferred		 = std::move(other.m_preferred);
		m_preferredApis	 = std::move(other.m_preferredApis);
		m_requestedName	 = other.m_requestedName;
		m_wasAskedFor	 = other.m_wasAskedFor;
		m_honoredRequest = other.m_honoredRequest;
		m_includeNull	 = other.m_includeNull;
		m_request		 = other.m_request;
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
		m_request		 = other.m_request;
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
		if (m_registry.IsRegistered(entry.id))
		{
			return;
		}

		const bool unwantedFallback = !m_includeNull && entry.rank == BackendRank::eFallback && !NameRefersTo(m_requestedName, entry.canonicalName);
		if (unwantedFallback)
		{
			return;
		}

		const bool notTheOneAskedFor = m_request == BackendRequest::eForce && m_wasAskedFor && !NameRefersTo(m_requestedName, entry.canonicalName);
		if (notTheOneAskedFor)
		{
			return;
		}

		if (const Result<void> added = AddOne(entry); !added && firstFailure)
		{
			firstFailure = added.GetError();
		}
	}

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

		detail::HostVector<BackendEntry> fromModule(entries.begin(), entries.end());
		SortForReproducibleOrder(fromModule);
		return AddInOrder(fromModule);
	}

	Result<void> BackendSelection::AddCatalog()
	{
		const std::scoped_lock guard(m_guard);

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
		for (std::size_t index = firstNew; index < Registered(); ++index)
		{
			Record(m_registry.EnumerateBackends()[index]);
		}
	}

	void BackendSelection::Record(const BackendInfo & info)
	{
		const bool matchesRequest = !m_honoredRequest && NameRefersTo(m_requestedName, info.canonicalName);
		if (matchesRequest)
		{
			m_honoredRequest = true;
		}

		if (matchesRequest && m_wasAskedFor)
		{
			m_preferred.insert(m_preferred.begin(), info);
			m_preferredApis.insert(m_preferredApis.begin(), info.id);
			return;
		}

		m_preferred.push_back(info);
		m_preferredApis.push_back(info.id);
	}

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

}

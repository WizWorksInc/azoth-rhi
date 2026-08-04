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

#pragma once

/**
 * \file
 * \brief Backend discovery, registration, and runtime backend selection.
 */

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>

namespace azo::rhi
{

	/**
	 * \brief Backend purpose used for preference ordering and fallback filtering.
	 *
	 * Software backends still draw. Fallback backends exist to keep a run alive and may produce no frame.
	 */
	enum class BackendRank : std::uint8_t
	{
		/**
		 * \brief Reaches a GPU through a driver.
		 */
		eHardware,

		/**
		 * \brief Draws on the CPU.
		 */
		eSoftware,

		/**
		 * \brief Creates devices for validation or survival paths without producing a real frame.
		 */
		eFallback,
	};

	/**
	 * \brief Backend registration candidate.
	 *
	 * MakeBackendEntry builds one from an API tag. Hand-filled entries are for backends discovered through another route, such as a shared library.
	 *
	 * \attention canonicalName and displayName are borrowed. Their storage must outlive every selection or registry that uses this entry.
	 */
	struct BackendEntry final
	{
		GraphicsApiId id{};

		/**
		 * \brief Globally namespaced backend name.
		 *
		 * ShortApiName trims this to the user-facing name used by command lines and environment variables.
		 */
		std::string_view canonicalName;

		std::string_view displayName;

		/**
		 * \brief Registers this backend into a GraphicsApiRegistry.
		 */
		Result<void> (*Register)(GraphicsApiRegistry &) = nullptr;

		/**
		 * \brief Backend purpose for sorting and fallback exclusion.
		 */
		BackendRank rank = BackendRank::eHardware;
	};

	/**
	 * \brief Builds a BackendEntry whose identity comes from its API tag.
	 *
	 * \param registerInto Backend registration function. Must not be null for an entry that will be added.
	 */
	template <GraphicsApiTag Api>
	[[nodiscard]] constexpr BackendEntry MakeBackendEntry(
		Result<void> (*const registerInto)(GraphicsApiRegistry &), const BackendRank rank = BackendRank::eHardware) noexcept
	{
		return BackendEntry{
			.id			   = Api::id,
			.canonicalName = Api::canonicalName,
			.displayName   = Api::displayName,
			.Register	   = registerInto,
			.rank		   = rank,
		};
	}

	/**
	 * \brief Returns the backends compiled into this RHI build.
	 *
	 * Entries point at static storage and are ordered by rank so Null is a fallback and not the first choice.
	 */
	[[nodiscard]] AZO_RHI_API std::span<const BackendEntry> AvailableBackends() noexcept;

	/**
	 * \brief Finds a compiled-in backend by short or canonical name.
	 */
	[[nodiscard]] AZO_RHI_API const BackendEntry * FindAvailableBackend(std::string_view name) noexcept;

	/**
	 * \brief Finds a compiled-in backend by id.
	 */
	[[nodiscard]] AZO_RHI_API const BackendEntry * FindAvailableBackend(GraphicsApiId id) noexcept;

	/**
	 * \brief Namespace-scope self-registration node for a backend linked into the image.
	 *
	 * Declare one through AZO_RHI_REGISTER_BACKEND in the backend's translation unit. BackendSelection::AddCatalog finds these beside bundled backends.
	 *
	 * \attention Static libraries may dead-strip the translation unit unless linked as whole-archive or built as an object library.
	 */
	class AZO_RHI_API StaticBackendRegistration final
	{
	public:
		explicit StaticBackendRegistration(const BackendEntry & entry) noexcept;

		StaticBackendRegistration(const StaticBackendRegistration &)			 = delete;
		StaticBackendRegistration & operator=(const StaticBackendRegistration &) = delete;
		StaticBackendRegistration(StaticBackendRegistration &&)					 = delete;
		StaticBackendRegistration & operator=(StaticBackendRegistration &&)		 = delete;

		// Nodes stay linked for process lifetime to avoid static-destruction ordering races.
		~StaticBackendRegistration() = default;

		[[nodiscard]] const BackendEntry & Entry() const noexcept
		{
			return m_entry;
		}

		[[nodiscard]] const StaticBackendRegistration * Next() const noexcept
		{
			return m_next;
		}

	private:
		BackendEntry m_entry;
		const StaticBackendRegistration * m_next = nullptr;
	};

	/**
	 * \brief Returns every backend that registered itself at static initialization time.
	 *
	 * Walk order follows load order. AddCatalog sorts before use so the final ordering is the same on every run.
	 */
	[[nodiscard]] AZO_RHI_API const StaticBackendRegistration * SelfRegisteredBackends() noexcept;

// Backend self-registration needs a generated namespace-scope object name. NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define AZO_RHI_DETAIL_REGISTER_CONCAT_INNER(a, b) a##b
#define AZO_RHI_DETAIL_REGISTER_CONCAT(a, b)	   AZO_RHI_DETAIL_REGISTER_CONCAT_INNER(a, b)

	/**
	 * \def AZO_RHI_REGISTER_BACKEND
	 * \brief Adds a namespace-scope backend self-registration node from a BackendEntry expression.
	 *
	 * \param entry BackendEntry expression, usually MakeBackendEntry<YourApi>(&RegisterYours)
	 */
#define AZO_RHI_REGISTER_BACKEND(entry)                                                                                                                        \
	static const ::azo::rhi::StaticBackendRegistration AZO_RHI_DETAIL_REGISTER_CONCAT(azoRhiSelfRegistered, __LINE__)                                          \
	{                                                                                                                                                          \
		(entry)                                                                                                                                                \
	}
	// NOLINTEND(cppcoreguidelines-macro-usage)

	/**
	 * \brief Registers one compiled-in backend by id.
	 *
	 * Returns eUnsupportedApi when this build did not compile a backend under that id.
	 */
	AZO_RHI_API Result<void> RegisterBackend(GraphicsApiRegistry & registry, GraphicsApiId id);

	/**
	 * \brief Registers one compiled-in backend by API tag.
	 */
	template <GraphicsApiTag Api>
	Result<void> RegisterBackend(GraphicsApiRegistry & registry)
	{
		return RegisterBackend(registry, Api::id);
	}

	/**
	 * \brief What naming a backend asks selection to do with the rest of the order.
	 */
	enum class BackendRequest : std::uint8_t
	{
		/**
		 * \brief Moves the named backend to the front and keeps the rest behind it.
		 *
		 * Creation walks that order until one hands back a device, so a machine without the named backend still comes up on another.
		 */
		eTry,

		/**
		 * \brief Makes the named backend the whole order, so its refusal is what creation reports.
		 *
		 * What a run testing one backend needs. Under eTry such a run comes up on the next backend and reads as the named one passing.
		 */
		eForce,
	};

	/**
	 * \brief User and build preference inputs used to form a backend order.
	 */
	struct BackendPreference final
	{
		/**
		 * \brief Backend name moved to the front when present.
		 *
		 * Either short or canonical form is accepted. The pointer is borrowed and must outlive the BackendSelection itself, not merely its construction: the
		 * name is kept as a view and every later Add reads it, as does RequestedName.
		 */
		const char * requested = nullptr;

		/**
		 * \brief Consults AZOTH_RHI_BACKEND when requested is null.
		 */
		bool consultEnvironment = true;

		/**
		 * \brief Adds the RHI's compiled-in backends during construction.
		 *
		 * Set false to add custom backends first and call AddAvailable later for the built-in remainder.
		 */
		bool includeAvailable = true;

		/**
		 * \brief Allows eFallback backends from AddAvailable unless explicitly requested by name.
		 */
		bool includeNull = true;

		/**
		 * \brief What the requested name asks for, whether it came from requested or from AZOTH_RHI_BACKEND.
		 *
		 * AZOTH_RHI_BACKEND_FORCE raises eTry to eForce from outside, which is how a run pins a binary it did not write. It never lowers eForce.
		 */
		BackendRequest request = BackendRequest::eTry;
	};

	/**
	 * \brief Owns a backend registry and the ordered backend preference list.
	 *
	 * Construction can register compiled-in backends. Add puts custom backends into the same order so runtime creation uses one path for both.
	 */
	class BackendSelection final
	{
	public:
		AZO_RHI_API explicit BackendSelection(const BackendPreference & preference = {});

		BackendSelection(const BackendSelection &)			   = delete;
		BackendSelection & operator=(const BackendSelection &) = delete;

		AZO_RHI_API BackendSelection(BackendSelection && other) noexcept;
		AZO_RHI_API BackendSelection & operator=(BackendSelection && other) noexcept;

		~BackendSelection() = default;

		/**
		 * \brief Registers a backend entry and records every backend it registers.
		 *
		 * If any registered backend answers to the requested name, it is moved to the front.
		 */
		AZO_RHI_API Result<void> Add(const BackendEntry & entry);

		/**
		 * \brief Adds a table of backend entries.
		 *
		 * Reports the first registration failure but still attempts the remaining entries.
		 */
		AZO_RHI_API Result<void> AddAll(std::span<const BackendEntry> entries);

		/**
		 * \brief Adds every compiled-in backend that is not already registered.
		 */
		AZO_RHI_API Result<void> AddAvailable();

		/**
		 * \brief Adds every self-registered backend, sorted so the order is the same on every run.
		 */
		AZO_RHI_API Result<void> AddSelfRegistered();

		/**
		 * \brief Adds backends supplied by a loaded module.
		 *
		 * \attention Entries must outlive this selection. Unload the module only after selection and creation no longer need its entries.
		 */
		AZO_RHI_API Result<void> AddModule(std::span<const BackendEntry> entries);

		/**
		 * \brief Adds bundled and self-registered backends ordered by rank and canonical name.
		 *
		 * A self-registered backend with the same GraphicsApiId as a bundled one replaces it, which lets a host substitute an implementation.
		 */
		AZO_RHI_API Result<void> AddCatalog();

		/**
		 * \brief Registers one custom backend directly from create-info.
		 *
		 * The API tag supplies identity so stale create-info cannot register under the wrong id.
		 */
		template <GraphicsApiTag Api>
		Result<void> Add(const BackendCreateInfo & createInfo)
		{
			const std::scoped_lock guard(m_guard);

			const std::size_t before	  = Registered();
			const Result<void> registered = m_registry.Register<Api>(createInfo);
			if (!registered)
			{
				return registered;
			}

			RecordFrom(before);
			return {};
		}

		/**
		 * \brief Returns backend info in first-choice order.
		 *
		 * \attention The returned span is valid only until the next registration on this object.
		 */
		[[nodiscard]] std::span<const BackendInfo> Preferred() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return std::span<const BackendInfo>{ m_preferred.data(), m_preferred.size() };
		}

		/**
		 * \brief Returns preferred backend ids in the same order as Preferred.
		 *
		 * \attention The returned span is valid only until the next registration on this object.
		 */
		[[nodiscard]] std::span<const GraphicsApiId> PreferredApis() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return std::span<const GraphicsApiId>{ m_preferredApis.data(), m_preferredApis.size() };
		}

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return m_preferred.empty();
		}

		/**
		 * \brief Returns the name used to resolve the order.
		 */
		[[nodiscard]] std::string_view RequestedName() const noexcept
		{
			return m_requestedName;
		}

		/**
		 * \brief Returns whether RequestedName matched a registered backend.
		 *
		 * A later Add can turn this true if the requested backend arrives after construction.
		 */
		[[nodiscard]] bool HonoredRequest() const noexcept
		{
			return m_honoredRequest;
		}

		/**
		 * \brief Returns what the resolved name asked for, after AZOTH_RHI_BACKEND_FORCE has had its say.
		 */
		[[nodiscard]] BackendRequest Request() const noexcept
		{
			return m_request;
		}

		[[nodiscard]] GraphicsApiRegistry & Registry() noexcept
		{
			return m_registry;
		}

		[[nodiscard]] const GraphicsApiRegistry & Registry() const noexcept
		{
			return m_registry;
		}

		/**
		 * \brief Creates an instance using the preferred backend order.
		 */
		[[nodiscard]] AZO_RHI_API Result<UniqueInstance> CreateInstance(const InstanceDesc & desc = {});

		/**
		 * \brief Creates a device using the preferred backend order.
		 */
		[[nodiscard]] AZO_RHI_API Result<UniqueDevice> CreateDevice(const DeviceDesc & desc = {});

		/**
		 * \brief Creates an instance on one named backend id.
		 */
		[[nodiscard]] AZO_RHI_API Result<UniqueInstance> CreateInstance(GraphicsApiId api, const InstanceDesc & desc);

		/**
		 * \brief Creates a device on one named backend id.
		 */
		[[nodiscard]] AZO_RHI_API Result<UniqueDevice> CreateDevice(GraphicsApiId api, const DeviceDesc & desc);

	private:
		// Private helpers run with m_guard already held. The spin lock is not recursive.
		[[nodiscard]] std::size_t Registered() const noexcept
		{
			return m_registry.EnumerateBackends().size();
		}

		AZO_RHI_API Result<void> AddOne(const BackendEntry & entry);
		AZO_RHI_API void Take(const BackendEntry & entry, Result<void> & firstFailure);
		AZO_RHI_API Result<void> AddInOrder(std::span<const BackendEntry> entries);
		AZO_RHI_API void RecordFrom(std::size_t firstNew);
		AZO_RHI_API void Record(const BackendInfo & info);

		// Process-scoped selection state uses its own lock because no device SyncOps exists yet.
		mutable SpinLock m_guard;

		GraphicsApiRegistry m_registry;

		// Stored separately because PreferredApis must return a contiguous span of ids.
		detail::HostVector<BackendInfo> m_preferred;
		detail::HostVector<GraphicsApiId> m_preferredApis;

		std::string_view m_requestedName;
		bool m_wasAskedFor		 = false;
		bool m_honoredRequest	 = false;
		bool m_includeNull		 = true;
		BackendRequest m_request = BackendRequest::eTry;
	};

} // namespace azo::rhi

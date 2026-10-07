// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/backend/support/spin_lock.hpp"
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>

namespace azo::rhi
{

	enum class BackendRank : std::uint8_t
	{
		eHardware,

		eSoftware,

		eFallback,
	};

	struct BackendEntry final
	{
		GraphicsApiId id{};

		std::string_view canonicalName;

		std::string_view displayName;

		Result<void> (*Register)(GraphicsApiRegistry &) = nullptr;

		BackendRank rank = BackendRank::eHardware;
	};

	template <GraphicsApiTag Api>
	[[nodiscard]] constexpr BackendEntry make_backend_entry(
		Result<void> (*const registerInto)(GraphicsApiRegistry &),
		const BackendRank rank = BackendRank::eHardware
	) noexcept
	{
		return BackendEntry{
			.id			   = Api::kId,
			.canonicalName = Api::kCanonicalName,
			.displayName   = Api::kDisplayName,
			.Register	   = registerInto,
			.rank		   = rank,
		};
	}

	[[nodiscard]] AZO_RHI_API std::span<const BackendEntry> available_backends() noexcept;

	[[nodiscard]] AZO_RHI_API const BackendEntry * find_available_backend(std::string_view name) noexcept;

	[[nodiscard]] AZO_RHI_API const BackendEntry * find_available_backend(GraphicsApiId id) noexcept;

	class AZO_RHI_API StaticBackendRegistration final
	{
	public:
		explicit StaticBackendRegistration(const BackendEntry & entry) noexcept;

		StaticBackendRegistration(const StaticBackendRegistration &)			 = delete;
		StaticBackendRegistration & operator=(const StaticBackendRegistration &) = delete;
		StaticBackendRegistration(StaticBackendRegistration &&)					 = delete;
		StaticBackendRegistration & operator=(StaticBackendRegistration &&)		 = delete;

		~StaticBackendRegistration() = default;

		[[nodiscard]] const BackendEntry & entry() const noexcept
		{
			return m_entry;
		}

		[[nodiscard]] const StaticBackendRegistration * next() const noexcept
		{
			return m_next;
		}

	private:
		BackendEntry m_entry;
		const StaticBackendRegistration * m_next = nullptr;
	};

	[[nodiscard]] AZO_RHI_API const StaticBackendRegistration * self_registered_backends() noexcept;

// Backend self-registration needs a generated namespace-scope object name. NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define AZO_RHI_DETAIL_REGISTER_CONCAT_INNER(a, b) a##b
#define AZO_RHI_DETAIL_REGISTER_CONCAT(a, b)	   AZO_RHI_DETAIL_REGISTER_CONCAT_INNER(a, b)

#define AZO_RHI_REGISTER_BACKEND(entry)                                                                                                                        \
	static const ::azo::rhi::StaticBackendRegistration AZO_RHI_DETAIL_REGISTER_CONCAT(azoRhiSelfRegistered, __LINE__)                                          \
	{                                                                                                                                                          \
		(entry)                                                                                                                                                \
	}
	// NOLINTEND(cppcoreguidelines-macro-usage)

	AZO_RHI_API Result<void> register_backend(GraphicsApiRegistry & registry, GraphicsApiId id);

	template <GraphicsApiTag Api>
	Result<void> register_backend(GraphicsApiRegistry & registry)
	{
		return register_backend(registry, Api::kId);
	}

	enum class BackendRequest : std::uint8_t
	{
		eTry,

		eForce,
	};

	struct BackendPreference final
	{
		const char * requested = nullptr;

		bool consultEnvironment = true;

		bool includeAvailable = true;

		bool includeNull = true;

		BackendRequest request = BackendRequest::eTry;
	};

	class BackendSelection final
	{
	public:
		AZO_RHI_API explicit BackendSelection(const BackendPreference & preference = {});

		BackendSelection(const BackendSelection &)			   = delete;
		BackendSelection & operator=(const BackendSelection &) = delete;

		AZO_RHI_API BackendSelection(BackendSelection && other) noexcept;
		AZO_RHI_API BackendSelection & operator=(BackendSelection && other) noexcept;

		~BackendSelection() = default;

		AZO_RHI_API Result<void> add(const BackendEntry & entry);

		AZO_RHI_API Result<void> add_all(std::span<const BackendEntry> entries);

		AZO_RHI_API Result<void> add_available();

		AZO_RHI_API Result<void> add_self_registered();

		AZO_RHI_API Result<void> add_module(std::span<const BackendEntry> entries);

		AZO_RHI_API Result<void> add_catalog();

		template <GraphicsApiTag Api>
		Result<void> add(const BackendCreateInfo & createInfo)
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

		[[nodiscard]] std::span<const BackendInfo> preferred() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return std::span<const BackendInfo>{ m_preferred.data(), m_preferred.size() };
		}

		[[nodiscard]] std::span<const GraphicsApiId> preferred_apis() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return std::span<const GraphicsApiId>{ m_preferredApis.data(), m_preferredApis.size() };
		}

		[[nodiscard]] bool is_empty() const noexcept
		{
			const std::scoped_lock guard(m_guard);
			return m_preferred.empty();
		}

		[[nodiscard]] std::string_view requested_name() const noexcept
		{
			return m_requestedName;
		}

		[[nodiscard]] bool honored_request() const noexcept
		{
			return m_honoredRequest;
		}

		[[nodiscard]] BackendRequest request() const noexcept
		{
			return m_request;
		}

		[[nodiscard]] GraphicsApiRegistry & registry() noexcept
		{
			return m_registry;
		}

		[[nodiscard]] const GraphicsApiRegistry & registry() const noexcept
		{
			return m_registry;
		}

		[[nodiscard]] AZO_RHI_API Result<UniqueInstance> create_instance(const InstanceDesc & desc = {});

		[[nodiscard]] AZO_RHI_API Result<UniqueDevice> create_device(const DeviceDesc & desc = {});

		[[nodiscard]] AZO_RHI_API Result<UniqueInstance> create_instance(GraphicsApiId api, const InstanceDesc & desc);

		[[nodiscard]] AZO_RHI_API Result<UniqueDevice> create_device(GraphicsApiId api, const DeviceDesc & desc);

	private:
		[[nodiscard]] std::size_t Registered() const noexcept
		{
			return m_registry.enumerate_backends().size();
		}

		AZO_RHI_API Result<void> AddOne(const BackendEntry & entry);
		AZO_RHI_API void Take(const BackendEntry & entry, Result<void> & firstFailure);
		AZO_RHI_API Result<void> AddInOrder(std::span<const BackendEntry> entries);
		AZO_RHI_API void RecordFrom(std::size_t firstNew);
		AZO_RHI_API void Record(const BackendInfo & info);

		mutable SpinLock m_guard;

		GraphicsApiRegistry m_registry;

		detail::HostVector<BackendInfo> m_preferred;
		detail::HostVector<GraphicsApiId> m_preferredApis;

		std::string_view m_requestedName;
		bool m_wasAskedFor		 = false;
		bool m_honoredRequest	 = false;
		bool m_includeNull		 = true;
		BackendRequest m_request = BackendRequest::eTry;
	};

}

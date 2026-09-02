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
#include "azoth/rhi/core/api.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/selection.hpp"
#include "azoth/rhi/module/entry_point.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace azo::rhi
{

	class BackendModule final
	{
	public:
		BackendModule() = default;

		BackendModule(const BackendModule &)			 = delete;
		BackendModule & operator=(const BackendModule &) = delete;

		AZO_RHI_API BackendModule(BackendModule && other) noexcept;
		AZO_RHI_API BackendModule & operator=(BackendModule && other) noexcept;

		AZO_RHI_API ~BackendModule();

		[[nodiscard]] AZO_RHI_API static Result<BackendModule> Load(std::string_view path);

		[[nodiscard]] std::span<const BackendEntry> Entries() const noexcept
		{
			return std::span<const BackendEntry>{ m_entries.data(), m_entries.size() };
		}

		[[nodiscard]] AZO_RHI_API std::size_t LiveObjects() const noexcept;

		[[nodiscard]] bool IsLoaded() const noexcept
		{
			return m_handle != nullptr;
		}

		[[nodiscard]] std::string_view Path() const noexcept
		{
			return m_path;
		}

		AZO_RHI_API Result<void> Unload();

	private:
		AZO_RHI_API void Adopt(BackendModule && other) noexcept;

		void * m_handle = nullptr;

		detail::HostVector<detail::HostString> m_names;
		detail::HostVector<BackendEntry> m_entries;

		detail::HostString m_path;
		std::size_t (*m_liveObjectCount)() = nullptr;
	};

}

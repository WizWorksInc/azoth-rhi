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
 * \brief Runtime-loaded backend module ownership and lifetime tracking.
 */

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

	/**
	 * \brief Owns a backend loaded from a shared library at runtime.
	 *
	 * Module entries expose string views, so this object copies names out of the module before exposing entries to BackendSelection. That keeps names
	 * valid for as long as this object stays loaded instead of tying them to constants inside an unloadable image.
	 *
	 * \attention Unload is refused while objects created through the module are still alive.
	 */
	class BackendModule final
	{
	public:
		BackendModule() = default;

		BackendModule(const BackendModule &)			 = delete;
		BackendModule & operator=(const BackendModule &) = delete;

		AZO_RHI_API BackendModule(BackendModule && other) noexcept;
		AZO_RHI_API BackendModule & operator=(BackendModule && other) noexcept;

		// Attempts unload on destruction. If live objects remain, the module is left loaded because destructors cannot report failure.
		AZO_RHI_API ~BackendModule();

		/**
		 * \brief Loads a shared library, validates its ABI stamp, and copies its backend entries.
		 *
		 * The stamp is checked before calling any other module surface. A mismatch returns eIncompatibleAbi and closes the library again.
		 */
		[[nodiscard]] AZO_RHI_API static Result<BackendModule> Load(std::string_view path);

		/**
		 * \brief Returns the backend entries exported by this module.
		 *
		 * Entry names point into storage owned by this object, not into the loaded module.
		 *
		 * \attention Valid until this module is unloaded or moved from.
		 */
		[[nodiscard]] std::span<const BackendEntry> Entries() const noexcept
		{
			return std::span<const BackendEntry>{ m_entries.data(), m_entries.size() };
		}

		/**
		 * \brief Returns how many objects created through this module are still alive.
		 */
		[[nodiscard]] AZO_RHI_API std::size_t LiveObjects() const noexcept;

		[[nodiscard]] bool IsLoaded() const noexcept
		{
			return m_handle != nullptr;
		}

		[[nodiscard]] std::string_view Path() const noexcept
		{
			return m_path;
		}

		/**
		 * \brief Unloads the module or reports why it cannot.
		 *
		 * Returns eInvalidState while LiveObjects is nonzero. Calling Unload on an already unloaded module succeeds and does nothing.
		 */
		AZO_RHI_API Result<void> Unload();

	private:
		// Moves ownership from other and leaves other in the unloaded state.
		AZO_RHI_API void Adopt(BackendModule && other) noexcept;

		void * m_handle = nullptr;

		// Names are stored separately so BackendEntry string_views remain stable after the entries are built.
		detail::HostVector<detail::HostString> m_names;
		detail::HostVector<BackendEntry> m_entries;

		detail::HostString m_path;
		std::size_t (*m_liveObjectCount)() = nullptr;
	};

} // namespace azo::rhi

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

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint> // NOLINT: (JB): Remove once CLion is fixed.
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Callbacks for adapter enumeration, device creation and instance lifetime.
	 */
	struct InstanceApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(InstanceApi), .version = 1 };

		/**
		 * \brief Returns the backend's graphics API identifier.
		 * \param impl Backend instance.
		 * \return Graphics API identifier.
		 */
		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept = nullptr;

		/**
		 * \brief Enumerates available adapters into the supplied storage.
		 * \param impl Backend instance.
		 * \param[out] adapters Adapter output storage. An empty span queries only the count.
		 * \param[out] out Total adapter count, which may exceed the span's size.
		 * \param[out] error Optional output for failure details.
		 * \return True on success, false on failure.
		 */
		bool (*enumerateAdapters)(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a device from the instance.
		 * \param impl Backend instance.
		 * \param desc Adapter selection, queues and device settings.
		 * \param[out] error Optional output for failure details.
		 * \return Backend device instance, or nullptr on failure.
		 */
		void * (*createDevice)(void * impl, const DeviceDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Destroys the backend instance.
		 * \param impl Backend instance.
		 */
		void (*destroyInstance)(void * impl) noexcept = nullptr;
	};

	/**
	 * \brief Callbacks for querying external handle support.
	 */
	struct ExternalCapabilityApi final
	{
		/**
		 * \brief Interface size and version for compatibility checks.
		 */
		InterfaceHeader header{ .byteSize = sizeof(ExternalCapabilityApi), .version = 1 };

		/**
		 * \brief Queries an adapter's external handle support.
		 * \param impl Backend instance.
		 * \param desc Adapter, object kind, handle type and texture format.
		 * \param[out] out Import and export support, with compatible handle types.
		 * \param[out] error Optional output for failure details.
		 * \return True if the query succeeds, false on failure.
		 */
		bool (*queryExternalHandleSupport)(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

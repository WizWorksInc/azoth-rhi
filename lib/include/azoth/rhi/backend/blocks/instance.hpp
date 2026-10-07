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

	struct InstanceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(InstanceApi), .version = 1 };

		/**
		 * \brief Returns the backend's graphics API identifier.
		 */
		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept = nullptr;

		/**
		 * \brief An empty adapter span queries only the count.
		 * \param impl Instance.
		 * \param adapters Adapter storage.
		 * \param[out] out Total adapter count, which may exceed the span's size.
		 * \param[out] error Optional error details.
		 */
		bool (*enumerateAdapters)(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept = nullptr;

		/**
		 * \brief Creates a device from the instance.
		 */
		void * (*createDevice)(void * impl, const DeviceDesc & desc, Error * error) noexcept = nullptr;

		/**
		 * \brief Destroys the backend instance.
		 */
		void (*destroyInstance)(void * impl) noexcept = nullptr;
	};

	struct ExternalCapabilityApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalCapabilityApi), .version = 1 };

		/**
		 * \brief A successful query may report that importing or exporting is unsupported.
		 */
		bool (*queryExternalHandleSupport)(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

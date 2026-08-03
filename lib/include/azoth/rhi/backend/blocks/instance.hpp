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
 * \brief Instance-level ABI block published by backend instance objects.
 */

#include "azoth/rhi/backend/blocks/common.hpp"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <span>

namespace azo::rhi
{

	/**
	 * \brief Required backend instance table for adapter enumeration and device creation.
	 *
	 * \attention After destroyInstance returns, no function in this block may be called again for the destroyed backend instance.
	 */
	struct InstanceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(InstanceApi), .version = 1 };

		/// Instance identity.

		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept = nullptr;

		/// Adapter discovery.

		/**
		 * \brief Enumerates adapters into caller-provided storage.
		 *
		 * \note out reports the number of adapters written when adapters has enough space, or the required count when the span is too small.
		 */
		bool (*enumerateAdapters)(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept = nullptr;

		/// Device creation.

		/**
		 * \brief Creates a backend device object owned by the returned public device facade.
		 *
		 * \note Returns nullptr when device creation fails. The returned object must publish its device ABI blocks through BackendObject.
		 */
		void * (*createDevice)(void * impl, const DeviceDesc & desc, Error * error) noexcept = nullptr;

		/// Instance destruction.

		void (*destroyInstance)(void * impl) noexcept = nullptr;
	};

	/**
	 * \brief Optional instance table answering what an adapter can share across an API, device or process boundary.
	 *
	 * Separate from InstanceApi and not an entry on it, because that block is the set an out-of-tree backend has to fill to be a backend at all. A backend
	 * that shares nothing declines this one.
	 *
	 * On the instance and not the device because it describes an adapter. A caller picking one for interop has to ask before creating a device.
	 */
	struct ExternalCapabilityApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalCapabilityApi), .version = 1 };

		/**
		 * \brief Answers what one enumerated adapter supports for one object kind and one external handle type.
		 *
		 * \note A pairing that cannot exist, such as a buffer against a semaphore-only handle type, is a cleared result and not a failure. Failure is
		 * reserved for a question that could not be asked at all, such as an adapter index this instance does not have.
		 */
		bool (*queryExternalHandleSupport)(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept = nullptr;
	};

} // namespace azo::rhi

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "azoth/rhi/backend/blocks/common.hpp"

#include <cstdint>
#include <span>

namespace azo::rhi
{

	struct InstanceApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(InstanceApi), .version = 1 };

		GraphicsApiId (*getGraphicsApiId)(void * impl) noexcept = nullptr;

		bool (*enumerateAdapters)(void * impl, std::span<AdapterInfo> adapters, std::uint32_t * out, Error * error) noexcept = nullptr;

		void * (*createDevice)(void * impl, const DeviceDesc & desc, Error * error) noexcept = nullptr;

		void (*destroyInstance)(void * impl) noexcept = nullptr;
	};

	struct ExternalCapabilityApi final
	{
		InterfaceHeader header{ .byteSize = sizeof(ExternalCapabilityApi), .version = 1 };

		bool (*queryExternalHandleSupport)(void * impl, const ExternalHandleSupportDesc & desc, ExternalHandleSupport * out, Error * error) noexcept = nullptr;
	};

}

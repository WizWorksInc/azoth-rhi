// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include "backends/metal4/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <string_view>

namespace azo::rhi::metal4
{
	bool metal4_refuse_unexportable(
		const Flags<ExternalHandleType> declared,
		const Flags<ExternalHandleType> allowed,
		const char * what,
		Error * error
	) noexcept
	{
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.empty() ? true : fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	[[nodiscard]] void * alloc_object(Metal4Device * device, const BackendObject * published, QueueType queueType)
	{
		Metal4Object * object = device->objects.New();
		if (object == nullptr)
		{
			return nullptr;
		}

		object->object	  = published;
		object->owner	  = device;
		object->queueType = queueType;
		object->list	  = nullptr;
		return object;
	}

	[[nodiscard]] Metal4BackendOwner & backend_owner()
	{
		static Metal4BackendOwner s_Owner;
		return s_Owner;
	}

	GraphicsApiId metal4_device_api_id([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::kId;
	}

	std::string_view metal4_device_api_name([[maybe_unused]] void * impl) noexcept
	{
		return Metal4Api::kDisplayName;
	}

	const DeviceCaps & metal4_device_caps(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->caps;
	}

	const AdapterInfo & metal4_device_adapter_info(void * impl) noexcept
	{
		return static_cast<Metal4Device *>(impl)->adapter;
	}

	ValidationMessageCounts metal4_device_validation_message_counts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

}

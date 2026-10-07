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

#include "azoth/rhi/backend/interface.hpp"
#include "azoth/rhi/backend/support/host_containers.hpp"
#include "azoth/rhi/core/enums.hpp"
#include "azoth/rhi/core/external.hpp"
#include "azoth/rhi/core/flags.hpp"
#include "azoth/rhi/core/result.hpp"
#include "azoth/rhi/device/api_tags.hpp"
#include "azoth/rhi/device/device.hpp"

#include "backends/metal/internal.hpp"
#include "backends/metal_common/conversions.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLAllocation.hpp>
#include <Metal/MTLResidencySet.hpp>

#include <cstddef>
#include <string_view>

namespace azo::rhi::metal
{
	bool metal_refuse_unexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.empty() ? true : fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	[[nodiscard]] void * alloc_object(MetalDevice * device, const BackendObject * published, QueueType queueType)
	{
		MetalObject * object = device->objects.New();
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

	void MetalDevice::note_allocation(const Residency kind, const MTL::Allocation * allocation) noexcept
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): kind is an enumerator of the array's own size.
		const NS::SharedPtr<MTL::ResidencySet> & set = azo::rhi::detail::at(residencySets, static_cast<std::size_t>(kind));
		if (allocation == nullptr || set.get() == nullptr)
		{
			return;
		}

		set->addAllocation(allocation);

		set->commit();
		set->requestResidency();
	}

	[[nodiscard]] MetalBackendOwner & backend_owner()
	{
		static MetalBackendOwner s_Owner;
		return s_Owner;
	}

	GraphicsApiId metal_device_api_id([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::kId;
	}

	std::string_view metal_device_api_name([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::kDisplayName;
	}

	const DeviceCaps & metal_device_caps(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->caps;
	}

	const AdapterInfo & metal_device_adapter_info(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->adapter;
	}

	ValidationMessageCounts metal_device_validation_message_counts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

} // namespace azo::rhi::metal

// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backends/metal/internal.hpp"

namespace azo::rhi::metal
{
	bool MetalRefuseUnexportable(const Flags<ExternalHandleType> declared, const Flags<ExternalHandleType> allowed, const char * what, Error * error) noexcept
	{
		const Flags<ExternalHandleType> unsupported = declared & ~allowed;
		return unsupported.Empty() ? true : Fail(error, ErrorCode::eUnsupportedFeature, what);
	}

	[[nodiscard]] void * AllocObject(MetalDevice * device, const BackendObject * published, QueueType queueType)
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

	void MetalDevice::NoteAllocation(const Residency kind, const MTL::Allocation * allocation) noexcept
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): kind is an enumerator of the array's own size.
		const NS::SharedPtr<MTL::ResidencySet> & set = residencySets[static_cast<std::size_t>(kind)];
		if (allocation == nullptr || set.get() == nullptr)
		{
			return;
		}

		set->addAllocation(allocation);

		set->commit();
		set->requestResidency();
	}

	[[nodiscard]] MetalBackendOwner & Owner()
	{
		static MetalBackendOwner owner;
		return owner;
	}

	GraphicsApiId MetalDeviceApiId([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::id;
	}

	std::string_view MetalDeviceApiName([[maybe_unused]] void * impl) noexcept
	{
		return MetalApi::displayName;
	}

	const DeviceCaps & MetalDeviceCaps(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->caps;
	}

	const AdapterInfo & MetalDeviceAdapterInfo(void * impl) noexcept
	{
		return static_cast<MetalDevice *>(impl)->adapter;
	}

	ValidationMessageCounts MetalDeviceValidationMessageCounts([[maybe_unused]] void * impl) noexcept
	{
		return {};
	}

}
